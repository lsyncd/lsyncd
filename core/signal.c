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

#include <stddef.h>
#include <signal.h>
#include <string.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "mem.h"


static volatile sig_atomic_t * queue;
static int queue_len;
static int queue_pos;

/*
| XXX
*/
static int *handlers;
static int handlers_len;
static int handlers_maxlen;


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
static void signal_child( int sig )
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
	switch( sig )
	{
		case SIGTERM:
		case SIGINT:
			term = 1;
			sigcode = sig;
			return;

		case SIGHUP:
			hup = 1;
			return;
	}
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

	handlers_maxlen = 5;
	handlers_len = 0;
	handlers = s_malloc( handlers_maxlen * sizeof( int * ) );
}

/*
	sigset_t set;
	sigemptyset( &set );
	sigaddset( &set, SIGCHLD );
	signal( SIGCHLD, signal_child );
	sigprocmask( SIG_BLOCK, &set, NULL );

	signal( SIGHUP,  signal_handler );
	signal( SIGTERM, signal_handler );
	signal( SIGINT,  signal_handler );
}
*/


/*
| Forwards the result of psiginfo to mantle.
|
| Params on Lua stack:
|
| Returns on Lua stack:
|     A table of all signalnames as keys and their signal number as value.
*/
int
l_signames( lua_State * L )
{
	int i;

	lua_newtable( L );

	for( i = 0; i < NSIG; i++ )
	{
		lua_pushnumber( L, i );
		lua_pushstring( L, strsignal( i ) );
		lua_settable( L, -3 );
	}

	return 1;
}

