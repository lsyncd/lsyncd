/** 
 * fsevents.c from Lsyncd - Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * -----------------------------------------------------------------------
 *
 * Interface to Linux' new filesystem monitor - fanotify.
 */
#include "lsyncd.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

static const luaL_reg lfanotifylib[] = {
		{NULL, NULL}
};

/** 
 * registers fanoitfy functions.
 */
extern void
register_fanotify(lua_State *L) {
	lua_pushstring(L, "fanotify");
	luaL_register(L, "fanotify", lfanotifylib);
}

/** 
 * opens and initalizes fsevents.
 */
extern void
open_fanotify(lua_State *L) {
	// TODO
}

/** 
 * closes fsevents
 */
extern void
close_fanotify() {
	// TODO
}

