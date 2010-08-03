/** 
 * lsyncd.c   Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axel.kittenberger@univie.ac.at>
 *          Eugene Sanivsky <eugenesan@gmail.com>
 */
#include "config.h"
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/times.h>

#ifdef HAVE_SYS_INOTIFY_H
#  include <sys/inotify.h>
#else
#  include "inotify-nosys.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <getopt.h>
#include <assert.h>
#include <syslog.h>

#ifdef XML_CONFIG
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

/**
 * Number of inotifies to read at once from the kernel.
 */
#define INOTIFY_BUF_LEN     (64 * (sizeof(struct inotify_event) + 16))

/**
 * Initial size of vectors
 */
#define VECT_INIT_SIZE 2

/**
 * Defaults values 
 */
#define DEFAULT_BINARY "/usr/bin/rsync"
#define DEFAULT_CONF_FILENAME "/etc/lsyncd.conf.xml"

/**
 * Macros to compare times() values
 * (borrowed from linux/jiffies.h)
 *
 * time_after(a,b) returns true if the time a is after time b.
 */
#define time_after(a,b)         ((long)(b) - (long)(a) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)

/**
 * Importance of log messages
 */
enum log_code {
	DEBUG  = 1,
	NORMAL = 2,
	ERROR  = 3,
};

/**
 * Possible exit codes for this application
 */
enum lsyncd_exit_code {
	LSYNCD_SUCCESS = 0,

	/* out-of memory */
	LSYNCD_OUTOFMEMORY = 1,

	/* file was not found, or failed to write */
	LSYNCD_FILENOTFOUND = 2,

	/* execution somehow failed */
	LSYNCD_EXECFAIL = 3,

	/* command-line arguments given to lsyncd are bad */
	LSYNCD_BADPARAMETERS = 4,

	/* Too many excludes files were specified */
	LSYNCD_TOOMANYDIRECTORYEXCLUDES = 5,

	/* something wrong with the config file */
	LSYNCD_BADCONFIGFILE = 6,

	/* cannot open inotify instance */
	LSYNCD_NOINOTIFY = 7,

	/* something internal went really wrong */
	LSYNCD_INTERNALFAIL = 255,
};

/**
 * An option paramater for the action call can either be: 
 */
enum call_option_kind {
	CO_EOL,                 // end of list,
	CO_TEXT,                // specified by text,
	CO_EXCLUDE,             // be the exclude file
	CO_SOURCE,              // be the source of the operation
	CO_DEST,                // be the destination of the operation
};

/*--------------------------------------------------------------------------*
 * Structure definitions
 *--------------------------------------------------------------------------*/

/**
 * An option parameter for the call.
 */
struct call_option {
	/**
	 * The kind of this option.
	 */
	enum call_option_kind kind;

	/**
	 * The text if its text.
	 */
	char *text;
};

/**
 * A configurated directory to sync (including subdirectories)
 *
 * In case of beeing called with simple argument without a config file
 * there will be exactly one sync_directory.
 */
struct dir_conf {
	/**
	 * Source dir to watch (no default value)
	 */
	char * source;

	/**
	 * NULL terminated list of targets to rsync to (no default value).
	 */
	char ** targets;

	/**
	 * binary to call (defaults to global default setting)
	 */
	char * binary;

	/**
	 * the options to call the binary (defaults to global default setting)
	 */
	struct call_option * callopts;

	/**
	 * the exclude-file to pass to rsync (defaults to global default setting)
	 * TODO, Currently ignored!
	 */
	char * exclude_file;
};

/**
 * Structure to store the directory watches.
 */
struct dir_watch {
	/**
	 * The watch descriptor returned by kernel.
	 */
	int wd;

	/**
	 * The name of the directory.
	 * In case of the root dir to be watched, it is a full path
	 * and parent == -1. Otherwise its just the name of the
	 * directory and parent points to the parent directory thats
	 * also watched.
	 */
	char * dirname;

	/**
	 * Points to the index of the parent.
	 * -1 if no parent
	 */
	int parent;

	/**
	 * On a delay to be handled.
	 */
	bool tackled;

	/**
	 * Point in time when rsync should be called.
	 */
	clock_t alarm;

	/**
	 * The applicable configuration for this directory
	 */
	struct dir_conf *dir_conf;
};

/**
 * Global options relevant for logging.
 * Part of struct global_options.
 */
struct log {
	/**
	 * Global Option: The loglevel is how eloquent lsyncd will be.
	 */
	int loglevel;

	/**
	 * Global Option: if true, do not detach and log to stdout/stderr.
	 */
	int flag_nodaemon;
	
	/**
	 * If not NULL, this file will be accessed directly to write log messages to.
	 * If NULL, syslog will be used.
	 *
	 * Not as difference that the output of child processes (rsync) will be redirected
	 * to the logfile if specified, if syslogging the child-output will run into /dev/null.
	 *
	 * If flag_nodaemon is present stdout/stderr will be used.
	 */
	char * logfile;
};

/**
 * Global variables
 */
struct global_options {
	/**
	 * Options relevant for logging.
	 */
	struct log log;

	/**
	 * Global Option: if true no action will actually be called.
	 */
	int flag_dryrun;

	/**
	 * Global Option: if true, ignore rsync errors on startup.
	 *                (during normal operations they have to be ignored eitherway,
	 *                 since rsync may also fail due e.g. the directory already
	 *                 beeing deleted when lsyncd wants to sync it.)
	 */
	int flag_stubborn;

	/**
	 * Global Option: if true, lsyncd will not perform the startup sync.
	 */
	int flag_nostartup;

	/**
	 * Global Option: pidfile, which holds the PID of the running daemon process.
	 */
	char *pidfile;

#ifdef XML_CONFIG
	/**
	 * Global Option: the filename to read config from.
	 */
	char *conf_filename;
#endif

	/**
	 * Global Option: this binary is used if no other specified in dir_conf.
	 */
	char *default_binary;

	/**
	 * Global Option: default exclude file
	 */
	char *default_exclude_file;

	/**
	 * Global Option: default options to call the binary with.
	 *
	 * TODO copy on init.
	 */
	struct call_option *default_callopts;

	/**
	 * Seconds of delay between event and action
	 */
	clock_t delay;

	/**
	 * The configuratiton for dirs to synchronize
	 */
	struct dir_conf *dir_confs;

	/**
	 * The number of configurated dirs to sync.
	 */
	int dir_conf_n;
};

/**
 * Standard default options to call the binary with.
 */
struct call_option standard_callopts[] = {
	{ CO_TEXT,    "-lt%r"    },
	{ CO_TEXT,    "--delete" },
	{ CO_EXCLUDE, NULL       },
	{ CO_SOURCE,  NULL       },
	{ CO_DEST,    NULL       },
	{ CO_EOL,     NULL       },
};

/**
 * General purpose growable vector of integers.
 */
struct ivector {
	/**
	 * data
	 */
	int *data;

	/**
	 * allocated mem
	 */
	size_t size;

	/**
	 * length used
	 */
	size_t len;
};

/**
 * General purpose growable vector of pointers.
 */
//struct pvector {
//	/**
//	 * data
//	 */
//	void *data;
//
//	/**
//	 * allocated mem
//	 */
//	size_t size;
//
//	/**
//	 * length used
//	 */
//	size_t len;
//};

/**
 * List of directories on a delay.
 */
struct ivector tackles_obj = {0, };
struct ivector *tackles = &tackles_obj;


/**
 * Structure to store strings for the diversve inotfy masked events.
 * Used for comfortable log messages only.
 */
struct inotify_mask_text {
	int mask;
	char const * text;
};

/**
 * A constant that assigns every inotify mask a printable string.
 * Used for debugging.
 */
struct inotify_mask_text mask_texts[] = {
	{ IN_ACCESS,        "ACCESS"        }, 
	{ IN_ATTRIB,        "ATTRIB"        }, 
	{ IN_CLOSE_WRITE,   "CLOSE_WRITE"   }, 
	{ IN_CLOSE_NOWRITE, "CLOSE_NOWRITE" }, 
	{ IN_CREATE,        "CREATE"        }, 
	{ IN_DELETE,        "DELETE"        }, 
	{ IN_DELETE_SELF,   "DELETE_SELF"   }, 
	{ IN_IGNORED,       "IGNORED"       }, 
	{ IN_MODIFY,        "MODIFY"        }, 
	{ IN_MOVE_SELF,     "MOVE_SELF"     }, 
	{ IN_MOVED_FROM,    "MOVED_FROM"    }, 
	{ IN_MOVED_TO,      "MOVED_TO"      }, 
	{ IN_OPEN,          "OPEN"          }, 
	{ 0, "" },
};

