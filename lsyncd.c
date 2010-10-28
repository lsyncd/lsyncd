/** 
 * lsyncd.c   Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * This is the core. It contains as minimal as possible glues 
 * to the operating system needed for lsyncd operation. All high-level
 * logic is coded (when feasable) into lsyncd.lua
 */
#include "config.h"
#define LUA_USE_APICHECK 1

#ifdef HAVE_SYS_INOTIFY_H
#  include <sys/inotify.h>
#else
#  error Missing <sys/inotify.h>; supply kernel-headers and rerun configure.
#endif

#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/**
 * Extended debugging, undefine these to enable respective parts.
 */
#define DBG_MASTERLOOP(x) 
#define DBG_STARTUP(x) 

/**
 * Debugging definitions
 */
#ifndef DBG_MASTERLOOP
#define DBG_MASTERLOOP(x) { logstring(DEBUG, x); }
#define DBG_STARTUP(x)    { logstring(DEBUG, x); }
#endif

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
 * Event types core sends to runner.
 */
enum event_type {
	NONE     = 0,
	ATTRIB   = 1,
	MODIFY   = 2,
	CREATE   = 3,
	DELETE   = 4,
	MOVE     = 5,
	/* MOVEFROM/TO never get passed to the runner, but it uses these
	 * enums to split events again. The core will only send complete
	 * move events to the runner. Moves into or out of the watch tree
	 * are replaced with CREATE/DELETE events. */
	MOVEFROM = 6, 
	MOVETO   = 7,
};

/**
 * The Lua part of lsyncd.
 */
//#define LSYNCD_DEFAULT_RUNNER_FILE "lsyncd.lua"
extern char _binary_luac_out_start;
extern char _binary_luac_out_end; 

static char * lsyncd_runner_file = NULL;
static char * lsyncd_config_file = NULL;

/**
 * The inotify file descriptor.
 */
static int inotify_fd;

/**
 * TODO allow configure.
 */
static const uint32_t standard_event_mask = 
		IN_ATTRIB   | IN_CLOSE_WRITE | IN_CREATE     |
		IN_DELETE   | IN_DELETE_SELF | IN_MOVED_FROM |
		IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR;


/** 
 * If not null lsyncd logs in this file.
 */
static char * logfile = NULL;

/**
 * If true lsyncd sends log messages to syslog
 */
bool logsyslog = false;

/**
 * lsyncd log level
 */
enum loglevel {
	DEBUG   = 1,    /* Log debug messages       */
	VERBOSE = 2,    /* Log more                 */
	NORMAL  = 3,    /* Log short summeries      */
	ERROR   = 4,    /* Log severe errors only   */
	CORE    = 0x80  /* Indicates a core message */
};

/**
 * configuration parameters
 */
static struct settings {
	/**
	 * threshold for log messages 
	 */
	enum loglevel loglevel;

	/**
	 * lsyncd will periodically write its status in this
	 * file if configured so. (for special observing only)
	 */
	char * statusfile;
} settings = {
	.loglevel = DEBUG,
	.statusfile = NULL,
};

/**
 * True when lsyncd daemonized itself.
 */
static bool is_daemon = false;

/**
 * True after first configuration phase. This is to write configuration error
 * messages to stdout/stderr after being first started. Then it uses whatever
 * it has been configured to. This survives a reset by HUP signal or 
 * inotify OVERFLOW!
 */
static bool running = false;

/**
 * loglevel before user configuration took place
 * set to DEBUG for upstart debugging, Normally its NORMAL.
 */
static const enum loglevel startup_loglevel = DEBUG;

/**
 * Set to TERM or HUP in signal handler, when lsyncd should end or reset ASAP.
 */
static volatile sig_atomic_t reset = 0;

/**
 * The kernels clock ticks per second.
 */
static long clocks_per_sec; 

/**
 * signal handler
 */
void
sig_child(int sig)
{
	/* nothing */
}

/**
 * predeclerations -- see belorw.
 */
static void 
logstring0(enum loglevel level,
	  const char *message);
/* core logs with CORE flag */
#define logstring(loglevel, message) logstring0(loglevel | CORE, message)

static void
printlogf(lua_State *L, 
          enum loglevel level, 
		  const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));


/**
 * "secured" calloc.
 */
void *
s_calloc(size_t nmemb, size_t size)
{
	void *r = calloc(nmemb, size);
	if (r == NULL) {
		logstring(ERROR, "Out of memory!");
		exit(-1); // ERRNO 
	}
	return r;
}

/**
 * "secured" malloc. the deamon shall kill itself
 * in case of out of memory.
 */
