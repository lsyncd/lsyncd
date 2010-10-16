#include "config.h"
#define LUA_USE_APICHECK 1

#ifdef HAVE_SYS_INOTIFY_H
#  include <sys/inotify.h>
#else
#  include "inotify-nosys.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/**
 * The inotify file descriptor.
 */
static int inotify_fd;


/* the Lua interpreter */
lua_State* L;

/**
 * Adds a directory to be watched.
 */
static int attend_dir (lua_State *L) {
	//lua_pushnumber(L, sin(luaL_checknumber(L, 1)));
	printf("ATTEND_DIR\n");
	return 0;
}

/**
 * The lsyncd-lua interface
 */
static const luaL_reg lsyncd_lib[] = {
    {"attend_dir",   attend_dir},
    {NULL, NULL},
};


int main (int argc, char *argv[])
{
	/* load Lua */
	L = lua_open();
	luaL_openlibs(L);
	luaL_register(L, "lsyncd", lsyncd_lib);

	luaL_loadfile(L, "lsyncd.lua");
	if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
		printf("error loading lsyncd.lua: %s\n", lua_tostring(L, -1));
		return -1; // ERRNO
	}

	/* open inotify */
    inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        printf("Cannot create inotify instance! (%d:%s)",
               errno, strerror(errno));
        return -1; // ERRNO
    }

	close(inotify_fd);
	lua_close(L);
	return 0;
}
