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
#include <math.h>
#include <time.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

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
};

/**
 * The Lua part of lsyncd if compiled into the binary.
 */
#ifndef LSYNCD_DEFAULT_RUNNER_FILE
extern char _binary_luac_out_start;
extern char _binary_luac_out_end; 
#endif

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
 * configuration parameters
 */
static struct settings {
	/** 
	 * If not NULL Lsyncd logs into this file.
	 */
	char * log_file;

	/**
	 * If true Lsyncd sends log messages to syslog
	 */
	bool log_syslog;

	/**
	 * -1 logs everything, 0 normal mode,
	 * LOG_ERROR logs errors only.
	 */
	int log_level;

	/**
	 * True if Lsyncd shall not daemonize.
	 */
	bool nodaemon;	
	
	/** 
	 * If not NULL Lsyncd writes its pid into this file.
	 */
	char * pidfile;

} settings = {
	.log_file = NULL,
	.log_syslog = false,
	.log_level = 0,
	.nodaemon = false,
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
 * Set to TERM or HUP in signal handler, when lsyncd should end or reset ASAP.
 */
static volatile sig_atomic_t hup  = 0;
static volatile sig_atomic_t term = 0;

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
 * signal handler
 */
void
sig_handler(int sig)
{
	switch (sig) {
	case SIGTERM:
		term = 1;
		return;
	case SIGHUP:
		hup = 1;
		return;
	}
}


/**
 * predeclerations -- see below
 */
static void * s_calloc(size_t nmemb, size_t size);
static void * s_malloc(size_t size);
static void * s_realloc(void *ptr, size_t size);
static char * s_strdup(const char *src);

/*****************************************************************************
 * Logging
 ****************************************************************************/

/**
 * A logging category 
 */
struct logcat {
	char *name;
	int priority;
};

/**
 * A table of all enabled logging categories.
 * Sorted by first letter to have to do less comparisons;
 */
static struct logcat *logcats[26] = {0,};

/**
 * Returns the positive priority if category is configured to be logged.
 * or -1 
 */
static int
check_logcat(const char *name)
{
	struct logcat *lc;
	if (name[0] < 'A' || name[0] > 'Z') {
		return false;
	}
	lc = logcats[name[0]-'A'];
	if (!lc) {
		return -1;
	}
	while (lc->name) {
		if (!strcmp(lc->name, name)) {
			return lc->priority;
		}
		lc++;
	}
	return -1;
}

/**
 * Adds a logging category
 * @return true if OK.
 */
static bool
add_logcat(const char *name, int priority)
{
	struct logcat *lc; 
	if (!strcmp("all", name)) {
		settings.log_level = -1;
		return true;
	}
	if (!strcmp("scarce", name)) {
		settings.log_level = LOG_ERR;
		return true;
	}

	/* category must start with capital letter */
	if (name[0] < 'A' || name[0] > 'Z') {
		return false;
	}
	if (!logcats[name[0]-'A']) {
		/* en empty capital letter */
		lc = logcats[name[0]-'A'] = s_calloc(2, sizeof(struct logcat));
	} else {
		/* length of letter list */
		int ll = 0; 	
		/* counts list length */
		for(lc = logcats[name[0]-'A']; lc->name; lc++) {
			ll++;
		}
		/* enlarge list */
		logcats[name[0]-'A'] = 
			s_realloc(logcats[name[0]-'A'], (ll + 2) * sizeof(struct logcat)); 
		/* go to list end */ 
		for(lc = logcats[name[0]-'A']; lc->name; lc++) {
			if (!strcmp(name, lc->name)) {
				/* already there */
				return true;
			}
		}
	}
	lc->name = s_strdup(name);
	lc->priority = priority;
	/* terminates the list */
	lc[1].name = NULL;
	return true;
}

/**
 * Logs a string.
 *
 * @param priorty  the priority of the log message
 * @param cat      the category
 * @param message  the log message
 */

#define logstring(cat, message) \
	{int p; if ((p = check_logcat(cat)) >= settings.log_level) \
	{logstring0(p, cat, message);}}

static void 
logstring0(int priority, const char *cat, const char *message)
{
	/* in case of logall and not found category priority will be -1 */
	if (priority < 0) {
		priority = LOG_DEBUG;
	}
	if (!running) {
		/* lsyncd is in intial configuration.
		 * thus just print to normal stdout/stderr. */
		if (priority >= LOG_ERR) {
			fprintf(stderr, "%s: %s\n", cat, message);
		} else {
			printf("%s: %s\n", cat, message);
		}
		return;
	}

	/* writes on console if not daemon */
	if (!is_daemon) {
		char ct[255];
		/* gets current timestamp hour:minute:second */
		time_t mtime;
		time(&mtime);
		strftime(ct, sizeof(ct), "%T", localtime(&mtime));
		FILE * flog = priority <= LOG_ERR ? stderr : stdout;
		fprintf(flog, "%s %s: %s\n", ct, cat, message);
	}

	/* writes to file if configured so */
	if (settings.log_file) {
		FILE * flog = fopen(settings.log_file, "a");
		/* gets current timestamp day-time-year */
		char * ct;
		time_t mtime;
		time(&mtime);
		ct = ctime(&mtime);
	 	/* cuts trailing linefeed */
 		ct[strlen(ct) - 1] = 0;

		if (flog == NULL) {
			fprintf(stderr, "Cannot open logfile [%s]!\n", 
				settings.log_file);
			exit(-1);  // ERRNO
		}
		fprintf(flog, "%s %s: %s\n", ct, cat, message);
		fclose(flog);
	}

	/* sends to syslog if configured so */
	if (settings.log_syslog) {
		syslog(priority, "%s, %s", cat, message);
	}
	return;
}

/**
 * Let the core print logmessage comfortably.
 * This uses the lua_State for it easy string buffers only.
 */
#define printlogf(L, cat, ...) \
	{int p; if ((p = check_logcat(cat)) >= settings.log_level)  \
	{printlogf0(L, p, cat, __VA_ARGS__);}}

static void
printlogf0(lua_State *L, 
          int priority, 
		  const char *cat,
		  const char *fmt, 
		  ...)
	__attribute__((format(printf, 4, 5)));

static void
printlogf0(lua_State *L, 
	int priority,
	const char *cat,
	const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);
	logstring0(priority, cat, luaL_checkstring(L, -1));
	lua_pop(L, 1);
	return;
}

