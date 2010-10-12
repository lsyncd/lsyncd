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
 * Define for debug memchecking.
 */
#define MEMCHECK

/**
 * Number of inotifies to read max. at once from the kernel.
 */
#define INOTIFY_BUF_LEN     (64 * (sizeof(struct inotify_event) + 16))

/**
 * Initial size of vectors
 */
#define VECT_INIT_SIZE 8

/**
 * Defaults values 
 */
#define DEFAULT_BINARY "/usr/bin/rsync"
#define DEFAULT_CONF_FILENAME "/etc/lsyncd.conf.xml"
const uint32_t standard_event_mask =
                        IN_ATTRIB   | IN_CLOSE_WRITE | IN_CREATE     |
						IN_DELETE   | IN_DELETE_SELF | IN_MOVED_FROM |
						IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR;
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
	DEBUG   = 1,
	VERBOSE = 2,
	NORMAL  = 3,
	ERROR   = 4,
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
	CO_FILTER,              // file filter
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
 	 * bitmask of inotify events to watch (defaults to global default setting)
 	 */
	uint32_t event_mask;

	/**
	 * the exclude-file to pass to rsync (defaults to global default setting)
	 * TODO, Currently ignored!
	 */
	char * exclude_file;
};

/**
 * Structure to store the directory watches.
 */
struct watch {
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
	 * Points to the parent. NULL if no parent.
	 */
	struct watch *parent;

	/**
	 * There is one or several delays to be handled 
	 * dor this directory
	 *
	 * In case of non-filtered opperation this points
	 * directly to the delay struct.
	 *
	 * In case of filtered opperatoin this points to
	 * a file_delay_vector struct.
	 */
	void * dirdelay;

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
	 * Global Option, if true handle files singular instead of grouping changes up into a dir.
	 *                by default call rsync with file filters.
	 */
	int flag_singular;

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
	 * Global Option: default bitmask of inotify events to react upon.
	 */
	uint32_t default_event_mask;

	/**
	 * Global Option: default exclude file
	 */
	char *default_exclude_file;

	/**
	 * Global Option: default options to call the binary with.
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
	{ CO_TEXT,    "-lts%r"   }, 
	{ CO_TEXT,    "--delete" },
	{ CO_EXCLUDE, NULL       },
	{ CO_FILTER,  NULL       },
	{ CO_SOURCE,  NULL       },
	{ CO_DEST,    NULL       },
	{ CO_EOL,     NULL       },
};

/**
 * Structure to store strings for the diversve inotfy masked events.
 * Used for comfortable log messages only.
 */
struct inotify_mask_text {
	/**
	 * the bit
	 */
	int mask;

	/**
	 * and its meaning
	 */
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
struct watch_vector {
	/**
	 * list of pointers to all watches
	 */
	struct watch **data;

	/**
	 * number of entries allocated
	 */
	size_t size;

	/**
	 * number of entries used
	 */
	size_t len;
};

/**
 * A delayed entry
 */
struct delay {
	/**
	 * Pointer the owner.
	 *
	 * In filtered  operation this points to a file_delay.
	 * In directory operation this points to the watch.
	 */
	void *owner;

	/**
	 * Alarm time for the delay.
	 */
	clock_t alarm;

	/**
	 * Pointer to the next delay.
	 */
	struct delay * next;

	/**
	 * Pointer to the before delay.
	 */
	struct delay * before;
};

/**
 * Holds all entries on delay.
 */
struct delay_vector {
	/**
	 * pointer to first delay
	 */
	struct delay *first;

	/**
	 * pointer to last delay
	 */
	struct delay *last;
};

/**
 * Delayed files for filtered operatoins
 */
struct file_delay {
	/**
	 * The filename without path.
	 */
	char *filename;

	/**
	 * The directory watch this file is in.
	 */
	struct watch *watch;

	/**
	 * The delay of this file.
	 */
	struct delay *delay;
};

/**
 * A vector of file delays.
 * In case of filtered operation dirdelay points
 * to one of these.
 */
struct file_delay_vector {
	/**
	 * The file delays.
	 */
	struct file_delay **data;

	/**
	 * size of vector
	 */
	size_t size;