/**
 * Holds all directories being watched.
 */
struct dir_watch_vector {
	struct dir_watch *data;
	size_t size;
	size_t len;
};
struct dir_watch_vector dir_watches_obj = {0, };
struct dir_watch_vector *dir_watches = &dir_watches_obj;

/**
 * Array of strings of directory names to include.
 * This is limited to MAX_EXCLUDES.
 * It's not worth to code a dynamic size handling...
 */
#define MAX_EXCLUDES 256
char * exclude_dirs[MAX_EXCLUDES] = {NULL, };
int exclude_dir_n = 0;

/**
 * (Re)sets global options to default values.
 *
 * TODO memfree's
 */
void
reset_options(struct global_options *opts) {
	opts->log.loglevel = NORMAL;
	opts->log.flag_nodaemon = 0;
	opts->log.logfile = NULL;
	
	opts->flag_dryrun = 0;
	opts->flag_stubborn = 0;
	opts->flag_nostartup = 0;
	opts->pidfile = NULL;
#ifdef XML_CONFIG
	opts->conf_filename = DEFAULT_CONF_FILENAME;
#endif
	opts->default_binary = DEFAULT_BINARY;
	opts->default_exclude_file = NULL;
	opts->default_callopts = standard_callopts;
	opts->delay = 5;
	opts->dir_confs = NULL;
	opts->dir_conf_n = 0;
};

/*--------------------------------------------------------------------------*
 * Small generic helper routines. 
 *    (signal catching, memory fetching, message output)
 *--------------------------------------------------------------------------*/

/**
 * Set to 0 in signal handler, when lsyncd should TERMinate nicely.
 */
volatile sig_atomic_t keep_going = 1;

/**
 * Called (out-of-order) when signals arrive
 */
void
catch_alarm(int sig)
{
	keep_going = 0;
}

/**
 * Just like exit, but logs the exit.
 * Does not return!
 */
void
terminate(const struct log *log, int status) 
{
	if (log && !log->flag_nodaemon) {
		if (log->logfile) {
			FILE * flog;
			flog = fopen(log->logfile, "a");
			if (flog) {
				fprintf(flog, "exit!");
				fclose(flog);
			}
		} else {
			syslog(LOG_ERR, "exit!");		
		}
	}
	exit(status);
}

/**
 * Prints a message to either the log stream, preceding a timestamp or 
 * forwards a message to syslogd.
 *
 * Otherwise it behaves like printf();
 *
 * It will also always produce error messages on stderr.
 * So when startup fails, the message will be logged 
 * _and_ displayed on screen. If lsyncd daemonized already, 
 * stderr will be run into the void of /dev/null.
 */
void
printlogf(const struct log *log, int level, const char *fmt, ...)
{
	va_list ap;
	char * ct;
	time_t mtime;
	FILE * flog1 = NULL, * flog2 = NULL;
	int sysp = 0;

	if (log && level < log->loglevel) {
		return;
	}

	if (log && !log->flag_nodaemon && log->logfile) {
		flog1 = fopen(log->logfile, "a");

		if (flog1 == NULL) {
			fprintf(stderr, "cannot open logfile [%s]!\n", log->logfile);
			terminate(log, LSYNCD_FILENOTFOUND);
		}
	}

	time(&mtime);
	ct = ctime(&mtime);
	ct[strlen(ct) - 1] = 0; // cut trailing linefeed

	switch (level) {
	case DEBUG  :
		sysp = LOG_DEBUG;
		if (!log || log->flag_nodaemon) {
			flog2 = stdout;
		}
		break;

	case NORMAL :
		sysp = LOG_NOTICE;
		if (!log || log->flag_nodaemon) {
			flog2 = stdout;
		}
		break;

	case ERROR  :
		sysp = LOG_ERR;
		// write on stderr even when daemon.
		flog2 = stderr;
		break;
	}

	// write time on fileoutput
	if (flog1) {
		fprintf(flog1, "%s: ", ct);
	}

	if (level == ERROR) {
		if (flog1) {
			fprintf(flog1, "ERROR: ");
		}
		if (flog2) {
			fprintf(flog2, "ERROR: ");
		}
	}

	if (flog1) {
		va_start(ap, fmt);
		vfprintf(flog1, fmt, ap);
		va_end(ap);
	} else {
		va_start(ap, fmt);
		vsyslog(sysp, fmt, ap);
		va_end(ap);
	}
	if (flog2) {
		va_start(ap, fmt);
		vfprintf(flog2, fmt, ap);
		va_end(ap);
	}

	if (flog1) {
		fprintf(flog1, "\n");
	}
	if (flog2) {
		fprintf(flog2, "\n");
	}

	if (flog1) {
		fclose(flog1);
	}
}

/**
 * "secured" malloc, meaning the deamon shall kill itself
 * in case of out of memory.
 *
 * On linux systems, which is actually the only system this
 * deamon will run at, due to the use of inotify, this is
 * an "academic" cleaness only, linux will never return out
 * memory, but kill a process to ensure memory will be
 * available.
 */
void *
s_malloc(const struct log *log, size_t size)
{
	void *r = malloc(size);

	if (r == NULL) {
		printlogf(log, ERROR, "Out of memory!");
		terminate(log, LSYNCD_OUTOFMEMORY);
	}

	return r;
}

/**
 * "secured" calloc.
 */
void *
s_calloc(const struct log *log, size_t nmemb, size_t size)
{
	void *r = calloc(nmemb, size);

	if (r == NULL) {
		printlogf(log, ERROR, "Out of memory!");
		terminate(log, LSYNCD_OUTOFMEMORY);
	}

	return r;
}

/**
 * "secured" realloc.
 */
void *
s_realloc(const struct log *log, void *ptr, size_t size)
{
	void *r = realloc(ptr, size);

	if (r == NULL) {
		printlogf(log, ERROR, "Out of memory!");
		terminate(log, LSYNCD_OUTOFMEMORY);
	}

	return r;
}

/**
 * "secured" strdup.
 */
char *
s_strdup(const struct log *log, const char *src)
{
	char *s = strdup(src);

	if (s == NULL) {
		printlogf(log, ERROR, "Out of memory!");
		terminate(log, LSYNCD_OUTOFMEMORY);
	}

	return s;
}

/**
 * Returns the canonicalized path of a directory with a final '/'.
 * Makes sure it is a directory.
 */
char *
realdir(const struct log *log, const char *dir) 
{
	char* cs = s_malloc(log, PATH_MAX+1);
	cs = realpath(dir, cs);

	if (cs == NULL) {
		return NULL;
	}

	if (strlen(cs) + 1 >= PATH_MAX) {
		// at systems maxpath already, we cannot add a '/' anyway.
		return NULL;
	}

	struct stat st;
	stat(cs, &st);
	if (!S_ISDIR(st.st_mode)) {
		free(cs);
		return NULL;
	}

	strcat(cs, "/");
	return cs;
}

/**
 * Appends one value on an integer vector.
 */
void
ivector_push(const struct log *log, struct ivector *ivect, int val){
	if (ivect->size > ivect->len + 1) {
		ivect->data[ivect->len++] = val;
	} else if (!ivect->data) {
		ivect->data = s_calloc(log, VECT_INIT_SIZE, sizeof(int)); 
		ivect->size = VECT_INIT_SIZE;
		ivect->len = 1;
		ivect->data[0] = val;
	} else {
		ivect->size *= 2;
		ivect->data = s_realloc(log, ivect->data, ivect->size * sizeof(int));
		ivect->data[ivect->len++] = val;
	}
}

/*--------------------------------------------------------------------------*
 * Per directory configuration handling. 
 *--------------------------------------------------------------------------*/

/**
 * (re)allocates space for a new dir_config and sets all values to 0/Null.
 *
 * (Yes we know, its a bit unoptimal, since when 6 dir_confs are given
 * in the config file, lsyncd will reallocate dir_confs 6 times. Well we
 * can live with that.) 
 *
 * @return the pointer to the newly allocated dir_conf
 */