/*****************************************************************************
 * Simple memory management
 ****************************************************************************/

/**
 * "secured" calloc.
 */
void *
s_calloc(size_t nmemb, size_t size)
{
	void *r = calloc(nmemb, size);
	if (r == NULL) {
		logstring0(LOG_ERR, "Error", "Out of memory!");
		exit(-1); // ERRNO 
	}	
	return r;
}

/**
 * "secured" malloc. the deamon shall kill itself
 * in case of out of memory.
 */
static void *
s_malloc(size_t size)
{
	void *r = malloc(size);
	if (r == NULL) {
		logstring0(LOG_ERR, "Error", "Out of memory!");
		exit(-1);  // ERRNO
	}
	return r;
}

/**
 * "secured" realloc.
 */
static void *
s_realloc(void *ptr, size_t size)
{
	void *r = realloc(ptr, size);
	if (r == NULL) {
		logstring0(LOG_ERR, "Error", "Out of memory!");
		exit(-1);
	}
	return r;
}

/**
 * "secured" strdup.
 */
static char *
s_strdup(const char *src)
{
	char *s = strdup(src);
	if (s == NULL) {
		logstring0(LOG_ERR, "Error", "Out of memory!");
		exit(-1); // ERRNO
	}
	return s;
}

/*****************************************************************************
 * Pipes management
 ****************************************************************************/

/**
 * A child process gets text piped longer than on
 * write() can manage.
 */
struct pipemsg {
	/* pipe file descriptor */
	int fd;

	/* message to send */
	char *text;

	/* length of text */
	int tlen;

	/* position in message */
	int pos;
};

/**
 * All pipes currently active.
 */
static struct pipemsg *pipes = NULL;

/**
 * amount of pipes allocated.
 */
size_t pipes_size = 0; 

/**
 * number of pipes used.
 */
size_t pipes_len = 0;


/*****************************************************************************
 * helper routines.
 ****************************************************************************/

/**
 * Sets the close-on-exit flag for an fd
 */
static void
close_exec_fd(int fd)
{
	int flags;
    flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
		logstring("Error", "cannot get descriptor flags!");
		exit(-1); // ERRNO
	}
	flags |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFD, flags) == -1) {
		logstring("Error", "cannot set descripptor flags!");
		exit(-1); // ERRNO
	}
}

/**
 * Sets the non-blocking flag for an fd
 */
static void
non_block_fd(int fd)
{
	int flags;
    flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
		logstring("Error", "cannot get status flags!");
		exit(-1); // ERRNO
	}
	flags |= O_NONBLOCK;;
	if (fcntl(fd, F_SETFL, flags) == -1) {
		logstring("Error", "cannot set status flags!");
		exit(-1); // ERRNO
	}
}

/**
 * Writes a pid file.
 */
void
write_pidfile(lua_State *L, const char *pidfile) {
	FILE* f = fopen(pidfile, "w");
	if (!f) {
		printlogf(L, "Error", "Cannot write pidfile; '%s'", pidfile);
		exit(-1); // ERRNO
	}
	fprintf(f, "%i\n", getpid());
	fclose(f); 
}


/*****************************************************************************
 * Library calls for lsyncd.lua
 * 
 * These are as minimal as possible glues to the operating system needed for 
 * lsyncd operation.
 *
 ****************************************************************************/

static int l_stackdump(lua_State* L);

/**
 * Adds an inotify watch
 * 
 * @param dir (Lua stack) path to directory
 * @return    (Lua stack) numeric watch descriptor
 */
