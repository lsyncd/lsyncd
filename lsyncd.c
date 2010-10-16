#include "config.h"
#define LUA_USE_APICHECK 1

#ifdef HAVE_SYS_INOTIFY_H
#  include <sys/inotify.h>
#else
#  include "inotify-nosys.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>

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
 * The inotify file descriptor.
 */
static int inotify_fd;

/**
 * Set to TERM or HUP in signal handler, when lsyncd should end or reset ASAP.
 */
volatile sig_atomic_t reset = 0;

/* the Lua interpreter */
lua_State* L;

/**
 * Dumps the LUA stack. For debugging purposes.
 */
int stackdump(lua_State* l)
{
	int i;
	int top = lua_gettop(l);
	printf("total in stack %d\n",top);
	for (i = 1; i <= top; i++) { 
		int t = lua_type(l, i);
		switch (t) {
			case LUA_TSTRING:
				printf("%d string: '%s'\n", i, lua_tostring(l, i));
				break;
			case LUA_TBOOLEAN:
				printf("%d boolean %s\n", i, lua_toboolean(l, i) ? "true" : "false");
				break;
            case LUA_TNUMBER: 
				printf("%d number: %g\n", i, lua_tonumber(l, i));
				break;
			default:  /* other values */
				printf("%d %s\n", i, lua_typename(l, t));
				break;
		}
    }
    printf("\n");
	return 0;
}



/**
 * Converts a relative directory path to an absolute.
 * 
 * @param dir a relative path to directory
 * @return    absolute path of directory
 */
static int real_dir(lua_State *L) {
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
 * Reads the directories sub directories.
 * 
 * @param  absolute path to directory.
 * @return a table of directory names.
 */
static int sub_dirs (lua_State *L) {
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
			char *subdir = malloc(strlen(dirname) + strlen(de->d_name) + 2);
			if (subdir == NULL) {
				printf("Out of memory. Cannot determine type of %s/%s\n", dirname, de->d_name);
				/* otherwise not so dramatic */
				continue;
			}
			struct stat st;
			strcpy(subdir, dirname);
			strcat(subdir, "/");
			strcat(subdir, de->d_name);
			stat(subdir, &st);
			isdir = S_ISDIR(st.st_mode);
			free(subdir);
		} else {
			/* we can trust readdir */
			isdir = de->d_type == DT_DIR;
		}
		if (!isdir || !strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
			/* ignore non directories and . and .. */
			continue;
		}

		/* add this to the LUA table */
		lua_pushnumber(L, idx++);
		lua_pushstring(L, de->d_name);
		lua_settable(L, -3);
	}

	return 1;
}


int main (int argc, char *argv[])
{
	/* load Lua */
	L = lua_open();
	luaL_openlibs(L);
	lua_register(L, "stackdump", stackdump);
	lua_register(L, "real_dir", real_dir);
	lua_register(L, "sub_dirs", sub_dirs);

	luaL_loadfile(L, "lsyncd.lua");
	if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
		printf("error loading lsyncd.lua: %s\n", lua_tostring(L, -1));
		return -1; // ERRNO
	}
	
	luaL_loadfile(L, "lsyncd-conf.lua");
	if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
		printf("error loading lsyncd-conf.lua: %s\n", lua_tostring(L, -1));
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