	/**
	 * length of vector
	 */
	size_t len;
};

/**
 * Array of strings of directory names to include.
 * This is limited to MAX_EXCLUDES.
 * It's not worth to code a dynamic size handling...
 */
#define MAX_EXCLUDES 256
struct exclude_vector {
	char * data[MAX_EXCLUDES];
	size_t len;
};

/*--------------------------------------------------------------------------*
 * MEMCHECK
 *--------------------------------------------------------------------------*/
/**
 * This routines keep track which memory allocs
 * have not been freed. Debugging purposes. 
 */
#ifdef MEMCHECK
#include <search.h>
/**
 * Counts the number of s_[m|c]alloc's.
 */
int    memc = 0;

/**
 * A binary tree administered by the clib to store the 
 * pointers with a short description.
 */
void * mroot = NULL;

/**
 * An entry to that tree
 */
struct mentry {
	const void *data;
    const char *desc;
};

/**
 * Compares two pointers simply by their address
 */
int mcompare(const void *pa, const void *pb) {
	const struct mentry *ma = (const struct mentry *) pa;
	const struct mentry *mb = (const struct mentry *) pb;
	if (ma->data < mb->data) {
		return -1;
	}
	if (ma->data > mb->data) {
		return 1;
	}
	return 0;
}

/**
 * Prints nonfreed memory usage on exit/HUP
 */
void maction(const void *nodep, const VISIT which, const int depth) {
	if (which == leaf || which == postorder) {
		struct mentry * r = *((struct mentry **) nodep);
		memc--;
		fprintf(stderr, "<*> unfreed data %p:%s\n", r->data, r->desc);
	}
}
#endif

/*--------------------------------------------------------------------------*
 * Small generic helper routines. 
 *    (signal catching, memory fetching, message output)
 *--------------------------------------------------------------------------*/

/**
 * Set to 0 in signal handler, when lsyncd should end ASAP.
 * This can be either a TERM or a HUP signal.
 * In case of HUP killed is 0 and start over.
 */
volatile sig_atomic_t keep_going = 1;

/**
 * Received a TERM signal, TERMinate nicely.
 */
volatile sig_atomic_t termed = 0;

/**
 * Called (out-of-order) when signals arrive
 */
void
catch_alarm(int sig)
{
	switch(sig) {
	case SIGTERM :
		termed = 1;
		/* fall through */
	case SIGHUP :
		keep_going = 0;
	}
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

// activates gcc's printf warnings.
void
printlogf(const struct log *log, 
          int level, 
		  const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

void
printlogf(const struct log *log, 
          int level, 
		  const char *fmt, ...)
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

	case VERBOSE :
	case NORMAL  :
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
s_malloc(const struct log *log, size_t size, const char *desc)
{
	void *r = malloc(size);

	if (r == NULL) {
		printlogf(log, ERROR, "Out of memory!");
		terminate(log, LSYNCD_OUTOFMEMORY);
	}

#ifdef MEMCHECK
	{	
		struct mentry * mentry;
		memc++;
		mentry = malloc(sizeof(struct mentry));
		mentry->data = r;
		mentry->desc = desc;
		if (!mentry) {
			printlogf(log, ERROR, "Out of memory in memcheck!");
			terminate(log, LSYNCD_OUTOFMEMORY);
		}
		tsearch(mentry, &mroot, mcompare);
	}
#endif
	return r;
}

/**
 * "secured" calloc.
 */
void *
s_calloc(const struct log *log, size_t nmemb, size_t size, const char *desc)
{
	void *r = calloc(nmemb, size);

	if (r == NULL) {
		printlogf(log, ERROR, "Out of memory!");
		terminate(log, LSYNCD_OUTOFMEMORY);
	}

#ifdef MEMCHECK
	{	
		struct mentry * mentry;
		memc++;
		mentry = malloc(sizeof(struct mentry));
		mentry->data = r;
		mentry->desc = desc;
		if (!mentry) {
			printlogf(log, ERROR, "Out of memory in memcheck!");
			terminate(log, LSYNCD_OUTOFMEMORY);
		}
		tsearch(mentry, &mroot, mcompare);
	}
#endif

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

#ifdef MEMCHECK
	{	
		struct mentry * mentry = malloc(sizeof(struct mentry));
		struct mentry **ret;
		if (!mentry) {
			printlogf(log, ERROR, "Out of memory in memcheck!");
			terminate(log, LSYNCD_OUTOFMEMORY);
		}
		if (ptr == NULL) {
			fprintf(stderr, "<*> Reallocating NULL!?\n");
			return r;
		}
		// first delete the old entry
		mentry->data = ptr;
		ret = tfind(mentry, &mroot, mcompare);
		if (ret == NULL) {
			fprintf(stderr, "<*> Memcheck error, reallocating unknown pointer %p!\n", ptr);
			return r;
		}
		mentry->desc = (*ret)->desc;
		tdelete(mentry, &mroot, mcompare);
		// and reenter the reallocated entry
		mentry->data = r;
		tsearch(mentry, &mroot, mcompare);
	}
#endif

	return r;
}

/**
 * "secured" strdup.
 */
char *
s_strdup(const struct log *log, const char *src, const char *desc)
{
	char *s = strdup(src);

	if (s == NULL) {
		printlogf(log, ERROR, "Out of memory!");
		terminate(log, LSYNCD_OUTOFMEMORY);
	}

#ifdef MEMCHECK
	{	
		struct mentry * mentry;
		memc++;
		mentry = malloc(sizeof(struct mentry));
		mentry->data = s;
		mentry->desc = desc;
		if (!mentry) {
			printlogf(log, ERROR, "Out of memory in memcheck!");
			terminate(log, LSYNCD_OUTOFMEMORY);
		}
		tsearch(mentry, &mroot, mcompare);
	}
#endif

	return s;
}


#ifdef MEMCHECK
/**
 * Only needed when memory usage checking. 
 * Removes the entry of the freed memory from the tracking tree.
 */
void
s_free(void *p) {
	struct mentry mentry = {0,};
	struct mentry **r;
	memc--;
	if (p == NULL) {
		fprintf(stderr, "<*> Memcheck freeing NULL!\n");
		return;
	}
	mentry.data = p;
	r = tdelete(&mentry, &mroot, mcompare); 
	if (r == NULL) {
		fprintf(stderr, "<*> Memcheck error, freeing unknown pointer %p!\n", p);
	} 
	free(p);
}
#else
#define s_free(x) free(x)
#endif

/**
 * Returns the canonicalized path of a directory with a final '/'.
 * Makes sure it is a directory.
 */
char *
realdir(const struct log *log, const char *dir) 
{
	char* cs = s_malloc(log, PATH_MAX+1, "realdir/cs");
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
		s_free(cs);
		return NULL;
	}

	strcat(cs, "/");
	return cs;
}

/*--------------------------------------------------------------------------*
 * Options.
 *--------------------------------------------------------------------------*/

/**
 * Cleans up the memory used by a CO_EOL terminated array of call options
 *
 * @param call_options the array to free.
 */
void
free_options(struct call_option *options) {
	struct call_option *co = options;
	while (co->kind != CO_EOL) {
		if (co->text) {
			s_free(co->text);
		}
		co++;
	}
	s_free(options);
}

/**
 * (Re)sets global options to default values.
 */
void
reset_options(struct global_options *opts) {
	opts->log.loglevel = NORMAL;
	opts->log.flag_nodaemon = 0;

	if (opts->log.logfile) {
		s_free(opts->log.logfile);
		opts->log.logfile = NULL;
	}
	
	opts->flag_singular = 0;
	opts->flag_dryrun = 0;
	opts->flag_stubborn = 0;
	opts->flag_nostartup = 0;

	if (opts->pidfile) {
		s_free(opts->pidfile);
		opts->pidfile = NULL;
	} 
#ifdef XML_CONFIG
	if (opts->conf_filename) {
		s_free(opts->conf_filename);
	}
	opts->conf_filename = s_strdup(&opts->log, DEFAULT_CONF_FILENAME, "DEFAULT_CONF_FILENAME");
#endif

	if (opts->default_binary) {
		s_free(opts->default_binary);
	}
	opts->default_binary = s_strdup(&opts->log, DEFAULT_BINARY, "DEFAULT_BINARY");
	
	opts->default_event_mask = standard_event_mask;

	if (opts->default_exclude_file) {
		s_free(opts->default_exclude_file);
		opts->default_exclude_file = NULL;
	}

	if (opts->default_callopts != standard_callopts) {
		if (opts->default_callopts) {
			free_options(opts->default_callopts);
		}
		opts->default_callopts = standard_callopts;
	}

	opts->delay = 5;

	if (opts->dir_confs) {
		int i;
		for(i = 0; i < opts->dir_conf_n; i++) {
			struct dir_conf *dc = opts->dir_confs + i;
			if (dc->source) {
				s_free(dc->source);
			}
			{
				char **t = dc->targets;
				while (*t) {
					s_free(*t);
					t++;
				}
				s_free(dc->targets);
			}
			if (dc->binary) {
				s_free(dc->binary);
			}
			if (dc->callopts) {
				free_options(dc->callopts);
				dc->callopts = NULL;
			}
			if (dc->exclude_file) {
				s_free(dc->exclude_file);
			}
		}
		s_free(opts->dir_confs);
		opts->dir_confs = NULL;
	}
	opts->dir_conf_n = 0;
};


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
		opts->dir_confs[opts->dir_conf_n - 1].targets = s_calloc(log, 1, sizeof(char *), "dir_conf");
		return opts->dir_confs + opts->dir_conf_n - 1;
	} else {
		// create the memory.
		opts->dir_conf_n = 1;
		opts->dir_confs = s_calloc(log, opts->dir_conf_n, sizeof(struct dir_conf), "dir_confs");
		// creates targets NULL terminator (no targets yet)
		opts->dir_confs[0].targets = s_calloc(log, 1, sizeof(char *), "dir_conf-target");
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
	dir_conf->targets[target_n] = s_strdup(log, target, "dupped target");
	dir_conf->targets[target_n + 1] = NULL;
}

/*--------------------------------------------------------------------------*
 * Tackle list handling. 
 *--------------------------------------------------------------------------*/

/**
 * Adds a directory on the delays list
 *
 * @param opts       global options
 * @param delays     the delays FIFO
 * @param watch      the index in watches to the directory
 * @param alarm      times() when the directory should be acted
 * @param filename   for filtered operations filename (otherwise NULL)
 */
bool
append_delay(const struct global_options *opts,
             struct delay_vector *delays,
			 struct watch *watch, 
			 clock_t alarm,
			 const char *filename) 
{
	const struct log *log = &opts->log;
	struct delay * newd;
	if (watch->dirdelay) {
		if (!opts->flag_singular) {
			// already watched in non filtered operatoin
			return false;
		} else {
			// check if this filename is already in the vector
			struct file_delay_vector *fdv = (struct file_delay_vector *) watch->dirdelay;
			int i; 
			for (i = 0; i < fdv->len; i++) {
				if (!strcmp(filename, fdv->data[i]->filename)) {
					return false;
				}
			}
		}
	}
	newd = s_calloc(log, 1, sizeof(struct delay), "a delay");
	newd->alarm = alarm;
	newd->before = NULL;
	newd->next = NULL;

	if (opts->flag_singular) {
		struct file_delay_vector *fdv = watch->dirdelay;
		struct file_delay *fd; 
		if (!fdv) {
			fdv = s_calloc(log, 1, sizeof(struct file_delay_vector), "file delay vector");
			fdv->len = 0;
			fdv->size = VECT_INIT_SIZE;
			fdv->data = s_calloc(log, VECT_INIT_SIZE, sizeof(struct file_delay *), "file delay vector data");
			watch->dirdelay = fdv;
		} else {
			if (fdv->len + 1 >= fdv->size) {
				fdv->size *= 2;
				fdv->data = s_realloc(log, fdv->data, fdv->size * sizeof(struct file_delay *));
			}
		}
		fd = fdv->data[fdv->len] = s_calloc(log, 1, sizeof(struct file_delay), "a file delay");
		fdv->len++;
		fd->filename = s_strdup(log, filename, "file delay.filename");
		fd->watch = watch;
		fd->delay = newd;
		newd->owner = fd;
	} else {
		watch->dirdelay = newd;
		newd->owner = watch;
	}

	if (delays->last) {
		delays->last->next = newd;
		newd->before = delays->last;
		delays->last = newd;
	} else {
		// delays vector was empty
		delays->first = delays->last = newd;
	}
	return true;
}

/**
 * Removes a delay.
 *
 * @param delays   the delays vector.
 * @param d        the delay to remove.
 */
void
remove_delay(struct delay_vector *delays, struct delay *d)
{
	if (d->before) {
		d->before->next = d->next;
	} else { 
		// this was first entry
		delays->first = d->next;
	}
	if (d->next) {
		d->next->before = d->before;
	} else {
		// this was last entry
		delays->last = d->before;
	}
	s_free(d);
}


/**
 * Removes the first entry on the delay list.
 *
 * @param delays    the delay FIFO.
 */
void 
remove_first_delay(const struct global_options *opts, struct delay_vector *delays) 
{
	const struct log *log = &opts->log;
	struct delay *d = delays->first;
	if (opts->flag_singular) {
		struct file_delay *fd = (struct file_delay *) d->owner;
		struct watch *w = fd->watch;
		struct file_delay_vector *fdv = (struct file_delay_vector *) w->dirdelay;
		int p = 0;
		while (fdv->data[p] != fd) {
			p++;
			if (p >= fdv->len) {
				printlogf(log, ERROR, "Internal error: removing a file delay not in file delay vector");
				terminate(log, LSYNCD_INTERNALFAIL);
			}
		}
		s_free(fd->filename);
		memmove(fdv->data + p, fdv->data + p + 1, (fdv->len - p - 1) * sizeof(struct file_delay *));
		fdv->len--;
		s_free(fd);
		if (fdv->len == 0) {
			// remove the vector when its empty (not needed but memory footprint reduction)
			s_free(fdv->data);
			s_free(fdv);
			w->dirdelay = NULL;
		}
	} else {
		((struct watch *) d->owner)->dirdelay = NULL;
	}
	remove_delay(delays, delays->first);
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
	char * str = s_strdup(log, text, "dupped option text");
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
			          "don't know how to handle '%%%c' specifier in \"%s\"!", chr[1], text);
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