static int
l_inotifyadd(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	lua_Integer wd = inotify_add_watch(inotify_fd, path, standard_event_mask);
	lua_pushinteger(L, wd);
	return 1;
}

/**
 * Removes an inotify watch
 * 
 * @param dir (Lua stack) numeric watch descriptor
 * @return    nil
 */
static int
l_inotifyrm(lua_State *L)
{
	lua_Integer wd = luaL_checkinteger(L, 1);
	inotify_rm_watch(inotify_fd, wd);
	return 0;
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
	/* log category */
	const char * cat;
	/* log message */
	const char * message;
	/* log priority */
	int priority;

	cat = luaL_checkstring(L, 1);
	priority = check_logcat(cat);
	/* skips filtered messages */
	if (priority < settings.log_level) {
		return 0;
	}

	{
		// replace non string values
		int i;
		int top = lua_gettop(L);
		for (i = 1; i <= top; i++) { 
			int t = lua_type(L, i);
			switch (t) {
			case LUA_TTABLE:
				lua_pushfstring(L, "(Table: %p)", lua_topointer(L, i));
				lua_replace(L, i);
				break;
			case LUA_TBOOLEAN:
				if (lua_toboolean(L, i)) {
					lua_pushstring(L, "(true)");
				} else {
					lua_pushstring(L, "(false)");
				}
				lua_replace(L, i);
				break;
			}
		}
	}

	/* concates if there is more than one string parameter */
	lua_concat(L, lua_gettop(L) - 1);

	message = luaL_checkstring(L, 2);
	logstring0(priority, cat, message);
	return 0;
}

/**
 * Returns (on Lua stack) true if time1 is earler than time2
 * @param (on Lua Stack) time1
 * @param (on Lua Stack) time2
 * @return the true if time1 < time2
 */
static int
l_clockbefore(lua_State *L) 
{
	clock_t t1 = (clock_t) luaL_checkinteger(L, 1);
	clock_t t2 = (clock_t) luaL_checkinteger(L, 2);
	lua_pushboolean(L, time_before(t1, t2));
	return 1;
}

/**
 * Returns (on Lua stack) true if time1 is earler or eq to time2
 * @param (on Lua Stack) time1
 * @param (on Lua Stack) time2
 * @return the true if time1 <= time2
 */
static int
l_clockbeforeq(lua_State *L) 
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
 * @param1 (Lua stack) the clock timer
 * @param2 (Lua stack) seconds to change clock.
 *
 * @return (Lua stack) clock timer + seconds.
 */
static int
l_addtoclock(lua_State *L) 
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
 *    or "<" in which case the next argument is a string that will be piped 
 *    on stdin. the arguments will follow that one.
 *
 * @return (Lua stack) the pid on success, 0 on failure.
 */