void *
s_malloc(size_t size)
{
	void *r = malloc(size);
	if (r == NULL) {
		logstring(ERROR, "Out of memory!");
		exit(-1);  // ERRNO
	}
	return r;
}

/**
 * "secured" realloc.
 */
void *
s_realloc(void *ptr, size_t size)
{
	void *r = realloc(ptr, size);
	if (r == NULL) {
		logstring(ERROR, "Out of memory!");
		exit(-1);
	}
	return r;
}

/**
 * "secured" strdup.
 */
char *
s_strdup(const char *src)
{
	char *s = strdup(src);
	if (s == NULL) {
		logstring(ERROR, "Out of memory!");
		exit(-1); // ERRNO
	}
	return s;
}

/**
 * Logs a string
 *
 * @param level   the level of the log message
 * @param message the log message
 */
static void 
logstring0(enum loglevel level,
	   const char *message)
{
	const char *prefix;
	/* true if logmessages comes from core */
	bool coremsg = (level & CORE) != 0;
	/* strip flags from level */
	level &= 0x0F;

	if (!running) {
		/* lsyncd is in intial configuration.
		 * thus just print to normal stdout/stderr. */
		if (level == ERROR) {
			fprintf(stderr, "%s\n", message);
		} else if (level >= startup_loglevel) {
			printf("%s\n", message);
		}
		return;
	}

	/* skips filtered messagaes */
	if (level < settings.loglevel) {
		return;
	}

	prefix = level == ERROR ? 
	                  (coremsg ? "CORE ERROR: " : "ERROR: ") :
	                  (coremsg ? "core: " : "");

	/* writes on console if not daemon */
	if (!is_daemon) {
		char ct[255];
		/* gets current timestamp hour:minute:second */
		time_t mtime;
		time(&mtime);
		strftime(ct, sizeof(ct), "%T", localtime(&mtime));
		FILE * flog = level == ERROR ? stderr : stdout;
		fprintf(flog, "%s %s%s\n", ct, prefix, message);
	}

	/* writes to file if configured so */
	if (logfile) {
		FILE * flog = fopen(logfile, "a");
		/* gets current timestamp day-time-year */
		char * ct;
		time_t mtime;
		time(&mtime);
		ct = ctime(&mtime);
	 	/* cuts trailing linefeed */
 		ct[strlen(ct) - 1] = 0;

		if (flog == NULL) {
			fprintf(stderr, "core: cannot open logfile [%s]!\n", logfile);
			exit(-1);  // ERRNO
		}
		fprintf(flog, "%s: %s%s", ct, prefix, message);
		fclose(flog);
	}

	/* sends to syslog if configured so */
	if (logsyslog) {
		int sysp;
		switch (level) {
		case DEBUG  :
			sysp = LOG_DEBUG;
			break;
		case VERBOSE :
		case NORMAL  :
			sysp = LOG_NOTICE;
			break;
		case ERROR  :
			sysp = LOG_ERR;
			break;
		default :
			/* should never happen */
			sysp = 0;
			break;
		}
		syslog(sysp, "%s%s", prefix, message);
	}
	return;
}


/*****************************************************************************
 * Library calls for lsyncd.lua
 * 
 * These are as minimal as possible glues to the operating system needed for 
 * lsyncd operation.
 *
 ****************************************************************************/

/**
 * Adds an inotify watch
 * 
 * @param dir (Lua stack) path to directory
 * @return    (Lua stack) numeric watch descriptor
 */
static int
l_add_watch(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	lua_Integer wd = inotify_add_watch(inotify_fd, path, standard_event_mask);
	lua_pushinteger(L, wd);
	return 1;
}

/**
 * Logs a message.
 *
 * @param loglevel (Lua stack) loglevel of massage
 * @param string   (Lua stack) the string to log
 */
static int 
l_log(lua_State *L)
{
	/* log message */
	const char * message;
	/* log level */
	int level = luaL_checkinteger(L, 1);
	/* skips filtered messages early */
	if ((level & 0x0F) < settings.loglevel) {
		return 0;
	}
	message = luaL_checkstring(L, 2);
	logstring0(level, message);
	return 0;
}

/**
 * Returns (on Lua stack) true if time1 is earler or eq to time2
 * @param (on Lua Stack) time1
 * @param (on Lua Stack) time2
 * @return the true if time1 <= time2
 */
static int
l_before_eq(lua_State *L) 
{
	clock_t t1 = (clock_t) luaL_checkinteger(L, 1);
	clock_t t2 = (clock_t) luaL_checkinteger(L, 2);
	lua_pushboolean(L, time_before_eq(t1, t2));
	return 1;
}

