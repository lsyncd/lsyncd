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
#include "mci.h"
#include "mem.h"


/*
| The queue of received signal to be pulled from the mantle.
|
| Every signal num is unique.
*/
static volatile sig_atomic_t * queue;
static int queue_maxlen;
static int queue_pos;


/*
| The core remembers a list of all signal numbers it registered
| handlers for, so it can reset them to OS default, if the mantle
| changes signal handlers (on soft reset after HUP for example)
*/
static volatile sig_atomic_t * handlers;
static int handlers_maxlen;
static int handlers_len;


/*
| Set by TERM or HUP signal handler
| telling Lsyncd should end or reset ASAP.
|
| FIXME remove
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
	// keeps finished sub-processes as zombies waiting to be reaped.
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

	if( queue_pos + 1 >= queue_maxlen )
	{
		// this should never ever happen
		logstring( "Error", "Signal queue overflow!" );
		exit( -1 );
	}

	queue[ queue_pos++ ] = sig;
}


/*
| Initializes signal handling.
*/
void
signal_init( )
{
	queue_maxlen = 4;
	queue = s_malloc( queue_maxlen * sizeof( sig_atomic_t ) );
	queue_pos = 0;

	handlers_maxlen = 4;
	handlers = s_malloc( handlers_len * sizeof( sig_atomic_t ) );
	handlers_len = 0;
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
l_onsignal(
	lua_State *L
)
{
	int sigc = 0;
	int ok;
	int h;
	struct sigaction act;
	bool have_sig_child = false;

	// the block mask includes all signals that have registered handlers.
	// it is used to block all signals outside the select() call
	// and also during the core signal handler runs.
	sigset_t blockmask;
	sigemptyset( &blockmask );
	sigaddset( &blockmask, SIGCHLD );

	// first time iterates the signal handler table to build
	// the blockmask and also to see what previously registered
	// signals are not to be handled anymore

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

		// marks this signal to be used again in the
		// new signal handler table.
		for( h = 0; h < handlers_len; h++ )
		{
			if( handlers[ h ] == signum )
			{
				handlers[ h ] = -1; break;
			}
		}

		sigc++;
	}

	// resets no longer handled signals
	// to their system default action
	for( h = 0; h < handlers_len; h++ )
	{
		int signum = handlers[ h ];

		if( signum == -1 ) continue;

		memset( &act, 0, sizeof( act ) );
		act.sa_handler = SIG_DFL;

		sigaction( signum, &act, NULL );
	}

	handlers_len = 0;

	// and now the signalmask is applied
	sigprocmask( SIG_BLOCK, &blockmask, NULL );

	// if there are more signal handlers than
	// the signal queue allows, it is enlarged.
	if( sigc >= queue_maxlen )
	{
		while( sigc >= queue_maxlen ) queue_maxlen *= 2;
		queue = s_realloc( (sig_atomic_t *)( queue ), queue_maxlen * sizeof( sig_atomic_t ) );
	}

	if( sigc >= handlers_maxlen )
	{
		while( sigc >= handlers_maxlen ) handlers_maxlen *= 2;
		queue = s_realloc( (sig_atomic_t *)( queue ), handlers_maxlen * sizeof( sig_atomic_t ) );
	}

	// now iterates the signal handler table is iterated again
	// to register the signal handlers.

	memset( &act, 0, sizeof( act ) );
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

		if( signum == SIGCHLD ) have_sig_child = true;

		// stores registered signal handlers
		handlers[ handlers_len++ ] = signum;

		sigaction( signum, &act, 0 );
	}

	// If there is no custom SIGCHLD handler add one
	// that will do nothing. This is needed by the OS
	// so the Lsyncd subprocesses are zombified and
	// can be reaped.
	if( !have_sig_child )
	{
		act.sa_handler = &signal_child;
		sigaction( SIGCHLD, &act, 0 );
	}

	return 0;
}


/*
| Notifies the mantle about queued signals.
*/
void
signal_notify(
	lua_State *L
)
{
	if( queue_pos == 0 ) return;

	load_mci( L, "signalEvent" );

	lua_createtable( L, queue_pos, 0 );

	int p = 1;
	while( queue_pos > 0 )
	{
		lua_pushinteger( L, p++ );
		lua_pushinteger( L, queue[ --queue_pos ] );
		lua_settable( L, -3 );
	}

	if( lua_pcall( L, 1, 0, -3 ) ) exit( -1 );
	lua_pop( L, 1 );

	if( lua_gettop( L ) )
	{
		logstring( "Error", "internal, stack is dirty." );
		l_stackdump( L );
		exit( -1 );
	}
}

