/** fsevents.c from Lsyncd - Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *          Damian Steward <damian.stewart@gmail.com>
 *          David Gauchard <gauchard@laas.fr>
 *
 * -----------------------------------------------------------------------
 *
 * Event interface for OSX
 *
 */
#include "lsyncd.h"
#include "fsevents-api.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <CoreServices/CoreServices.h>

#define PATH_MAX_LEN 1024 // (wip, will be improved)

static char path [PATH_MAX_LEN + 1];
static char newpath [PATH_MAX_LEN + 1];
static int fsevents_fd = -1;

/**
 * Called when fsevents has something to read
 */
static void
fsevents_ready(lua_State *L, struct observance *obs)
{
	const char* etype;
	uint32_t flags, len;
	int isdir;
	size_t status;

	if (obs->fd != fsevents_fd) {
		logstring("Error", "Internal, fsevents_fd != ob->fd");
		exit(-1); // ERRNO
	}

// E2BIG is a temporarily hack (wip)

	if (   (status = read(fsevents_fd, &flags, sizeof(uint32_t))) != sizeof(uint32_t)
	    || (status = read(fsevents_fd, &len, sizeof(uint32_t))) != sizeof(uint32_t)
	    || (status = (len >= PATH_MAX_LEN)? E2BIG: 0) != 0
	    || (status = read(fsevents_fd, path, len + 1)) != len + 1
	    || (status = read(fsevents_fd, &len, sizeof(uint32_t))) != sizeof(uint32_t)
	    || (status = (len >= PATH_MAX_LEN)? E2BIG: 0) != 0
	    || (status = read(fsevents_fd, newpath, len + 1)) != len + 1
	   )
	{
		//logstring("Error", "Internal, reading from fsevents pipe: %s", strerror(status));
		printlogf(L, "Error", "Internal, reading from fsevents pipe: %s", strerror(status));
		exit(-1);
	}
	
	/*
		kFSEventStreamEventFlagItemCreated = 0x00000100,
		kFSEventStreamEventFlagItemRemoved = 0x00000200,
		kFSEventStreamEventFlagItemInodeMetaMod = 0x00000400,
		kFSEventStreamEventFlagItemRenamed = 0x00000800,
		kFSEventStreamEventFlagItemModified = 0x00001000,
		kFSEventStreamEventFlagItemFinderInfoMod = 0x00002000,
		kFSEventStreamEventFlagItemChangeOwner = 0x00004000,
		kFSEventStreamEventFlagItemXattrMod = 0x00008000,
		kFSEventStreamEventFlagItemIsFile = 0x00010000,
		kFSEventStreamEventFlagItemIsDir = 0x00020000,
		kFSEventStreamEventFlagItemIsSymlink = 0x00040000
	*/

	if (flags & (kFSEventStreamEventFlagItemInodeMetaMod | kFSEventStreamEventFlagItemXattrMod | kFSEventStreamEventFlagItemChangeOwner | kFSEventStreamEventFlagItemFinderInfoMod))
		etype = "Attrib";
	else if (flags & (kFSEventStreamEventFlagItemCreated))
		etype = "Create";
	else if (flags & (kFSEventStreamEventFlagItemRemoved))
		etype = "Delete";
	else if (flags & (kFSEventStreamEventFlagItemRenamed))
		etype = "Move";
	else if (flags & (kFSEventStreamEventFlagItemModified))
		etype = "Modify";
	else
	{
		printlogf(L, "Error", "Internal, flags not managed (0x%08x, '%s', '%s')", flags, path, newpath);
		return;
	}
		
	isdir = !!(flags & kFSEventStreamEventFlagItemIsDir);

	load_runner_func(L, "fsEventsEvent");
	lua_pushstring(L, etype);
	lua_pushboolean(L, isdir);
	l_now(L);
	lua_pushstring(L, (const char*)path);

	if (newpath[0])
		lua_pushstring(L, (const char*)newpath);
	else
		lua_pushnil(L);

	if (lua_pcall(L, 5, 0, -7)) {
		exit(-1); // ERRNO
	}
	lua_pop(L, 1);
}

/**
 * Called to close/tidy fsevents
 */
static void
fsevents_tidy(struct observance *obs)
{
	if (obs->fd != fsevents_fd) {
		logstring("Error", "Internal, fsevents_fd != ob->fd");
		exit(-1); // ERRNO
	}
	fsevents_api_stop_thread();
}

/**
 * opens and initalizes fsevents.
 */
extern void
open_fsevents_api(lua_State *L)
{
	printlogf(L, "Normal", "WIP: latency set to 1 regardless user's request");

	fsevents_api_start_thread(1);
	fsevents_fd = fsevents_api_getfd();
//	close_exec_fd(fsevents_fd);
//	non_block_fd(fsevents_fd);
	observe_fd(fsevents_fd, fsevents_ready, NULL, fsevents_tidy, NULL);
}



/*
| Adds an fsevents watch
|
| param dir         (Lua stack) path to directory
| param inotifyMode (Lua stack) which inotify event to react upon
|                               "CloseWrite", "CloseWrite or Modify"
|
| returns           (Lua stack) numeric watch descriptor
*/
static int l_addwatch( lua_State *L )
{
	const char *path  = luaL_checkstring( L, 1 );
//	const char *imode = luaL_checkstring( L, 2 );

	// OSX api call to create the fsevents watch
	// (mode is ignored)
fprintf(stderr, "fsevent api addwatch=%s\n", path);
	fsevents_api_add_path(path);

	lua_pushinteger( L, 0 );

	return 1;
}

static const luaL_Reg lfseventslib[] = {
	{ "addwatch",   l_addwatch   },
//	{ "rmwatch",    l_rmwatch    },
	{ NULL, NULL}
};

/*
| Registers the inotify functions.
*/
extern void register_fsevents_api( lua_State *L )
{
fprintf(stderr, "register fsevent-api\n");
	luaL_register( L, LSYNCD_FSEVENTSAPILIBNAME, lfseventslib );
}