/**
 * Returns (on Lua stack) the earlier or two clock times.
 *
 * @param (on Lua Stack) time1
 * @param (on Lua Stack) time2
 * @return the earlier time
 */
static int
l_earlier(lua_State *L) 
{
	clock_t t1 = (clock_t) luaL_checkinteger(L, 1);
	clock_t t2 = (clock_t) luaL_checkinteger(L, 2);
	lua_pushinteger(L, time_before(t1, t2) ? t1 : t2);
	return 1;
}

/**
 * Returns (on Lua stack) the current kernels 
 * clock state (jiffies)
 */
static int
l_now(lua_State *L) 
{
	lua_pushinteger(L, times(NULL));
	return 1;
}

/**
 * Returns (on Lua stack) the addition of a clock timer by seconds. 
 *
 * @param1 the clock timer
 * @param2 seconds to change clock.
 * TODO
 */
static int
l_addto_clock(lua_State *L) 
{
	clock_t c1 = luaL_checkinteger(L, 1);
	clock_t c2 = luaL_checkinteger(L, 2);
	lua_pop(L, 2);
	lua_pushinteger(L, c1 + c2 * clocks_per_sec);
	return 1;
}

/**
 * Executes a subprocess. Does not wait for it to return.
 * 
 * @param  (Lua stack) Path to binary to call
 * @params (Lua stack) list of string as arguments
 * @return (Lua stack) the pid on success, 0 on failure.
 */
static int
l_exec(lua_State *L)
{
	const char *binary = luaL_checkstring(L, 1);
	int argc = lua_gettop(L) - 1;
	pid_t pid;
	int i;
	char const **argv = s_calloc(argc + 2, sizeof(char *));

	argv[0] = binary;
	for(i = 1; i <= argc; i++) {
		argv[i] = luaL_checkstring(L, i + 1);
	}
	argv[i] = NULL;

	pid = fork();

	if (pid == 0) {
		/* if lsyncd runs as a daemon and has a logfile it will redirect
		   stdout/stderr of child processes to the logfile. */
		if (is_daemon && logfile) {
			if (!freopen(logfile, "a", stdout)) {
				printlogf(L, ERROR, 
					"cannot redirect stdout to '%s'.", logfile);
			}
			if (!freopen(logfile, "a", stderr)) {
				printlogf(L, ERROR, 
					"cannot redirect stderr to '%s'.", logfile);
			}
		}
		execv(binary, (char **)argv);
		/* in a sane world execv does not return! */
		printlogf(L, ERROR, "Failed executing [%s]!", binary);
		exit(-1); // ERRNO
	}

	free(argv);
	lua_pushnumber(L, pid);
	return 1;
}


/**
 * Converts a relative directory path to an absolute.
 * 
 * @param dir a relative path to directory
 * @return    absolute path of directory
 */
static int
l_real_dir(lua_State *L)
{
	luaL_Buffer b;
	char *cbuf;
	const char *rdir = luaL_checkstring(L, 1);
	
	/* use c-library to get absolute path */
	cbuf = realpath(rdir, NULL);
	if (cbuf == NULL) {
		printlogf(L, ERROR, "failure getting absolute path of [%s]", rdir);
		return 0;
	}
	{
		/* makes sure its a directory */
	    struct stat st;
	    if (stat(cbuf, &st)) {
			printlogf(L, ERROR, 
				"cannot get absolute path of dir '%s': %s", 
				rdir, strerror(errno));
			return 0;
		}
	    if (!S_ISDIR(st.st_mode)) {
			printlogf(L, ERROR, 
				"cannot get absolute path of dir '%s': is not a directory", 
				rdir);
			free(cbuf);
			return 0;
	    }
	}

	/* returns absolute path with a concated '/' */
	luaL_buffinit(L, &b);
	luaL_addstring(&b, cbuf);
	luaL_addchar(&b, '/');
	luaL_pushresult(&b);
	free(cbuf);
	return 1;
}

/**
 * Dumps the LUA stack. For debugging purposes.
 */
