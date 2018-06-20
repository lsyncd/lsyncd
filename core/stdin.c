/*
| stdin.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Reads a config file from stdin and buffers it.
|
| On every run of Lsyncd a config file from stdin is
| read only once, in case of a HUP soft reset the buffered
| version is used.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "log.h"
#include "mem.h"


/*
| Stdin read buffer.
*/
static char * buf = NULL;


/*
| Size of stdin buffer.
*/
static size_t bsize = 0;


/*
| Bytes read from stdin
*/
static size_t bread = 0;


/*
| Reads a config file from stdin.
| Or returns an already read file.
*/
char const *
read_stdin(
	lua_State *L
)
{
	if( buf ) return buf;

	bsize = 1024;
	buf = s_malloc( bsize );

	while( true )
	{
		bread += fread( buf + bread, 1, bsize - bread - 1, stdin );

		if( ferror( stdin ) )
		{
			printlogf( L, "Error", "Failure reading stdin" );

			exit( -1 );
		}

		if( feof( stdin ) ) break;

		if( bsize - bread < 1024 ) buf = s_realloc( buf, bsize *= 2 );
	}

	buf[ bread ] = 0;

	return buf;
}


/*
| Lua wrapper to read_stdin( ).
|
| Params on Lua stack:
|     none
|
| Returns on Lua stack:
|     the config file
*/
int
l_stdin(
	lua_State *L
)
{
	char const * b = read_stdin( L );

	lua_pushstring( L, b );

	return 1;
}