	argc--; // because of the terminating NULL.
	// calc length
	for (i = 0; i < argc; i++) {
		len += strlen(argv[i]);
	}

    // alloc 
	str = s_malloc(log, len + argc + 1, "argument string");
		
	str[0] = 0;
	for(i = 0; i < argc; i++) {
		if (i > 0) {
			strcat(str, " ");
		}
		strcat(str, argv[i]);
	}
	return str;
}

/**
 * Calls the specified action (most likely rsync) to sync from src to dest.
 * Returns after the forked process has finished.
 *
 * @param dir_conf    the config applicatable for this dir.
 * @param src         source string.
 * @param dest        destination string,
 * @param filename    filename for filtered operation
 * @param recursive   if true -r will be handled on, -d (single directory) otherwise
 *
 * @return true if successful, false if not.
 */
pid_t
action(const struct global_options *opts,
       struct dir_conf *dir_conf, 
       const char *src, 
       const char *dest, 
	   const char *filename,
       bool recursive)
{
	pid_t pid;
	const int MAX_ARGS = 100;
	char * argv[MAX_ARGS];
	int argc = 0;
	int i;
	struct call_option* optp;
	const struct log* log = &opts->log;
	
	optp = dir_conf->callopts ? dir_conf->callopts : opts->default_callopts;

	// makes a copy of all call parameters
	// step 1 binary itself
	argv[argc++] = s_strdup(log, dir_conf->binary ? dir_conf->binary : opts->default_binary, "argv");

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
			argv[argc++] = s_strdup(log, "--exclude-from", "argv exclude-from");
			argv[argc++] = s_strdup(log, dir_conf->exclude_file ? dir_conf->exclude_file : opts->default_exclude_file, "argv exclude-file"); 
			continue;
		case CO_FILTER :
			if (!filename) {
				continue;
			}
			argv[argc] = s_malloc(log, strlen("--include=''") + strlen(filename) + 1, "argv filter include");
			strcpy(argv[argc], "--include=\"");
			strcat(argv[argc], filename);
			strcat(argv[argc++], "\"");
			argv[argc++] = s_strdup(log, "--exclude=\"*\"", "argv filter exclude");
			continue;
		case CO_SOURCE :
			argv[argc++] = s_strdup(log, src, "argv source");
			continue;
		case CO_DEST :
			argv[argc++] = s_strdup(log, dest, "argv dest");
			continue;
		default:
			printlogf(log, ERROR, "Internal error: unknown kind of option.");
			terminate(log, LSYNCD_INTERNALFAIL); 
		}
		if (argc >= MAX_ARGS) {
			// check for error condition
			printlogf(log, ERROR, 
			          "Error: too many (>%i) options passed", argc);
			return 0;
		}
	}
	argv[argc++] = NULL;

	if (opts->flag_dryrun || log->loglevel == DEBUG) {
		// just make a nice log message
		char * argall = get_arg_str(log, argv, argc);
		if (opts->flag_dryrun) {
			printlogf(log, NORMAL, "dry run: would call %s", argall); 
		} else {
			printlogf(log, DEBUG, "calling %s", argall); 
		}
		s_free(argall);
		if (opts->flag_dryrun) {
			for (i = 0; i < argc; ++i) {
				if (argv[i]) {
					s_free(argv[i]);
				}
			}
			return 0;
		}
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
	
	printlogf(log, NORMAL, "  %s %s --> %s%s [%d]", argv[0], src, dest, recursive ? " (recursive)" : "", pid);
	
	// free the memory from the arguments.
	for (i = 0; i < argc; ++i) {
		if (argv[i]) {
			s_free(argv[i]);
		}
	}
	
	return pid;
}