static int
l_stackdump(lua_State* L)
{
	int i;
	int top = lua_gettop(L);
	printlogf(L, DEBUG, "total in stack %d",top);
	for (i = 1; i <= top; i++) { 
		int t = lua_type(L, i);
		switch (t) {
		case LUA_TSTRING:
			printlogf(L, DEBUG, "%d string: '%s'", 
				i, lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:
			printlogf(L, DEBUG, "%d boolean %s", 
				i, lua_toboolean(L, i) ? "true" : "false");
			break;
		case LUA_TNUMBER: 
			printlogf(L, DEBUG, "%d number: %g", 
				i, lua_tonumber(L, i));
			break;
		default: 
			printlogf(L, DEBUG, "%d %s", 
				i, lua_typename(L, t));
			break;
		}
	}
	return 0;
}

/**
 * Reads the directories sub directories.
 * 
 * @param  (Lua stack) absolute path to directory.
 * @return (Lua stack) a table of directory names.
 */
static int
l_sub_dirs (lua_State *L)
{
	const char * dirname = luaL_checkstring(L, 1);
	DIR *d;
	int idx = 1;

	d = opendir(dirname);
	if (d == NULL) {
		printlogf(L, ERROR, "cannot open dir [%s].", dirname);
		return 0;
	}
	
	lua_newtable(L);
	while (!reset) {
		struct dirent *de = readdir(d);
		bool isdir;
		if (de == NULL) {
			/* finished */
			break;
		}
		if (de->d_type == DT_UNKNOWN) {
			/* must call stat on some systems :-/ */
			char *subdir = s_malloc(strlen(dirname) + strlen(de->d_name) + 2);
			struct stat st;
			strcpy(subdir, dirname);
			strcat(subdir, "/");
			strcat(subdir, de->d_name);
			stat(subdir, &st);
			isdir = S_ISDIR(st.st_mode);
			free(subdir);
		} else {
			/* readdir can trusted */
			isdir = de->d_type == DT_DIR;
		}
		if (!isdir || !strcmp(de->d_name, ".") || 
			!strcmp(de->d_name, "..")) 
		{ /* ignore non directories and . and .. */
			continue;
		}

		/* add this to the Lua table */
		lua_pushnumber(L, idx++);
		lua_pushstring(L, de->d_name);
		lua_settable(L, -3);
	}
	return 1;
}

/**
 * Writes a string to a file descriptor
 * 
 * @param (Lua Stack) file descriptor
 * @param (Lua Stack) string.
 */
int 
l_writefd(lua_State *L) 
{
	int fd = luaL_checkinteger(L, 1);
	const char *s = luaL_checkstring(L, 2);
	write(fd, s, strlen(s));
	return 0;
}

/**
 * Terminates lsyncd daemon.
 * 
 * @param (Lua stack) exitcode for lsyncd.
 *
 * Does not return.
 */
int 
l_terminate(lua_State *L) 
{
	int exitcode = luaL_checkinteger(L, 1);
	exit(exitcode);
	return 0;
}

/**
 * Suspends execution until a table of child processes returned.
 * 
 * @param (Lua stack) a table of the process ids.
 * @param (Lua stack) a function of a collector to be called 
 *                    when a process finishes.
 */
int 
l_wait_pids(lua_State *L) 
{
	/* the number of pids in table */
	int pidn; 
	/* the pid table */
	int *pids; 
	/* the number of processes to be waited for */
	int remaining = 0;
	int i;
	/* global function to call on finished processes */
	const char * collector;
	/* checks if Lua script returned a table */
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_type(L, 2) == LUA_TNIL) {
		collector = NULL;
	} else {
		collector = luaL_checkstring(L, 2);
	}

	/* determines size of the pid-table */
	pidn = lua_objlen (L, 1);
	if (pidn == 0) {
		/* nothing to do on zero pids */
		return 0;
	}
	/* reads the pid table from Lua stack */
	pids = s_calloc(pidn, sizeof(int));
	for(i = 0; i < pidn; i++) {
		lua_rawgeti(L, 1, i + 1);
		pids[i] = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		/* ignores zero pids */
		if (pids[i]) {
			remaining++;
		}
	}
	/* starts waiting for the processes */
	while(remaining) {
		/* argument for waitpid, and exitcode of process */
		int status, exitcode;
		/* new process id in case of retry */
		int newp;
		/* process id of terminated child process */
		int wp = waitpid(0, &status, 0);

		/* if nothing really finished ignore */
		if (wp == 0 || !WIFEXITED(status)) {
			continue;
		}

		exitcode = WEXITSTATUS(status);
		/* checks if the pid is one waited for */
		for(i = 0; i < pidn; i++) {
			if (pids[i] == wp) {
				break;
			}
		}
		if (i >= pidn) {
			/* not a pid waited for */
			continue;
		}
		/* calls the lua collector to determine further actions */
		if (collector) {
			lua_getglobal(L, collector);
			lua_pushinteger(L, wp);
			lua_pushinteger(L, exitcode);
			lua_call(L, 2, 1);
			newp = luaL_checkinteger(L, -1);
			lua_pop(L, 1);
		} else {
			newp = 0;
		}

		/* replace the new pid in the pidtable,
		   or zero it on no new pid */
		for(i = 0; i < pidn; i++) {
			if (pids[i] == wp) {
				pids[i] = newp;
				if (newp == 0) {
					remaining--;
				}
				/* does not break!
				 * in case there are duplicate pids (why-ever) */
			}
		}
	}
	free(pids);
	return 0;
}

