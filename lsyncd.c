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

#ifdef XML_CONFIG
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

#define INOTIFY_BUF_LEN     (512 * (sizeof(struct inotify_event) + 16))

#define LOG_DEBUG  1
#define LOG_NORMAL 2
#define LOG_ERROR  3

/**
 * Possible Exit codes for this application
 */
enum lsyncd_exit_code
{
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
 * Structure to store the directory watches of the deamon.
 */
struct dir_watch {
	/**
	 * The watch descriptor returned by kernel.
	 */
	int wd;

	/**
	 * The name of the directory.
	 * In case of the root dir to be watched, it is a full path
	 * and parent == NULL. Otherwise its just the name of the
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
	 * The applicable configuration for this directory
	 */
	struct dir_conf * dir_conf;
};

/**
 * Structure to store strings for the diversve inotfy masked events.
 * Actually used for comfortable debugging only.
 */
struct inotify_mask_text {
	int mask;
	char const * text;
};


/*--------------------------------------------------------------------------*
 * Global variables
 *--------------------------------------------------------------------------*/

/**
 * Global Option: The loglevel is how eloquent lsyncd will be.
 */
int loglevel = LOG_NORMAL;

/**
 * Global Option: if true no action will actually be called.
 */
int flag_dryrun = 0;

/**
 * Global Option: if true, do not detach and log to stdout/stderr.
 */
int flag_nodaemon = 0;

/**
 * Global Option: if true, ignore rsync errors on startup.
 *                (during normal operations they have to be ignored eitherway,
 *                 since rsync may also fail due e.g. the directory already
 *                 beeing deleted when lsyncd wants to sync it.)
 */
int flag_stubborn = 0;

/**
 * Global Option: pidfile, which holds the PID of the running daemon process.
 */
char * pidfile = NULL;

#ifdef XML_CONFIG
/**
 * Global Option: the filename to read config from.
 */
char * conf_filename = "/etc/lsyncd.conf.xml";
#endif

/**
 * Global Option: this binary is used if no other specified in dir_conf.
 */
char * default_binary = "/usr/bin/rsync"; 

/**
 * Global Option: default exclude file
 */
char * default_exclude_file = NULL;

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
 * Global Option: default options to call the binary with.
 */
struct call_option * default_callopts = standard_callopts;

/**
 * The configuratiton for dirs to synchronize
 */
struct dir_conf * dir_confs = NULL;

/**
 * The number of configurated dirs to sync.
 */
int dir_conf_n = 0;

/**
 * A stack of offset pointers to dir_watches to directories to sync.
 */
int *tosync = NULL;

/**
 * Number of ints allocaetd for tosync stack
 */
int tosync_size = 0;

/**
 * The pointer of the current tosync position.
 */
int tosync_pos = 0; 

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
 * Holds an allocated array of all directories watched.
 */
struct dir_watch *dir_watches;

/**
 * The allocated size of dir_watches;
 */
int dir_watch_size = 0;

/**
 * The largest dir_watch number used;
 */
int dir_watch_num = 0;

/**
 * lsyncd will log into this file/stream.
 */
char * logfile = "/var/log/lsyncd";

/**
 * The inotify instance.
 */
int inotf;


/**
 * Array of strings of directory names to include.
 * This is limited to MAX_EXCLUDES.
 * It's not worth to code a dynamic size handling...
 */
#define MAX_EXCLUDES 256
char * exclude_dirs[MAX_EXCLUDES] = {NULL, };
int exclude_dir_n = 0;

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
void catch_alarm(int sig)
{
	keep_going = 0;
}

/**
 * Prints a message to the log stream, preceding a timestamp.
 * Otherwise it behaves like printf();
 */
void printlogf(int level, const char *fmt, ...)
{
	va_list ap;
	char * ct;
	time_t mtime;
	FILE * flog;

	if (level < loglevel) {
		return;
	}

	if (!flag_nodaemon) {
		flog = fopen(logfile, "a");

		if (flog == NULL) {
			fprintf(stderr, "cannot open logfile [%s]!\n", logfile);
			exit(LSYNCD_FILENOTFOUND);
		}
	} else {
		flog = stdout;
	}

	va_start(ap, fmt);

	time(&mtime);
	ct = ctime(&mtime);
	ct[strlen(ct) - 1] = 0; // cut trailing \n
	fprintf(flog, "%s: ", ct);

	switch (level) {

	case LOG_DEBUG  :
		break;

	case LOG_NORMAL :
		break;

	case LOG_ERROR  :
		fprintf(flog, "ERROR: ");
		break;
	}

	vfprintf(flog, fmt, ap);

	fprintf(flog, "\n");
	va_end(ap);

	if (!flag_nodaemon) {
		fclose(flog);
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
void *s_malloc(size_t size)
{
	void *r = malloc(size);

	if (r == NULL) {
		printlogf(LOG_ERROR, "Out of memory!");
		exit(LSYNCD_OUTOFMEMORY);
	}

	return r;
}

/**
 * "secured" calloc.
 */
void *s_calloc(size_t nmemb, size_t size)
{
	void *r = calloc(nmemb, size);

	if (r == NULL) {
		printlogf(LOG_ERROR, "Out of memory!");
		exit(LSYNCD_OUTOFMEMORY);
	}

	return r;
}

/**
 * "secured" realloc.
 */
void *s_realloc(void *ptr, size_t size)
{
	void *r = realloc(ptr, size);

	if (r == NULL) {
		printlogf(LOG_ERROR, "Out of memory!");
		exit(LSYNCD_OUTOFMEMORY);
	}

	return r;
}

/**
 * "secured" strdup.
 */
char *s_strdup(const char* src)
{
	char *s = strdup(src);

	if (s == NULL) {
		printlogf(LOG_ERROR, "Out of memory!");
		exit(LSYNCD_OUTOFMEMORY);
	}

	return s;
}

/**
 * Returns the canonicalized path of a directory with a final '/', 
 * Makes sure it is a directory.
 */
char *realdir(const char * dir) 
{
	char* cs = s_malloc(PATH_MAX+1);
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

/*--------------------------------------------------------------------------*
 * dir_configuration handling. 
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
struct dir_conf * new_dir_conf() {
	if (dir_conf_n > 0) {
		dir_conf_n++;
		dir_confs = s_realloc(dir_confs, dir_conf_n * sizeof(struct dir_conf));
		memset(&dir_confs[dir_conf_n - 1], 0, sizeof(struct dir_conf));
		// creates targets NULL terminator (no targets yet)
		dir_confs[dir_conf_n - 1].targets = s_calloc(1, sizeof(char *));
		return &dir_confs[dir_conf_n - 1];
	}
	dir_conf_n++;
	dir_confs = s_calloc(dir_conf_n, sizeof(struct dir_conf));
	// creates targets NULL terminator (no targets yet)
	dir_confs[0].targets = s_calloc(1, sizeof(char *));
	return dir_confs;
}

/**
 * Adds a target to a dir_conf. target string will duped.
 *
 * @param dir_conf   dir_conf to add the target to.
 * @param target     target to add.
 */
void dir_conf_add_target(struct dir_conf * dir_conf, char *target)
{
	char **t;
	int target_n = 0;

	/* count current targets */
	for (t = dir_conf->targets; *t; ++t) {
		target_n++;
	}

	dir_conf->targets = s_realloc(dir_conf->targets, (target_n + 2) * sizeof(char *));
	dir_conf->targets[target_n] = s_strdup(target);
	dir_conf->targets[target_n + 1] = NULL;
}

/*--------------------------------------------------------------------------*
 * ToSync Stack handling. 
 *--------------------------------------------------------------------------*/

/**
 * Adds a directory to sync.
 *
 * @param watch         the index in dir_watches to the directory.
 */
bool append_tosync_watch(int watch) {
	int i;

	printlogf(LOG_DEBUG, "append_tosync_watch(%d)", watch);
	// look if its already in the tosync list.
	for(i = 0; i < tosync_pos; i++) {
		if (tosync[i] == watch) {
			return true;
		} 
	}

	if (tosync_pos + 1 >= tosync_size) {
		tosync_size *= 2;
		tosync = s_realloc(tosync, tosync_size*sizeof(int));
	}

	tosync[tosync_pos++] = watch;
	return true;
}


/**
 * Removes a tosync entry in the stack at the position p.
 */
bool remove_tosync_pos(int p) {
	int i;
	assert(p < tosync_pos);

	//TODO improve performance by using memcpy.
	for(i = p; i < tosync_pos; i++) {
		tosync[i] = tosync[i + 1];
	}
	tosync_pos--;
	return true;
}

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
char *parse_option_text(char *text, bool recursive)
{
	char * str = s_strdup(text);
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
			printlogf(LOG_ERROR, 
			          "don't know how to handle '\%' specifier in \"%s\"!", *text);
			exit(LSYNCD_BADPARAMETERS);
		}
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
 */
bool action(struct dir_conf * dir_conf, 
            char const * src, 
            const char * dest, 
            bool recursive)
{
	pid_t pid;
	int status;
	const int MAX_ARGS = 100;
	char * argv[MAX_ARGS];
	int argc=0;
	int i;
	struct call_option* optp;
	
	optp = dir_conf->callopts ? dir_conf->callopts : default_callopts;
	
	argv[argc++] = s_strdup(dir_conf->binary ? dir_conf->binary : default_binary);
	for(;optp->kind != CO_EOL; optp++) {
		switch (optp->kind) {
		case CO_TEXT :
			argv[argc++] = parse_option_text(optp->text, recursive);
			continue;
		case CO_EXCLUDE :
		    // --exclude-from and the exclude file
		    // insert only when the exclude file is present otherwise skip it.
			if (dir_conf->exclude_file == NULL) {
				continue;
			}
			argv[argc++] = s_strdup("--exclude-from");
			argv[argc++] = s_strdup(dir_conf->exclude_file);
			continue;
		case CO_SOURCE :
			argv[argc++] = s_strdup(src);
			continue;
		case CO_DEST :
			argv[argc++] = s_strdup(dest);
			continue;
		default:
			assert(false);
		}
		if (argc >= MAX_ARGS) {
			/* check for error condition */
			printlogf(LOG_ERROR, 
			          "Internal error: too many (>%i) options passed", argc);
			return false;
		}
	}
	argv[argc++] = NULL;

	/* debug dump of command-line options */
	//for (i=0; i<argc; ++i) {
	//  printlogf(LOG_DEBUG, "exec parameter %i:%s", i, argv[i]);
	//}

	if (flag_dryrun) {
		return true;
	}

	pid = fork();

	if (pid == 0) {
		char * binary = dir_conf->binary ? dir_conf->binary : default_binary;
		if (!flag_nodaemon) {
			if (!freopen(logfile, "a", stdout)) {
				printlogf(LOG_ERROR, "cannot redirect stdout to [%s].", logfile);
			}
			if (!freopen(logfile, "a", stderr)) {
				printlogf(LOG_ERROR, "cannot redirect stderr to [%s].", logfile);
			}
		}

		execv(binary, argv);          // in a sane world does not return!
		printlogf(LOG_ERROR, "Failed executing [%s]", binary);
		exit(LSYNCD_INTERNALFAIL);
	}

	for (i=0; i<argc; ++i) {
		if (argv[i]) {
			free(argv[i]);
		}
	}
	
	waitpid(pid, &status, 0);
	assert(WIFEXITED(status));
	if (WEXITSTATUS(status)==LSYNCD_INTERNALFAIL){
		printlogf(LOG_ERROR, 
		          "Fork exit code of %i, execv failure", 
		          WEXITSTATUS(status));
		return false;
	} else if (WEXITSTATUS(status)) {
		printlogf(LOG_NORMAL, 
		          "Forked binary process returned non-zero return code: %i", 
		          WEXITSTATUS(status));
		return false;
	}

	printlogf(LOG_DEBUG, "Rsync of [%s] -> [%s] finished", src, dest);
	return true;
}

/**
 * Adds a directory to watch.
 *
 * @param pathname the absolute path of the directory to watch.
 * @param dirname  the name of the directory only (yes this is a bit redudant, but oh well)
 * @param parent   if not -1 the index to the parent directory that is already watched
 * @param dir_conf the applicateable configuration
 *
 * @return index to dir_watches of the new dir, -1 on error.
 */
int add_watch(char const * pathname, 
              char const * dirname, 
              int parent, 
              struct dir_conf * dir_conf)
{
	int wd;
	int newdw;

	wd = inotify_add_watch(inotf, pathname,
	                       IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | 
	                       IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM | 
	                       IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR);

	if (wd == -1) {
		printlogf(LOG_ERROR, "Cannot add watch %s (%d:%s)", 
		          pathname, errno, strerror(errno));
		return -1;
	}

	// look if an unused slot can be found.
	for (newdw = 0; newdw < dir_watch_num; newdw++) {
		if (dir_watches[newdw].wd < 0) {
			break;
		}
	}

	if (newdw == dir_watch_num) {
		if (dir_watch_num + 1 >= dir_watch_size) {
			dir_watch_size *= 2;
			dir_watches = s_realloc(dir_watches, 
			                        dir_watch_size * sizeof(struct dir_watch));
		}

		dir_watch_num++;
	}

	dir_watches[newdw].wd = wd;
	dir_watches[newdw].parent = parent;
	dir_watches[newdw].dirname = s_strdup(dirname);
	dir_watches[newdw].dir_conf = dir_conf;

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
int builddir(char *pathname, int pathsize, int watch, char const * prefix)
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
	} else if (dir_watches[watch].parent == -1) {
		// this is a watch root.
		char const * p = prefix ? prefix : dir_watches[watch].dirname;
		len = strlen(p);
		if (pathsize <= len) {
			return -1;
		}
		strcpy(pathname, p);
	} else {
		// this is some sub dir
		len = builddir(pathname, pathsize, dir_watches[watch].parent, prefix); /* recurse */
		len += strlen(dir_watches[watch].dirname);
		if (pathsize <= len) {
			return -1;
		}
		strcat(pathname, dir_watches[watch].dirname);
	}
	/* add the trailing slash if it is missing */
	if (*pathname && pathname[strlen(pathname)-1] != '/') {
		strcat(pathname, "/");
		len++;
	}
	return len;
}

/**
 * Builds the abolute path name of a given directory beeing watched from the dir_watches information.
 *
 * @param pathname      destination buffer to store the result to.
 * @param pathsize      max size of this buffer
 * @param watch         the index in dir_watches to the directory.
 * @param dirname       if not NULL it is added at the end of pathname
 * @param prefix        if not NULL it is added at the beginning of pathname
 */
bool buildpath(char *pathname,
               int pathsize,
               int watch,
               const char *dirname,
               const char *prefix)
{
	int len = builddir(pathname, pathsize, watch, prefix);
	if (len < 0) {
		printlogf(LOG_ERROR, "path too long!");
		return false;
	}
	if (dirname) {
		if (pathsize < len + strlen(dirname) + 1) {
			printlogf(LOG_ERROR, "path too long!");
			return false;
		}
		strcat(pathname, dirname);
	}
	printlogf(LOG_DEBUG, "  BUILDPATH(%d, %s, %s) -> %s", watch, dirname, prefix, pathname);
	return true;
}

/**
 * Syncs a directory.
 *
 * @param watch         the index in dir_watches to the directory.
 *
 * @returns true when all targets were successful.
 */
bool rsync_dir(int watch)
{
	char pathname[PATH_MAX+1];
	char destname[PATH_MAX+1];
	bool status = true;
	char ** target;

	if (!buildpath(pathname, sizeof(pathname), watch, NULL, NULL)) {
		return false;
	}

	for (target = dir_watches[watch].dir_conf->targets; *target; target++) {
		if (!buildpath(destname, sizeof(destname), watch, NULL, *target)) {
			status = false;
			continue;
		}
		printlogf(LOG_NORMAL, "rsyncing %s --> %s", pathname, destname);

		// call rsync to propagate changes in the directory
		if (!action(dir_watches[watch].dir_conf, pathname, destname, false)) {
			printlogf(LOG_ERROR, "Rsync from %s to %s failed", pathname, destname);
			status = false;
		}
	}
	return status;
}

/**
 * Adds a dir to watch.
 *
 * @param dirname   The name or absolute path of the directory to watch.
 * @param parent    If not -1, the index in dir_watches to the parent directory already watched.
 *                  Must have absolute path if parent == -1.
 *
 * @return the index in dir_watches off the directory or -1 on fail.
 *
 */
int add_dirwatch(char const * dirname, int parent, struct dir_conf * dir_conf)
{
	DIR *d;

	struct dirent *de;
	int dw, i;
	char pathname[PATH_MAX+1];

	printlogf(LOG_DEBUG, "add_dirwatch(%s, p->dirname:%s, ...)", 
	          dirname,
	          parent >= 0 ? dir_watches[parent].dirname : "NULL");

	if (!buildpath(pathname, sizeof(pathname), parent, dirname, NULL)) {
		return -1;
	}

	for (i = 0; i < exclude_dir_n; i++) {
		if (!strcmp(dirname, exclude_dirs[i])) {
			return -1;
		}
	}

	dw = add_watch(pathname, dirname, parent, dir_conf);
	if (dw == -1) {
		return -1;
	}

	if (strlen(pathname) + strlen(dirname) + 2 > sizeof(pathname)) {
		printlogf(LOG_ERROR, "pathname too long %s//%s", pathname, dirname);
		return -1;
	}

	d = opendir(pathname);

	if (d == NULL) {
		printlogf(LOG_ERROR, "cannot open dir %s.", dirname);
		return -1;
	}

	while (keep_going) {
		struct stat st;
		char subdir[PATH_MAX+1];
		bool isdir;
		de = readdir(d);

		if (de == NULL) {
			break;
		}

		if (de->d_type == DT_DIR) {
			isdir = true;
		} else if (de->d_type == DT_UNKNOWN) {
			// in case of reiserfs, d_type will be UNKNOWN, how evil! :-(
			// use traditional means to determine if its a directory.
			isdir = buildpath(subdir, sizeof(subdir), dw, de->d_name, NULL) && 
			        !stat(subdir, &st) && 
			        S_ISDIR(st.st_mode);
		} else {
			isdir = false;
		}
		if (isdir && strcmp(de->d_name, "..") && strcmp(de->d_name, ".")) {
			int ndw = add_dirwatch(de->d_name, dw, dir_conf);
			printlogf(LOG_NORMAL, 
			          "found new directory: %s in %s -- added on tosync stack.", 
			          de->d_name, dirname);
			append_tosync_watch(ndw);
		}
	}

	closedir(d);
	return dw;
}

/**
 * Removes a watched dir, including recursevily all subdirs.
 *
 * @param name   Optionally. If not NULL, the directory name to remove which is a child of parent.
 * @param parent The index to the parent directory of the directory 'name' to remove,
 *               or to be removed itself if name == NULL.
 */
bool remove_dirwatch(const char * name, int parent)
{
	int i;
	int dw;

	if (name) {
		// look for the child with the name
		for (i = 0; i < dir_watch_num; i++) {
			if (dir_watches[i].wd >= 0 && dir_watches[i].parent == parent &&
			    !strcmp(name, dir_watches[i].dirname)
			   ) {
				dw = i;
				break;
			}
		}

		if (i >= dir_watch_num) {
			printlogf(LOG_ERROR, "Cannot find entry for %s:/:%s :-(", 
			          dir_watches[parent].dirname, name);
			return false;
		}
	} else {
		dw = parent;
	}

	for (i = 0; i < dir_watch_num; i++) {
		if (dir_watches[i].wd >= 0 && dir_watches[i].parent == dw) {
			remove_dirwatch(NULL, i);
		}
	}

	inotify_rm_watch(inotf, dir_watches[dw].wd);

	dir_watches[dw].wd = -1;

	free(dir_watches[dw].dirname);
	dir_watches[dw].dirname = NULL;

	return true;
}

/**
 * Find the matching dw entry from wd (watch descriptor), and return
 * the offset in the table.
 *
 * @param wd   The wd (watch descriptor) given by inotify
 * @return offset, or -1 if not found
 */
int get_dirwatch_offset(int wd) {
	int i;
	for (i = 0; i < dir_watch_num; i++) {
		if (dir_watches[i].wd == wd) {
			break;
		}
	}

	if (i >= dir_watch_num) {
		return -1;
	} else {
		return i;
	}
}

/**
 * Processes through the tosync stack, rysncing all its directories.
 *
 * TODO: make special logic to determine who is a subdirectory of whom, and maybe optimizie calls.
 */
bool process_tosync_stack()
{
	printlogf(LOG_DEBUG, "Processing through tosync stack.");
	while(tosync_pos > 0) {
		rsync_dir(tosync[--tosync_pos]);
	}
	printlogf(LOG_DEBUG, "being done with tosync stack");
	return true;
}


/**
 * Handles an inotify event.
 *
 * @param event   The event to handle
 */
bool handle_event(struct inotify_event *event)
{
	char masktext[255] = {0,};

	int mask = event->mask;
	int i, watch;
	int subwatch = -1;

	struct inotify_mask_text *p;

	for (p = mask_texts; p->mask; p++) {
		if (mask & p->mask) {
			if (strlen(masktext) + strlen(p->text) + 3 >= sizeof(masktext)) {
				printlogf(LOG_ERROR, "bufferoverflow in handle_event");
				return false;
			}

			if (*masktext) {
				strcat(masktext, ", ");
			}

			strcat(masktext, p->text);
		}
	}
	printlogf(LOG_DEBUG, "inotfy event: %s:%s", masktext, event->name);

	if (IN_IGNORED & event->mask) {
		return true;
	}

	for (i = 0; i < exclude_dir_n; i++) {
		if (!strcmp(event->name, exclude_dirs[i])) {
			return true;
		}
	}

	watch = get_dirwatch_offset(event->wd);
	if (watch == -1) {
		printlogf(LOG_ERROR, 
		          "received an inotify event that doesnt match any watched directory :-(%d,%d)", 
		          event->mask, event->wd);
		return false;
	}

	if (((IN_CREATE | IN_MOVED_TO) & event->mask) && (IN_ISDIR & event->mask)) {
		subwatch = add_dirwatch(event->name, watch, dir_watches[watch].dir_conf);
	}

	if (((IN_DELETE | IN_MOVED_FROM) & event->mask) && (IN_ISDIR & event->mask)) {
		remove_dirwatch(event->name, watch);
	}
	
	if ((IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | 
	     IN_MOVED_TO | IN_MOVED_FROM) & event->mask
	   ) {
		printlogf(LOG_NORMAL, "event %s:%s triggered.", masktext, event->name);
		rsync_dir(watch);            // TODO, worry about errors?
		if (subwatch >= 0) {     // sync through the new created directory as well.
			rsync_dir(subwatch);
		}
	} else {
		printlogf(LOG_DEBUG, "... ignored this event.");
	}
	process_tosync_stack();
	return true;
}

/**
 * The control loop waiting for inotify events.
 */
bool master_loop()
{
	char buf[INOTIFY_BUF_LEN];
	int len, i = 0;

	while (keep_going) {
		len = read (inotf, buf, INOTIFY_BUF_LEN);

		if (len < 0) {
			printlogf(LOG_ERROR, "failed to read from inotify (%d:%s)", errno, strerror(errno));
			return false;
		}

		if (len == 0) {
			printlogf(LOG_ERROR, "eof?");
			return false;
		}

		i = 0;

		while (i < len) {
			struct inotify_event *event = (struct inotify_event *) &buf[i];
			handle_event(event);
			i += sizeof(struct inotify_event) + event->len;
		}
	}

	return true;
}

/**
 * Utility function to check file exists. Print out error message and die.
 *
 * @param filename  filename to check
 */
void check_file_exists(const char* filename)
{
	struct stat st;
	if (-1==stat(filename, &st)) {
		printlogf(LOG_ERROR, "File [%s] does not exist\n", filename);
		exit (LSYNCD_FILENOTFOUND);
	}
}


/**
 * Utility function to check given path is absolute path.
 *
 * @param filename  filename to check
 */
void check_absolute_path(const char* filename)
{
	if (filename[0] != '/') {
		printlogf(LOG_ERROR, "Filename [%s] is not an absolute path\n", filename);
		exit (LSYNCD_FILENOTFOUND);
	}
}


/**
 * Prints the help text and exits 0.
 *
 * @param arg0   argv[0] to show what lsyncd was called with.
 */
void print_help(char *arg0)
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
	printf("  --binary FILE          Call this binary to sync (DEFAULT: %s)\n", 
	       default_binary);
#ifdef XML_CONFIG
	printf("   --conf FILE           Load configuration from this file\n");
	printf("                         (DEFAULT: %s if called without SOURCE/TARGET)\n", conf_filename);
#endif
	printf("  --debug                Log debug messages\n");
	printf("  --dryrun               Do not call any actions, run dry only\n");
	printf("  --exclude-from FILE    Exclude file handled to rsync (DEFAULT: None)\n");
	printf("  --help                 Print this help text and exit.\n");
	printf("  --logfile FILE         Put log here (DEFAULT: %s)\n", 
	       logfile);
	printf("  --no-daemon            Do not detach, log to stdout/stderr\n");
	printf("  --pidfile FILE         Create a file containing pid of the daemon\n");
	printf("  --scarce               Only log errors\n");
	printf("  --stubborn             Ignore rsync errors on startup.\n");
	printf("  --version              Print version an exit.\n");
	printf("\n");
	printf("Take care that lsyncd is allowed to write to the logfile specified.\n");
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
 * config file parsing
 *--------------------------------------------------------------------------*/

/**
 * Parses <callopts>
 *
 * @return the allocated and filled calloptions structure
 */
struct call_option * parse_callopts(xmlNodePtr node) {
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
			fprintf(stderr, "error unknown call option type \"%s\"", cnode->name);
			exit(LSYNCD_BADCONFIGFILE);
		}
		opt_n++;
	}
	opt_n++;
	asw = (struct call_option *) s_calloc(opt_n, sizeof(struct call_option));

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
				fprintf(stderr, "error in config file: text attribute missing from <option/>\n");
				exit(LSYNCD_BADCONFIGFILE);
			}
			asw[opt_n].kind = CO_TEXT;
			asw[opt_n].text = s_strdup((char *) xc);
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
bool parse_directory(xmlNodePtr node) {
	xmlNodePtr dnode;
	xmlChar *xc;
	struct dir_conf * dc = new_dir_conf();
	for (dnode = node->children; dnode; dnode = dnode->next) {
		if (dnode->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(dnode->name, BAD_CAST "source")) {
			xc = xmlGetProp(dnode, BAD_CAST "path");
			if (xc == NULL) {
				fprintf(stderr, "error in config file: attribute path missing from <source>\n");
				exit(LSYNCD_BADCONFIGFILE);
			}
			if (dc->source) {
				fprintf(stderr, "error in config file: cannot have more than one source in one <directory>\n");
				exit(LSYNCD_BADCONFIGFILE);
			}
			// TODO: use realdir() on xc
			dc->source = s_strdup((char *) xc);
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "target")) {
			xc = xmlGetProp(dnode, BAD_CAST "path");
			if (xc == NULL) {
				fprintf(stderr, "error in config file: attribute path missing from <target>\n");
		        exit(LSYNCD_BADCONFIGFILE);
			}
			dir_conf_add_target(dc, (char *) xc);
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "binary")) {
			xc = xmlGetProp(dnode, BAD_CAST "filename");
			if (xc == NULL) {
				fprintf(stderr, "error in config file: attribute filename missing from <binary>\n");
				exit(LSYNCD_BADCONFIGFILE);
			}
			dc->exclude_file = s_strdup((char *) xc);
		} else if (!xmlStrcmp(dnode->name, BAD_CAST "callopts")) {
			if (dc->callopts) {
				fprintf(stderr, "error in config file: there is more than one <callopts> in a <directory>\n");
				exit(LSYNCD_BADCONFIGFILE);
			}
			dc->callopts = parse_callopts(dnode);
		} else {
			fprintf(stderr, "error in config file: unknown node in <directory> \"%s\"\n", dnode->name);
			exit(LSYNCD_BADCONFIGFILE);
		}
	}
	if (!dc->source) {
		fprintf(stderr, "error in config file: source missing from <directory>\n");
		exit(LSYNCD_BADCONFIGFILE);
	}
	if (dc->targets[0] == NULL) {
		fprintf(stderr, "error in config file: target missing from <directory>\n");
		exit(LSYNCD_BADCONFIGFILE);
	}
	return true;
}