struct dir_conf *
new_dir_conf(struct global_options *opts) {
	const struct log* log = &opts->log;

	if (opts->dir_conf_n > 0) {
		// enhance allocated space by 1.
		opts->dir_conf_n++;
		opts->dir_confs = s_realloc(log, opts->dir_confs, opts->dir_conf_n * sizeof(struct dir_conf));
		memset(opts->dir_confs + opts->dir_conf_n - 1, 0, sizeof(struct dir_conf));
		// creates targets NULL terminator (no targets yet)
		opts->dir_confs[opts->dir_conf_n - 1].targets = s_calloc(log, 1, sizeof(char *));
		return opts->dir_confs + opts->dir_conf_n - 1;
	} else {
		// create the memory.
		opts->dir_conf_n = 1;
		opts->dir_confs = s_calloc(log, opts->dir_conf_n, sizeof(struct dir_conf));
		// creates targets NULL terminator (no targets yet)
		opts->dir_confs[0].targets = s_calloc(log, 1, sizeof(char *));
		return opts->dir_confs;
	}
}

/**
 * Adds a target to a dir_conf.target.
 * *target string will duped.
 *
 * @param dir_conf   dir_conf to add the target to.
 * @param target     target to add.
 */
void
dir_conf_add_target(const struct log *log, struct dir_conf *dir_conf, char *target)
{
	char **t;
	int target_n = 0;

	// count current targets
	for (t = dir_conf->targets; *t; ++t) {
		target_n++;
	}

	dir_conf->targets = s_realloc(log, dir_conf->targets, (target_n + 2) * sizeof(char *));
	dir_conf->targets[target_n] = s_strdup(log, target);
	dir_conf->targets[target_n + 1] = NULL;
}

/*--------------------------------------------------------------------------*
 * Tackle list handling. 
 *--------------------------------------------------------------------------*/

/**
 * Adds a directory on the tackle len (on a delay)
 *
 * @param watch         the index in dir_watches to the directory.
 * @param alarm         times() when the directory should be acted.
 */
bool
append_tackle(const struct log *log, int watch, clock_t alarm) {
	printlogf(log, DEBUG, "add tackle(%d)", watch);
	if (dir_watches->data[watch].tackled) {
		printlogf(log, DEBUG, "ignored since already tackled.", watch);
		return false;
	}
	dir_watches->data[watch].tackled = true;
	dir_watches->data[watch].alarm = alarm;

	ivector_push(log, tackles, watch);
	return true;
}

/**
 * Removes the first directory on the tackle list.
 */
void 
remove_first_tackle() {
	int tw = tackles->data[0];
	memmove(tackles->data, tackles->data + 1, (--tackles->len) * sizeof(int));
	dir_watches->data[tw].tackled = false;
}

/*--------------------------------------------------------------------------*
 * ToSync Stack handling. 
 *--------------------------------------------------------------------------*/

/**
 * Parses an option text, replacing all '%' specifiers with 
 * elaborated stuff. duh, currently there is only one, so this 
 * fuction is a bit overkill but oh well :-)
 *
 * @param text      string to parse.
 * @param recursive info for replacements.
 *
 * @return a newly allocated string.
 */
char *
parse_option_text(const struct log *log, char *text, bool recursive)
{
	char * str = s_strdup(log, text);
	char * chr; // search result for %.

	// replace all '%' specifiers with there special meanings
	for(chr = strchr(str, '%'); chr; chr = strchr(str, '%')) {
		char *p;
		// chr points now to the '%' thing to be replaced
		switch (chr[1]) {
		case 'r' : // replace %r with 'r' when recursive or 'd' when not.
			chr[0] = recursive ? 'r' : 'd';
			for(p = chr + 1; *p != 0; p++) {
				p[0] = p[1];
			}
			break;
		case 0:    // wtf, '%' was at the end of the string!
		default :  // unknown char
			printlogf(log, ERROR, 
			          "don't know how to handle '\%' specifier in \"%s\"!", *text);
			terminate(log, LSYNCD_BADPARAMETERS);
		}
	}
	return str;
}

/**
 * Creates one string with all arguments concated.
 *
 * @param argv the arguments
 * @param argc number of arguments
 */
char *
get_arg_str(const struct log *log, char **argv, int argc) {
	int i;
	int len = 0;
	char * str;

	// calc length
	for (i = 0; i < argc; i++) {
		len += strlen(argv[i]);
	}

    // alloc 
	str = s_malloc(log, len + 2 * argc + 1);
		
	str[0] = 0;
	for(i = 0; i < argc; i++) {
		if (i > 0) {
			strcat(str, ", ");
		}
		strcat(str, argv[i]);
	}
	return str;
}

/**
 * Calls the specified action (most likely rsync) to sync from src to dest.
 * Returns after the forked process has finished.
 *
 * @param dir_conf  The config the is applicatable for this dir.
 * @param src       Source string.
 * @param dest      Destination string,
 * @param recursive If true -r will be handled on, -d (single directory) otherwise
 * @return true if successful, false if not.
 *
 * TODO change dir_conf and src pointer simply to index offset.
 */
bool
action(const struct global_options *opts,
       struct dir_conf * dir_conf, 
       char const * src, 
       const char * dest, 
       bool recursive)
{
	pid_t pid;
	int status;
	const int MAX_ARGS = 100;
	char * argv[MAX_ARGS];
	int argc = 0;
	int i;
	struct call_option* optp;
	const struct log* log = &opts->log;
	
	optp = dir_conf->callopts ? dir_conf->callopts : opts->default_callopts;

	// makes a copy of all call parameters
	// step 1 binary itself
	argv[argc++] = s_strdup(log, dir_conf->binary ? dir_conf->binary : opts->default_binary);
	// now all other parameters
	for(; optp->kind != CO_EOL; optp++) {
		switch (optp->kind) {
		case CO_TEXT :
			argv[argc++] = parse_option_text(log, optp->text, recursive);
			continue;
		case CO_EXCLUDE :
		    // --exclude-from and the exclude file
		    // insert only when the exclude file is present otherwise skip it.
			if (dir_conf->exclude_file == NULL && opts->default_exclude_file == NULL) {
				continue;
			}
			argv[argc++] = s_strdup(log, "--exclude-from");
			argv[argc++] = s_strdup(log, dir_conf->exclude_file ? dir_conf->exclude_file : opts->default_exclude_file); 
			continue;
		case CO_SOURCE :
			argv[argc++] = s_strdup(log, src);
			continue;
		case CO_DEST :
			argv[argc++] = s_strdup(log, dest);
			continue;
		default:
			assert(false);
		}
		if (argc >= MAX_ARGS) {
			/* check for error condition */
			printlogf(log, ERROR, 
			          "Internal error: too many (>%i) options passed", argc);
			return false;
		}
	}
	argv[argc++] = NULL;

	if (opts->flag_dryrun) {
		// just make a nice log message
		char * binary = dir_conf->binary ? dir_conf->binary : opts->default_binary;
		char * argall = get_arg_str(log, argv, argc);
		printlogf(log, NORMAL, "dry run: would call %s(%s)", binary, argall); 
		free(argall);
		for (i = 0; i < argc; ++i) {
			if (argv[i]) {
				free(argv[i]);
			}
		}
		return true;
	}

	pid = fork();

	if (pid == 0) {
		char * binary = dir_conf->binary ? dir_conf->binary : opts->default_binary;
		if (!log->flag_nodaemon && log->logfile) {
			if (!freopen(log->logfile, "a", stdout)) {
				printlogf(log, ERROR, "cannot redirect stdout to [%s].", log->logfile);
			}
			if (!freopen(log->logfile, "a", stderr)) {
				printlogf(log, ERROR, "cannot redirect stderr to [%s].", log->logfile);
			}
		}

		execv(binary, argv);
		// in a sane world execv does not return!
		printlogf(log, ERROR, "Failed executing [%s]", binary);
		terminate(log, LSYNCD_INTERNALFAIL);
	}

	// free the memory from the arguments.
	for (i = 0; i < argc; ++i) {
		if (argv[i]) {
			free(argv[i]);
		}
	}
	
	waitpid(pid, &status, 0);
	assert(WIFEXITED(status));
	if (WEXITSTATUS(status) == LSYNCD_INTERNALFAIL){
		printlogf(log, ERROR, 
		          "Fork exit code of %i, execv failure", 
		          WEXITSTATUS(status));
		return false;
	} else if (WEXITSTATUS(status)) {
		printlogf(log, NORMAL, 
		          "Forked binary process returned non-zero return code: %i", 
		          WEXITSTATUS(status));
		return false;
	}

	printlogf(log, DEBUG, "Rsync of [%s] -> [%s] finished", src, dest);
	return true;
}