static int
l_exec(lua_State *L)
{
	/* the binary to call */
	const char *binary = luaL_checkstring(L, 1);
	/* number of arguments */
	int argc = lua_gettop(L) - 1;
	/* the pid spawned */
	pid_t pid;
	/* the arguments position in the lua arguments */
	int li = 1;

	/* the pipe to text */
	char const *pipe_text = NULL;
	/* the arguments */
	char const **argv;
	/* pipe file descriptors */
	int pipefd[2];

	/* writes a log message, prepares the message only if actually needed. */
	if (check_logcat("Exec") >= settings.log_level) {
		int i;
		lua_pushvalue(L, 1);
		for(i = 1; i <= argc; i++) {
			lua_pushstring(L, " [");
			lua_pushvalue(L, i + 1);
			lua_pushstring(L, "]");
		}
		lua_concat(L, 3 * argc + 1);
		logstring0(LOG_DEBUG, "Exec", luaL_checkstring(L, -1));
		lua_pop(L, 1);
	}

	if (argc >= 2 && !strcmp(luaL_checkstring(L, 2), "<")) {
		/* pipes something into stdin */
		pipe_text = luaL_checkstring(L, 3);
		/* creates the pipe */
		if (pipe(pipefd) == -1) {
			logstring("Error", "cannot create a pipe!");
			exit(-1); // ERRNO
		}
		/* always close the write end for child processes */
		close_exec_fd(pipefd[1]);
		/* set the write end on non-blocking */
		non_block_fd(pipefd[1]);

		argc -= 2;
		li += 2;
	}

	{
		/* prepares the arguments */
		int i;
		argv = s_calloc(argc + 2, sizeof(char *));
		argv[0] = binary;
		for(i = 1; i <= argc; i++) {
			argv[i] = luaL_checkstring(L, i + li);
		}
		argv[i] = NULL;
	}
	pid = fork();

	if (pid == 0) {
		/* replaces stdin for pipes */
		if (pipe_text) {
			dup2(pipefd[0], STDIN_FILENO);
		}
		/* if lsyncd runs as a daemon and has a logfile it will redirect
		   stdout/stderr of child processes to the logfile. */
		if (is_daemon && settings.log_file) {
			if (!freopen(settings.log_file, "a", stdout)) {
				printlogf(L, "Error", 
					"cannot redirect stdout to '%s'.", 
					settings.log_file);
			}
			if (!freopen(settings.log_file, "a", stderr)) {
				printlogf(L, "Error", 
					"cannot redirect stderr to '%s'.", 
					settings.log_file);
			}
		}
		execv(binary, (char **)argv);
		/* in a sane world execv does not return! */
		printlogf(L, "Error", "Failed executing [%s]!", binary);
		exit(-1); // ERRNO
	}

	if (pipe_text) {
		int tlen = strlen(pipe_text);
		int len;
		/* first closes read-end of pipe, this is for child process only */
		close(pipefd[0]);
		/* start filling the pipe */
		len = write(pipefd[1], pipe_text, tlen);
		if (len < 0) {
			logstring("Normal", "immediatly broken pipe.");
			close(pipefd[0]);
		}
		if (len == tlen) {
			/* usual and best case, the pipe accepted all input -> close */
			close(pipefd[1]);
			logstring("Exec", "one-sweeped pipe");
		} else {
			int p = pipes_len;
			logstring("Exec", "adding delayed pipe");
			pipes_len++;
			if (pipes_len > pipes_size) {
				pipes_size = pipes_len;
				pipes = s_realloc(pipes, pipes_size*sizeof(struct pipemsg));
			}
			pipes[p].fd = pipefd[1];
			pipes[p].tlen = tlen;
			pipes[p].pos = len;
			pipes[p].text = s_strdup(pipe_text);
		}
		close(pipefd[0]);
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
l_realdir(lua_State *L)
{
	luaL_Buffer b;
	char *cbuf;
	const char *rdir = luaL_checkstring(L, 1);
	
	/* use c-library to get absolute path */
	cbuf = realpath(rdir, NULL);
	if (cbuf == NULL) {
		printlogf(L, "Error", "failure getting absolute path of [%s]", rdir);
		return 0;
	}
	{
		/* makes sure its a directory */
	    struct stat st;
	    if (stat(cbuf, &st)) {
			printlogf(L, "Error", 
				"cannot get absolute path of dir '%s': %s", 
				rdir, strerror(errno));
			return 0;
		}
	    if (!S_ISDIR(st.st_mode)) {
			printlogf(L, "Error", 
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
	printlogf(L, "Debug", "total in stack %d",top);
	for (i = 1; i <= top; i++) { 
		int t = lua_type(L, i);
		switch (t) {
		case LUA_TSTRING:
			printlogf(L, "Debug", "%d string: '%s'", 
				i, lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:
			printlogf(L, "Debug", "%d boolean %s", 
				i, lua_toboolean(L, i) ? "true" : "false");
			break;
		case LUA_TNUMBER: 
			printlogf(L, "Debug", "%d number: %g", 
				i, lua_tonumber(L, i));
			break;
		default: 
			printlogf(L, "Debug", "%d %s", 
				i, lua_typename(L, t));
			break;
		}
	}
	return 0;
}

/**
 * Reads the directories entries.
 * XXX
 * @param  (Lua stack) absolute path to directory.
 * @return (Lua stack) a table of directory names.
 *                     names are keys, values are boolean 
 *                     true on dirs.
 */
static int
l_readdir (lua_State *L)
{
	const char * dirname = luaL_checkstring(L, 1);
	DIR *d;

	d = opendir(dirname);
	if (d == NULL) {
		printlogf(L, "Error", "cannot open dir [%s].", dirname);
		return 0;
	}
	
	lua_newtable(L);
	while (!hup && !term) {
		struct dirent *de = readdir(d);
		bool isdir;
		if (de == NULL) {
			/* finished */
			break;
		}

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) { 
			/* ignores . and .. */
			continue;
		}

		if (de->d_type == DT_UNKNOWN) {
			/* must call stat on some systems :-/ */
			char *entry = s_malloc(strlen(dirname) + strlen(de->d_name) + 2);
			struct stat st;
			strcpy(entry, dirname);
			strcat(entry, "/");
			strcat(entry, de->d_name);
			stat(entry, &st);
			isdir = S_ISDIR(st.st_mode);
			free(entry);
		} else {
			/* readdir can trusted */
			isdir = de->d_type == DT_DIR;
		}

		/* adds this entry to the Lua table */
		lua_pushstring(L, de->d_name);
		lua_pushboolean(L, isdir);
		lua_settable(L, -3);
	}
	return 1;
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
 * Configures core parameters.
 * 
 * @param (Lua stack) a string for a core configuratoin
 * @param (Lua stack) --differes depending on string. 
 */
static int 
l_configure(lua_State *L) 
{
	const char * command = luaL_checkstring(L, 1);
	if (!strcmp(command, "running")) {
		/* set by runner after first initialize 
		 * from this on log to configurated log end instead of 
		 * stdout/stderr */
		running = true;
		if (settings.pidfile) {
			write_pidfile(L, settings.pidfile);
		}
		if (!settings.nodaemon && !is_daemon) {
			if (!settings.log_file) {
				settings.log_syslog = true;
			}
			if (daemon(0, 0)) {
				logstring("Error", "Failed to daemonize");
				exit(-1); //ERRNO
			}
			is_daemon = true;
		}
	} else if (!strcmp(command, "nodaemon")) {
		settings.nodaemon = true;
	} else if (!strcmp(command, "logfile")) {
		const char * file = luaL_checkstring(L, 2);
		if (settings.log_file) {
			free(settings.log_file);
		}
		settings.log_file = s_strdup(file);
	} else if (!strcmp(command, "pidfile")) {
		const char * file = luaL_checkstring(L, 2);
		if (settings.pidfile) {
			free(settings.pidfile);
		}
		settings.pidfile = s_strdup(file);
	} else {
		printlogf(L, "Error", 
			"Internal error, unknown parameter in l_configure(%s)", 
			command);
		exit(-1); //ERRNO
	}
	return 0;
}


static const luaL_reg lsyncdlib[] = {
		{"addtoclock",   l_addtoclock   },
		{"clockbefore",  l_clockbefore  },
		{"clockbeforeq", l_clockbeforeq },
		{"configure",    l_configure    },
		{"earlier",      l_earlier      },
		{"exec",         l_exec         },
		{"inotifyadd",   l_inotifyadd   },
		{"inotifyrm",    l_inotifyrm    },
		{"log",          l_log          },
		{"now",          l_now          },
		{"readdir",      l_readdir      },
		{"realdir",      l_realdir      },
		{"stackdump",    l_stackdump    },
		{"terminate",    l_terminate    },
		{NULL, NULL}
};

/*****************************************************************************
 * Lsyncd Core 
****************************************************************************/

/**
 * Dummy variable whos address is used as the cores index in the lua registry
 * to the lua runners function table in the lua registry.
 */
static int runner;

/**
 * Dummy variable whos address is used as the cores index n the lua registry
 * to the lua runners error handler.
 */
static int callError;

/**
 * Pushes a function from the runner on the stack.
 * Prior it pushed the callError handler.
 */
static void
load_runner_func(lua_State *L, 
                 const char *name)
{
	printlogf(L, "Call", "%s()", name);
    
	/* pushes the error handler */
	lua_pushlightuserdata(L, (void *) &callError);
	lua_gettable(L, LUA_REGISTRYINDEX);
	
	/* pushes the function */
	lua_pushlightuserdata(L, (void *) &runner);
	lua_gettable(L, LUA_REGISTRYINDEX);
	lua_pushstring(L, name);
	lua_gettable(L, -2);
	lua_remove(L, -2);
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
static void 
handle_event(lua_State *L, 
             struct inotify_event *event) 
{
	int event_type;

	/* used to execute two events in case of unmatched MOVE_FROM buffer */
	struct inotify_event *after_buf = NULL;
	if (hup || term) {
		return;
	}
	if (event && (IN_Q_OVERFLOW & event->mask)) {
		/* and overflow happened, tells the runner */
		load_runner_func(L, "overflow");
		if (lua_pcall(L, 0, 0, -2)) {
			exit(-1); // ERRNO
		}
		lua_pop(L, 1);
		hup = 1;
		return;
	}
	/* cancel on ignored or resetting */
	if (event && (IN_IGNORED & event->mask)) {
		return;
	}
	if (event && event->len == 0) {
		/* sometimes inotify sends such strange events, 
		 * (e.g. when touching a dir */
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
		logstring("Inotify", "skipped some inotify event.");
		return;
	}

	/* and hands over to runner */
	load_runner_func(L, "inotifyEvent"); 
	switch(event_type) {
	case ATTRIB : lua_pushstring(L, "Attrib"); break;
	case MODIFY : lua_pushstring(L, "Modify"); break;
	case CREATE : lua_pushstring(L, "Create"); break;
	case DELETE : lua_pushstring(L, "Delete"); break;
	case MOVE   : lua_pushstring(L, "Move");   break;
	default : 
		logstring("Error", "Internal: unknown event in handle_event()"); 
		exit(-1);	// ERRNO
	}
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
	if (lua_pcall(L, 6, 0, -8)) {
		exit(-1); // ERRNO
	}
	lua_pop(L, 1);
	/* if there is a buffered event executes it */
	if (after_buf) {
		logstring("Inotify", "handling buffered event.");
		handle_event(L, after_buf);
	}
}

/**
 * Normal operation happens in here.
 */
static void
masterloop(lua_State *L)
{
	size_t readbuf_size = 2048;
	char *readbuf = s_malloc(readbuf_size);
	while(true) {
		bool have_alarm;
		clock_t now = times(NULL);
		clock_t alarm_time;
		bool do_read = false;
		ssize_t len; 

		/* queries runner about soonest alarm  */
		load_runner_func(L, "getAlarm"); 
		if (lua_pcall(L, 0, 1, -2)) {
			exit(-1); // ERRNO
		}
		
		if (lua_type(L, -1) == LUA_TBOOLEAN) {
			have_alarm = lua_toboolean(L, -1);
		} else {
			have_alarm = true;
			alarm_time = (clock_t) luaL_checkinteger(L, -1);
		}
		lua_pop(L, 2);

		if (have_alarm && time_before_eq(alarm_time, now)) {
			/* there is a delay that wants to be handled already thus do not 
			 * read from inotify_fd and jump directly to its handling */
			logstring("Masterloop", "immediately handling delays.");
		} else {
			/* use select() to determine what happens next
			 * + a new event on inotify
			 * + an alarm on timeout  
			 * + the return of a child process */
			struct timespec tv;

			if (have_alarm) { 
				double d = ((double)(alarm_time - now)) / clocks_per_sec;
				tv.tv_sec  = d;
				tv.tv_nsec = ((d - (long) d)) * 1000000000.0;
				printlogf(L, "Masterloop", 
					"going into select (timeout %f seconds)", d);
			} else {
				logstring("Masterloop", "going into select (no timeout).");
			}
			/* if select returns a positive number there is data on inotify
			 * on zero the timemout occured. */
			{
				fd_set rfds;
				fd_set wfds;
				sigset_t sigset;
				sigemptyset(&sigset);
				int nfds = inotify_fd;
				int pi;

				FD_ZERO(&rfds);
				FD_ZERO(&wfds);
				FD_SET(inotify_fd, &rfds);
				for(pi = 0; pi < pipes_len; pi++) {
					int pfd = pipes[pi].fd;
					nfds = pfd > nfds ? pfd : nfds;
					FD_SET(pfd, &wfds);
				}

				/* reuse pi for result */
				pi = pselect(nfds + 1, &rfds, &wfds, NULL, 
					have_alarm ? &tv : NULL, &sigset);
				if (pi >= 0) {
					do_read = FD_ISSET(inotify_fd, &rfds);
				}
				if (do_read) {
					logstring("Masterloop", do_read > 0 ? 
						"theres data on inotify." :
						"core: select() timeout or signal.");
				}
			}
		} 
		
		/* reads possible events from inotify stream */
		while(do_read) {
			int i = 0;
			do {
				len = read (inotify_fd, readbuf, readbuf_size);
				if (len < 0 && errno == EINVAL) {
					/* kernel > 2.6.21 indicates that way that way that
					 * the buffer was too small to fit a filename.
					 * double its size and try again. When using a lower
					 * kernel and a filename > 2KB appears lsyncd
					 * will fail. (but does a 2KB filename really happen?)
					 */
					readbuf_size *= 2;
					readbuf = s_realloc(readbuf, readbuf_size);
					continue;
				}
			} while(0);
			if (len == 0) {
				/* nothing more inotify */
				break;
			}
			while (i < len && !hup && !term) {
				struct inotify_event *event = 
					(struct inotify_event *) &readbuf[i];
				handle_event(L, event);
				i += sizeof(struct inotify_event) + event->len;
			}
			if (!move_event) {
				/* give it a pause if not endangering splitting a move */
				break;
			}
		} 
		/* checks if there is an unary MOVE_FROM left in the buffer */
		if (move_event) {
			logstring("Inotify", "handling unary move from.");
			handle_event(L, NULL);	
		}

		{
			/* writes into pipes if any */
			int pi;
			for(pi = 0; pi < pipes_len; pi++) {
				struct pipemsg *pm = pipes + pi;
				int len = write(pm->fd, pm->text + pm->pos, pm->tlen - pm->pos);
				bool do_close = false;
				pm->pos += len;
				if (len < 0) {
					logstring("Normal", "broken pipe.");
					do_close = true;
				} else if (pm->pos >= pm->tlen) {
					logstring("Debug", "finished pipe.");
					do_close = true;
				}
				if (do_close) {
					close(pm->fd);
					free(pm->text);
					pipes_len--;
					memmove(pipes + pi, pipes + pi + 1, 
						(pipes_len - pi) * sizeof(struct pipemsg));
					pi--;
					continue;
				}
			}
		}

		/* collects zombified child processes */
		while(1) {
			int status;
			pid_t pid = waitpid(0, &status, WNOHANG);
			if (pid <= 0) {
				break;
			}
			load_runner_func(L, "collectProcess"); 
			lua_pushinteger(L, pid);
			lua_pushinteger(L, WEXITSTATUS(status));
			if (lua_pcall(L, 2, 0, -4)) {
				exit(-1); // ERRNO
			}
			lua_pop(L, 1);
		} 

		if (hup) {
			load_runner_func(L, "hup");
			if (lua_pcall(L, 0, 0, -2)) {
				exit(-1); // ERRNO
			}
			lua_pop(L, 1);
			hup = 0;
		}

		if (term == 1) {
			load_runner_func(L, "term");
			if (lua_pcall(L, 0, 0, -2)) {
				exit(-1); // ERRNO
			}
			lua_pop(L, 1);
			term = 2;
		}

		/* lets the runner do stuff every cycle, 
		 * like starting new processes, writing the statusfile etc. */
		load_runner_func(L, "cycle");
		lua_pushinteger(L, times(NULL));
		if (lua_pcall(L, 1, 1, -3)) {
			exit(-1); // ERRNO
		}
		if (!lua_toboolean(L, -1)) {
			/* cycle told core to break mainloop */
			free(readbuf);
			lua_pop(L, 2);
			return;
		}
		lua_pop(L, 2);
	}
}

/**
 * Main
 */
int
main1(int argc, char *argv[])
{
	/* the Lua interpreter */
	lua_State* L;

	/* scripts */
	char * lsyncd_runner_file = NULL;
	char * lsyncd_config_file = NULL;

	int argp = 1;

	/* load Lua */
	L = lua_open();
	luaL_openlibs(L);
	{
		/* checks the lua version */
		const char *version;
		int major, minor;
		lua_getglobal(L, "_VERSION");
		version = luaL_checkstring(L, -1);
		if (sscanf(version, "Lua %d.%d", &major, &minor) != 2) {
			fprintf(stderr, "cannot parse lua library version!\n");
			exit(-1); // ERRNO
		}
		if ((major < 5) || (major == 5 && minor < 1)) {
			fprintf(stderr, "lua library is too old. Need 5.1 at least");
			exit(-1); // ERRNO
		}
		lua_pop(L, 1);
	}

	{
		/* prepares logging early */
		int i = 1;
		add_logcat("Normal", LOG_NOTICE);
		add_logcat("Error",  LOG_ERR);
		while (i < argc) {
			if (strcmp(argv[i], "-log") && strcmp(argv[i], "--log")) {
				i++; continue;
			}
			if (++i >= argc) {
				break;
			}
			if (!add_logcat(argv[i], LOG_NOTICE)) {
				printlogf(L, "Error", "'%s' is not a valid logging category", 
					argv[i]);
				exit(-1); // ERRNO
			}
		}
	}

	/* registers lsycnd core */
	luaL_register(L, "lsyncd", lsyncdlib);
	lua_setglobal(L, "lysncd");

	if (check_logcat("Debug") >= settings.log_level) {
		/* printlogf doesnt support %ld :-( */
		printf("kernels clocks_per_sec=%ld\n", clocks_per_sec);
	}

	/* checks if the user overrode default runner file */ 
	if (argp < argc && !strcmp(argv[argp], "--runner")) {
		if (argp + 1 >= argc) {
			logstring("Error", 
				"Lsyncd Lua-runner file missing after --runner.");
#ifdef LSYNCD_DEFAULT_RUNNER_FILE
			printlogf(L, "Error", 
				"Using '%s' as default location for runner.",
				LSYNCD_DEFAULT_RUNNER_FILE);
#else
			logstring("Error", 
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
			printlogf(L, "Error", 
				"Cannot find Lsyncd Lua-runner at '%s'.", lsyncd_runner_file);
			printlogf(L, "Error", "Maybe specify another place?");
			printlogf(L, "Error", 
				"%s --runner RUNNER_FILE CONFIG_FILE", argv[0]);
			exit(-1); // ERRNO
		}
		/* loads the runner file */
		if (luaL_loadfile(L, lsyncd_runner_file)) {
			printlogf(L, "Error", 
				"error loading '%s': %s", 
				lsyncd_runner_file, lua_tostring(L, -1));
			exit(-1); // ERRNO
		}
	} else {
#ifndef LSYNCD_DEFAULT_RUNNER_FILE
		/* loads the runner from binary */
		if (luaL_loadbuffer(L, &_binary_luac_out_start, 
				&_binary_luac_out_end - &_binary_luac_out_start, "lsyncd.lua"))
		{
			printlogf(L, "Error", 
				"error loading precompiled lsyncd.lua runner: %s", 
				lua_tostring(L, -1));
			exit(-1); // ERRNO
		}
#else
		/* this should never be possible, security code nevertheless */
		logstring("Error", 
			"Internal fail: lsyncd_runner is NULL with non-static runner");
		exit(-1); // ERRNO
#endif
	}

	{
		/* place to store the lua runners functions */
		/* executes the runner defining all its functions */
		if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
			printlogf(L, "Error", 
				"error preparing '%s': %s", 
				lsyncd_runner_file ? lsyncd_runner_file : "internal runner", 
				lua_tostring(L, -1));
			exit(-1); // ERRNO
		}
		lua_pushlightuserdata(L, (void *)&runner);
		/* switches the value (result of preparing) and the key &runner */
		lua_insert(L, 1);
		/* saves the table of the runners functions in the lua registry */
		lua_settable(L, LUA_REGISTRYINDEX);

		/* saves the error function extra */
		/* &callError is the key */
		lua_pushlightuserdata(L, (void *) &callError);
		/* &runner[callError] the value */
		lua_pushlightuserdata(L, (void *) &runner);
		lua_gettable(L, LUA_REGISTRYINDEX);
		lua_pushstring(L, "callError");
		lua_gettable(L, -2);
		lua_remove(L, -2);
		lua_settable(L, LUA_REGISTRYINDEX);
	}

	{
		/* asserts version match between runner and core */
		const char *lversion;
		lua_getglobal(L, "lsyncd_version");
		lversion = luaL_checkstring(L, -1);
		if (strcmp(lversion, PACKAGE_VERSION)) {
			printlogf(L, "Error",
				"Version mismatch '%s' is '%s', but core is '%s'",
 				lsyncd_runner_file ? lsyncd_runner_file : "internal runner",
				lversion, PACKAGE_VERSION);
			exit(-1); // ERRNO
		}
		lua_pop(L, 1);
	}

	{
		/* checks if there is a "-help" or "--help" */
		int i;
		for(i = argp; i < argc; i++) {
			if (!strcmp(argv[i],"-help") || !strcmp(argv[i],"--help")) {
				load_runner_func(L, "help");
				if (lua_pcall(L, 0, 0, -2)) {
					exit(-1); // ERRNO
				}
				lua_pop(L, 1);
				exit(-1); // ERRNO
			}
		}
	}

	{
		/* starts the option parser in lua script */
		int idx = 1;
		const char *s;
		/* creates a table with all remaining argv option arguments */
		load_runner_func(L, "configure");
		lua_newtable(L);
		while(argp < argc) {
			lua_pushnumber(L, idx++);
			lua_pushstring(L, argv[argp++]);
			lua_settable(L, -3);
		}
		if (lua_pcall(L, 1, 1, -3)) {
			exit(-1); // ERRNO
		}
		s = lua_tostring(L, -1);
		if (s) {
			lsyncd_config_file = s_strdup(s);
		}
		lua_pop(L, 2); 
	}

	if (lsyncd_config_file) {
		/* checks for the configuration and existence of the config file */
		struct stat st;
		if (stat(lsyncd_config_file, &st)) {
			printlogf(L, "Error",
				"Cannot find config file at '%s'.",
				lsyncd_config_file);
			exit(-1); // ERRNO
		}

		/* loads and executes the config file */
		if (luaL_loadfile(L, lsyncd_config_file)) {
			printlogf(L, "Error",
				"error loading %s: %s",
				lsyncd_config_file, lua_tostring(L, -1));
			exit(-1); // ERRNO
		}
		if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
			printlogf(L, "Error",
				"error preparing %s: %s",
				lsyncd_config_file, lua_tostring(L, -1));
			exit(-1); // ERRNO
		}
	}

	/* opens inotify */
	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		printlogf(L, "Error", 
			"Cannot create inotify instance! (%d:%s)", 
			errno, strerror(errno));
		exit(-1); // ERRNO
	}
	close_exec_fd(inotify_fd);
	non_block_fd(inotify_fd);

	{
		/* adds signal handlers *
		 * listens to SIGCHLD, but blocks it until pselect() 
		 * opens up*/
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGCHLD);
		signal(SIGCHLD, sig_child);
		sigprocmask(SIG_BLOCK, &set, NULL);
		
		signal(SIGHUP,  sig_handler);
		signal(SIGTERM, sig_handler);
	}

	{
		/* runs initialitions from runner 
		 * lua code will set configuration and add watches */
		load_runner_func(L, "initialize");
		if (lua_pcall(L, 0, 0, -2)) {
			exit(-1); // ERRNO
		}
		lua_pop(L, 1);
	}

	masterloop(L);

	/* cleanup */
	{
		/* frees logging categories */
		int ci;
		struct logcat *lc;
		for(ci = 'A'; ci <= 'Z'; ci++) {
			for(lc = logcats[ci]; lc && lc->name; lc++) {
				free(lc->name);
				lc->name = NULL;
			}
			if (logcats[ci]) {
				free(logcats[ci]);
			}
		}
	}
	if (lsyncd_config_file) {
		free(lsyncd_config_file);
		lsyncd_config_file = NULL;
	}

	/* resets settings to default. */
	if (settings.log_file) {
		free(settings.log_file);
		settings.log_file = NULL;
	}
	settings.log_syslog = false,
	settings.log_level = 0,
	settings.nodaemon = false,

	/* closes inotify */
	close(inotify_fd);
	lua_close(L);
	return 0;
}


/**
 * Main
 */
int
main(int argc, char *argv[])
{
	/* kernel parameters */
	clocks_per_sec = sysconf(_SC_CLK_TCK);

	while(!term) {
		main1(argc, argv);
	}
	return 0;
}

