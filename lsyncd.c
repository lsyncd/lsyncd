/** 
 * lsyncd.c   Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * -----------------------------------------------------------------------
 *
 * This is the core. It contains as minimal as possible glues 
 * to the operating system needed for lsyncd operation. All high-level
 * logic is coded (when feasable) into lsyncd.lua
 */

#include "lsyncd.h"

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

/**
 * The Lua part of lsyncd if compiled into the binary.
 */
#ifndef LSYNCD_DEFAULT_RUNNER_FILE
	extern char _binary_luac_out_start;
	extern char _binary_luac_out_end; 
#endif

/**
 * Makes sure there is one monitor.
 */
#ifndef LSYNCD_WITH_INOTIFY
#ifndef LSYNCD_WITH_FANOTIFY
#ifndef LSYNCD_WITH_FSEVENTS
#	error "need at least one notifcation system. please rerun ./configure"
#endif
#endif
#endif

/**
 * All monitors supported by this Lsyncd.
 */
static char *monitors[] = {
#ifdef LSYNCD_WITH_INOTIFY
	"inotify",
#endif
#ifdef LSYNCD_WITH_FANOTIFY
	"fanotify",
#endif
#ifdef LSYNCD_WITH_FSEVENTS
	"fsevents",
#endif
	NULL,
};

/**
 * configuration parameters
 */
struct settings settings = {
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
 * inotify OVERFLOW.
 */
static bool running = false;

/**
 * Set to TERM or HUP in signal handler, when lsyncd should end or reset ASAP.
 */
volatile sig_atomic_t hup  = 0;
volatile sig_atomic_t term = 0;

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
extern int
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
 * Do not call directly, but the macro logstring() in lsyncd.h
 *
 * @param priorty  the priority of the log message
 * @param cat      the category
 * @param message  the log message
 */
extern void 
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
 * Lets the core print logmessages comfortably as formated string.
 * This uses the lua_State for it easy string buffers only.
 */
extern void
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
 * TODO: call the garbace collector in case of out of memory.
 ****************************************************************************/

/**
 * "secured" calloc.
 */
extern void *
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
extern void *
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
extern void *
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
extern char *
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
	/* message to send */
	char *text;

	/* length of text */
	int tlen;

	/* position in message */
	int pos;
};

/**
 * Called by the core whenever a pipe becomes 
 * writeable again 
 */
static void 
pipe_writey(lua_State *L, struct observance *observance) 
{
	int fd = observance->fd;
	struct pipemsg *pm = (struct pipemsg *) observance->extra;
	int len = write(fd, pm->text + pm->pos, pm->tlen - pm->pos);
	pm->pos += len;
	if (len < 0) {
		logstring("Normal", "broken pipe.");
		nonobserve_fd(fd);
	} else if (pm->pos >= pm->tlen) {
		logstring("Debug", "finished pipe.");
		nonobserve_fd(fd);
	}
}

/**
 * Called when cleaning up a pipe 
 */
static void
pipe_tidy(struct observance *observance) 
{
	struct pipemsg *pm = (struct pipemsg *) observance->extra;
	close(observance->fd);
	free(pm->text);
	free(pm);
}

/*****************************************************************************
 * helper routines.
 ****************************************************************************/

/**
 * Sets the close-on-exit flag for an fd
 */
extern void
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
extern void
non_block_fd(int fd)
{
	int flags;
    flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
		logstring("Error", "cannot get status flags!");
		exit(-1); // ERRNO
	}
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1) {
		logstring("Error", "cannot set status flags!");
		exit(-1); // ERRNO
	}
}

/**
 * Writes a pid file.
 */
static void
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
			case LUA_TUSERDATA:
				{
					clock_t *c = (clock_t *)
						luaL_checkudata(L, i, "Lsyncd.jiffies");
					double d = (*c);
					d /= clocks_per_sec;
					lua_pushfstring(L, "(Timestamp: %f)", d);
					lua_replace(L, i);
					break;
				}
			case LUA_TNIL:
				lua_pushstring(L, "(nil)");
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
 * Returns (on Lua stack) the current kernels 
 * clock state (jiffies)
 */
