/*
| util.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
|
| Small commonly used utils by Lsyncd.
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/

#include "lsyncd.h"

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "log.h"


/*
| Returns the absolute path of a path.
|
| This is a wrapper to various C-Library differences.
*/
char *
get_realpath( char const * rpath )
{
#ifdef __GLIBC__

	// in case of GLIBC the task is easy.
	return realpath( rpath, NULL );

#else
#warning having to use old style realpath()

	// otherwise less so and requires PATH_MAX limit
	char buf[ PATH_MAX] ;
	char *asw = realpath( rpath, buf );
	if( !asw ) return NULL;

	return s_strdup( asw );

#endif
}


/*
| Sets the non-blocking flag of a file descriptor.
*/
void
non_block_fd( int fd )
{
	int flags;

	flags = fcntl( fd, F_GETFL );

	if( flags == -1 )
	{
		logstring( "Error", "cannot get status flags!" );
		exit( -1 );
	}

	flags |= O_NONBLOCK;

	if( fcntl( fd, F_SETFL, flags ) == -1 )
	{
		logstring( "Error", "cannot set status flags!" );
		exit( -1 );
	}
}


/*
| Sets the close-on-exit flag of a file descriptor.
*/
void
close_exec_fd( int fd )
{
	int flags;

	flags = fcntl( fd, F_GETFD );

	if( flags == -1 )
	{
		logstring( "Error", "cannot get descriptor flags!" );
		exit( -1 );
	}

	flags |= FD_CLOEXEC;

	if( fcntl( fd, F_SETFD, flags ) == -1 )
	{
		logstring( "Error", "cannot set descripptor flags!" );
		exit( -1 );
	}
}