/**
 * Configures core parameters.
 * 
 * @param (Lua stack) a string for a core configuratoin
 * @param (Lua stack) --differes depending on string. 
 */
static int 
l_configure(lua_State *L) 
{
	const char * command = luaL_checkstring(L, 1);
	if (!strcmp(command, "statusfile")) {
		/* configures the status file lsyncd will dump its status to */
		if (settings.statusfile) {
			free(settings.statusfile);
		}
		settings.statusfile = s_strdup(luaL_checkstring(L, 2));
	} else if (!strcmp(command, "loglevel")) {
		settings.loglevel = luaL_checkinteger(L, 2);
	} else if (!strcmp(command, "running")) {
		/* set by runner after first initialize 
		 * from this on log to configurated log end instead of 
		 * stdout/stderr */
		running = true;
	} else {
		printlogf(L, ERROR, 
			"Internal error, unknown parameter in l_configure(%s)", 
			command);
		exit(-1); //ERRNO
	}
	return 0;
}


static const luaL_reg lsyncdlib[] = {
		{"add_watch",    l_add_watch    },
		{"addto_clock",  l_addto_clock  },
		{"before_eq",    l_before_eq    },
		{"configure",    l_configure    },
		{"earlier",      l_earlier      },
		{"exec",         l_exec         },
		{"log",          l_log          },
		{"now",          l_now          },
		{"writefd",      l_writefd      },
		{"real_dir",     l_real_dir     },
		{"stackdump",    l_stackdump    },
		{"sub_dirs",     l_sub_dirs     },
		{"terminate",    l_terminate    },
		{"wait_pids",    l_wait_pids    },
		{NULL, NULL}
};


/*****************************************************************************
 * Lsyncd Core 
 ****************************************************************************/

/**
 * Let the core print logmessage comfortably.
 */
static void
printlogf(lua_State *L, 
          enum loglevel level, 
	  const char *fmt, ...)
{
	va_list ap;
	/* skips filtered messages early */
	if (level < settings.loglevel) {
		return;
	}
	lua_pushcfunction(L, l_log);
	lua_pushinteger(L, level | CORE);
	va_start(ap, fmt);
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);
	lua_call(L, 2, 0);
	return;
}

/**
 * Buffer for MOVE_FROM events.
 * Lsyncd buffers MOVE_FROM events to check if 
 */
struct inotify_event * move_event_buf = NULL;

/**
 * Memory allocated for move_event_buf
 */
size_t move_event_buf_size = 0;

/**
 * true if the buffer is used.
 */
bool move_event = false;

/**
 * Handles an inotify event.
 */
