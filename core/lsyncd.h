/**
* lsyncd.h   Live (Mirror) Syncing Demon
*
* Interface between the core modules.
*
* License: GPLv2 (see COPYING) or any later version
* Authors: Axel Kittenberger <axkibe@gmail.com>
*
*/
#ifndef LSYNCD_H
#define LSYNCD_H

// some older machines need this to see pselect
#define _DEFAULT_SOURCE 1
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

#define LSYNCD_CORE_LIBNAME "core"
#define LSYNCD_INOTIFY_LIBNAME "inotify"

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


/*
* Lsyncd runtime configuration
*/
extern struct settings {
	char * log_file;  // If not NULL Lsyncd logs into this file.
	bool log_syslog;  // If true Lsyncd sends log messages to syslog
	char * log_ident; // If not NULL the syslog identity (otherwise "Lsyncd")
	int log_facility; // The syslog facility
	int log_level;    // -1 logs everything, 0 normal mode, LOG_ERROR errors only.
} settings;


/*
* time comparisons - wrap around safe
*/
#define time_after(a,b)         ((long)(b) - (long)(a) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)

// returns (on Lua stack) the current kernels * clock state (jiffies)
extern int l_now(lua_State *L);

// pushes a runner function and the runner error handler onto Lua stack
extern void load_mci(lua_State *L, const char *name);

// set to 1 on hup signal or term signal
extern volatile sig_atomic_t hup;
extern volatile sig_atomic_t term;



#endif

