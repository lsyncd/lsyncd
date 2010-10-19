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
#  include "inotify-nosys.h"
#endif

#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

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
 * Number of inotifies to read max. at once from the kernel.
 */
#define INOTIFY_BUF_LEN     (64 * (sizeof(struct inotify_event) + 16))

/**
 * The Lua part of lsyncd.
 */
#define LSYNCD_RUNNER_FILE "lsyncd.lua"

/**
 * The inotify file descriptor.
 */
static int inotify_fd;

/**
 * TODO allow configure.
 */
const uint32_t standard_event_mask =
		IN_ATTRIB   | IN_CLOSE_WRITE | IN_CREATE     |
		IN_DELETE   | IN_DELETE_SELF | IN_MOVED_FROM |
		IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR;

/**
 * Configuration settings relevant for core.
 */
struct settings {
	char * logfile;
};
struct settings settings = {0,};

/**
 * Set to TERM or HUP in signal handler, when lsyncd should end or reset ASAP.
 */
volatile sig_atomic_t reset = 0;

/**
 * The kernels clock ticks per second.
 */
long clocks_per_sec; 

/**
 * "secured" calloc.
 */
void *
s_calloc(size_t nmemb, size_t size)
{
	void *r = calloc(nmemb, size);
	if (r == NULL) {
		printf("Out of memory!\n");
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
		printf("Out of memory!\n");
		exit(-1);  // ERRNO
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
		printf("Out of memory!\n");
		exit(-1); // ERRNO
	}
	return s;
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
 * Returns (on Lua stack) the current kernels clock state (jiffies, times() call)
 */
static int
l_now(lua_State *L) 
{
	clock_t c = times(NULL);
	lua_pushinteger(L, c);
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
		//if (!log->flag_nodaemon && log->logfile) {
		//	if (!freopen(log->logfile, "a", stdout)) {
		//		printlogf(log, ERROR, "cannot redirect stdout to [%s].", log->logfile);
		//	}
		//	if (!freopen(log->logfile, "a", stderr)) {
		//		printlogf(log, ERROR, "cannot redirect stderr to [%s].", log->logfile);
		//	}
		//}
		execv(binary, (char **)argv);
		// in a sane world execv does not return!
		printf("Failed executing [%s]!\n", binary);
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
		printf("failure getting absolute path of \"%s\"\n", rdir);
		return 0;
	}
	{
		/* makes sure its a directory */
	    struct stat st;
	    stat(cbuf, &st);
	    if (!S_ISDIR(st.st_mode)) {
			printf("failure in real_dir \"%s\" is not a directory\n", rdir);
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
	printf("total in stack %d\n",top);
	for (i = 1; i <= top; i++) { 
		int t = lua_type(L, i);
		switch (t) {
			case LUA_TSTRING:
				printf("%d string: '%s'\n", i, lua_tostring(L, i));
				break;
			case LUA_TBOOLEAN:
				printf("%d boolean %s\n", i, lua_toboolean(L, i) ? "true" : "false");
				break;
			case LUA_TNUMBER: 
				printf("%d number: %g\n", i, lua_tonumber(L, i));
				break;
			default:  /* other values */
				printf("%d %s\n", i, lua_typename(L, t));
				break;
		}
	}
	
	printf("\n");
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
		printf("cannot open dir %s.\n", dirname);
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
		if (!isdir || !strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
			/* ignore non directories and . and .. */
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

static const luaL_reg lsyncdlib[] = {
		{"add_watch", l_add_watch },
		{"now",       l_now       },
		{"exec",      l_exec      },
		{"real_dir",  l_real_dir  },
		{"stackdump", l_stackdump },
		{"sub_dirs",  l_sub_dirs  },
		{"terminate", l_terminate },
		{NULL, NULL}
};


/*****************************************************************************
 * Lsyncd Core 
 ****************************************************************************/

/**
 * Transfers the core relevant settings from lua's global "settings" into core.
 * This saves time in normal operation instead of bothering lua all the time.
 */
void
get_settings(lua_State *L)
{
	/* frees old settings */
	if (settings.logfile) {
		free(settings.logfile);
		settings.logfile = NULL;
	}
	
	/* gets settings table */
	lua_getglobal(L, "settings");
	if (!lua_istable(L, -1)) {
		/* user has not specified any settings */
		return;
	}
	
	/* get logfile */
	lua_pushstring(L, "logfile");
	lua_gettable(L, -2);
	if (settings.logfile) {
		free(settings.logfile);
		settings.logfile = NULL;
	}
	if (lua_isstring(L, -1)) {
		settings.logfile = s_strdup(luaL_checkstring(L, -1));
	}
	lua_pop(L, 1);
	
	/* pop the settings table */
	lua_pop(L, 1);
}

/**
 * Waits after startup for all children.
 * 
 * @param (Lua stack) a table of the children pids.
 */
void
wait_startup(lua_State *L) 
{
	/* the number of pids in table */
	int pidn; 
	/* the pid table */
	int *pids; 
	/* the number of children to be waited for */
	int remaining = 0;
	int i;
	/* checks if Lua script returned a table */
	if (lua_type(L, 1) == LUA_TNIL) {
		printf("Lua function startup did not return a pidtable!\n");
		exit(-1); // ERRNO
	}
	/* determines size of the pid-table */
	pidn = lua_objlen (L, -1);
	if (pidn == 0) {
		/* nothing to do on zero pids */
		return;
	}
	/* reads the pid table from Lua stack */
	pids = s_calloc(pidn, sizeof(int));
	for(i = 0; i < pidn; i++) {
		lua_rawgeti(L, -1, i + 1);
		pids[i] = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		/* ignores zero pids */
		if (pids[i]) {
			remaining++;
		}
	}
	/* since contents are copied into pids[] pop the lua table */
	lua_pop(L, 1);
	
	/* starts waiting for the children */
	while(remaining) {
		/* argument for waitpid, and exitcode of child */
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
		/* calls the lua script to determine what to do on child failure */
		lua_getglobal(L, "startup_returned");
		lua_pushinteger(L, wp);
		lua_pushinteger(L, exitcode);
		lua_call(L, 2, 1);
		newp = luaL_checkinteger(L, -1);
		lua_pop(L, 1);

		/* replace the new pid in the pidtable,
		   or zero it on no new pid */
		for(i = 0; i < pidn; i++) {
			if (pids[i] == wp) {
				pids[i] = newp;
				if (newp == 0) {
					remaining--;
				}
				/* does not break, in case there are duplicate pids (whyever) */
			}
		}
	}
	free(pids);
}


/**
 * Handles an inotify event.
 */
void handle_event(lua_State *L, struct inotify_event *event) {
	printf("handle_event\n");
	if (IN_Q_OVERFLOW & event->mask) {
		/* and overflow happened, lets runner/user decide what to do. */
		lua_getglobal(L, "overflow");
		lua_call(L, 0, 0);
		return;
	}
	if (IN_IGNORED & event->mask || reset) {
		return;
	}
	{
		if (IN_ACCESS & event->mask) {
			printf("ACCESS id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_MODIFY & event->mask) {
			printf("MODIFY id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_ATTRIB & event->mask) {
			printf("ATTRIB id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_CLOSE_WRITE & event->mask) {
			printf("CLOSE_WRITE id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_CLOSE_NOWRITE & event->mask) {
			printf("CLOSE_WRITE id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_OPEN & event->mask) {
			printf("OPEN id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_MOVED_FROM & event->mask) {
			printf("MOVED_FROM id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_MOVED_TO & event->mask) {
			printf("MOVED_TO id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_CREATE & event->mask) {
			printf("CREATE id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_DELETE & event->mask) {
			printf("DELETE id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_DELETE_SELF & event->mask) {
			printf("DELETE_SELF id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
		if (IN_MOVE_SELF & event->mask) {
			printf("MOVE_SELF id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);
		}
	}

	printf("id=%d mask=%d cookie=%d name=%s\n", event->wd, event->mask, event->cookie, event->name);


	// TODO
}

/**
 * Normal operation happens in here.
 */
void
masterloop(lua_State *L)
{
	while(!reset) {
		char readbuf[INOTIFY_BUF_LEN];
		int alarm_state;
		clock_t now = times(NULL);
		clock_t alarm_time;
		bool do_read = false;
		ssize_t len; 

		/* query runner about soonest alarm  */
		lua_getglobal(L, "lsyncd_get_alarm");
		lua_pushnumber(L, now);
		lua_call(L, 1, 2);
		alarm_state = luaL_checkinteger(L, -2);
		alarm_time = (clock_t) luaL_checknumber(L, -1);
		lua_pop(L, 2);

	
		if (alarm_state < 0) {
			/* there is a delay that wants to be handled already                  */
			/* thus do not read from inotify_fd and jump directly to its handling */
			printf("core: immediately handling delayed entries\n");
			do_read = 0;
		} else if (alarm_state > 0) {
			/* use select() to determine what happens next      */
			/* + a new event on inotify                         */
			/* + an alarm on timeout                            */
			/* + the return of a child process                  */
			fd_set readfds;
			struct timeval tv;

			if (time_after(now, alarm_time)) {
				/* should never happen */
				printf("Internal failure, alarm_time is in past!\n");
				exit(-1); //ERRNO
			}

			tv.tv_sec  = (alarm_time - now) / clocks_per_sec;
			tv.tv_usec = (alarm_time - now) * 1000000 / clocks_per_sec % 1000000;
			/* if select returns a positive number there is data on inotify */
			/* on zero the timemout occured.                                */
			FD_ZERO(&readfds);
			FD_SET(inotify_fd, &readfds);
			do_read = select(inotify_fd + 1, &readfds, NULL, NULL, &tv);

			if (do_read) {
				printf("core: theres data on inotify.\n");
			} else {
				printf("core: select() timeout or signal, doing delays.\n");
			}
		} else {
			// if nothing to wait for, enter a blocking read
			printf("core: gone blocking\n");
			do_read = 1;
		}
		
		/* reads possible events from inotify stream */
		do {
			int i = 0;
			if (do_read) {
				len = read (inotify_fd, readbuf, INOTIFY_BUF_LEN);
			} else {
				len = 0;
			}
			while (i < len && !reset) {
				struct inotify_event *event = (struct inotify_event *) &readbuf[i];
				handle_event(L, event);
				i += sizeof(struct inotify_event) + event->len;
			}
			/* check if there is more data */
			if (do_read) {
				struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
				fd_set readfds;

				FD_ZERO(&readfds);
				FD_SET(inotify_fd, &readfds);
				do_read = select(inotify_fd + 1, &readfds, NULL, NULL, &tv);
				if (do_read) {
					printf("core: there is more data on inotify\n");
				}
			}
		} while (do_read);
	}
}


/**
 * Main
 */
int
main(int argc, char *argv[])
{
	/* kernel parameters */
	clocks_per_sec = sysconf(_SC_CLK_TCK);

	/* the Lua interpreter */
	lua_State* L;

	/* TODO check lua version */

	/* load Lua */
	L = lua_open();
	luaL_openlibs(L);
	luaL_register(L, "lsyncd", lsyncdlib);
	lua_setglobal(L, "lysncd");

	/* register inotify identifiers */
	lua_pushinteger(L, IN_ACCESS);        lua_setglobal(L, "IN_ACCESS");
	lua_pushinteger(L, IN_ATTRIB);        lua_setglobal(L, "IN_ATTRIB");
	lua_pushinteger(L, IN_CLOSE_WRITE);   lua_setglobal(L, "IN_CLOSE_WRITE");
	lua_pushinteger(L, IN_CLOSE_NOWRITE); lua_setglobal(L, "IN_CLOSE_NOWRITE");
	lua_pushinteger(L, IN_CREATE);        lua_setglobal(L, "IN_CREATE");
	lua_pushinteger(L, IN_DELETE);        lua_setglobal(L, "IN_DELETE");
	lua_pushinteger(L, IN_DELETE_SELF);   lua_setglobal(L, "IN_DELETE_SELF");
	lua_pushinteger(L, IN_MODIFY);        lua_setglobal(L, "IN_MODIFY");
	lua_pushinteger(L, IN_MOVED_FROM);    lua_setglobal(L, "IN_MOVED_FROM");
	lua_pushinteger(L, IN_MOVED_TO);      lua_setglobal(L, "IN_MOVED_TO");
	lua_pushinteger(L, IN_OPEN);          lua_setglobal(L, "IN_OPEN");

	if (luaL_loadfile(L, "lsyncd.lua")) {
		printf("error loading '%s': %s\n", 
		       LSYNCD_RUNNER_FILE, lua_tostring(L, -1));
		return -1; // ERRNO
	}
	if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
		printf("error preparing '%s': %s\n", 
		       LSYNCD_RUNNER_FILE, lua_tostring(L, -1));
		return -1; // ERRNO
	}

	{
		/* checks version match between runner/core */
		const char *lversion;
		lua_getglobal(L, "lsyncd_version");
		lversion = luaL_checkstring(L, -1);
		lua_pop(L, 1);
		if (strcmp(lversion, PACKAGE_VERSION)) {
			printf("Version mismatch '%s' is '%s', but core is '%s'\n",
			       LSYNCD_RUNNER_FILE,
			       lversion,
			       PACKAGE_VERSION);
			return -1; // ERRNO
		}
	}

	if (luaL_loadfile(L, "lsyncd-conf.lua")) {
		printf("error load lsyncd-conf.lua: %s\n", lua_tostring(L, -1));
		return -1; // ERRNO
	}
	if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
		printf("error prep lsyncd-conf.lua: %s\n", lua_tostring(L, -1));
		return -1; // ERRNO
	}

	/* open inotify */
	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		printf("Cannot create inotify instance! (%d:%s)\n", errno, strerror(errno));
		return -1; // ERRNO
	}

	/* initialize */
	/* lua code will set configuration and add watches */
	lua_getglobal(L, "lsyncd_initialize");
	lua_call(L, 0, 0);

	/* load core settings into core */
	get_settings(L);
	
	/* startup */
	/* lua code will perform startup calls like recursive rsync */
	lua_getglobal(L, "startup");
	lua_call(L, 0, 1);
	/* wait for children spawned at startup */
	wait_startup(L);

	/* enter normal operation */
	lua_getglobal(L, "normalop");
	lua_call(L, 0, 0);

	masterloop(L);

	/* cleanup */
	close(inotify_fd);
	lua_close(L);
	return 0;
}
