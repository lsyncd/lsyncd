/*
| inotify.c from Lsyncd - Live (Mirror) Syncing Demon
|
| License: GPLv2 (see COPYING) or any later version
|
| Authors: Axel Kittenberger <axkibe@gmail.com>
|
| -----------------------------------------------------------------------
|
| Event interface for Lsyncd to Linux´ inotify.
*/

#include "lsyncd.h"

#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


/*
| Event types.
*/
static const char * ATTRIB = "Attrib";
static const char * MODIFY = "Modify";
static const char * CREATE = "Create";
static const char * DELETE = "Delete";
static const char * MOVE   = "Move";


/*
 * The inotify file descriptor.
 */
static int inotify_fd = -1;


/*
| Standard inotify events to listen to.
*/
static const uint32_t standard_event_mask =
	IN_ATTRIB      |
	IN_CLOSE_WRITE |
	IN_CREATE      |
	IN_DELETE      |
	IN_DELETE_SELF |
	IN_MOVED_FROM  |
	IN_MOVED_TO    |
	IN_DONT_FOLLOW |
	IN_ONLYDIR;


/*
| Adds an inotify watch
|
| param dir         (Lua stack) path to directory
| param inotifyMode (Lua stack) which inotify event to react upon
|                               "CloseWrite", "CloseWrite or Modify"
|
| returns           (Lua stack) numeric watch descriptor
*/
static int
l_addwatch( lua_State *L )
{
	const char *path  = luaL_checkstring( L, 1 );
	const char *imode = luaL_checkstring( L, 2 );
	uint32_t mask = standard_event_mask;

	// checks the desired inotify reaction mode
	if (*imode)
	{
		if ( !strcmp( imode, "Modify" ) )
		{
			// acts on modify instead of closeWrite
			mask |=  IN_MODIFY;
			mask &= ~IN_CLOSE_WRITE;
		}
		else if ( !strcmp( imode, "CloseWrite" ) )
		{
			// thats default
		}
		else if ( !strcmp( imode, "CloseWrite or Modify" ) )
		{
			// acts on modify and closeWrite
			mask |= IN_MODIFY;
		}
		else if ( ! strcmp( imode, "CloseWrite after Modify") )
		{
			// might be done in future
			printlogf(
				L, "Error",
				"'CloseWrite after Modify' not implemented."
			);
			exit(-1);
		}
		else
		{
			printlogf(
				L, "Error",
				"'%s' not a valid inotfiyMode.",
				imode
			);
			exit(-1);
		}
	}

	// kernel call to create the inotify watch
	int wd = inotify_add_watch( inotify_fd, path, mask );

	if( wd < 0 )
	{
		if( errno == ENOSPC )
		{
			printlogf(
				L, "Error",
				"%s\n%s",
				"Terminating since out of inotify watches.",
				"Consider increasing /proc/sys/fs/inotify/max_user_watches"
			);
			exit(-1); // ERRNO.
		}

		printlogf(
			L, "Inotify",
			"addwatch( %s )-> %d; err= %d : %s",
			path, wd, errno, strerror( errno )
		);
	}
	else
	{
		printlogf(L, "Inotify", "addwatch( %s )-> %d ", path, wd );
	}
	lua_pushinteger( L, wd );

	return 1;
}


/*
* Removes an inotify watch.
*
* param dir (Lua stack) numeric watch descriptor
*
* return    nil
*/
static int
l_rmwatch( lua_State *L )
{
	int wd = luaL_checkinteger( L, 1 );
	inotify_rm_watch( inotify_fd, wd );
	printlogf( L, "Inotify", "rmwatch()<-%d", wd );
	return 0;
}


/*
| Lsyncd's core's inotify functions.
*/
static const luaL_Reg linotfylib[ ] =
{
	{ "addwatch",   l_addwatch   },
	{ "rmwatch",    l_rmwatch    },
	{ NULL, NULL}
};


