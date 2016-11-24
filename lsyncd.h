/**
 * lsyncd.h   Live (Mirror) Syncing Demon
 *
 * Interface between the core modules.
 *
 * License: GPLv2 (see COPYING) or any later version
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 **/

#ifndef LSYNCD_H
#define LSYNCD_H

// some older machines need this to see pselect
#define _BSD_SOURCE 1
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1

#define LUA_COMPAT_ALL
#define LUA_COMPAT_5_1

// includes needed for headerfile
#include "config.h"

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>

#define LUA_USE_APICHECK 1
#include <lua.h>

#define LSYNCD_LIBNAME "lsyncd"
#define LSYNCD_INOTIFYLIBNAME "inotify"

/*
| Workaround to register a library for different lua versions.
*/
#if LUA_VERSION_NUM > 502
	#define lua_compat_register( L, name, lib ) \
		{ \
			lua_newtable((L)); \
			luaL_setfuncs((L), (lib), 0); \
		}
#else
	#define lua_compat_register( L, name, lib ) \
		{luaL_register( (L), (name), (lib) );}
#endif

/**
 * Lsyncd runtime configuration
 */
extern struct settings {
	char * log_file;  // If not NULL Lsyncd logs into this file.
	bool log_syslog;  // If true Lsyncd sends log messages to syslog
	char * log_ident; // If not NULL the syslog identity (otherwise "Lsyncd")
	int log_facility; // The syslog facility
	int log_level;    // -1 logs everything, 0 normal mode, LOG_ERROR errors only.
	bool nodaemon;    // True if Lsyncd shall not daemonize.
	char * pidfile;   // If not NULL Lsyncd writes its pid into this file.

} settings;

/**
 * time comparisons - wrap around safe
 */
#define time_after(a,b)         ((long)(b) - (long)(a) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)

// returns (on Lua stack) the current kernels * clock state (jiffies)
extern int l_now(lua_State *L);

// pushes a runner function and the runner error handler onto Lua stack
extern void load_runner_func(lua_State *L, const char *name);

// set to 1 on hup signal or term signal
extern volatile sig_atomic_t hup;
extern volatile sig_atomic_t term;

/**
 * wrappers for heap management, they exit if out-of-memory.
 */
extern void * s_calloc(size_t nmemb, size_t size);
extern void * s_malloc(size_t size);
extern void * s_realloc(void *ptr, size_t size);
extern char * s_strdup(const char *src);


/**
 * Logging
 */

// Returns the positive priority if name is configured to be logged, or -1
extern int check_logcat(const char *name);

// logs a string
#define logstring(cat, message) \
	{int p; if ((p = check_logcat(cat)) <= settings.log_level) \
	{logstring0(p, cat, message);}}
extern void logstring0(int priority, const char *cat, const char *message);

// logs a formated string 
#define printlogf(L, cat, ...) \
	{int p; if ((p = check_logcat(cat)) <= settings.log_level)  \
	{printlogf0(L, p, cat, __VA_ARGS__);}}
extern void
printlogf0(lua_State *L,
          int priority,
		  const char *cat,
		  const char *fmt,
		  ...)
	__attribute__((format(printf, 4, 5)));

/**
 * File-descriptor helpers
 */

// Sets the non-blocking flag for a file descriptor.
extern void non_block_fd(int fd);

// Sets the close-on-exit flag for a file descriptor.
extern void close_exec_fd(int fd);


/**
 * An observance to be called when a file descritor becomes
 * read-ready or write-ready.
 */
struct observance {
	// The file descriptor to observe.
	int fd;

	// Function to call when read becomes ready.
	void (*ready)(lua_State *, struct observance *);

	// Function to call when write becomes ready.
	void (*writey)(lua_State *, struct observance *);

	// Function to call to clean up
	void (*tidy)(struct observance *);

	// Extra tokens to pass to the functions.
	void *extra;
};

// makes the core observe a file descriptor
extern void observe_fd(
	int fd,
	void (*ready) (lua_State *, struct observance *),
	void (*writey)(lua_State *, struct observance *),
	void (*tidy)  (struct observance *),
	void *extra
);

// stops the core to observe a file descriptor
extern void nonobserve_fd(int fd);

/*
 * inotify
 */
#ifdef WITH_INOTIFY
extern void register_inotify(lua_State *L);
extern void open_inotify(lua_State *L);
#endif

/*
 * /dev/fsevents
 */
#ifdef WITH_FSEVENTS
extern void open_fsevents(lua_State *L);
#endif

#endif