void handle_event(lua_State *L, struct inotify_event *event) {
	/* TODO */
	int event_type = NONE;

	/* used to execute two events in case of unmatched MOVE_FROM buffer */
	struct inotify_event *after_buf = NULL;
	logstring(DEBUG, "got an event");

	if (reset) {
		return;
	}
	if (event && (IN_Q_OVERFLOW & event->mask)) {
		/* and overflow happened, lets runner/user decide what to do. */
		lua_getglobal(L, "overflow");
		lua_call(L, 0, 0);
		return;
	}
	/* cancel on ignored or resetting */
	if (event && (IN_IGNORED & event->mask)) {
		return;
	}

	if (event == NULL) {
		/* a buffered MOVE_FROM is not followed by anything, 
		   thus it is unary */
		event = move_event_buf;
		event_type = DELETE;
		move_event = false;
	} else if (move_event && 
	            ( !(IN_MOVED_TO & event->mask) || 
			      event->cookie != move_event_buf->cookie) ) {
		/* there is a MOVE_FROM event in the buffer and this is not the match
		 * continue in this function iteration to handler the buffer instead */
		after_buf = event;
		event = move_event_buf;
		event_type = DELETE;
		move_event = false;
	} else if ( move_event && 
	            (IN_MOVED_TO & event->mask) && 
			    event->cookie == move_event_buf->cookie ) {
		/* this is indeed a matched move */
		event_type = MOVE;
		move_event = false;
	} else if (IN_MOVED_FROM & event->mask) {
		/* just the MOVE_FROM, buffers this event, and wait if next event is 
		 * a matching MOVED_TO of this was an unary move out of the watched 
		 * tree. */
		size_t el = sizeof(struct inotify_event) + event->len;
		if (move_event_buf_size < el) {
			move_event_buf_size = el;
			move_event_buf = s_realloc(move_event_buf, el);
		}
		memcpy(move_event_buf, event, el);
		move_event = true;
		return;
	} else if (IN_MOVED_TO & event->mask) {
		/* must be an unary move-to */
		event_type = CREATE;
	} else if (IN_MOVED_FROM & event->mask) {
		/* must be an unary move-from */
		event_type = DELETE;
	} else if (IN_ATTRIB & event->mask) {
		/* just attrib change */
		event_type = ATTRIB;
	} else if (IN_CLOSE_WRITE & event->mask) {
		/* closed after written something */
		event_type = MODIFY;
	} else if (IN_CREATE & event->mask) {
		/* a new file */
		event_type = CREATE;
	} else if (IN_DELETE & event->mask) {
		/* rm'ed */
		event_type = DELETE;
	} else {
		logstring(DEBUG, "skipped some inotify event.");
		return;
	}

	/* and hands over to runner */
	lua_getglobal(L, "lsyncd_event");
	lua_pushnumber(L, event_type);
	lua_pushnumber(L, event->wd);
	lua_pushboolean(L, (event->mask & IN_ISDIR) != 0);
	lua_pushinteger(L, times(NULL));
	if (event_type == MOVE) {
		lua_pushstring(L, move_event_buf->name);
		lua_pushstring(L, event->name);
	} else {
		lua_pushstring(L, event->name);
		lua_pushnil(L);
	}
	lua_call(L, 6, 0);

	/* if there is a buffered event executes it */
	if (after_buf) {
		handle_event(L, after_buf);
	}
}

/**
 * Normal operation happens in here.
 */
void
masterloop(lua_State *L)
{
	size_t readbuf_size = 2048;
	char *readbuf = s_malloc(readbuf_size);
	while(!reset) {
		bool have_alarm;
		clock_t now = times(NULL);
		clock_t alarm_time;
		int do_read;
		ssize_t len; 

		/* query runner about soonest alarm  */
		lua_getglobal(L, "lsyncd_get_alarm");
		lua_call(L, 0, 2);
		have_alarm = lua_toboolean(L, -2);
		alarm_time = (clock_t) luaL_checkinteger(L, -1);
		lua_pop(L, 2);

		if (have_alarm && time_before(alarm_time, now)) {
			/* there is a delay that wants to be handled already thus do not 
			 * read from inotify_fd and jump directly to its handling */
			DBG_MASTERLOOP("immediately handling delays.");
			do_read = 0;
		} else {
			/* use select() to determine what happens next
			 * + a new event on inotify
			 * + an alarm on timeout  
			 * + the return of a child process */
			sigset_t sigset;
			fd_set readfds;
			struct timespec tv;
			sigemptyset(&sigset);

			if (have_alarm) { 
				DBG_MASTERLOOP("going into timed select.");
				tv.tv_sec  = (alarm_time - now) / clocks_per_sec;
				tv.tv_nsec = (alarm_time - now) * 
					1000000000 / clocks_per_sec % 1000000000;
			} else {
				DBG_MASTERLOOP("going into blocking select.");
			}
			/* if select returns a positive number there is data on inotify
			 * on zero the timemout occured. */
			FD_ZERO(&readfds);
			FD_SET(inotify_fd, &readfds);
			do_read = pselect(inotify_fd + 1, &readfds, NULL, NULL, 
				have_alarm ? &tv : NULL, &sigset);

			DBG_MASTERLOOP(do_read > 0 ? 
				"theres data on inotify." :
				"core: select() timeout or signal.");
		} 
		
		/* reads possible events from inotify stream */
		while(do_read > 0) {
			int i = 0;
			do {
				len = read (inotify_fd, readbuf, readbuf_size);
				if (len < 0 && errno == EINVAL) {
					/* kernel > 2.6.21 indicates that way that way that
					 * the buffer was too small to fit a filename.
					 * double its size and try again. When using a lower
					 * kernel and a filename > 2KB       appears lsyncd
					 * will fail. (but does a 2KB filename really happen?)*/
					readbuf_size *= 2;
					readbuf = s_realloc(readbuf, readbuf_size);
					continue;
				}
			} while(0);
			while (i < len && !reset) {
				struct inotify_event *event = 
					(struct inotify_event *) &readbuf[i];
				handle_event(L, event);
				i += sizeof(struct inotify_event) + event->len;
			}
			/* check if there is more data */
			{
				struct timespec tv = {.tv_sec = 0, .tv_nsec = 0};
				fd_set readfds;
				FD_ZERO(&readfds);
				FD_SET(inotify_fd, &readfds);
				do_read = pselect(inotify_fd + 1, &readfds, 
					NULL, NULL, &tv, NULL);
				if (do_read > 0) {
					DBG_MASTERLOOP("there is more data on inotify.");
				}
			}
		} 
		/* checks if there is an unary MOVE_FROM left in the buffer */
		if (move_event) {
			handle_event(L, NULL);	
		}

		/* collects zombified child processes */
		while(1) {
			int status;
			pid_t pid = waitpid(0, &status, WNOHANG);
			if (pid <= 0) {
				break;
			}
			lua_getglobal(L, "lsyncd_collect_process");
			lua_pushinteger(L, pid);
			lua_pushinteger(L, WEXITSTATUS(status));
			lua_call(L, 2, 0);
		} 

		/* writes status of lsyncd in a file */
		/* this is not a real loop, it will only be runned once max. 
		 * this is just using break as comfortable jump down. */
		while (settings.statusfile) {
			int fd = open(settings.statusfile, O_WRONLY | O_CREAT | O_TRUNC);
			if (fd < 0) {
				printlogf(L, ERROR, 
					"Cannot open statusfile '%s' for writing.", 
					settings.statusfile);
				break;
			}
			/* calls the lua runner to write the status. */
			lua_getglobal(L, "lsyncd_status_report");
			lua_pushinteger(L, fd);
			lua_call(L, 1, 0);

			/* TODO */
			fsync(fd);
			close(fd);
			break;
		}

		/* lets the runner spawn new processes */
		lua_getglobal(L, "lsyncd_alarm");
		lua_pushinteger(L, times(NULL));
		lua_call(L, 1, 0);
	}
}