/*
| Buffer for MOVE_FROM events.
| Lsyncd buffers MOVE_FROM events to check if
| they are followed by MOVE_TO events with identical cookie
| then they are condensed into one move event to be sent to the
| runner
*/
static struct inotify_event * move_event_buf = NULL;


/*
| Memory allocated for move_event_buf
*/
static size_t move_event_buf_size = 0;


/*
| True if the buffer is used.
*/
static bool move_event = false;


/*
| Handles an inotify event.
*/
static void
handle_event(
	lua_State *L,
	struct inotify_event *event
)
{
	const char *event_type = NULL;

	// used to execute two events in case of unmatched MOVE_FROM buffer
	struct inotify_event *after_buf = NULL;

	if( event && ( IN_Q_OVERFLOW & event->mask ) )
	{
		// and overflow happened, tells the runner
		load_runner_func( L, "overflow" );

		if( lua_pcall( L, 0, 0, -2 ) )
		{
			exit( -1 );
		}

		lua_pop( L, 1 );

		hup = 1;

		return;
	}

	// cancel on ignored or resetting
	if( event && ( IN_IGNORED & event->mask ) )
	{
		return;
	}

	if( event && event->len == 0 )
	{
		// sometimes inotify sends such strange events,
		// (e.g. when touching a dir
		return;
	}

	if( event == NULL )
	{
		// a buffered MOVE_FROM is not followed by anything,
		// thus it is unary
		event = move_event_buf;
		event_type = "Delete";
		move_event = false;
	}
	else if(
		move_event
		&& (
			!( IN_MOVED_TO & event->mask )
			|| event->cookie != move_event_buf->cookie
		)
	)
	{
		// there is a MOVE_FROM event in the buffer and this is not the match
		// continue in this function iteration to handle the buffer instead */
		logstring(
			"Inotify",
			"icore, changing unary MOVE_FROM into DELETE"
		);

		after_buf = event;

		event = move_event_buf;

		event_type = "Delete";

		move_event = false;
	}
	else if(
		move_event
		&& ( IN_MOVED_TO & event->mask )
		&& event->cookie == move_event_buf->cookie
	)
	{
		// this is indeed a matched move */
		event_type = "Move";
		move_event = false;
	}
	else if( IN_MOVED_FROM & event->mask )
	{
		// just the MOVE_FROM, buffers this event, and wait if next event is
		// a matching MOVED_TO of this was an unary move out of the watched
		// tree.
		size_t el = sizeof( struct inotify_event ) + event->len;

		if( move_event_buf_size < el )
		{
			move_event_buf_size = el;

			move_event_buf = s_realloc( move_event_buf, el );
		}

		memcpy( move_event_buf, event, el );

		move_event = true;

		return;

	}
	else if( IN_MOVED_TO & event->mask )
	{
		// must be an unary move-to
		event_type = CREATE;
	}
	else if( IN_ATTRIB & event->mask )
	{
		// just attrib change
		event_type = ATTRIB;
	}
	else if( ( IN_CLOSE_WRITE | IN_MODIFY) & event->mask )
	{
		// modify, or closed after written something
		// the event type received depends settings.inotifyMode
		event_type = MODIFY;
	}
	else if( IN_CREATE & event->mask )
	{
		// a new file
		event_type = CREATE;
	}
	else if( IN_DELETE & event->mask )
	{
		// rm'ed
		event_type = DELETE;
	}
	else
	{
		logstring(
			"Inotify",
			"skipped some inotify event."
		);
		return;
	}

	// hands the event over to the runner
	load_runner_func( L, "inotifyEvent" );

	if( !event_type )
	{
		logstring(
			"Error",
			"internal failure: unknown event in handle_event()"
		);

		exit( -1 );
	}

	lua_pushstring( L, event_type );

	if( event_type != MOVE )
	{
		lua_pushnumber( L, event->wd );
	}
	else
	{
		lua_pushnumber( L, move_event_buf->wd );
	}
	lua_pushboolean( L, ( event->mask & IN_ISDIR ) != 0 );

	l_now( L );

	if( event_type == MOVE )
	{
		lua_pushstring( L, move_event_buf->name );
		lua_pushnumber( L, event->wd            );
		lua_pushstring( L, event->name          );
	}
	else
	{
		lua_pushstring( L, event->name );
		lua_pushnil( L );
		lua_pushnil( L );
	}

	if( lua_pcall( L, 7, 0, -9 ) )
	{
		exit( -1 );
	}

	lua_pop( L, 1 );

	// if there is a buffered event, executes it
	if (after_buf) {
		logstring("Inotify", "icore, handling buffered event.");
		handle_event(L, after_buf);
	}
}


