/*
| time.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Keeps time for Lsyncd,
|
| Provides a "jiffies" userdata for Lua which can be used
| to track time, based on kernel jiffies.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "lsyncd.h"

#include <sys/times.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "time.h"
#include "log.h"

/*
| The kernel's clock ticks per second.
*/
// FIXME make static again
long clocks_per_sec;

/*
| Non glibc builds need a real tms structure for the times( ) call
*/
#ifdef __GLIBC__
	static struct tms * dummy_tms = NULL;
#else
	static struct tms  _dummy_tms;
	static struct tms * dummy_tms = &_dummy_tms;
#endif


/*
| Returns the current time.
*/
clock_t now( )
{
	return times( dummy_tms );
}


/*
| Returns (on Lua stack) the current kernels clock state (jiffies).
*/
int
l_now(lua_State *L)
{
	clock_t * j = lua_newuserdata( L, sizeof( clock_t ) );
	luaL_getmetatable( L, "Lsyncd.jiffies" );
	lua_setmetatable( L, -2 );
	*j = times( dummy_tms );

	return 1;
}


/*
| Adds a number in seconds to a jiffy timestamp.
*/
static int
l_jiffies_add( lua_State *L )
{
	clock_t *p1 = ( clock_t * ) lua_touserdata( L, 1 );
	clock_t *p2 = ( clock_t * ) lua_touserdata( L, 2 );

	if( p1 && p2 )
	{
		logstring( "Error", "Cannot add two timestamps!" );
		exit( -1 );
	}

	{
		clock_t a1  = p1 ? *p1 :  luaL_checknumber( L, 1 ) * clocks_per_sec;
		clock_t a2  = p2 ? *p2 :  luaL_checknumber( L, 2 ) * clocks_per_sec;
		clock_t *r  = ( clock_t * ) lua_newuserdata( L, sizeof( clock_t ) );

		luaL_getmetatable( L, "Lsyncd.jiffies" );
		lua_setmetatable( L, -2 );
		*r = a1 + a2;
		return 1;
	}
}


/*
| Subracts two jiffy timestamps resulting in a number in seconds
| or substracts a jiffy by a number in seconds resulting a jiffy timestamp.
*/
static int
l_jiffies_sub( lua_State *L )
{
	clock_t *p1 = ( clock_t * ) lua_touserdata( L, 1 );
	clock_t *p2 = ( clock_t * ) lua_touserdata( L, 2 );

	if( p1 && p2 )
	{
		// substracting two timestamps result in a timespan in seconds
		clock_t a1  = *p1;
		clock_t a2  = *p2;
		lua_pushnumber(L, ((double) (a1 -a2)) / clocks_per_sec);
		return 1;
	}

	// makes a timestamp earlier by NUMBER seconds
	clock_t a1  = p1 ? *p1 :  luaL_checknumber( L, 1 ) * clocks_per_sec;
	clock_t a2  = p2 ? *p2 :  luaL_checknumber( L, 2 ) * clocks_per_sec;

	clock_t *r  = (clock_t *) lua_newuserdata( L, sizeof( clock_t ) );
	luaL_getmetatable( L, "Lsyncd.jiffies" );
	lua_setmetatable( L, -2 );

	*r = a1 - a2;

	return 1;
}


/*
| Compares two jiffy timestamps for equalness.
*/
static int
l_jiffies_eq( lua_State *L )
{
	clock_t a1 = ( *( clock_t * ) luaL_checkudata( L, 1, "Lsyncd.jiffies" ) );
	clock_t a2 = ( *( clock_t * ) luaL_checkudata( L, 2, "Lsyncd.jiffies" ) );

	lua_pushboolean( L, a1 == a2 );

	return 1;
}


/*
* True if jiffy1 timestamp is eariler than jiffy2 timestamp.
*/
static int
l_jiffies_lt( lua_State *L )
{
	clock_t a1 = ( *( clock_t * ) luaL_checkudata( L, 1, "Lsyncd.jiffies" ) );
	clock_t a2 = ( *( clock_t * ) luaL_checkudata( L, 2, "Lsyncd.jiffies" ) );

	lua_pushboolean( L, time_before( a1, a2 ) );

	return 1;
}


/*
| True if jiffy1 is before or equals jiffy2.
*/
static int
l_jiffies_le(lua_State *L)
{
	clock_t a1 = ( *( clock_t * ) luaL_checkudata( L, 1, "Lsyncd.jiffies" ) );
	clock_t a2 = ( *( clock_t * ) luaL_checkudata( L, 2, "Lsyncd.jiffies" ) );

	lua_pushboolean( L, ( a1 == a2 ) || time_before( a1, a2 ) );
	return 1;
}


/*
| Registers the jiffies meta table in a Lua state.
*/
void
register_jiffies( lua_State *L )
{
	// creates the metatable for the jiffies ( timestamps ) userdata
	luaL_newmetatable( L, "Lsyncd.jiffies" );
	int mt = lua_gettop( L );

	lua_pushcfunction( L, l_jiffies_add );
	lua_setfield( L, mt, "__add" );

	lua_pushcfunction( L, l_jiffies_sub );
	lua_setfield( L, mt, "__sub" );

	lua_pushcfunction( L, l_jiffies_lt );
	lua_setfield( L, mt, "__lt" );

	lua_pushcfunction( L, l_jiffies_le );
	lua_setfield( L, mt, "__le" );

	lua_pushcfunction( L, l_jiffies_eq );
	lua_setfield( L, mt, "__eq" );

	lua_pop( L, 1 ); // pop( mt )
}