/**
 * Adds a directory to watch.
 *
 * @param log        logging information.
 * @param inotify_fd inotify file descriptor.
 * @param pathname   the absolute path of the directory to watch.
 * @param dirname    the name of the directory only (yes this is a bit redudant, but oh well)
 * @param parent     if not -1 the index to the parent directory that is already watched
 * @param dir_conf   the applicateable configuration
 *
 * @return index to dir_watches of the new dir, -1 on error.
 */
int
add_watch(const struct log *log,
          int inotify_fd,
          char const * pathname, 
          char const * dirname, 
          int parent, 
          struct dir_conf * dir_conf)
{
	int wd;    // kernels inotify descriptor
	int newdw; // position to insert this watch into the watch vector

	wd = inotify_add_watch(inotify_fd, pathname,
	                       IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | 
	                       IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM | 
	                       IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR);

	if (wd == -1) {
		printlogf(log, ERROR, "Cannot add watch %s (%d:%s)", 
		          pathname, errno, strerror(errno));
		return -1;
	}

	// look if an unused slot can be found.
	for (newdw = 0; newdw < dir_watches->len; newdw++) {
		if (dir_watches->data[newdw].wd < 0) {
			break;
		}
	}

	if (newdw == dir_watches->len) {
		// TODO move this
		if (dir_watches->len + 1 >= dir_watches->size) {
			dir_watches->size *= 2;
			dir_watches->data = s_realloc(log, dir_watches->data, 
			                        dir_watches->size * sizeof(struct dir_watch));
		}
		dir_watches->len++;
	}

	dir_watches->data[newdw].wd = wd;
	dir_watches->data[newdw].parent = parent;
	dir_watches->data[newdw].dirname = s_strdup(log, dirname);
	dir_watches->data[newdw].dir_conf = dir_conf;
	dir_watches->data[newdw].alarm = 0; // not needed, just to be save
	dir_watches->data[newdw].tackled = false;
	return newdw;
}

/**
 * Writes the path of a watched directory into pathname.
 *
 * @param pathname path to write to
 * @param pathsize size of the pathname buffer
 * @param watch index of watched dir to build path for
 * @param prefix replace root dir with this (as target)
 *
 * @return -1 if pathname buffer was too small 
 *            contents of pathname will be garbled then.
 *         strlen(pathname) if successful
 */
int
builddir(char *pathname, int pathsize, int watch, char const * prefix)
{
	int len = 0;
	if (watch == -1) {
		// When is this called this way???
		char const * p = prefix ? prefix : "";
		len = strlen(p);
		if (pathsize <= len) {
			return -1;
		}
		strcpy(pathname, p);
	} else if (dir_watches->data[watch].parent == -1) {
		// this is a watch root.
		char const * p = prefix ? prefix : dir_watches->data[watch].dirname;
		len = strlen(p);
		if (pathsize <= len) {
			return -1;
		}
		strcpy(pathname, p);
	} else {
		// this is some sub dir
		len = builddir(pathname, pathsize, dir_watches->data[watch].parent, prefix); /* recurse */
		len += strlen(dir_watches->data[watch].dirname);
		if (pathsize <= len) {
			return -1;
		}
		strcat(pathname, dir_watches->data[watch].dirname);
	}
	// add the trailing slash if it is missing
	if (*pathname && pathname[strlen(pathname)-1] != '/') {
		strcat(pathname, "/");
		len++;
	}
	return len;
}

/**
 * Builds the abolute path name of a given directory beeing 
 * watched from the dir_watches information.
 *
 * @param pathname      destination buffer to store the result to.
 * @param pathsize      max size of this buffer
 * @param watch         the index in dir_watches to the directory.
 * @param dirname       if not NULL it is added at the end of pathname
 * @param prefix        if not NULL it is added at the beginning of pathname
 */
bool
buildpath(const struct log *log, 
          char *pathname,
          int pathsize,
          int watch,
          const char *dirname,
          const char *prefix)
{
	int len = builddir(pathname, pathsize, watch, prefix);
	if (len < 0) {
		printlogf(log, ERROR, "path too long!");
		return false;
	}
	if (dirname) {
		if (pathsize < len + strlen(dirname) + 1) {
			printlogf(log, ERROR, "path too long!");
			return false;
		}
		strcat(pathname, dirname);
	}
	printlogf(log, DEBUG, "  BUILDPATH(%d, %s, %s) -> %s", watch, dirname, prefix, pathname);
	return true;
}

/**
 * Syncs a directory.
 *   TODO: make better error handling (differ between
 *         directory gone away, and thus cannot work, or network
 *         failed)
 *
 * @param watch   the index in dir_watches to the directory.
 *
 * @returns true when all targets were successful.
 */
bool
rsync_dir(const struct global_options *opts, int watch)
{
	char pathname[PATH_MAX+1];
	char destname[PATH_MAX+1];
	bool status = true;
	char ** target;
	const struct log *log = &opts->log;

	if (!buildpath(log, pathname, sizeof(pathname), watch, NULL, NULL)) {
		return false;
	}

	for (target = dir_watches->data[watch].dir_conf->targets; *target; target++) {
		if (!buildpath(log, destname, sizeof(destname), watch, NULL, *target)) {
			status = false;
			continue;
		}
		printlogf(log, NORMAL, "rsyncing %s --> %s", pathname, destname);

		// call rsync to propagate changes in the directory
		if (!action(opts, dir_watches->data[watch].dir_conf, pathname, destname, false)) {
			printlogf(log, ERROR, "Rsync from %s to %s failed", pathname, destname);
			status = false;
		}
	}
	return status;
}

/**
 * Puts a directory on the TO-DO list. Waiting for its delay 
 * to be actually executed.
 *
 * Directly calls rsync_dir if delay == 0;
 *
 * @param watch   the index in dir_watches to the directory.
 * @param alarm   times() when the directory handling should be fired.
 */
void
tackle_dir(const struct global_options *opts, int watch, clock_t alarm)
{
	char pathname[PATH_MAX+1];
	const struct log *log = &opts->log;
	
	if (opts->delay == 0) {
		rsync_dir(opts, watch);
		return;
	}

	if (!buildpath(log, pathname, sizeof(pathname), watch, NULL, NULL)) {
		return;
	}

	if (append_tackle(log, watch, alarm)) {
		printlogf(log, NORMAL, "Putted %s on a delay", pathname);
	} else {
		printlogf(log, NORMAL, "Not acted on %s already on delay", pathname);
	}
}

/**
 * Adds a directory including all subdirectories to watch.
 * Puts the directory with all subdirectories on the tackle FIFO.
 *
 * @param opts       global options
 * @param inotify_fd inotify file descriptor.
 * @param dirname    The name or absolute path of the directory to watch.
 * @param parent     If not -1, the index in dir_watches to the parent directory already watched.
 *                   Must have absolute path if parent == -1.
 * @param dir_conf   ???  TODO
 *
 * @returns the index in dir_watches of the directory or -1 on fail.
 */
int
add_dirwatch(const struct global_options *opts,
			 int inotify_fd,
             char const *dirname, 
			 int parent, 
			 struct dir_conf *dir_conf)
{
	const struct log *log = &opts->log;
	DIR *d;

	int dw;
	char pathname[PATH_MAX+1];

	printlogf(log, DEBUG, "add_dirwatch(%s, p->dirname:%s, ...)", 
	          dirname,
	          parent >= 0 ? dir_watches->data[parent].dirname : "NULL");

	if (!buildpath(log, pathname, sizeof(pathname), parent, dirname, NULL)) {
		return -1;
	}

	{
		int i;
		for (i = 0; i < exclude_dir_n; i++) {
			if (!strcmp(pathname, exclude_dirs[i])) {
				printlogf(log, NORMAL, "Excluded %s", pathname);
				return -1;
			}
			printlogf(log, DEBUG, "comparing %s with %s not an exclude so far.", pathname, exclude_dirs[i]);
		}
	}

	// watch this directory
	dw = add_watch(log, inotify_fd, pathname, dirname, parent, dir_conf);
	if (dw == -1) {
		return -1;
	}

	// put this directory on list to be synced ASAP.
	tackle_dir(opts, dw, times(NULL));
	
	if (strlen(pathname) + strlen(dirname) + 2 > sizeof(pathname)) {
		printlogf(log, ERROR, "pathname too long %s//%s", pathname, dirname);
		return -1;
	}

	d = opendir(pathname);
	if (d == NULL) {
		printlogf(log, ERROR, "cannot open dir %s.", dirname);
		return -1;
	}

