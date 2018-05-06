/*
| observe.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Handles observing file descriptors and the big select.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "log.h"
#include "signal.h"
#include "mem.h"


/**
* An observance to be called when a file descritor becomes
* read-ready or write-ready.
*/
struct observance {
	// The file descriptor to observe.
	int fd;

	// Function to call when read becomes ready.
	void (*ready)( lua_State *, int fd, void * extra );

	// Function to call when write becomes ready.
	void (*writey)( lua_State *, int fd, void * extra );

	// Function to call to clean up
	void (*tidy)( int fd, void * extra );

	// Extra tokens to pass to the functions.
	void *extra;
};


/*
| List of file descriptor watches.
*/
static struct observance * observances = NULL;
static int observances_len = 0;
static int observances_size = 0;


/*
| List of file descriptors to not observe.
|
| While working for the oberver lists, it may
| not be altered, thus nonobserve stores the
| delayed removals.
*/
static int * nonobservances = NULL;
static int nonobservances_len = 0;
static int nonobservances_size = 0;


/*
| True while the observances list is being handled.
*/
static bool observance_action = false;


/*
| Core watches a filedescriptor to become ready,
| one of read_ready or write_ready may be zero
*/
extern void
observe_fd(
	int fd,
	void ( * ready  ) ( lua_State *, int fd, void * extra ),
	void ( * writey ) ( lua_State *, int fd, void * extra ),
	void ( * tidy   ) ( int fd, void * extra ),
	void * extra
)
{
	int pos;

	// looks if the fd is already there as pos or
	// stores the position to insert the new fd in pos
	for( pos = 0; pos < observances_len; pos++)
	{
		if( fd <= observances[ pos ].fd ) break;
	}

	if( pos < observances_len && observances[ pos ].fd == fd )
	{
		// just updates an existing observance
		logstring( "Masterloop", "updating fd observance" );
		observances[ pos ].ready  = ready;
		observances[ pos ].writey = writey;
		observances[ pos ].tidy   = tidy;
		observances[ pos ].extra  = extra;

		return;
	}

	if( observance_action )
	{
		// FIXME
		logstring( "Error", "New observances in ready/writey handlers not yet supported" );
		exit( -1 );
	}

	if( !tidy )
	{
		logstring( "Error", "internal, tidy( ) in observe_fd() must not be NULL." );
		exit( -1 );
	}

	if( observances_len + 1 > observances_size )
	{
		observances_size = observances_len + 1;
		observances = s_realloc(
			observances,
			observances_size * sizeof( struct observance )
		);
	}

	memmove(
		observances + pos + 1,
		observances + pos,
		( observances_len - pos ) * sizeof( struct observance )
	);

	observances_len++;

	observances[ pos ].fd     = fd;
	observances[ pos ].ready  = ready;
	observances[ pos ].writey = writey;
	observances[ pos ].tidy   = tidy;
	observances[ pos ].extra  = extra;
}


/*
| Makes the core no longer observe a filedescriptor.
*/
extern void
nonobserve_fd( int fd )
{
	int pos;

	if( observance_action )
	{
		// this function is called through a ready/writey handler
		// while the core works through the observance list, thus
		// it does not alter the list, but stores this actions
		// on a stack
		nonobservances_len++;

		if( nonobservances_len > nonobservances_size )
		{
			nonobservances_size = nonobservances_len;
			nonobservances = s_realloc( nonobservances, nonobservances_size * sizeof( int ) );
		}

		nonobservances[ nonobservances_len - 1 ] = fd;

		return;
	}

	// looks for the fd
	for( pos = 0; pos < observances_len; pos++ )
	{
		if( observances[ pos ].fd == fd ) break;
	}

	if( pos >= observances_len )
	{
		logstring( "Error", "internal fail, not observance file descriptor in nonobserve" );

		exit( -1 );
	}

	// tidies up the observance
	observances[ pos ].tidy( observances[ pos ].fd, observances[ pos ].extra );

	// and moves the list down
	memmove(
		observances + pos,
		observances + pos + 1,
		(observances_len - pos) * sizeof( struct observance )
	);

	observances_len--;
}


// time for Lsyncd to try to put itself to rest into the big select( )
// this configures:
//    timeout,
//    filedescriptors and
//    signals
// that will wake Lsyncd
void
observe_select
(
	lua_State * L,
	struct timespec const * timeout
)
{
	fd_set rfds;
	fd_set wfds;
	sigset_t sigset;
	int pi, pr;

	// Opens to all signals, FIXME might be a global.
	sigemptyset( &sigset );

	FD_ZERO( &rfds );
	FD_ZERO( &wfds );

	for( pi = 0; pi < observances_len; pi++ )
	{
		struct observance *obs = observances + pi;

		if ( obs->ready  ) FD_SET( obs->fd, &rfds );
		if ( obs->writey ) FD_SET( obs->fd, &wfds );
	}

	if( !observances_len )
	{
		logstring( "Error", "Internal fail, no observances, no monitor!" );

		exit( -1 );
	}

	// the great select, this is the very heart beat of Lsyncd
	// that puts Lsyncd to sleep until anything worth noticing
	// happens

	pr =
		pselect(
			observances[ observances_len - 1 ].fd + 1,
			&rfds, &wfds, NULL,
			timeout, &sigset
		);

	// FIXME handle signals

	// something happened!
	if( pr >= 0 )
	{
		// walks through the observances calling ready/writey
		observance_action = true;

		for( pi = 0; pi < observances_len; pi++ )
		{
			struct observance *obs = observances + pi;
			int fd = obs->fd;

			// checks for signals
			if( hup || term ) break;

			// a file descriptor became read-ready
			if( obs->ready && FD_ISSET( fd, &rfds ) ) obs->ready( L, fd, obs->extra );

			// Checks for signals, again, better safe than sorry
			if ( hup || term ) break;

			// FIXME breaks on multiple nonobservances in one beat
			if(
				nonobservances_len > 0 &&
				nonobservances[ nonobservances_len - 1 ] == fd
			) continue;

			// a file descriptor became write-ready
			if( obs->writey && FD_ISSET( fd, &wfds ) ) obs->writey( L, fd, obs->extra );
		}

		observance_action = false;

		// works through delayed nonobserve_fd() calls
		for( pi = 0; pi < nonobservances_len; pi++ )
		{
			nonobserve_fd( nonobservances[ pi ] );
		}

		nonobservances_len = 0;
	}
}


/*
| Tidies up all observances.
*/
void observe_tidy_all( )
{
	int i;

	for( i = 0; i < observances_len; i++ )
	{
		struct observance *obs = observances + i;
		obs->tidy( obs->fd, obs->extra );
	}

	observances_len = 0;
	nonobservances_len = 0;
}