extern int
l_now(lua_State *L) 
{
	clock_t *j = lua_newuserdata(L, sizeof(clock_t));
	luaL_getmetatable(L, "Lsyncd.jiffies");
	lua_setmetatable(L, -2);
	*j = times(NULL);
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
	size_t pipe_len = 0;
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
		if (!lua_isstring(L, 3)) {
			logstring("Error", "in spawn(), expected a string after pipe '<'");
			exit(-1); // ERRNO
		}
		pipe_text = lua_tolstring(L, 3, &pipe_len);
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
		int len;
		/* first closes read-end of pipe, this is for child process only */
		close(pipefd[0]);
		/* start filling the pipe */
		len = write(pipefd[1], pipe_text, pipe_len);
		if (len < 0) {
			logstring("Normal", "immediatly broken pipe.");
			close(pipefd[0]);
		}
		if (len == pipe_len) {
			/* usual and best case, the pipe accepted all input -> close */
			close(pipefd[1]);
			logstring("Exec", "one-sweeped pipe");
		} else {
			struct pipemsg *pm;
			logstring("Exec", "adding pipe observance");
			pm = s_calloc(1, sizeof(struct pipemsg));
			pm->text = s_calloc(pipe_len + 1, sizeof(char*));
			memcpy(pm->text, pipe_text, pipe_len + 1);
			pm->tlen = pipe_len;
			pm->pos  = len;
			observe_fd(pipefd[1], NULL, pipe_writey, pipe_tidy, pm);
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
 *
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
	closedir(d);
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
			logstring("Debug", "daemonizing now.");
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
		{"configure",    l_configure    },
		{"exec",         l_exec         },
		{"log",          l_log          },
		{"now",          l_now          },
		{"readdir",      l_readdir      },
		{"realdir",      l_realdir      },
		{"stackdump",    l_stackdump    },
		{"terminate",    l_terminate    },
		{NULL, NULL}
};

/**
 * Adds two jiffies or a number to a jiffy
 */
static int 
l_jiffies_add(lua_State *L) 
{
	clock_t *p1 = (clock_t *) lua_touserdata(L, 1);
	clock_t *p2 = (clock_t *) lua_touserdata(L, 2);
	if (p1 && p2) {
		logstring("Error", "Cannot add to timestamps!");
		exit(-1); /* ERRNO */
	}
	{
		clock_t a1  = p1 ? *p1 :  luaL_checknumber(L, 1) * clocks_per_sec;
		clock_t a2  = p2 ? *p2 :  luaL_checknumber(L, 2) * clocks_per_sec;
		clock_t *r  = (clock_t *) lua_newuserdata(L, sizeof(clock_t));
		luaL_getmetatable(L, "Lsyncd.jiffies");
		lua_setmetatable(L, -2);
		*r = a1 + a2; 
		return 1;
	}
}

/**
 * Adds two jiffies or a number to a jiffy
 */
static int 
l_jiffies_sub(lua_State *L) 
{
	clock_t *p1 = (clock_t *) lua_touserdata(L, 1);
	clock_t *p2 = (clock_t *) lua_touserdata(L, 2);
	if (p1 && p2) {
		/* substracting two timestamps result in a timespan in seconds */
		clock_t a1  = *p1;
		clock_t a2  = *p2;
		lua_pushnumber(L, ((double) (a1 -a2)) / clocks_per_sec);
		return 1;
	}
	/* makes a timestamp earlier by NUMBER seconds */
	clock_t a1  = p1 ? *p1 :  luaL_checknumber(L, 1) * clocks_per_sec;
	clock_t a2  = p2 ? *p2 :  luaL_checknumber(L, 2) * clocks_per_sec;
	clock_t *r  = (clock_t *) lua_newuserdata(L, sizeof(clock_t));
	luaL_getmetatable(L, "Lsyncd.jiffies");
	lua_setmetatable(L, -2);
	*r = a1 - a2; 
	return 1;
}

/**
 * Substracts two jiffies or a number to a jiffy
 */
static int 
l_jiffies_eq(lua_State *L) 
{
	clock_t a1 = (*(clock_t *) luaL_checkudata(L, 1, "Lsyncd.jiffies"));
	clock_t a2 = (*(clock_t *) luaL_checkudata(L, 2, "Lsyncd.jiffies"));
	lua_pushboolean(L, a1 == a2);
	return 1;
}

/**
 * True if jiffy1 before jiffy2
 */
static int 
l_jiffies_lt(lua_State *L) 
{
	clock_t a1 = (*(clock_t *) luaL_checkudata(L, 1, "Lsyncd.jiffies"));
	clock_t a2 = (*(clock_t *) luaL_checkudata(L, 2, "Lsyncd.jiffies"));
	lua_pushboolean(L, time_before(a1, a2));
	return 1;
}

/**
 * True if jiffy1 before or == jiffy2
 */
static int 
l_jiffies_le(lua_State *L) 
{
	clock_t a1 = (*(clock_t *) luaL_checkudata(L, 1, "Lsyncd.jiffies"));
	clock_t a2 = (*(clock_t *) luaL_checkudata(L, 2, "Lsyncd.jiffies"));
	lua_pushboolean(L, (a1 == a2) || time_before(a1, a2));
	return 1;
}


/**
 * Registers the lsyncd lib
 */
void
register_lsyncd(lua_State *L) 
{
	luaL_register(L, "lsyncd", lsyncdlib);
	lua_setglobal(L, "lysncd");

	/* creates the metatable for jiffies userdata */
	luaL_newmetatable(L, "Lsyncd.jiffies");
	lua_pushstring(L, "__add");
	lua_pushcfunction(L, l_jiffies_add);
	lua_settable(L, -3);

	lua_pushstring(L, "__sub");
	lua_pushcfunction(L, l_jiffies_sub);
	lua_settable(L, -3);
	
	lua_pushstring(L, "__lt");
	lua_pushcfunction(L, l_jiffies_lt);
	lua_settable(L, -3);
	
	lua_pushstring(L, "__le");
	lua_pushcfunction(L, l_jiffies_le);
	lua_settable(L, -3);
	
	lua_pushstring(L, "__eq");
	lua_pushcfunction(L, l_jiffies_eq);
	lua_settable(L, -3);
	lua_pop(L, 1);
	
	lua_getglobal(L, "lysncd");
#ifdef LSYNCD_WITH_INOTIFY
	register_inotify(L);
	lua_settable(L, -3);
#endif
#ifdef LSYNCD_WITH_FSEVENTS
	register_fsevents(L);
	lua_settable(L, -3);
#endif
	lua_pop(L, 1);
	if (lua_gettop(L)) {
		logstring("Error", "internal, stack not empty in lsyncd_register()");
		exit(-1); // ERRNO
	}
}



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
extern void
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
 * List of file descriptor watches.
 */
static struct observance * observances = NULL;
static int observances_len = 0;
static int observances_size = 0;

/**
 * List of file descriptors to nonobserve.
 * While working for the oberver lists, it may
 * not be altered, thus nonobserve stores here the
 * actions that will be delayed.
 */
static int *nonobservances = NULL;
static int nonobservances_len = 0;
static int nonobservances_size = 0;

/**
 * true while the observances list is being handled.
 */
static bool observance_action = false;

/**
 * Core watches a filedescriptor to become ready,
 * one of read_ready or write_ready may be zero
 */
extern void
observe_fd(int fd, 
           void (*ready) (lua_State *, struct observance *),
           void (*writey)(lua_State *, struct observance *),
           void (*tidy)  (struct observance *),
		   void *extra)
{
	int pos;
	if (observance_action) {
		// TODO
		logstring("Error", 
	"internal, New observances in ready/writey handlers not yet supported");
		exit(-1); // ERRNO
	}

	if (!tidy) {
		logstring("Error", 
			"internal, tidy() in observe_fd() must not be NULL.");
		exit(-1); // ERRNO
	}
	if (observances_len + 1 > observances_size) {
		observances_size = observances_len + 1;
		observances = s_realloc(observances, 
			observances_size * sizeof(struct observance));
	}
	for(pos = 0; pos < observances_len; pos++) {
		if (observances[pos].fd <= fd) {
			break;
		}
	}
	if (observances[pos].fd == fd) {
		logstring("Error", 
			"Observing already an observed file descriptor.");
		exit(-1); // ERRNO
	}
	memmove(observances + pos + 1, observances + pos, 
	        (observances_len - pos) * (sizeof(struct observance)));

	observances_len++;
	observances[pos].fd = fd;
	observances[pos].ready  = ready;
	observances[pos].writey = writey;
	observances[pos].tidy   = tidy;
	observances[pos].extra  = extra;
}

/**
 * Makes core no longer watch fd.
 */
extern void
nonobserve_fd(int fd)
{
	int pos;

	if (observance_action) {
		/* this function is called through a ready/writey handler 
		 * while the core works through the observance list, thus
		 * it does not alter the list, but stores this actions
		 * on a stack 
		 */
		nonobservances_len++;
		if (nonobservances_len > nonobservances_size) {
			nonobservances_size = nonobservances_len;
			nonobservances = s_realloc(nonobservances, 
				nonobservances_size * sizeof(int));
		}
		nonobservances[nonobservances_len - 1] = fd;
		return;
	}

	/* looks for the fd */
	for(pos = 0; pos < observances_len; pos++) {
		if (observances[pos].fd == fd) {
			break;
		}
	}
	if (pos >= observances_len) {
		logstring("Error", 
			"internal fail, not observance file descriptor in nonobserve");
		exit(-1); //ERRNO
	}

	/* and moves the list down */
	memmove(observances + pos, observances + pos + 1, 
	        (observances_len - pos) * (sizeof(struct observance)));
	observances_len--;
}

/**
 * Normal operation happens in here.
 */
static void
masterloop(lua_State *L)
{
	while(true) {
		bool have_alarm;
		bool force_alarm;
		clock_t now = times(NULL);
		clock_t alarm_time;

		/* queries runner about soonest alarm  */
		load_runner_func(L, "getAlarm"); 
		if (lua_pcall(L, 0, 1, -2)) {
			exit(-1); // ERRNO
		}
		
		if (lua_type(L, -1) == LUA_TBOOLEAN) {
			have_alarm = false;
			force_alarm = lua_toboolean(L, -1);
		} else {
			have_alarm = true;
			alarm_time = 
				*((clock_t *) luaL_checkudata(L, -1, "Lsyncd.jiffies"));
		}
		lua_pop(L, 2);

		if (force_alarm || 
		    (have_alarm && time_before_eq(alarm_time, now))
		) {
			/* there is a delay that wants to be handled already thus instead 
			 * of reading/writing from observances it jumps directly to 
			 * handling */

			// TODO: Actually it might be smarter to handler observances 
			// eitherway. since event queues might overflow.
			logstring("Masterloop", "immediately handling delays.");
		} else {
			/* use select() to determine what happens next
			 * + a new event on an observance
			 * + an alarm on timeout  
			 * + the return of a child process */
			struct timespec tv;

			if (have_alarm) { 
				// TODO use trunc instead of long converstions
				double d = ((double)(alarm_time - now)) / clocks_per_sec;
				tv.tv_sec  = d;
				tv.tv_nsec = ((d - (long) d)) * 1000000000.0;
				printlogf(L, "Masterloop", 
					"going into select (timeout %f seconds)", d);
			} else {
				logstring("Masterloop", "going into select (no timeout).");
			}
			/* time for Lsyncd to try to put itself to rest into a select(), 
			 * configures timeouts, filedescriptors and signals 
			 * that will wake it */
			{
				fd_set rfds;
				fd_set wfds;
				sigset_t sigset;
				int pi, pr;

				sigemptyset(&sigset);
				FD_ZERO(&rfds);
				FD_ZERO(&wfds);

				for(pi = 0; pi < observances_len; pi++) {
					int fd = observances[pi].fd;
					if (observances[pi].ready) {
						FD_SET(fd, &rfds);
					}
					if (observances[pi].writey) {
						FD_SET(fd, &wfds);
					}
				}

				/* the great select */
				pr = pselect(
					observances[observances_len - 1].fd + 1,
					&rfds, &wfds, NULL, 
					have_alarm ? &tv : NULL, &sigset);

				if (pr >= 0) {
					/* walks through the observances calling ready/writey */
					observance_action = true;
					for(pi = 0; pi < observances_len; pi++) {
						struct observance *obs = observances + pi;
						if (hup || term) {
							break;
						}
						if (obs->ready && FD_ISSET(obs->fd, &rfds)) {
							obs->ready(L, obs);
						}
						if (hup || term) {
							break;
						}
						if (nonobservances_len > 0 && 
							nonobservances[nonobservances_len-1] == obs->fd) {
							/* TODO breaks if more nonobserves */
							/* ready() nonobserved itself */
							continue;
						}
						if (obs->writey && FD_ISSET(obs->fd, &wfds)) {
							obs->writey(L, obs);
						}
					}
					observance_action = false;
					/* work through delayed nonobserve_fd() calls */
					for (pi = 0; pi < nonobservances_len; pi++) {
						nonobserve_fd(nonobservances[pi]);
					}
					nonobservances_len = 0;
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

		/* reacts on signals */
		if (hup) {
			load_runner_func(L, "hup");
			if (lua_pcall(L, 0, 0, -2)) {
				exit(-1); // ERRNO
			}
			lua_pop(L, 1);
			hup = 0;
		}

		/* reacts on signals */
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
		l_now(L);
		if (lua_pcall(L, 1, 1, -3)) {
			exit(-1); // ERRNO
		}
		if (!lua_toboolean(L, -1)) {
			/* cycle told core to break mainloop */
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
	register_lsyncd(L);

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
			exit(-1); //ERRNO
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
		/* creates a table with the cores event monitor interfaces */
		idx = 0;
		lua_newtable(L);
		while (monitors[idx]) {
			lua_pushnumber(L, idx + 1);
			lua_pushstring(L, monitors[idx++]);
			lua_settable(L, -3);
		}
		if (lua_pcall(L, 2, 1, -3)) {
			exit(-1); // ERRNO
		}
		s = lua_tostring(L, -1);
		if (s) {
			lsyncd_config_file = s_strdup(s);
		}
		lua_pop(L, 2); 
	}

	if (lsyncd_config_file) {
		/* checks existence of the config file */
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

#ifdef LSYNCD_WITH_INOTIFY
	open_inotify(L);
#endif
#ifdef LSYNCD_WITH_FSEVENTS
	open_fsevents(L);
#endif

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
		/* tidies all observances */
		int i;
		for(i = 0; i < observances_len; i++) {
			struct observance *obs = observances + i;
			obs->tidy(obs);
		}
		observances_len = 0;
		nonobservances_len = 0;
	}

	{
		/* frees logging categories */
		int ci;
		struct logcat *lc;
		for(ci = 'A'; ci <= 'Z'; ci++) {
			for(lc = logcats[ci - 'A']; lc && lc->name; lc++) {
				free(lc->name);
				lc->name = NULL;
			}
			if (logcats[ci - 'A']) {
				free(logcats[ci - 'A']);
				logcats[ci - 'A'] = NULL;
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