/*
| buffer to read inotify events into
*/
static size_t readbuf_size = 2048;

static char * readbuf = NULL;


/*
| Called when the inotify file descriptor became ready.
| Reads it contents and forwards all received events
| to the runner.
*/
static void
inotify_ready(
	lua_State *L,
	struct observance *obs
)
{
	// sanity check
	if( obs->fd != inotify_fd )
	{
		logstring(
			"Error",
			"internal failure, inotify_fd != ob->fd"
		);
		exit( -1 );
	}

	while( true )
	{
		ptrdiff_t len;
		int err;
		do {
			len = read( inotify_fd, readbuf, readbuf_size );
			err = errno;
			if( len < 0 && err == EINVAL )
			{
				// kernel > 2.6.21 indicates that way that way that
				// the buffer was too small to fit a filename.
				// double its size and try again. When using a lower
				// kernel and a filename > 2KB appears lsyncd
				// will fail. (but does a 2KB filename really happen?)
				//
				readbuf_size *= 2;
				readbuf = s_realloc(readbuf, readbuf_size);
			}
		} while( len < 0 && err == EINVAL );

		if( len == 0 )
		{
			// no more inotify events
			break;
		}

		if (len < 0)
		{
			if (err == EAGAIN) {
				// nothing more inotify
				break;
			}
			else
			{
				printlogf(
					L, "Error",
					"Read fail on inotify"
				);
				exit( -1 );
			}
		}

		{
			int i = 0;
			while( i < len && !hup && !term )
			{
				struct inotify_event *event =
					( struct inotify_event * )
					(readbuf + i);

				handle_event( L, event );

				i += sizeof( struct inotify_event ) + event->len;
			}
		}

		if( !move_event )
		{
			// give it a pause if not endangering splitting a move
			break;
		}
	}

	// checks if there is an unary MOVE_FROM left in the buffer
	if( move_event )
	{
		logstring(
			"Inotify",
			"handling unary move from."
		);
		handle_event( L, NULL );
	}
}


/*
| Registers the inotify functions.
*/
extern void
register_inotify( lua_State *L )
{
	lua_compat_register( L, LSYNCD_INOTIFYLIBNAME, linotfylib );
}


/*
| Cleans up the inotify handling.
*/
static void
inotify_tidy( struct observance *obs )
{
	if( obs->fd != inotify_fd )
	{
		logstring(
			"Error",
			"internal failure: inotify_fd != ob->fd"
		);

		exit( -1 );
	}

	close( inotify_fd );

	free( readbuf );

	readbuf = NULL;
}

/*
| Initalizes inotify handling
*/
extern void
open_inotify( lua_State *L )
{
	if( readbuf )
	{
		logstring(
			"Error",
			"internal failure, inotify readbuf != NULL in open_inotify()"
		)
		exit(-1);
	}

	readbuf = s_malloc( readbuf_size );

	inotify_fd = inotify_init( );

	if( inotify_fd < 0 )
	{
		printlogf(
			L,
			"Error",
			"Cannot access inotify monitor! ( %d : %s )",
			errno, strerror(errno)
		);
		exit( -1 );
	}

	printlogf(
		L, "Inotify",
		"inotify fd = %d",
		inotify_fd
	);

	close_exec_fd( inotify_fd );
	non_block_fd( inotify_fd );

	observe_fd(
		inotify_fd,
		inotify_ready,
		NULL,
		inotify_tidy,
		NULL
	);
}