/**
 * Adds a directory to watch.
 *
 * @param opts       global options
 * @param watches    the vector of watches
 * @param inotify_fd inotify file descriptor
 * @param pathname   the absolute path of the directory to watch
 * @param dirname    the name of the directory only 
 * @param parent     if not -1 the index to the parent directory that is already watched
 * @param dir_conf   the applicateable configuration
 *
 * @return the watches of the new dir, NULL on error
 */
struct watch *
add_watch(const struct global_options *opts,
          struct watch_vector *watches,
          int inotify_fd,
          char const *pathname, 
          char const *dirname, 
          struct watch *parent, 
          struct dir_conf *dir_conf)
{
	const struct log *log = &opts->log; // loginfo shortcut
	int wd;                             // kernels inotify descriptor
	int wi;                             // index to insert this watch into the watch vector
	struct watch *w;                    // the new watch

	wd = inotify_add_watch(inotify_fd, pathname, 
	                       dir_conf->event_mask ? dir_conf->event_mask : opts->default_event_mask);

	if (wd == -1) {
		printlogf(log, ERROR, "Cannot add watch %s (%d:%s)", 
		          pathname, errno, strerror(errno));
		return NULL;
	}

	// look if an unused slot can be found.
	//
	// lsyncd currently does not free unused slots, but marks
	// them as unused with wd < 0. 
	for (wi = 0; wi < watches->len; wi++) {
		if (watches->data[wi]->wd < 0) {
			break;
		}
	}

	// there is no unused entry
	if (wi == watches->len) {
		// extend the vector if necessary
		if (watches->len + 1 >= watches->size) {
			watches->size *= 2;
			watches->data = s_realloc(log, watches->data, 
			                          watches->size * sizeof(struct watch *));
		}
		// allocate memory for a new watch
		watches->data[watches->len++] = s_calloc(log, 1, sizeof(struct watch), "watch");
	}

	w = watches->data[wi];
	w->wd = wd;
	w->parent = parent;
	w->dirname = s_strdup(log, dirname, "dirname");
	w->dir_conf = dir_conf;
	w->dirdelay = NULL;
	return w;
}

/**
 * Writes the path of a watched directory into pathname.
 *
 * @param pathname   path to write to
 * @param pathsize   size of the pathname buffer
 * @param watch      watched dir to build path for
 * @param prefix     replace root dir with this (as target)
 *
 * @return -1 if pathname buffer was too small 
 *            contents of pathname will be garbled then.
 *         strlen(pathname) if successful
 */
int
builddir(char *pathname, 
         int pathsize, 
		 struct watch *watch, 
		 char const *prefix)
{
	int len = 0;
	if (!watch) {
		// TODO Is this ever called?
		char const * p = prefix ? prefix : "";
		len = strlen(p);
		if (pathsize <= len) {
			return -1;
		}
		strcpy(pathname, p);
	} else if (!watch->parent) {
		// this is a watch root.
		char const * p = prefix ? prefix : watch->dirname;
		len = strlen(p);
		if (pathsize <= len) {
			return -1;
		}
		strcpy(pathname, p);
	} else {
		// this is some sub dir
		len = builddir(pathname, pathsize, watch->parent, prefix); /* recurse */
		len += strlen(watch->dirname);
		if (pathsize <= len) {
			return -1;
		}
		strcat(pathname, watch->dirname);
	}
	// add the trailing slash if it is missing
	if (*pathname && pathname[strlen(pathname)-1] != '/') {
		strcat(pathname, "/");
		len++;
	}
	return len;
}

/**
 * Builds the abolute path name of a given directory watch
 *
 * @param pathname      destination buffer to store the result to.
 * @param pathsize      max size of this buffer
 * @param watch         the watches of the directory.
 * @param dirname       if not NULL it is added at the end of pathname
 * @param prefix        if not NULL it is added at the beginning of pathname
 */
bool
buildpath(const struct log *log, 
          char *pathname,
          int pathsize,
          struct watch *watch,
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
	return true;
}

/**
 * Waits for n children to return.
 * Returns true if all had 0 exit code.
 */
bool
waitchildren(const struct log *log,
             pid_t *children,
             int n) 
{
	int nr;      // amount of children returned 
	bool retval = true;
	for(nr = 0; nr < n;) {
		int status, i;
		pid_t pid = wait(&status);    // wait for a child.

		if (!keep_going) {
			// canceled.
			return false;
		}

		for (i = 0; i < n; i++) {
			if (children[i] == pid) { // found child in the list
				children[i] = 0;
				break;
			}
		}
		if (i >= n) {
			printlogf(log, DEBUG, "unknown child %d returned!", pid);
			continue;
		}
		nr++;
		assert(WIFEXITED(status));
		if (WEXITSTATUS(status) == LSYNCD_INTERNALFAIL){
			printlogf(log, ERROR, 
		          "Fork exit code of %i, execv failure", 
		          WEXITSTATUS(status));
			retval = false;
			continue;
		} else if (WEXITSTATUS(status)) {
			printlogf(log, NORMAL, 
			          "Forked binary process returned non-zero return code: %i", 
		    	      WEXITSTATUS(status));
			retval = false;
			continue;
		}
		printlogf(log, DEBUG, "Rsync of pid %d finished", pid);
		if (nr + 1 < n) {
			printlogf(log, DEBUG, "Waiting for another %d child(ren).", n - nr - 1);
		}
	}
	printlogf(log, DEBUG, "Finished waiting for all children");
	return retval;
}

/**
 * Syncs a directory.
 *   TODO: make better error handling (differ between
 *         directory gone away, and thus cannot work, or network
 *         failed)
 *
 * @param opts       global options
 * @param watch      the watch of the directory.
 * @param filename   the filename to sync for filtered ops.
 * @param evet_text  event text for logging. 
 *
 * @returns true when all targets were successful.
 */