	while (keep_going) {    // terminate early on KILL signal
		bool isdir;
		struct dirent *de = readdir(d);

		if (de == NULL) {   // finished reading the directory
			break;
		}

		// detemine if an entry is a directory or file
		if (de->d_type == DT_DIR) {
			isdir = true;
		} else if (de->d_type == DT_UNKNOWN) {
			// in case of reiserfs, d_type will be UNKNOWN, how evil! :-(
			// use traditional means to determine if its a directory.
			char subdir[PATH_MAX+1];
			struct stat st;
			isdir = buildpath(log, subdir, sizeof(subdir), dw, de->d_name, NULL) && 
			        !stat(subdir, &st) && 
			        S_ISDIR(st.st_mode);
		} else {
			isdir = false;
		}

		// add watches if its a directory and not . or ..
		if (isdir && strcmp(de->d_name, "..") && strcmp(de->d_name, ".")) {
			// recurse into subdir
			int ndw = add_dirwatch(opts, inotify_fd, de->d_name, dw, dir_conf); 
			printlogf(log, NORMAL, 
			          "found new directory: %s in %s -- %s", 
			          de->d_name, dirname, ndw >= 0 ? "will be synced" : "ignored it");
		}
	}

	closedir(d);
	return dw;
}

/**
 * Removes a watched dir, including recursevily all subdirs.
 *
 * @param opts       global options.
 * @param inotify_fd inotify file descriptor.
 * @param name       optionally. If not NULL, the directory name 
 *                   to remove which is a child of parent.
 * @param parent     the index to the parent directory of the 
 *                   directory 'name' to remove, or to be removed 
 *                   itself if name == NULL.
 */
bool
remove_dirwatch(const struct global_options *opts,
                int inotify_fd,
                const char * name, 
				int parent)
{
	const struct log *log = &opts->log;
	int dw;   // the directory index to remove.
	if (name) {
		int i;
		// look for the child with the name
		for (i = 0; i < dir_watches->len; i++) {
			struct dir_watch *p = dir_watches->data + i;
			if (p->wd >= 0 && p->parent == parent && !strcmp(name, p->dirname)) {
				dw = i;
				break;
			}
		}

		if (i >= dir_watches->len) {
			printlogf(log, ERROR, "Cannot find entry for %s:/:%s :-(", 
			          dir_watches->data[parent].dirname, name);
			return false;
		}
	} else {
		dw = parent;
	}

	{
		// recurse into all subdirectories removing them.
		int i;
		for (i = 0; i < dir_watches->len; i++) {
			if (dir_watches->data[i].wd >= 0 && dir_watches->data[i].parent == dw) {
				remove_dirwatch(opts, inotify_fd, NULL, i);
			}
		}
	}

	inotify_rm_watch(inotify_fd, dir_watches->data[dw].wd);
	// mark this entry invalid (cannot remove, since indexes point into this vector)
	// TODO from where?
	dir_watches->data[dw].wd = -1;  

	free(dir_watches->data[dw].dirname);
	dir_watches->data[dw].dirname = NULL;

	// remove a possible tackle
	// (this dir is on the to do/delay list)
	if (tackles->len > 0 && dir_watches->data[dw].tackled) {
		int i;
		for(i = 0; i < tackles->len; i++) {
			if (tackles->data[i] == dw) {
				// move the list up.
				memmove(tackles->data + i, tackles->data + i + 1, (tackles->len - i - 1) * sizeof(int));
				tackles->len--;
				break;
			} 
		}
	}

	return true;
}

/**
 * Find the matching dw entry from wd (watch descriptor), and return
 * the offset in the table.
 *
 * @param wd   The wd (watch descriptor) given by inotify
 * @return offset, or -1 if not found
 */
int
get_dirwatch_offset(int wd) {
	int i;
	for (i = 0; i < dir_watches->len; i++) {
		if (dir_watches->data[i].wd == wd) {
			return i;
		}
	}
	return -1;
}

/**
 * Handles an inotify event.
 * 
 * @param opts       global options
 * @param inotify_fd inotify file descriptor
 * @param event      the event to handle
 * @param alarm      times() moment when it should fire
 */
bool 
handle_event(const struct global_options *opts, 
             int inotify_fd,
             struct inotify_event *event, 
			 clock_t alarm)
{
	char masktext[255] = {0,};
	int mask = event->mask;
	int i, watch;
	int subwatch = -1;
	struct inotify_mask_text *p;
	const struct log *log = &opts->log;

	// creates a string for logging that shows which flags 
	// were raised in the event
	for (p = mask_texts; p->mask; p++) {
		if (mask & p->mask) {
			if (strlen(masktext) + strlen(p->text) + 3 >= sizeof(masktext)) {
				printlogf(log, ERROR, "bufferoverflow in handle_event");
				return false;
			}

			if (*masktext) {
				strcat(masktext, ", ");
			}

			strcat(masktext, p->text);
		}
	}
	printlogf(log, DEBUG, "inotfy event: %s:%s", masktext, event->name);

	if (IN_IGNORED & event->mask) {
		return true;
	}

	// TODO, is this needed?
	for (i = 0; i < exclude_dir_n; i++) {
		if (!strcmp(event->name, exclude_dirs[i])) {
			return true;
		}
	}

	watch = get_dirwatch_offset(event->wd);
	if (watch == -1) {
		// this can happen in case of data moving faster than lsyncd can monitor it.
		printlogf(log, NORMAL, 
		          "received an inotify event that doesnt match any watched directory.");
		return false;
	}

	// in case of a new directory create new watches
	if (((IN_CREATE | IN_MOVED_TO) & event->mask) && (IN_ISDIR & event->mask)) {
		subwatch = add_dirwatch(opts, inotify_fd, event->name, watch, dir_watches->data[watch].dir_conf);
	}

	// in case of a removed directory remove watches
	if (((IN_DELETE | IN_MOVED_FROM) & event->mask) && (IN_ISDIR & event->mask)) {
		remove_dirwatch(opts, inotify_fd, event->name, watch);
	}
	
	// call the binary if something changed
	if ((IN_ATTRIB | IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | 
	     IN_MOVED_TO | IN_MOVED_FROM) & event->mask
	   ) {
		printlogf(log, NORMAL, "received event %s:%s.", masktext, event->name);
		tackle_dir(opts, watch, alarm); 
	} else {
		printlogf(log, DEBUG, "... ignored this event.");
	}
	return true;
}

/**
 * The control loop waiting for inotify events.
 *
 * @param opts        global options
 * @param inotify_fd  inotify file descriptor
 */
bool 
master_loop(const struct global_options *opts,
            int inotify_fd)
{
	char buf[INOTIFY_BUF_LEN];
	int len, i = 0;
	long clocks_per_sec = sysconf(_SC_CLK_TCK);

	struct timeval tv;
	fd_set readfds;
	clock_t now;
	clock_t alarm;
	const struct log *log = &opts->log;
			
	FD_ZERO(&readfds);
	FD_SET(inotify_fd, &readfds);

	if (opts->delay > 0) {
		if (clocks_per_sec <= 0) {
			printlogf(log, ERROR, "Clocks per seoond invalid! %d", printlogf);
			terminate(log, LSYNCD_INTERNALFAIL);
		}
	}

	while (keep_going) {
		int do_read;
		if (tackles->len > 0 && time_after(times(NULL), dir_watches->data[tackles->data[0]].alarm)) {
			// there is a tackle that wants to be handled already
			// do not read from inotify_fd and jump directly to tackles handling
			printlogf(log, DEBUG, "immediately handling tackles");
			do_read = 0;
		} else if (opts->delay > 0 && tackles->len > 0) {
			// use select() to determine what happens first
			// a new event or "alarm" of an event to actually
			// call its binary. The tackle with the index 0 
			// should have the nearest alarm time.
			alarm = dir_watches->data[tackles->data[0]].alarm;
			now = times(NULL);
			tv.tv_sec  = (alarm - now) / clocks_per_sec;
			tv.tv_usec = (alarm - now) * 1000000 / clocks_per_sec % 1000000;
			if (tv.tv_sec > opts->delay) {
					// security boundary in case of times() wrap around.
					tv.tv_sec = opts->delay;
					tv.tv_usec = 0;
			}
			// if select returns a positive number there is data on inotify
			// on zero the timemout occured.
			do_read = select(inotify_fd + 1, &readfds, NULL, NULL, &tv);

			if (do_read) {
				printlogf(log, DEBUG, "theres data on inotify.");
			} else {
				printlogf(log, DEBUG, "select() timeouted, doiong tackles.");
			}
		} else {
			// if nothing to wait for, enter a blocking read
			printlogf(log, DEBUG, "gone blocking");
			do_read = 1;
		}

		if (do_read) {
			len = read (inotify_fd, buf, INOTIFY_BUF_LEN);
		} else {
			len = 0;
		}

		if (len < 0) {
			if (!keep_going) {
				printlogf(log, NORMAL, "read exited due to TERM signal.");
			} else {
				printlogf(log, ERROR, "failed to read from inotify (%d:%s)", errno, strerror(errno));
			}
			return false;
		}

		now = times(NULL);
		alarm = now + opts->delay * clocks_per_sec;

		// first handle all events that might have happened
		i = 0;
		while (i < len) {
			struct inotify_event *event = (struct inotify_event *) &buf[i];
			handle_event(opts, inotify_fd, event, alarm);
			i += sizeof(struct inotify_event) + event->len;
		}

		// Then pull of directories from the top of the tackle stack 
		// until one item is found whose expiry time has not yet come
		// or the stack is empty.
		while (tackles->len > 0 && time_after(times(NULL), dir_watches->data[tackles->data[0]].alarm)) {
			printlogf(log, DEBUG, "time for %d arrived.", tackles[0]);
			rsync_dir(opts, tackles->data[0]);
			remove_first_tackle();
		}
	}

	return true;
}