/**
 * Parses <settings>
 */
bool parse_settings(xmlNodePtr node) {
	xmlNodePtr snode;
	xmlChar *xc;

	for (snode = node->children; snode; snode = snode->next) {
		if (snode->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(snode->name, BAD_CAST "debug")) {
			loglevel = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "dryrun")) {
			flag_dryrun = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "exclude-from")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				fprintf(stderr, "error in config file: attribute filename missing from <exclude-from/>\n");
		        exit(LSYNCD_BADCONFIGFILE);
			}
			default_exclude_file = s_strdup((char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "logfile")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				fprintf(stderr, "error in config file: attribute filename missing from <logfile/>\n");
		        exit(LSYNCD_BADCONFIGFILE);
			}
			logfile = s_strdup((char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "binary")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				fprintf(stderr, "error in config file: attribute filename missing from <binary/>\n");
		        exit(LSYNCD_BADCONFIGFILE);
			}
			default_binary = s_strdup((char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "pidfile")) {
			xc = xmlGetProp(snode, BAD_CAST "filename");
			if (xc == NULL) {
				fprintf(stderr, "error in config file: attribute filename missing from <pidfile/>\n");
		        exit(LSYNCD_BADCONFIGFILE);
			}
			pidfile = s_strdup((char *) xc);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "callopts")) {
			default_callopts = parse_callopts(snode);
		} else if (!xmlStrcmp(snode->name, BAD_CAST "scarce")) {
			loglevel = 3;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "no-daemon")) {
			flag_nodaemon = 1;
		} else if (!xmlStrcmp(snode->name, BAD_CAST "stubborn")) {
			flag_stubborn = 1;
		} else {
			fprintf(stderr, "error unknown node in <settings> \"%s\"", snode->name);
			exit(LSYNCD_BADCONFIGFILE);
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
bool parse_config(bool fullparse) {
	LIBXML_TEST_VERSION
	xmlDoc *doc = NULL;
	xmlNode *root_element = NULL;
	xmlNodePtr node;
	xmlChar *xc;

	doc = xmlReadFile(conf_filename, NULL, 0);
	if (doc == NULL) {
		fprintf(stderr, "error: could not parse config file \"%s\"\n", conf_filename);
		exit(LSYNCD_BADCONFIGFILE);
	}
	root_element = xmlDocGetRootElement(doc);

	// check version specifier
	if (xmlStrcmp(root_element->name, BAD_CAST "lsyncd")) {
		fprintf(stderr, "error in config file: root node is not \"lsyncd\".\n");
		exit(LSYNCD_BADCONFIGFILE);
	}
	xc = xmlGetProp(root_element, BAD_CAST "version");
	if (xc == NULL) {
		fprintf(stderr, "error in config file: version specifier missing in \"%s\",\n", conf_filename);
		exit(LSYNCD_BADCONFIGFILE);
	}
	if (xmlStrcmp(xc, BAD_CAST "1") && xmlStrcmp(xc, BAD_CAST "1.25")) { //1.25, backward stuff
		fprintf(stderr, "error in config file: expected a \"1\" versioned file, found \"%s\"\n", xc);
		exit(LSYNCD_BADCONFIGFILE);
	}

	for (node = root_element->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(node->name, BAD_CAST "settings")) {
			parse_settings(node);
		} else if (!xmlStrcmp(node->name, BAD_CAST "directory")) {
			if (fullparse) {
				parse_directory(node);
			}
		} else {
			fprintf(stderr, "error in config file: unknown node in <lsyncd> \"%s\"\n", node->name);
			exit(LSYNCD_BADCONFIGFILE);
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
 * exits() in some cases of badparameters, or on 
 * --version or --help
 */
void parse_options(int argc, char **argv)
{
	char **target;

	static struct option long_options[] = {
		{"binary",       1, NULL,           0}, 
#ifdef XML_CONFIG
		{"conf",         1, NULL,           0}, 
#endif
		{"debug",        0, &loglevel,      1}, 
		{"dryrun",       0, &flag_dryrun,   1}, 
		{"exclude-from", 1, NULL,           0}, 
		{"help",         0, NULL,           0}, 
		{"logfile",      1, NULL,           0}, 
		{"no-daemon",    0, &flag_nodaemon, 1}, 
		{"pidfile",      1, NULL,           0}, 
		{"scarce",       0, &loglevel,      3},
		{"stubborn",     0, &flag_stubborn, 1},
		{"version",      0, NULL,           0}, 
		{NULL, 0, NULL, 0}
	};


#ifdef XML_CONFIG
	bool read_conf = false;
	// First determine if the config file should be read at all. If read it
	// before parsing all options in detail, because command line options
	// should overwrite global settings in the conf file.
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
			exit(LSYNCD_BADPARAMETERS);
		}
		if (c == 0) { // longoption
			if (!strcmp("conf", long_options[oi].name)) {
				read_conf = true;
				conf_filename = s_strdup(optarg);
			} 
			
			if (!strcmp("help", long_options[oi].name)) {
				// in case --help do not go further, or else 
				// lsyncd would complain of not being configured ...
				print_help(argv[0]);
			}

			if (!strcmp("version", long_options[oi].name)) {
				// same here 
				printf("Version: %s\n", VERSION);
				exit(LSYNCD_SUCCESS);
			}
		}
	}
	if (read_conf) {
		parse_config(optind == argc);
	} else if (optind == argc) {
		parse_config(true);
	}
	/* reset get option parser*/
	optind = 1;
#endif
	
	while (1) {
		int oi = 0;
		int c = getopt_long_only(argc, argv, "", long_options, &oi);
		if (c == -1) {
			break;
		}

		if (c == '?') {
			exit(LSYNCD_BADPARAMETERS);
		}

		if (c == 0) { // longoption
			if (!strcmp("binary", long_options[oi].name)) {
				default_binary = s_strdup(optarg);
			}
			
			if (!strcmp("exclude-from", long_options[oi].name)) {
				default_exclude_file = s_strdup(optarg);
			}

			if (!strcmp("help", long_options[oi].name)) {
				print_help(argv[0]);
			}

			if (!strcmp("logfile", long_options[oi].name)) {
				logfile = s_strdup(optarg);
			}
			
			if (!strcmp("pidfile", long_options[oi].name)) {
				pidfile = s_strdup(optarg);
			}

			if (!strcmp("version", long_options[oi].name)) {
				printf("Version: %s\n", VERSION);
				exit(LSYNCD_SUCCESS);
			}
		}
	}

	// If the config file specified something to do already
	// dir_conf_n will already be > 0
	if (dir_conf_n == 0) {
		struct dir_conf * odc;    // dir_conf specified by command line options.
		bool first_target = true;

		if (optind + 2 > argc) {
			fprintf(stderr, "Error: please specify SOURCE and at least one TARGET (see --help)\n");
#ifdef XML_CONFIG
			fprintf(stderr, "       or at least one <directory> entry in the conf file.\n");
#endif
			exit(LSYNCD_BADPARAMETERS);
		}
		odc = new_dir_conf();
		/* Resolves relative source path, lsyncd might chdir to / later. */
		odc->source = realdir(argv[optind]);
		if (!odc->source) {
			fprintf(stderr, "Error: Source [%s] not found or not a directory.\n", argv[optind]);
			exit(LSYNCD_FILENOTFOUND);
		}
		for (target = &argv[optind + 1]; *target; target++) {
			dir_conf_add_target(odc, *target);
			if (first_target) {
				printlogf(LOG_NORMAL, "command line options: syncing %s -> %s\n",
				          odc->source, *target);
				first_target = false;
			} else {
				printlogf(LOG_NORMAL, "                             and -> %s\n", 
				          *target);
			}
		}
	}

	/* sanity checking here */
	if (default_exclude_file) {
		check_absolute_path(default_exclude_file);
		check_file_exists(default_exclude_file);
	}
	if (pidfile) {
		check_absolute_path(pidfile);
	}
}

/**
 * Parses the exclude file looking for directory masks to not watch.
 */
bool parse_exclude_file(char *filename) {
	FILE * ef;
	char line[PATH_MAX+1];
	int sl;

	ef = fopen(filename, "r");
	if (ef == NULL) {
		printlogf(LOG_ERROR, "Meh, cannot open exclude file '%s'\n", filename);
		exit(LSYNCD_FILENOTFOUND);
	}

	while (1) {
		if (!fgets(line, sizeof(line), ef)) {
			if (feof(ef)) {
				fclose(ef);
				return true;
			}
			printlogf(LOG_ERROR, "Reading file '%s' (%d:%s)\n", 
			          filename, errno, strerror(errno));

			exit(LSYNCD_FILENOTFOUND);
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
				printlogf(LOG_ERROR, 
				          "Too many directory excludes, can only have %d at the most", 
				          MAX_EXCLUDES);
				exit(LSYNCD_TOOMANYDIRECTORYEXCLUDES);
			}

			line[sl - 1] = 0;

			sl--;

			if (sl == 0) {
				continue;
			}

			printlogf(LOG_NORMAL, "Excluding directories of the name '%s'", line);

			exclude_dirs[exclude_dir_n] = s_malloc(strlen(line) + 1);
			strcpy(exclude_dirs[exclude_dir_n], line);
			exclude_dir_n++;
		}
	}

	return true;
}

