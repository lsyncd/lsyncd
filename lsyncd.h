/** 
 * lsyncd.h   Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * Interface between the core modules.
 */

#ifndef LSYNCD_H
#define LSYNCD_H

/* includes needed for headerfile */
#include "config.h"

#include <stdlib.h>

#define LUA_USE_APICHECK 1
#include <lua.h>

/* time comparisons - wrap around safe */
#define time_after(a,b)         ((long)(b) - (long)(a) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)

/**
 * Event types.
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
 * wrappers for heap management, they exit if out-of-memory. 
 */
extern void * s_calloc(size_t nmemb, size_t size);
extern void * s_malloc(size_t size);
extern void * s_realloc(void *ptr, size_t size);
extern char * s_strdup(const char *src);

/* logs a string */
#define logstring(cat, message) \
	{int p; if ((p = check_logcat(cat)) >= settings.log_level) \
	{logstring0(p, cat, message);}}
extern void logstring0(int priority, const char *cat, const char *message);

/* logs a formated string */
#define printlogf(L, cat, ...) \
	{int p; if ((p = check_logcat(cat)) >= settings.log_level)  \
	{printlogf0(L, p, cat, __VA_ARGS__);}}
extern void
printlogf0(lua_State *L, 
          int priority, 
		  const char *cat,
		  const char *fmt, 
		  ...)
	__attribute__((format(printf, 4, 5)));

/* Sets the non-blocking flag for a file descriptor. */
extern void non_block_fd(int fd);

/* Sets the close-on-exit flag for a file descriptor. */
extern void close_exec_fd(int fd);

#endif
