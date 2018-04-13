/*
| pipe.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Manages the pipes used to communicate with spawned subprocesses (usually rsync).
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "log.h"
#include "mem.h"
#include "pipe.h"
#include "observe.h"


/*
| A child process gets text piped through stdin
*/
struct pipemsg
{
	char * text;   // message to send
	int tlen;      // length of text
	int pos;       // position in message
};


/*
| Called by the core whenever a pipe becomes
| writeable again.
*/
static void
pipe_writey(
	lua_State * L,
	int fd,
	void * extra
)
{
	struct pipemsg * pm = (struct pipemsg *) extra;

	int len = write( fd, pm->text + pm->pos, pm->tlen - pm->pos );

	pm->pos += len;

	if( len < 0 )
	{
		logstring( "Normal", "broken pipe." );

		nonobserve_fd( fd );

		return;
	}

	if( pm->pos >= pm->tlen )
	{
		logstring( "Exec", "finished pipe." );

		nonobserve_fd( fd );
	}
}


/*
| Called when cleaning up a pipe.
*/
static void
pipe_tidy( int fd, void * extra )
{
	struct pipemsg * pm = (struct pipemsg *) extra;

	close( fd );
	free( pm->text );
	free( pm );
}


/*
| Creates a pipe.
|
| Sets the write end non blocking and close on exec.
*/
void
pipe_create( int pipefd[ 2 ] )
{
	// creates the pipe
	if( pipe( pipefd ) == -1 )
	{
		logstring( "Error", "cannot create a pipe!" );

		exit( -1 );
	}

	// always closes the write end for child processes
	close_exec_fd( pipefd[ 1 ] );

	// sets the write end on non-blocking
	non_block_fd( pipefd[ 1 ] );
}


/*
| Writes to a pipe and handles observing for further writing
| if it's buffer is fully filled on first try.
|
| This may be used only once for every pipe manged by Lsyncd.
*/
void
pipe_write(
	int pipefd[ 2 ],        // the pipe file descriptors
	char const * pipe_text, // text to pipe
	size_t pipe_len         // the pipe's text length
)
{
	// starts filling the pipe
	int len = write( pipefd[ 1 ], pipe_text, pipe_len );

	if( len < 0 )
	{
		logstring( "Error", "immediatly broken pipe." );

		close( pipefd[ 1 ] );
	}
	else if( len == pipe_len )
	{
		// usual and best case, the pipe accepted all input -> close
		logstring( "Exec", "one-sweeped pipe" );

		close( pipefd[ 1 ] );
	}
	else
	{
		struct pipemsg *pm;

		logstring( "Exec", "adding pipe observance" );

		pm = s_calloc( 1, sizeof( struct pipemsg ) );

		pm->text = s_calloc( pipe_len + 1, sizeof( char ) );
		memcpy( pm->text, pipe_text, pipe_len + 1 );
		pm->tlen = pipe_len;
		pm->pos  = len;

		observe_fd( pipefd[ 1 ], NULL, pipe_writey, pipe_tidy, pm );
	}
}

