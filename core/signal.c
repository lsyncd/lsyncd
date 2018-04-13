/*
| singal.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Signal handling.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

#include <stddef.h>
#include <signal.h>


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
	sigset_t set;
	sigemptyset( &set );
	sigaddset( &set, SIGCHLD );
	signal( SIGCHLD, signal_child );
	sigprocmask( SIG_BLOCK, &set, NULL );

	signal( SIGHUP,  signal_handler );
	signal( SIGTERM, signal_handler );
	signal( SIGINT,  signal_handler );
}

