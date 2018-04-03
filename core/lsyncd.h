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


// Pushes a runner function and the runner error handler onto Lua stack
extern void load_mci(lua_State *L, const char *name);


// Dummy variable which address is used as
// index in the lua registry to store/get the error handler.
extern int callError;

#endif