/**
 * Writes a pid file (specified by global "pidfile")
 */
void write_pidfile() {
	FILE* f = fopen(pidfile, "w");
	if (!f) {
		printlogf(LOG_ERROR, "Error: cannot write pidfile [%s]\n", pidfile);
		exit(LSYNCD_FILENOTFOUND);
	}
	
	fprintf(f, "%i\n", getpid());
	fclose(f); 
}

/**
 * main
 */
int main(int argc, char **argv)
{
	int i;

	parse_options(argc, argv);

	if (default_exclude_file) {
		parse_exclude_file(default_exclude_file);
	}

	inotf = inotify_init();
	if (inotf == -1) {
		printlogf(LOG_ERROR, "Cannot create inotify instance! (%d:%s)", 
		          errno, strerror(errno));
		return LSYNCD_NOINOTIFY;
	}

	if (!flag_nodaemon) {
		// this will make this process child of init
		// close stdin/stdout/stderr and 
		// chdir to /
		if (daemon(0, 0)) {
			printlogf(LOG_ERROR, "Cannot daemonize! (%d:%s)",
			          errno, strerror(errno));
			return LSYNCD_INTERNALFAIL;
		}
	}

	printlogf(LOG_NORMAL, "Starting up");

	if (pidfile) {
		write_pidfile();
	}

	dir_watch_size = 2;
	dir_watches = s_calloc(dir_watch_size, sizeof(struct dir_watch));

	tosync_size = 2;
	tosync = s_calloc(tosync_size, sizeof(int));

	// add all watches
	for (i = 0; i < dir_conf_n; i++) {
		printlogf(LOG_NORMAL, "watching %s", dir_confs[i].source);
		add_dirwatch(dir_confs[i].source, -1, &dir_confs[i]);
	}

	// clears tosync stack again, because the startup 
	// super recursive rsync will handle it eitherway.
	printlogf(LOG_DEBUG, "dumped tosync stack.");
	tosync_pos = 0;

	// startup recursive sync.
	for (i = 0; i < dir_conf_n; i++) {
		char **target;
		for (target = dir_confs[i].targets; *target; ++target) {
			if (!action(&dir_confs[i], dir_confs[i].source, *target, true)) {
				printlogf(LOG_ERROR, "Initial rsync from %s to %s failed%s", 
				          dir_confs[i].source, *target,
				          flag_stubborn ? ", but continuing because being stubborn." : ".");
				if (!flag_stubborn) {
					exit(LSYNCD_EXECFAIL);
				} 
			}
		}
	}

	printlogf(LOG_NORMAL, 
	          "--- Entering normal operation with [%d] monitored directories ---",
	          dir_watch_num);

	signal(SIGTERM, catch_alarm);

	master_loop();

	return 0;
}