bool
rsync_dir(const struct global_options *opts, 
          struct watch *watch,
		  const char *filename,
		  const char *event_text)
{
	char pathname[PATH_MAX+1];
	char destname[PATH_MAX+1];
	char ** target;
	const struct log *log = &opts->log;
	int ntarget = 0;

	if (!buildpath(log, pathname, sizeof(pathname), watch, NULL, NULL)) {
		return false;
	}

	if (filename) {
		printlogf(log, NORMAL, "%s: acting for %s/%s.", event_text, pathname, filename);
	} else {
		printlogf(log, NORMAL, "%s: acting for %s.", event_text, pathname);
	}

	// count the amount of targets
	for (target = watch->dir_conf->targets; *target; target++) {
		ntarget++;
	}

	if (ntarget == 1) {
		pid_t child;
		if (!buildpath(log, destname, sizeof(destname), watch, NULL, *watch->dir_conf->targets)) {
			return false;
		}
		// call the action to propagate changes in the directory
		child = action(opts, watch->dir_conf, pathname, destname, filename, false);
		if (!child) {
			printlogf(log, ERROR, "Action %s --> %s has failed.", pathname, destname);
			return false;
		}
		return waitchildren(log, &child, 1);
	} else {
		bool status = true;
		int ci = 0;   // position of children started.
		pid_t *children = s_calloc(log, ntarget, sizeof(pid_t), "children pid list");
		for (target = watch->dir_conf->targets; *target; target++) {
			if (!buildpath(log, destname, sizeof(destname), watch, NULL, *target)) {
				status = false;
				ntarget--;
				continue;
			}
			// call the action to propagate changes in the directory
			children[ci] = action(opts, watch->dir_conf, pathname, destname, filename, false);
			if (!children[ci]) {
				printlogf(log, ERROR, "Action %s --> %s has failed.", pathname, destname);
				status = false;
			}
			ci++;
		}
		if (!waitchildren(log, children, ntarget)) {
			status = false;
		}
		s_free(children);
		return status;
	}
}

/**
 * Puts a directory on the delay list OR 
 *   directly calls rsync_dir if delay == 0;
 * 
 * @param delays      the delay FIFO
 * @param delay       if true will put it on the delay, if false act immediately
 * @param watch       the watches of the delayed directory.
 * @param alarm       times() when the directory handling should be fired.
 * @param filename    filename
 * @param event_text  event text for logging output
 * @param event_name  event name for logging output
 */
void
delay_or_act_dir(const struct global_options *opts,
                 struct delay_vector *delays,
                 bool delay, 
                 struct watch *watch,
		         clock_t alarm,
				 const char *filename,
				 const char *event_text,
				 const char *event_name)
{
	const struct log *log = &opts->log;
	char pathname[PATH_MAX+1];
	
	if (!delay) {
		if (opts->flag_singular) {
			rsync_dir(opts, watch, filename, event_text);
		} else {
			rsync_dir(opts, watch, NULL, event_text);
		}
	} else {
		bool ret;
		if (!buildpath(log, pathname, sizeof(pathname), watch, NULL, NULL)) {
			return;
		}

		ret = append_delay(opts, delays, watch, alarm, filename);
		if (event_name) {
			printlogf(log, NORMAL, "%s %s in %s -%s delayed.", 
			          event_text, event_name, pathname,  ret ? "" : " already");
		} else {
			printlogf(log, NORMAL, "%s in %s -%s delayed.", 
			          event_text, pathname, ret ? "" : " already");
		}
	}
}

/**
 * Looks up the inotify event mask for the specified event text.
 *
 * @param text the name of the event to look up
 *
 * @return the inotify event mask or 0 if the mask name is unknown.
 */
int
event_text_to_mask(char * text)
{
	int mask = 0;
	struct inotify_mask_text *p;

	for (p = mask_texts; p->mask; p++) {
		if (!strcmp(p->text, text)) {
			mask = p->mask;
			break;
		}
	}

	return mask;
}

/**
 * Prints a verbose message how many items are left in the delay queue.
 *
 * @param opts    global options.
 * @param delays  delays list.
 */
void
print_queue(const struct global_options *opts, const struct delay_vector *delays) {
	const struct log *log = &opts->log;
	int expired = 0;
	int future  = 0;
	struct delay * d = delays->first;
	while (d && time_after_eq(times(NULL), d->alarm) && keep_going) {
		expired++;
		d = d->next;
	}
	while (d && keep_going) {
		future++;
		d = d->next;
	}
	if (!keep_going) {
		return;
	}
	printlogf(log, VERBOSE, "in queue: %d expired / %d delayed %s", expired, future, opts->flag_singular ? "files" : "dirs");
}

/**
 * Adds a directory including all subdirectories to watch.
 * And puts the directory with all subdirectories on the delay FIFO if act is true.
 *
 * @param opts       global options
 * @param watches    the watch vector
 * @param delays     the delay vector
 * @param excludes   the excludes vector
 * @param inotify_fd inotify file descriptor.
 * @param dirname    The name or absolute path of the directory to watch.
 * @param parent     If not NULL, the watches to the parent directory already watched.
 *                   Must have absolute path if parent == NULL.
 * @param dir_conf   applicateable configuration
 * @param act        if true delay or act on the new directory (dont on startup)
 *
 * @returns          the watches of the directory or NULL on fail.
 */
struct watch *
add_dirwatch(const struct global_options *opts,
             struct watch_vector *watches,
			 struct delay_vector *delays,
			 struct exclude_vector *excludes,
			 int inotify_fd,
             char const *dirname, 
			 struct watch *parent, 
			 struct dir_conf *dir_conf,
			 bool act)
{
	const struct log *log = &opts->log;
	DIR *d;
	struct watch *w;
	char pathname[PATH_MAX+1];

	if (!buildpath(log, pathname, sizeof(pathname),  parent, dirname, NULL)) {
		return NULL;
	}

	{
		int i;
		for (i = 0; i < excludes->len; i++) {
			if (!strcmp(pathname, excludes->data[i])) {
				printlogf(log, NORMAL, "ignored %s because of exclusion.", pathname);
				return NULL;
			}
		}
	}

	// watch this directory
	w = add_watch(opts, watches, inotify_fd, pathname, dirname, parent, dir_conf);
	if (!w) {
		return NULL;
	}

	// if acting put this directory on list to be synced.
	// time is now so it as soon as possible, but it will be on
	// top of the delay FIFO, so the current directory is
	// guaranteed to be synced first.
	if (act) {
		delay_or_act_dir(opts, delays, true, w, times(NULL), dirname, "new subdirectory", "");
	}
	
	if (strlen(pathname) + strlen(dirname) + 2 > sizeof(pathname)) {
		printlogf(log, ERROR, "pathname too long %s//%s", pathname, dirname);
		return NULL;
	}

	d = opendir(pathname);
	if (d == NULL) {
		printlogf(log, ERROR, "cannot open dir %s.", dirname);
		return NULL;
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
			isdir = buildpath(log, subdir, sizeof(subdir), w, de->d_name, NULL) && 
			        !stat(subdir, &st) && 
			        S_ISDIR(st.st_mode);
		} else {
			isdir = false;
		}

		// add watches if its a directory and not . or ..
		if (isdir && strcmp(de->d_name, "..") && strcmp(de->d_name, ".")) {
			// recurse into subdirectories
			printlogf(log, NORMAL, "%s %s%s", 
			          act ? "encountered" : "watching", 
					  pathname, de->d_name);
			add_dirwatch(opts, watches, delays, excludes, inotify_fd, de->d_name, w, dir_conf, act); 
		}
	}

	closedir(d);
	return w;
}

/**
 * Removes a watched dir, including recursevily all subdirs.
 *
 * @param opts        global options
 * @param watches     the watch vector
 * @param delays      the delay FIFO
 * @param inotify_fd  inotify file descriptor
 * @param name        optionally. If not NULL, the directory name 
 *                    to remove which is a child of parent
 * @param parent      the parent directory of the 
 *                    directory 'name' to remove, or to be removed 
 *                    itself if name == NULL.
 */
