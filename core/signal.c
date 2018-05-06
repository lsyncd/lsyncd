/*
| singal.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Albeit this signal handling system at first seems to violate
| rentry rules things are evened out by sigmasks taking care
| only one signal at a time can enter the core.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "log.h"
#include "mem.h"


static volatile sig_atomic_t * queue;
static int queue_len;
static int queue_pos;


/*
| Set by TERM or HUP signal handler
| telling Lsyncd should end or reset ASAP.
*/
volatile sig_atomic_t hup  = 0;
volatile sig_atomic_t term = 0;
volatile sig_atomic_t sigcode = 0;





/*
| signal handler
*/
static void
signal_child( int sig )
{
	// Nothing!
	//
	// This signal handler is just installed so the kernel
	// keeps finished child processes as zombies waiting to be reaped.
}


/*
| signal handler
*/
static void
signal_handler( int sig )
{
	// looks if this signal is already queued
	for( int i = 0; i < queue_pos; i++ )
	{
		// if so it is dropped
		if( queue[ i ] == sig ) return;
	}

	if( queue_pos + 1 >= queue_len )
	{
		// this should never ever happen
		logstring( "Error", "Signal queue overflow!" );
		exit( -1 );
	}

	queue[ queue_pos++ ] = sig;
}


/*
| Initializes signal handling.
|
| Listens to SIGCHLD, but blocks it until pselect( )
| opens the signal handler up.
*/
void
signal_init( )
{
	queue_len = 5;
	queue = s_malloc( queue_len * sizeof( sig_atomic_t ) );
	queue_pos = 0;
}


/*
| Registers (or deregisters) a signal handlers.
|
| Params on Lua stack:
|     1: table of all signal handlers
|
| Returns on Lua stack:
|
|     true if the signal could be registered with the kernel/libc
|     false if they denied it
*/
int
l_onsignal( lua_State *L )
{
	int sigc = 0;
	int ok;

	// the block mask includes all signals that have registered handlers.
	// it is used to block all signals outside the select() call
	// and also during the core signal handler runs.
	sigset_t blockmask;
	sigemptyset( &blockmask );
	sigaddset( &blockmask, SIGCHLD );

	// first time iterates the signal handler table to build
	// the blockmask

	lua_pushnil( L );
	while( lua_next( L, -2 ) )
	{
		int htype = lua_type( L, -1 ); // the handle

		// pops the value, leaves the key on stack
		lua_pop( L, 1 );

		// not a handler function (probably false)
		if( htype != LUA_TFUNCTION ) continue;

		int signum = lua_tointegerx( L, -1 , &ok );
		if( !ok ) continue;

		sigc++;
	}

	// and block those signals
	sigprocmask( SIG_BLOCK, &blockmask, NULL );

	// if there are more signal handlers than
	// the signal queue allows, it is enlarged.
	if( sigc >= queue_len )
	{
		while( sigc >= queue_len ) queue_len *= 2;
		queue = s_realloc( (sig_atomic_t *)( queue ), queue_len * sizeof( sig_atomic_t ) );
	}

	// now iterates the signal handler table
	// once again to register the signal handlers.

	struct sigaction act;
	memset (&act, '\0', sizeof(act));
	act.sa_mask = blockmask;

	lua_pushnil( L );
	while( lua_next( L, -2 ) )
	{
		int htype = lua_type( L, -1 ); // the handle
		act.sa_handler = &signal_handler;


		// pops the value, leaves the key on stack
		lua_pop( L, 1 );

		// not a handler function (probably false)
		if( htype != LUA_TFUNCTION ) continue;

		int signum = lua_tointegerx( L, -1 , &ok );
		if( !ok ) continue;

		sigaction( signum, &act, 0 );
	}

	return 0;
}