/**
 * Utility function to check file exists. 
 * Prints out error message and die.
 *
 * @param filename  filename to check
 */
void 
check_file_exists(const struct log* log, const char* filename, const char *errmsg)
{
	struct stat st;
	if (-1==stat(filename, &st)) {
		printlogf(log, ERROR, "%s [%s] does not exist.\n", filename);
		terminate(log, LSYNCD_FILENOTFOUND);
	}
}

/**
 * Utility function to check given path is absolute path.
 *
 * @param filename  Filename to check
 * @param errmsg    Filetype text to prepend to the error message.
 */
void
check_absolute_path(const struct log* log, const char* filename, const char *filetype)
{
	if (filename[0] != '/') {
		printlogf(log, ERROR, "%s [%s] has do be an absolute path.\n", filetype, filename);
		terminate(log, LSYNCD_FILENOTFOUND);
	}
}

/**
 * Prints the help text and exits 0.
 *
 * @param arg0   argv[0] to show what lsyncd was called with.
 */
void
print_help(char *arg0)
{
	printf("\n");
#ifdef XML_CONFIG
	printf("USAGE: %s [OPTION]... [SOURCE] [TARGET 1] [TARGET 2] ...\n", arg0);
#else
	printf("USAGE: %s [OPTION]... SOURCE TARGET-1 TARGET-2 ...\n", arg0);
#endif
	printf("\n");
	printf("SOURCE: a directory to watch and rsync.\n");
	printf("\n");
	printf("TARGET: can be any name accepted by rsync. e.g. \"foohost::barmodule/\"\n");
	printf("\n");
#ifdef XML_CONFIG
	printf("When called without SOURCE and TARGET, the\n");
	printf("configuration will be read from the config file\"\n");
#endif
	printf("\n");
	printf("OPTIONS:\n");
	printf("  --binary FILE          Call this binary to sync " "(DEFAULT: %s)\n", DEFAULT_BINARY);
#ifdef XML_CONFIG
	printf("   --conf FILE           Load configuration from this file\n");
	printf("                         (DEFAULT: %s if called without SOURCE/TARGET)\n", DEFAULT_CONF_FILENAME);
#endif
	printf("  --debug                Log debug messages\n");
	printf("  --delay SECS           Delay between event and action\n");
	printf("  --dryrun               Do not call any actions, run dry only\n");
	printf("  --exclude-from FILE    Exclude file handled to rsync (DEFAULT: None)\n");
	printf("  --help                 Print this help text and exit.\n");
	printf("  --logfile FILE         Put log here (DEFAULT: uses syslog if not specified)\n"); 
	printf("  --no-daemon            Do not detach, log to stdout/stderr\n");
	printf("  --no-startup           Do not execute a startup sync (disadviced, know what you doing)\n");
	printf("  --pidfile FILE         Create a file containing pid of the daemon\n");
	printf("  --scarce               Only log errors\n");
	printf("  --stubborn             Ignore rsync errors on startup.\n");
	printf("  --version              Print version an exit.\n");
	printf("\n");
	printf("EXCLUDE FILE: \n");
	printf("  The exclude file may have either filebased general masks like \"*.php\" without directory specifications,\n");
	printf("  or exclude complete directories like \"Data/\". lsyncd will recognize directory excludes by the trailing '/'\n");
	printf("  and will not add watches of directories of exactly such name including sub-directories of them.\n");
	printf("  Please do not try to use more sophisticated exclude masks like \"Data/*.dat\" or \"Da*a/\", \"Data/Volatile/\" etc.\n");
	printf("  This will not work like you would expect it to.\n");
	printf("\n");
	printf("LICENSE\n");
	printf("  GPLv2 or any later version. See COPYING\n");
	printf("\n");
#ifndef XML_CONFIG
	printf("(this lsyncd binary was not compiled to be able to read config files)\n");
#endif
	exit(0);
}

#ifdef XML_CONFIG
/*--------------------------------------------------------------------------*
 * Config file parsing
 *--------------------------------------------------------------------------*/

/**
 * Parses <callopts>
 *
 * @return the allocated and filled calloptions structure
 */