bool
remove_dirwatch(const struct global_options *opts,
                struct watch_vector *watches,
				struct delay_vector *delays,
                int inotify_fd,
                const char * name, 
				struct watch *parent)
{
	const struct log *log = &opts->log;
	struct watch *w;   // the watch to remove 
	if (name) {
		int i;
		// look for the child with the name
		// TODO optimize by using subdir lists
		for (i = 0; i < watches->len; i++) {
			w = watches->data[i];
			if (w->wd >= 0 && w->parent == parent && !strcmp(name, w->dirname)) {
				break;
			}
		}

		if (i >= watches->len) {
			printlogf(log, ERROR, "Cannot find entry for %s:/:%s :-(", 
			          parent->dirname, name);
			return false;
		}
	} else {
		w = parent;
	}

	{
		// recurse into all subdirectories removing them.
		// TODO possible optimization by keeping a list of subdirs
		int i;
		for (i = 0; i < watches->len; i++) {
			struct watch * iw = watches->data[i];
			if (iw->wd >= 0 && iw->parent == w) {
				// recurse into the subdirectory
				remove_dirwatch(opts, watches, delays, inotify_fd, NULL, iw);
			}
		}
	}

	inotify_rm_watch(inotify_fd, w->wd);
	// mark this entry invalid 
	w->wd = -1;  

	s_free(w->dirname);
	w->dirname = NULL;


	if (!w->dirdelay) {
		return true;
	} 
	// otherwise remove the delay entries for this dir.
	if (opts->flag_singular) {
		struct file_delay_vector *fdv = (struct file_delay_vector *) w->dirdelay;
		int i;
		for (i = 0; i < fdv->len; i++) {
			remove_delay(delays, fdv->data[i]->delay);
			s_free(fdv->data[i]->filename);
			s_free(fdv->data[i]);
		}
		s_free(fdv->data);
		s_free(fdv);
		w->dirdelay = NULL;
	} else {
		remove_delay(delays, (struct delay *) w->dirdelay);
		w->dirdelay = NULL;
	}

	return true;
}

/**
 * Find the matching watch descriptor 
 *
 * @param watches  the watch vector
 * @param wd       the wd (watch descriptor) given by inotify
 *
 * @return the watch or NULL if not found
 */
struct watch *
get_watch(const struct watch_vector *watches, 
          int wd) 
{
	int i;
	for (i = 0; i < watches->len; i++) {
		if (watches->data[i]->wd == wd) {
			return watches->data[i];
		}
	}
	return NULL;
}

/**
 * Handles an inotify event.
 * 
 * @param opts        global options
 * @param watches     the watch vector
 * @param delays      the delay FIFO
 * @param exlucdes    the exclusions
 * @param inotify_fd  inotify file descriptor
 * @param event       the event to handle
 * @param alarm       times() moment when it should fire
 */
bool 
handle_event(const struct global_options *opts,
             struct watch_vector *watches,
			 struct delay_vector *delays,
			 struct exclude_vector *excludes,
             int inotify_fd,
             struct inotify_event *event, 
			 clock_t alarm)
{
	const struct log *log = &opts->log;
	char masktext[255] = {0,};
	struct watch *watch;

	if (IN_Q_OVERFLOW & event->mask) {
		printlogf(log, ERROR, "EVENT OVERFLOW, kernel sent an overflow message");
		printlogf(log, ERROR, "EVENT OVERFLOW, thus events have been missed, lsyncd will now restart.");
		keep_going = 0;
		return false;
	}

	{
		// creates a string for logging that shows which flags 
		// were raised in the event
		struct inotify_mask_text *p; 
		int mask = event->mask;
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
	}

	if (IN_IGNORED & event->mask) {
		return true;
	}

	{
		// TODO, is this needed? or will it be excluded already?
		int i;
		for (i = 0; i < excludes->len; i++) {
			if (!strcmp(event->name, excludes->data[i])) {	
				return true;
			}
		}
	}

	watch = get_watch(watches, event->wd);
	if (!watch) {
		// this can happen in case of data moving faster than lsyncd can monitor it.
		printlogf(log, NORMAL, 
		          "event %s:%s is not from a watched directory (which presumably was recently deleted)", masktext, event->name ? event->name : "");
		return false;
	}

	// put the watch on the delay or act if delay == 0
	if ((IN_ATTRIB | IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | 
	     IN_MOVED_TO | IN_MOVED_FROM) & event->mask
	   ) {
		delay_or_act_dir(opts, delays, opts->delay > 0, watch, alarm, event->name, masktext, event->name);
	} else {
		printlogf(log, DEBUG, "... ignored this event.");
	}
	
	// in case of a new directory create new watches of the subdir
	if (((IN_CREATE | IN_MOVED_TO) & event->mask) && (IN_ISDIR & event->mask)) {
		add_dirwatch(opts, watches, delays, excludes, inotify_fd, event->name, watch, watch->dir_conf, true);
	}

	// in case of a removed directory remove watches from the subdir
	if (((IN_DELETE | IN_MOVED_FROM) & event->mask) && (IN_ISDIR & event->mask)) {
		remove_dirwatch(opts, watches, delays, inotify_fd, event->name, watch);
	}
	
	return true;
}

/**
 * The control loop waiting for inotify events
 *
 * @param opts        global options
 * @param watches     the watch vector
 * @param delays      the delay vector
 * @param excludes    the exclude vector
 * @param inotify_fd  inotify file descriptor
 */