/**
 * Prints a minimal help if e.g. config_file is missing. 
 */
void minihelp(char *arg0) 
{
	fprintf(stderr, "Missing config file\n");
	fprintf(stderr, "Minimal Usage: %s CONFIG_FILE\n", arg0);
	fprintf(stderr, "  Specify -help for more help.\n");
}

/**
 * Main
 */
int
main(int argc, char *argv[])
{
	/* the Lua interpreter */
	lua_State* L;

	/* position at cores (minimal) argument parsing *
	 * most arguments are parsed in the lua runner  */
	int argp = 1; 
	if (argc <= 1) {
		minihelp(argv[0]);
		return -1;
	}

	/* kernel parameters */
	clocks_per_sec = sysconf(_SC_CLK_TCK);

	/* load Lua */
	L = lua_open();
	/* TODO check lua version */
	luaL_openlibs(L);
	luaL_register(L, "lsyncd", lsyncdlib);
	lua_setglobal(L, "lysncd");

	/* register event types */
	lua_pushinteger(L, ATTRIB);   lua_setglobal(L, "ATTRIB");
	lua_pushinteger(L, MODIFY);   lua_setglobal(L, "MODIFY");
	lua_pushinteger(L, CREATE);   lua_setglobal(L, "CREATE");
	lua_pushinteger(L, DELETE);   lua_setglobal(L, "DELETE");
	lua_pushinteger(L, MOVE);     lua_setglobal(L, "MOVE");
	lua_pushinteger(L, MOVEFROM); lua_setglobal(L, "MOVEFROM");
	lua_pushinteger(L, MOVETO);   lua_setglobal(L, "MOVETO");
	
	/* register log levels */
	lua_pushinteger(L, DEBUG);   lua_setglobal(L, "DEBUG");
	lua_pushinteger(L, VERBOSE); lua_setglobal(L, "VERBOSE");
	lua_pushinteger(L, NORMAL);  lua_setglobal(L, "NORMAL");
	lua_pushinteger(L, ERROR);   lua_setglobal(L, "ERROR");

	/* checks if the user overrode default runner file */ 
	if (!strcmp(argv[argp], "--runner")) {
		if (argc < 3) {
			logstring(ERROR, "Lsyncd Lua-runner file missing after --runner.");
#ifdef LSYNCD_DEFAULT_RUNNER_FILE
			printlogf(L, ERROR, 
				"Using '%s' as default location for runner.",
				LSYNCD_DEFAULT_RUNNER_FILE);
#else
			logstring(ERROR, 
				"Using a staticly included runner as default.");
#endif
			return -1; //ERRNO
		}
		lsyncd_runner_file = argv[argp + 1];
		argp += 2;
	} else {
#ifdef LSYNCD_DEFAULT_RUNNER_FILE
		lsyncd_runner_file = LSYNCD_DEFAULT_RUNNER_FILE;
#endif
	}
	if (lsyncd_runner_file) {
		/* checks if the runner file exists */
		struct stat st;
		if (stat(lsyncd_runner_file, &st)) {
			printlogf(L, ERROR, 
				"Cannot find Lsyncd Lua-runner at '%s'.", lsyncd_runner_file);
			printlogf(L, ERROR, 
				"Maybe specify another place?");
			printlogf(L, ERROR, 
				"%s --runner RUNNER_FILE CONFIG_FILE", argv[0]);
			return -1; // ERRNO
		}
		/* loads the runner file */
		if (luaL_loadfile(L, lsyncd_runner_file)) {
			printlogf(L, ERROR, 
				"error loading '%s': %s", 
				lsyncd_runner_file, lua_tostring(L, -1));
			return -1; // ERRNO
		}
	}
#ifndef LSYNCD_DEFAULT_RUNNER_FILE
	else {
		/* loads the runner from binary */
		if (luaL_loadbuffer(L, &_binary_luac_out_start, 
				&_binary_luac_out_end - &_binary_luac_out_start, "lsyncd.lua"))
		{
			printlogf(L, ERROR, 
				"error loading precompiled lsyncd.lua runner: %s", 
				lua_tostring(L, -1));
			return -1; // ERRNO
		}
	}
#else
	else {
		/* this should never be possible, security code nevertheless */
		logstring(ERROR, 
			"Internal fail: lsyncd_runner is NULL with non-static runner");
		return -1; // ERRNO
	}
#endif
	/* execute the runner defining all its functions */
	if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
		printlogf(L, ERROR, 
			"error preparing '%s': %s", 
			lsyncd_runner_file ? lsyncd_runner_file : "internal runner", 
			lua_tostring(L, -1));
		return -1; // ERRNO
	}

	{
		/* checks version match between runner/core */
		const char *lversion;
		lua_getglobal(L, "lsyncd_version");
		lversion = luaL_checkstring(L, -1);
		lua_pop(L, 1);
		if (strcmp(lversion, PACKAGE_VERSION)) {
			printlogf(L, ERROR, 
				"Version mismatch '%s' is '%s', but core is '%s'",
 				lsyncd_runner_file ? lsyncd_runner_file : "internal runner",
				lversion, PACKAGE_VERSION);
			return -1; // ERRNO
		}
	}

	{
		/* checks if there is a "-help" or "--help" before anything else */
		int i;
		for(i = argp; i < argc; i++) {
			if (!strcmp(argv[i],"-help") || !strcmp(argv[i],"--help")) {
				lua_getglobal(L, "lsyncd_help");
				lua_call(L, 0, 0);
				return -1; // ERRNO	
			}
		}
	}

	{	
		/* checks for the configuration and existence of the config file */
		struct stat st;
		if (argp + 1 > argc) {
			minihelp(argv[0]);
			return -1; // ERRNO
		}
		lsyncd_config_file = argv[argp++];
		if (stat(lsyncd_config_file, &st)) {
			printlogf(L, ERROR, 
				"Cannot find config file at '%s'.", 
				lsyncd_config_file);
			return -1; // ERRNO
		}
	}

	/* loads and executes the config file */
	if (luaL_loadfile(L, lsyncd_config_file)) {
		printlogf(L, ERROR, 
			"error loading %s: %s", 
			lsyncd_config_file, lua_tostring(L, -1));
		return -1; // ERRNO
	}
	if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
		printlogf(L, ERROR, 
			"error preparing %s: %s", 
			lsyncd_config_file, lua_tostring(L, -1));
		return -1; // ERRNO
	}

	/* opens inotify */
	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		printlogf(L, ERROR, 
			"Cannot create inotify instance! (%d:%s)", 
			errno, strerror(errno));
		return -1; // ERRNO
	}

	{
		/* adds signal handlers *
		 * listens to SIGCHLD, but blocks it until pselect() 
		 * opens up*/
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGCHLD);
		signal(SIGCHLD, sig_child);
		sigprocmask(SIG_BLOCK, &set, NULL);
	}

	{
		/* runs initialitions from runner *
		 * lua code will set configuration and add watches */
		int idx = 1;
		/* creates a table with all remaining argv option arguments */
		lua_getglobal(L, "lsyncd_initialize");
		lua_newtable(L);
		while(argp < argc) {
			lua_pushnumber(L, idx++);
			lua_pushstring(L, argv[argp++]);
			lua_settable(L, -3);
		}
		lua_call(L, 1, 0);
	}

	masterloop(L);

	/* cleanup */
	close(inotify_fd);
	lua_close(L);
	return 0;
}
