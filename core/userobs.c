/*
| userobs.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Allows user Lua scripts to observe file descriptors.
|
| They have to be opened by some other utility tough,
| for example lua-posix.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "observe.h"
#include "userobs.h"

/*
| Used to load error handler
*/
extern int callError;


/*
| A user observance became read-ready.
*/
static void
user_obs_ready(
	lua_State * L,
	int fd,
	void * extra
)
{
	// pushes the ready table on table
	lua_pushlightuserdata( L, ( void * ) user_obs_ready );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the error handler
	lua_pushlightuserdata( L, (void *) &callError );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the user func
	lua_pushnumber( L, fd );
	lua_gettable( L, -3 );

	// gives the ufunc the fd
	lua_pushnumber( L, fd );

	// calls the user function
	if( lua_pcall( L, 1, 0, -3 ) ) exit( -1 );

	lua_pop( L, 2 );
}


/*
| A user observance became write-ready
*/
static void
user_obs_writey(
	lua_State * L,
	int fd,
	void * extra
)
{
	// pushes the writey table on table
	lua_pushlightuserdata( L, (void *) user_obs_writey );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the error handler
	lua_pushlightuserdata( L, (void *) &callError);
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the user func
	lua_pushnumber( L, fd );
	lua_gettable( L, -3 );

	// gives the user func the fd
	lua_pushnumber( L, fd );

	// calls the user function
	if( lua_pcall( L, 1, 0, -3 ) ) exit(-1);

	lua_pop( L, 2 );
}

/*
| Tidies up a user observance
| FIXME - give the user a chance to do something in that case!
*/
static void
user_obs_tidy(
	int fd,
	void * extra
)
{
	close( fd );
}


/*
| Allows user scripts to observe filedescriptors
|
| Params on Lua stack:
|     1: file descriptor
|     2: function to call when read  becomes ready
|     3: function to call when write becomes ready
*/
int
l_observe_fd( lua_State *L )
{
	int fd = luaL_checknumber( L, 1 );
	bool ready  = false;
	bool writey = false;

	// Stores the user function in the lua registry.
	// It uses the address of the cores ready / writey functions
	// for the user as key
	if( !lua_isnoneornil( L, 2 ) )
	{
		lua_pushlightuserdata( L, (void *) user_obs_ready );

		lua_gettable( L, LUA_REGISTRYINDEX );

		if( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1  );
			lua_newtable( L );
			lua_pushlightuserdata( L, (void *) user_obs_ready );
			lua_pushvalue( L, -2 );
			lua_settable( L, LUA_REGISTRYINDEX );
		}

		lua_pushnumber( L, fd );
		lua_pushvalue( L,  2 );
		lua_settable( L, -3 );
		lua_pop( L,  1 );

		ready = true;
	}

	if( !lua_isnoneornil( L, 3 ) )
	{
		lua_pushlightuserdata( L, (void *) user_obs_writey );

		lua_gettable (L, LUA_REGISTRYINDEX );

		if( lua_isnil(L, -1) )
		{
			lua_pop               ( L, 1                        );
			lua_newtable          ( L                           );
			lua_pushlightuserdata ( L, (void *) user_obs_writey );
			lua_pushvalue         ( L, -2                       );
			lua_settable          ( L, LUA_REGISTRYINDEX        );
		}

		lua_pushnumber ( L, fd );
		lua_pushvalue  ( L,  3 );
		lua_settable   ( L, -3 );
		lua_pop        ( L,  1 );

		writey = true;
	}

	// tells the core to watch the fd
	observe_fd(
		fd,
		ready  ? user_obs_ready : NULL,
		writey ? user_obs_writey : NULL,
		user_obs_tidy,
		NULL
	);

	return 0;
}

/*
| Removes a user observance
|
| Params on Lua stack:
|     1:  exitcode of Lsyncd.
*/
int
l_nonobserve_fd( lua_State *L )
{
	int fd = luaL_checknumber( L, 1 );

	// removes the read function
	lua_pushlightuserdata( L, (void *) user_obs_ready );
	lua_gettable( L, LUA_REGISTRYINDEX );

	if( !lua_isnil( L, -1 ) )
	{
		lua_pushnumber ( L, fd );
		lua_pushnil    ( L     );
		lua_settable   ( L, -2 );
	}
	lua_pop( L, 1 );

	lua_pushlightuserdata( L, (void *) user_obs_writey );
	lua_gettable( L, LUA_REGISTRYINDEX );
	if ( !lua_isnil( L, -1 ) )
	{
		lua_pushnumber ( L, fd );
		lua_pushnil    ( L     );
		lua_settable   ( L, -2 );
	}
	lua_pop( L, 1 );

	nonobserve_fd( fd );
	return 0;
}