bool 
master_loop(const struct global_options *opts,
            struct watch_vector *watches,
			struct delay_vector *delays,
			struct exclude_vector *excludes,
            int inotify_fd)
{
	const struct log *log = &opts->log;
	const long clocks_per_sec = sysconf(_SC_CLK_TCK);
	clock_t now;
	clock_t alarm;

	if (opts->delay > 0) {
		if (clocks_per_sec <= 0) {
			printlogf(log, ERROR, "Clocks per second invalid (%li)!", clocks_per_sec);
			terminate(log, LSYNCD_INTERNALFAIL);
		}
	}

	while (keep_going) {
		char buf[INOTIFY_BUF_LEN];
		int do_read;
		int len;

		if (delays->first && time_after_eq(times(NULL), delays->first->alarm)) {
			// there is a delay that wants to be handled already
			// do not read from inotify_fd and jump directly to delay handling
			printlogf(log, DEBUG, "immediately handling delayed entries");
			do_read = 0;
		} else if (opts->delay > 0 && delays->first) {
			// use select() to determine what happens first
			// a new event or "alarm" of an event to actually
			// call its binary. The delay with the index 0 
			// should have the nearest alarm time.
			fd_set readfds;
			struct timeval tv;

			alarm = delays->first->alarm;
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
			FD_ZERO(&readfds);
			FD_SET(inotify_fd, &readfds);
			do_read = select(inotify_fd + 1, &readfds, NULL, NULL, &tv);

			if (do_read) {
				printlogf(log, DEBUG, "theres data on inotify.");
			} else {
				printlogf(log, DEBUG, "select() timeout, doing delays.");
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
			if (keep_going) {
				// if !keep_going it is perfectly normal to be interupted.
				printlogf(log, ERROR, "read error from inotify (%d:%s)", errno, strerror(errno));
			}
			return false;
		}

		now = times(NULL);
		alarm = now + opts->delay * clocks_per_sec;

		{
			// first handle all events read.
			int i = 0;
			while (i < len && keep_going) {
				struct inotify_event *event = (struct inotify_event *) &buf[i];
				handle_event(opts, watches, delays, excludes, inotify_fd, event, alarm);
				i += sizeof(struct inotify_event) + event->len;
			}
		}

		// Then take of directories from the top of the delay FIFO 
		// until one item is found whose expiry time has not yet come
		// or the stack is empty. Using now time - times(NULL) - everytime 
		// again as time may progresses while handling delayed entries.
		while (delays->first && time_after_eq(times(NULL), delays->first->alarm) && keep_going) {
			if (log->loglevel <= VERBOSE) {
				print_queue(opts, delays);
			}
			if (!keep_going) {
				break;
			}
			if (opts->flag_singular) {
				struct file_delay * fd = (struct file_delay *) delays->first->owner;
				rsync_dir(opts, fd->watch, fd->filename, "delay expired");
			} else {
				rsync_dir(opts, (struct watch *) delays->first->owner, NULL, "delay expired");
			}
			remove_first_delay(opts, delays);
		}
		if (log->loglevel <= VERBOSE) {
			print_queue(opts, delays);
		}
	}

	return true;
}

/**
 * Utility function to check file exists. 
 * Prints out error message and die.
 *
 * @param log       logging information
 * @param filename  filename to check
 * @param errmsg    error message to print
 */
void 
check_file_exists(const struct log* log, 
                  const char* filename, 
                  const char *errmsg)
{
	struct stat st;
	if (-1==stat(filename, &st)) {
		printlogf(log, ERROR, "%s [%s] does not exist.\n", errmsg, filename);
		terminate(log, LSYNCD_FILENOTFOUND);
	}
}

/**
 * Utility function to check given path is absolute path.
 *
 * @param log       logging information
 * @param filename  filename to check
 * @param errmsg    filetype text to prepend to the error message.
 */
void
check_absolute_path(const struct log* log,
                    const char* filename,
                    const char *filetype)
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
	printf("  --singular             Call the sync binary for each file instead of grouping directories\n");
	printf("  --binary FILE          Call this binary to sync " "(DEFAULT: %s)\n", DEFAULT_BINARY);
#ifdef XML_CONFIG
	printf("   --conf FILE           Load configuration from this file\n");
	printf("                         (DEFAULT: %s if called without SOURCE/TARGET)\n", DEFAULT_CONF_FILENAME);
#endif
	printf("  --debug                Log debug messages\n");
	printf("  --delay SECS           Delay between event and action\n");
	printf("  --dryrun               Do not call any actions, run dry only\n");
	printf("  --exclude-from FILE    Exclude file handled to rsync (DEFAULT: None)\n");
	printf("  --help                 Print this help text and exit\n");
	printf("  --logfile FILE         Put log here (DEFAULT: uses syslog if not specified)\n"); 
	printf("  --no-daemon            Do not detach, log to stdout/stderr\n");
	printf("  --no-startup           Do not execute a startup sync (disadviced, know what you doing)\n");
	printf("  --pidfile FILE         Create a file containing pid of the daemon\n");
	printf("  --scarce               Only log errors\n");
	printf("  --stubborn             Ignore rsync errors on startup\n");
	printf("  --verbose              Log more messages\n");
	printf("  --version              Print version an exit\n");
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
		    xmlStrcmp(cnode->name, BAD_CAST "file-filter") &&
		    xmlStrcmp(cnode->name, BAD_CAST "source") &&
		    xmlStrcmp(cnode->name, BAD_CAST "destination")
		   ) {
			printlogf(NULL, ERROR, "error unknown call option type \"%s\"", cnode->name);
			terminate(NULL, LSYNCD_BADCONFIGFILE);
		}
		opt_n++;
	}
	opt_n++;
	asw = (struct call_option *) s_calloc(NULL, opt_n, sizeof(struct call_option), "call options");

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
			asw[opt_n].text = s_strdup(NULL, (char *) xc, "asw text");
		} else if (!xmlStrcmp(cnode->name, BAD_CAST "exclude-file")) {
			asw[opt_n].kind = CO_EXCLUDE;
		} else if (!xmlStrcmp(cnode->name, BAD_CAST "file-filter")) {
			asw[opt_n].kind = CO_FILTER;
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
 * Parses <inotify>
 */
uint32_t
parse_inotify(xmlNodePtr node) {
	xmlNodePtr dnode;
	xmlChar *xc;
	uint32_t mask = 0;
	int id = 0;
	for (dnode = node->children; dnode; dnode = dnode->next) {
		if (dnode->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(dnode->name, BAD_CAST "event")) {
			xc = xmlGetProp(dnode, BAD_CAST "id");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute id missing from <event>\n");
				exit(LSYNCD_BADCONFIGFILE);
			}
			id = event_text_to_mask((char*) xc);
			if (!id) {
				printlogf(NULL, ERROR, "error in config file: attribute id of <event>: \"%s\" not known.\n", (char*) xc);
				exit(LSYNCD_BADCONFIGFILE);
			}
			mask |= id;
		} else {
			printlogf(NULL, ERROR, "error in config file: unknown node in <inotify> \"%s\"\n", dnode->name);
			exit(LSYNCD_BADCONFIGFILE);
		}
	}
	if (!mask) {
		printlogf(NULL, ERROR, "error in config file: no valid <event> node in <inotify>\n");
		exit(LSYNCD_BADCONFIGFILE);
	}
	return mask;
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
			dc->source = s_strdup(NULL, (char *) xc, "xml source");
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
			dc->binary = s_strdup(NULL, (char *) xc, "xml binary"); 
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "callopts")) {
			if (dc->callopts) {
				printlogf(NULL, ERROR, "error in config file: there is more than one <callopts> in a <directory>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			dc->callopts = parse_callopts(opts, dnode);
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "exclude-from")) {
			xc = xmlGetProp(dnode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <exclude-from>\n");
				terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			dc->exclude_file = s_strdup(NULL, (char *) xc, "xml exclude"); 
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "inotify")) {
			if (dc->event_mask) {
 				fprintf(stderr, "error in config file: there is more than one <inotify> in a <directory>\n");
 				exit(LSYNCD_BADCONFIGFILE);
 			}
 			dc->event_mask = parse_inotify(dnode);
		} else {
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
			opts->log.loglevel = DEBUG;
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
			opts->default_exclude_file = s_strdup(NULL, (char *) xc, "xml default-exclude-file");
		} else if (!xmlStrcmp(snode->name, BAD_CAST "inotify")) {
 			if (opts->default_event_mask) {
 				printlogf(NULL, ERROR, "error in config file: there is more than one <inotify> in a <directory>\n");
 				exit(LSYNCD_BADCONFIGFILE);
 			}
 			opts->default_event_mask = parse_inotify(snode);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "logfile")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <logfile/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			opts->log.logfile = s_strdup(NULL, (char *) xc, "xml logfile");
		} else if (!xmlStrcmp(snode->name, BAD_CAST "binary")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <binary/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			if (opts->default_binary) {
				s_free(opts->default_binary);
			}
			opts->default_binary = s_strdup(NULL, (char *) xc, "xml default-binary");
		} else if (!xmlStrcmp(snode->name, BAD_CAST "pidfile")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				printlogf(NULL, ERROR, "error in config file: attribute filename missing from <pidfile/>\n");
		        terminate(NULL, LSYNCD_BADCONFIGFILE);
			}
			opts->pidfile = s_strdup(NULL, (char *) xc, "xml pidfile");
		} else if (!xmlStrcmp(snode->name, BAD_CAST "callopts")) {
			opts->default_callopts = parse_callopts(opts, snode);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "scarce")) {
			opts->log.loglevel = ERROR;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "no-daemon")) {
			opts->log.flag_nodaemon = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "no-startup")) {
			opts->flag_nostartup = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "singular")) {
			opts->flag_singular = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "stubborn")) {
			opts->flag_stubborn = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "verbose")) {
			opts->log.loglevel = VERBOSE;
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
		{"binary",       1, NULL, 0       }, 
#ifdef XML_CONFIG
		{"conf",         1, NULL, 0       }, 
#endif
		{"debug",        0, NULL, DEBUG   },
		{"delay",        1, NULL, 0       }, 
		{"dryrun",       0, NULL, 1       }, 
		{"exclude-from", 1, NULL, 0       }, 
		{"help",         0, NULL, 0       }, 
		{"logfile",      1, NULL, 0       }, 
		{"no-daemon",    0, NULL, 1       }, 
		{"no-startup",   0, NULL, 1       }, 
		{"pidfile",      1, NULL, 0       }, 
		{"scarce",       0, NULL, ERROR   },
		{"singular",     0, NULL, 1       }, 
		{"stubborn",     0, NULL, 1       },
		{"version",      0, NULL, 0       }, 
		{"verbose",      0, NULL, VERBOSE },
		{NULL,           0, NULL, 0       }
	};