struct call_option *
parse_callopts(struct global_options *opts, xmlNodePtr node) {
	xmlNodePtr cnode;
	xmlChar *xc;
	int opt_n = 0;
	struct call_option * asw;

	// count how many options are there
	for (cnode = node->children; cnode; cnode = cnode->next) {
		if (cnode->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (xmlStrcmp(cnode->name, BAD_CAST "option") &&
		    xmlStrcmp(cnode->name, BAD_CAST "exclude-file") &&
		    xmlStrcmp(cnode->name, BAD_CAST "source") &&
		    xmlStrcmp(cnode->name, BAD_CAST "destination")
		   ) {
			printlogf(NULL, ERROR, "error unknown call option type \"%s\"", cnode->name);
			terminate(NULL, LSYNCD_BADCONFIGFILE);
		}
		opt_n++;
	}
	opt_n++;
	asw = (struct call_option *) s_calloc(NULL, opt_n, sizeof(struct call_option));

	// fill in the answer
	opt_n = 0;
	for (cnode = node->children; cnode; cnode = cnode->next) {
		if (cnode->type != XML_ELEMENT_NODE) {
			continue;
		}
		asw[opt_n].text = NULL;
		if (!xmlStrcmp(cnode->name, BAD_CAST "option")) {
			xc = xmlGetProp(cnode, BAD_CAST "text");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: text attribute missing from <option/>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			asw[opt_n].kind = CO_TEXT;
			asw[opt_n].text = s_strdup(NULL, (char *) xc);
		} else if (!xmlStrcmp(cnode->name, BAD_CAST "exclude-file")) {
			asw[opt_n].kind = CO_EXCLUDE;
		} else if (!xmlStrcmp(cnode->name, BAD_CAST "source")) {
			asw[opt_n].kind = CO_SOURCE;
		} else if (!xmlStrcmp(cnode->name, BAD_CAST "destination")) {
			asw[opt_n].kind = CO_DEST;
		} else {
			assert(false);
		}
		opt_n++;
	}
	asw[opt_n].text = NULL;
	asw[opt_n].kind = CO_EOL;
	return asw;
}

/**
 * Parses <diretory>
 */
bool
parse_directory(struct global_options *opts, xmlNodePtr node) {
	xmlNodePtr dnode;
	xmlChar *xc;
	struct dir_conf * dc = new_dir_conf(opts);
	for (dnode = node->children; dnode; dnode = dnode->next) {
		if (dnode->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(dnode->name, BAD_CAST "source")) {
			xc = xmlGetProp(dnode, BAD_CAST "path");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute path missing from <source>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			if (dc->source) {
				printlogf(NULL, ERROR, "error in config file: cannot have more than one source in one <directory>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			// TODO: use realdir() on xc
			dc->source = s_strdup(NULL, (char *) xc);
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "target")) {
			xc = xmlGetProp(dnode, BAD_CAST "path");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute path missing from <target>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			dir_conf_add_target(NULL, dc, (char *) xc);
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "binary")) {
			xc = xmlGetProp(dnode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <binary>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			dc->binary = s_strdup(NULL, (char *) xc); 
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "exclude-from")) {
			xc = xmlGetProp(dnode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <exclude-from>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			dc->exclude_file = s_strdup(NULL, (char *) xc); 
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "callopts")) {
			if (dc->callopts) {
				printlogf(NULL, ERROR, "error in config file: there is more than one <callopts> in a <directory>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			dc->callopts = parse_callopts(opts, dnode);
		} else {
			// TODO missing sourcespecific exclude files?
			printlogf(NULL, ERROR, "error in config file: unknown node in <directory> \"%s\"\n", dnode->name);
			terminate(NULL, LSYNCD_BADCONFIGFILE);
		}
	}
	if (!dc->source) {
		printlogf(NULL, ERROR, "error in config file: source missing from <directory>\n");
		terminate(NULL, LSYNCD_BADCONFIGFILE);
	}
	if (dc->targets[0] == NULL) {
		printlogf(NULL, ERROR, "error in config file: target missing from <directory>\n");
		terminate(NULL, LSYNCD_BADCONFIGFILE);
	}
	return true;
}

/**
 * Parses <settings>
 */
bool 
parse_settings(struct global_options *opts, xmlNodePtr node) {
	xmlNodePtr snode;
	xmlChar *xc;

	for (snode = node->children; snode; snode = snode->next) {
		if (snode->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(snode->name, BAD_CAST "debug")) {
			opts->log.loglevel = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "delay")) {
			char *p;
			xc = xmlGetProp(snode, BAD_CAST "value");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute value missing from <delay/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			opts->delay = strtol((char *) xc, &p, 10);
			if (*p) {
				printlogf(NULL, ERROR, "<delay> value %s is not an integer.\n", xc);
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			if (opts->delay < 0) {
				printlogf(NULL, ERROR, "<delay> value may not be negative.\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
		} else if (!xmlStrcmp(snode->name, BAD_CAST "dryrun")) {
			opts->flag_dryrun = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "exclude-from")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <exclude-from/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			opts->default_exclude_file = s_strdup(NULL, (char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "logfile")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <logfile/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			opts->log.logfile = s_strdup(NULL, (char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "binary")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <binary/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			opts->default_binary = s_strdup(NULL, (char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "pidfile")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <pidfile/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			opts->pidfile = s_strdup(NULL, (char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "callopts")) {
			opts->default_callopts = parse_callopts(opts, snode);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "scarce")) {
			opts->log.loglevel = 3;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "no-daemon")) {
			opts->log.flag_nodaemon = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "no-startup")) {
			opts->flag_nostartup = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "stubborn")) {
			opts->flag_stubborn = 1;
		} else {
			printlogf(NULL, ERROR, "error unknown node in <settings> \"%s\"", snode->name);
			terminate(NULL, LSYNCD_BADCONFIGFILE);
		}
	}
	return true;
}

/**
 * Parses the config file specified in the global variable
 * conf_filename, fills the global options value according
 * to the config file.
 *
 * @param fullparse       if false only read globals.
 */
bool
parse_config(struct global_options *opts, bool fullparse) {
	LIBXML_TEST_VERSION
	xmlDoc *doc = NULL;
	xmlNode *root_element = NULL;
	xmlNodePtr node;
	xmlChar *xc;

	doc = xmlReadFile(opts->conf_filename, NULL, 0);
	if (doc == NULL) {
		printlogf(NULL, ERROR, "error: could not parse config file \"%s\"\n", opts->conf_filename);
		terminate(NULL, LSYNCD_BADCONFIGFILE);
	}
	root_element = xmlDocGetRootElement(doc);

	// check version specifier
	if (xmlStrcmp(root_element->name, BAD_CAST "lsyncd")) {
		printlogf(NULL, ERROR, "error in config file: root node is not \"lsyncd\".\n");
		terminate(NULL, LSYNCD_BADCONFIGFILE);
	}
	xc = xmlGetProp(root_element, BAD_CAST "version");
	if (xc == NULL) {
		printlogf(NULL, ERROR, "error in config file: version specifier missing in \"%s\",\n", opts->conf_filename);
		terminate(NULL, LSYNCD_BADCONFIGFILE);
	}
	if (xmlStrcmp(xc, BAD_CAST "1") && xmlStrcmp(xc, BAD_CAST "1.25")) { //1.25, backward stuff
		printlogf(NULL, ERROR, "error in config file: expected a \"1\" versioned file, found \"%s\"\n", xc);
		terminate(NULL, LSYNCD_BADCONFIGFILE);
	}

	for (node = root_element->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(node->name, BAD_CAST "settings")) {
			parse_settings(opts, node);
		} else if (!xmlStrcmp(node->name, BAD_CAST "directory")) {
			if (fullparse) {
				parse_directory(opts, node);
			}
		} else {
			printlogf(NULL, ERROR, "error in config file: unknown node in <lsyncd> \"%s\"\n", node->name);
			terminate(NULL, LSYNCD_BADCONFIGFILE);
		}
	}

	xmlFreeDoc(doc);
	xmlCleanupParser();
	return true;
}
#endif

/**
 * Parses the command line options.
 *
 * terminates in some cases of badparameters, or on 
 * --version or --help
 */
void
parse_options(struct global_options *opts, int argc, char **argv)
{
	char **target;

	static struct option long_options[] = {
		{"binary",       1, NULL, 0}, 
#ifdef XML_CONFIG
		{"conf",         1, NULL, 0}, 
#endif
		{"debug",        0, NULL, 1},
		{"delay",        1, NULL, 0}, 
		{"dryrun",       0, NULL, 1}, 
		{"exclude-from", 1, NULL, 0}, 
		{"help",         0, NULL, 0}, 
		{"logfile",      1, NULL, 0}, 
		{"no-daemon",    0, NULL, 1}, 
		{"no-startup",   0, NULL, 1}, 
		{"pidfile",      1, NULL, 0}, 
		{"scarce",       0, NULL, 3},
		{"stubborn",     0, NULL, 1},
		{"version",      0, NULL, 0}, 
		{NULL,           0, NULL, 0}
	};
	bool read_conf = false;

	{
		// replace NULL targets with actual targets
		// because compiler wont allow to init with them.
		struct option *o;
		for(o = long_options; o->name; o++) {
			if (!strcmp("debug",      o->name)) o->flag = &opts->log.loglevel;
			if (!strcmp("dryrun",     o->name)) o->flag = &opts->flag_dryrun;
			if (!strcmp("no-daemon",  o->name)) o->flag = &opts->log.flag_nodaemon;
			if (!strcmp("no-startup", o->name)) o->flag = &opts->flag_nostartup;
			if (!strcmp("scarce",     o->name)) o->flag = &opts->log.loglevel;
			if (!strcmp("stubborn",   o->name)) o->flag = &opts->flag_stubborn;
		}
	}

#ifdef XML_CONFIG
	// First determine if the config file should be read at all. 
	// If so, read it before parsing all other options in detail, 
	// because command line options should overwrite settings in 
	// the confing file.
	//
	// There are 2 conditions in which the conf file is read, either 
	// --conf FILE is given as option, or there isn't a SOURCE and 
	// DESTINATION given, in which getting the config from the conf
	// file will be the default option.
	
	while (1) {
		int oi = 0;
		int c = getopt_long_only(argc, argv, "", long_options, &oi);
		if (c == -1) {
			break;
		}
		if (c == '?') {
			terminate(NULL, LSYNCD_BADPARAMETERS);
		}
		if (c == 0) { // longoption
			if (!strcmp("conf", long_options[oi].name)) {
				read_conf = true;
				opts->conf_filename = s_strdup(NULL, optarg);
			} 
			
			if (!strcmp("help", long_options[oi].name)) {
				// in case --help do not go further, or else 
				// lsyncd would complain of not being configured ...
				print_help(argv[0]);
			}

			if (!strcmp("version", long_options[oi].name)) {
				// same here 
				printf("Version: %s\n", VERSION);
				terminate(NULL, LSYNCD_SUCCESS);
			}
		}
	}
	if (read_conf) {
		// parse config file, when additional source/dest parameters are 
		// given on the command line, then the directory settings
		// in the config file are ignored.
		parse_config(opts, optind == argc);
	} else if (optind == argc) {
		// when no config file is specified and there are also
		// no source/targets, read the default config file.
		parse_config(opts, true);
	}

	// resets the get option parser
	optind = 1;
#endif

	// now parse all the other options normally.
	while (1) {
		int oi = 0;
		int c = getopt_long_only(argc, argv, "", long_options, &oi);
		if (c == -1) {
			break;
		}

		if (c == '?') {
			terminate(NULL, LSYNCD_BADPARAMETERS);
		}

		if (c == 0) { // longoption
			if (!strcmp("binary", long_options[oi].name)) {
				opts->default_binary = s_strdup(NULL, optarg);
			}
			
			if (!strcmp("delay", long_options[oi].name)) {
				char *p;
				opts->delay = strtol(optarg, &p, 10);
				if (*p) {
					printf("%s is not an integer.\n", optarg);
					terminate(NULL, LSYNCD_BADPARAMETERS);
				}
				if (opts->delay < 0) {
					printf("delay may not be negative.\n");
					terminate(NULL, LSYNCD_BADPARAMETERS);
				}
			}
			
			if (!strcmp("exclude-from", long_options[oi].name)) {
				opts->default_exclude_file = s_strdup(NULL, optarg);
			}

			if (!strcmp("help", long_options[oi].name)) {
				print_help(argv[0]);
			}

			if (!strcmp("logfile", long_options[oi].name)) {
				opts->log.logfile = s_strdup(NULL, optarg);
			}
			
			if (!strcmp("pidfile", long_options[oi].name)) {
				opts->pidfile = s_strdup(NULL, optarg);
			}

			if (!strcmp("version", long_options[oi].name)) {
				printf("Version: %s\n", VERSION);
				terminate(NULL, LSYNCD_SUCCESS);
			}
		}
	}

	// If the config file specified something to do already
	// dir_conf_n will already be > 0
	if (opts->dir_conf_n == 0) {
		struct dir_conf * odc;    // dir_conf specified by command line options.
		bool first_target = true;

		if (optind + 2 > argc) {
			printf("Error: please specify SOURCE and at least one TARGET (see --help)\n");
#ifdef XML_CONFIG
			printf("       or at least one <directory> entry in the conf file.\n");
#endif
			terminate(NULL, LSYNCD_BADPARAMETERS);
		}
		odc = new_dir_conf(opts);
		/* Resolves relative source path, lsyncd might chdir to / later. */
		odc->source = realdir(NULL, argv[optind]);
		if (!odc->source) {
			printf("Error: Source [%s] not found or not a directory.\n", argv[optind]);
			terminate(NULL, LSYNCD_FILENOTFOUND);
		}
		for (target = &argv[optind + 1]; *target; target++) {
			dir_conf_add_target(NULL, odc, *target);
			if (first_target) {
				printlogf(&opts->log, NORMAL, "command line options: syncing %s -> %s\n",
				          odc->source, *target);
				first_target = false;
			} else {
				printlogf(&opts->log, NORMAL, "                             and -> %s\n", 
				          *target);
			}
		}
	}

	// some sanity checks
	if (opts->default_exclude_file) {
		check_absolute_path(NULL, opts->default_exclude_file, "Exclude file");
		check_file_exists  (NULL, opts->default_exclude_file, "Exclude file");
	}
	if (opts->pidfile) {
		check_absolute_path(NULL, opts->pidfile, "Pid file");
	}
	if (opts->flag_stubborn && opts->flag_nostartup) {
		printlogf(&opts->log, NORMAL, "Warning: specifying 'stubborn' when skipping with 'no-startup' has no effect.");
	}
}

/**
 * Parses the exclude file looking for directory masks to not watch.
 */
bool
parse_exclude_file(struct log *log, char *filename) {
	FILE * ef;
	char line[PATH_MAX+1];
	int sl;

	ef = fopen(filename, "r");
	if (ef == NULL) {
		printlogf(log, ERROR, "Cannot open exclude file '%s'\n", filename);
		terminate(log, LSYNCD_FILENOTFOUND);
	}

	while (1) {
		if (!fgets(line, sizeof(line), ef)) {
			if (feof(ef)) {
				fclose(ef);
				return true;
			}
			printlogf(log, ERROR, "Reading file '%s' (%d:%s)\n", 
			          filename, errno, strerror(errno));

			terminate(log, LSYNCD_FILENOTFOUND);
		}

		sl = strlen(line);

		if (sl == 0) {
			continue;
		}

		if (line[sl - 1] == '\n') {
			line[sl - 1] = 0;
			sl--;
		}

		if (sl == 0) {
			continue;
		}

		if (line[sl - 1] == '/') {
			if (exclude_dir_n + 1 >= MAX_EXCLUDES) {
				printlogf(log, ERROR, 
				          "Too many directory excludes, can only have %d at the most", 
				          MAX_EXCLUDES);
				terminate(log, LSYNCD_TOOMANYDIRECTORYEXCLUDES);
			}

			line[sl - 1] = 0;

			sl--;

			if (sl == 0) {
				continue;
			}

			printlogf(log, NORMAL, "Excluding directories of the name '%s'", line);

			exclude_dirs[exclude_dir_n] = s_malloc(log, strlen(line) + 1);
			strcpy(exclude_dirs[exclude_dir_n], line);
			exclude_dir_n++;
		}
	}

	return true;
}

/**
 * Writes a pid file.
 */
void
write_pidfile(const struct log *log, const char *pidfile) {
	FILE* f = fopen(pidfile, "w");
	if (!f) {
		printlogf(log, ERROR, "Error: cannot write pidfile [%s]\n", pidfile);
		terminate(log, LSYNCD_FILENOTFOUND);
	}
	
	fprintf(f, "%i\n", getpid());
	fclose(f); 
}

/**
 * Main.
 */
int
main(int argc, char **argv)
{
	struct global_options opts = {{0,}};  // global options 
	struct log *log = &opts.log;
	int inotify_fd;                       // inotify file descriptor

	openlog("lsyncd", LOG_CONS | LOG_PID, LOG_DAEMON);

	reset_options(&opts);
	parse_options(&opts, argc, argv);

	if (opts.default_exclude_file) {
		parse_exclude_file(log, opts.default_exclude_file);
	}

	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		printlogf(log, ERROR, "Cannot create inotify instance! (%d:%s)", 
		          errno, strerror(errno));
		return LSYNCD_NOINOTIFY;
	}

	if (!opts.log.flag_nodaemon) {
		// this will make this process child of init
		// close stdin/stdout/stderr and 
		// chdir to /
		if (daemon(0, 0)) {
			printlogf(log, ERROR, "Cannot daemonize! (%d:%s)",
			          errno, strerror(errno));
			return LSYNCD_INTERNALFAIL;
		}
	}

	printlogf(log, NORMAL, "Starting up");

	if (opts.pidfile) {
		write_pidfile(log, opts.pidfile);
	}

    dir_watches->size = 2;
    dir_watches->data = s_calloc(log, dir_watches->size, sizeof(struct dir_watch));

	{
		// add all watches
		int i;
		for (i = 0; i < opts.dir_conf_n; i++) {
			printlogf(log, NORMAL, "watching %s", opts.dir_confs[i].source);
			add_dirwatch(&opts, inotify_fd, opts.dir_confs[i].source, -1, &opts.dir_confs[i]);
		}
	}

	// clears tackle FIFO again, because the startup recursive rsync will 
	// handle it eitherway or if started no-startup it has to be ignored.
	printlogf(log, DEBUG, "dumped list of stuff to do.");
	{
		int i;
		for(i = 0; i < tackles->len; i++) {
			dir_watches->data[i].tackled = false;
			dir_watches->data[i].alarm = 0;
		}
		tackles->len = 0;
	}

	// startup recursive sync.
	if (!opts.flag_nostartup) {
		int i;
		for (i = 0; i < opts.dir_conf_n; i++) {
			char **target;
			for (target = opts.dir_confs[i].targets; *target; ++target) {
				printlogf(log, NORMAL, "Initial recursive sync for %s -> %s", opts.dir_confs[i].source, *target);
				if (!action(&opts, &opts.dir_confs[i], opts.dir_confs[i].source, *target, true)) {
					printlogf(log, ERROR, "Initial rsync from %s -> %s failed%s", 
					          opts.dir_confs[i].source, *target,
					          opts.flag_stubborn ? ", but continuing because being stubborn." : ".");
					if (!opts.flag_stubborn) {
						terminate(log, LSYNCD_EXECFAIL);
					} 
				}
			}
		}
	} else {
		printlogf(log, NORMAL, "Skipped startup since nostartup flag is turned on.");
	}

	printlogf(log, NORMAL, 
	          "--- Entering normal operation with [%d] monitored directories ---",
	          dir_watches->len);

	signal(SIGTERM, catch_alarm);

	master_loop(&opts, inotify_fd);

	return 0;
}