#ifdef XML_CONFIG
	bool read_conf = false;
#endif

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
			if (!strcmp("singular",   o->name)) o->flag = &opts->flag_singular;
			if (!strcmp("stubborn",   o->name)) o->flag = &opts->flag_stubborn;
			if (!strcmp("verbose",    o->name)) o->flag = &opts->log.loglevel;
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
				if (opts->conf_filename) {
					s_free(opts->conf_filename);
				}
				opts->conf_filename = s_strdup(NULL, optarg, "opt conf-filename");
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
				if (opts->default_binary) {
					s_free(opts->default_binary);
				}
				opts->default_binary = s_strdup(NULL, optarg, "opt default-binary");
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
				opts->default_exclude_file = s_strdup(NULL, optarg, "opt default-exclude-file");
			}

			if (!strcmp("help", long_options[oi].name)) {
				print_help(argv[0]);
			}

			if (!strcmp("logfile", long_options[oi].name)) {
				opts->log.logfile = s_strdup(NULL, optarg, "opt logfile");
			}
			
			if (!strcmp("pidfile", long_options[oi].name)) {
				opts->pidfile = s_strdup(NULL, optarg, "opt pidfile");
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
				printlogf(&opts->log, NORMAL, "command line options: syncing %s -> %s",
				          odc->source, *target);
				first_target = false;
			} else {
				printlogf(&opts->log, NORMAL, "                             and -> %s", 
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
 *
 *
 */
bool
parse_exclude_file(struct log *log,
                   struct exclude_vector * excludes,
				   char *filename) {
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
			if (excludes->len + 1 >= MAX_EXCLUDES) {
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

			excludes->data[excludes->len] = s_malloc(log, strlen(line) + 1, "exclude_dir");
			strcpy(excludes->data[excludes->len], line);
			excludes->len++;
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
 * Main for one run. 
 * Can be runned through several times on HUPs.
 */
int
one_main(int argc, char **argv)
{
	struct global_options opts = {{0,}};      // global options 
	struct log *log = &opts.log;              // shortcut to logging options.
	struct watch_vector watches = {0, };      // all watches
	struct delay_vector delays  = {0, };      // delayed entries
	struct exclude_vector excludes = {{0, }}; // excludes
	int inotify_fd;                           // inotify file descriptor

	openlog("lsyncd", LOG_CONS | LOG_PID, LOG_DAEMON);

	reset_options(&opts);
	parse_options(&opts, argc, argv);

	if (opts.default_exclude_file) {
		parse_exclude_file(log, &excludes, opts.default_exclude_file);
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
			close(inotify_fd);
			return LSYNCD_INTERNALFAIL;
		}
	}

	printlogf(log, NORMAL, "--- Starting up ---");

	if (opts.pidfile) {
		write_pidfile(log, opts.pidfile);
	}

    watches.size = VECT_INIT_SIZE;
    watches.data = s_calloc(log, watches.size, sizeof(struct watch *), "watches vector");

	delays.first = delays.last = NULL;

	{
		// add all watches
		int i;
		for (i = 0; i < opts.dir_conf_n; i++) {
			printlogf(log, NORMAL, "watching %s", opts.dir_confs[i].source);
			add_dirwatch(&opts, &watches, &delays, &excludes, inotify_fd, 
			             opts.dir_confs[i].source, NULL, &opts.dir_confs[i], false);
		}
	}

	// startup recursive sync.
	if (!opts.flag_nostartup) {
		int i;
		for (i = 0; i < opts.dir_conf_n; i++) {
			char **target;
			for (target = opts.dir_confs[i].targets; *target; ++target) {
				pid_t child;
				printlogf(log, NORMAL, "Initial recursive sync for %s -> %s", opts.dir_confs[i].source, *target);
				child = action(&opts, &opts.dir_confs[i], opts.dir_confs[i].source, *target, NULL, true);
				if (!child || !waitchildren(log, &child, 1)) {
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
	          "--- Entering normal operation with [%lu] monitored directories ---",
	          (unsigned long) watches.len);

	signal(SIGTERM, catch_alarm);
	signal(SIGHUP, catch_alarm);

	master_loop(&opts, &watches, &delays, &excludes, inotify_fd);

	if (!termed) {
		printlogf(log, NORMAL, "--- Received HUP-Signal, cleaning up and starting over ---");
	}
	{
		// memory clean up
		int i;
		struct delay * d;

		reset_options(&opts);
		for(i = 0; i < watches.len; i++) {
			if (watches.data[i]->dirname) {
				s_free(watches.data[i]->dirname);
			}
			s_free(watches.data[i]);
		}
		s_free(watches.data);
		for(d = delays.first; d; d = d->next) {
			s_free(d);
		}
		for(i = 0; i < excludes.len; i++) {
			s_free(excludes.data[i]);
		}
	}
	close(inotify_fd);

#ifdef MEMCHECK
	fprintf(stderr, "Memcheck count: %d\n", memc);
	twalk(mroot, maction);
	fprintf(stderr, "Memcheck count: %d\n", memc);
#endif

	return 0;
}

/**
 * Main wrapper
 *
 * Start actual main over and over on HUPs.
 */
int
main(int argc, char **argv)
{
	int ret;
	do {
		ret = one_main(argc, argv);
		if (ret) {
			return ret;
		}
		// start over 
		keep_going = 1;
	} while (!termed);

	return ret;
}

