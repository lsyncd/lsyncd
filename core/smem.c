/*
| smem.c from Lsyncd - Live (Mirror) Syncing Demon
|
|
| Simple "secured" memory management.
|
| In future it might be an idea to call the lua garbage collecter in case memory allocation
| fails. However on Linux it's a mood point since practically a NULL is only returned
| when requesting a way too large memory block the system can ever handle, if the kernel
| runs out of memory it goes instead into oom-killer mode.
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/

#include <stdlib.h>
#include <syslog.h>
#include <string.h>

#include "lsyncd.h"
#include "smem.h"
#include "log.h"

/*
| "Secured" calloc
*/
extern void *
s_calloc( size_t nmemb, size_t size )
{
	void * r = calloc( nmemb, size );

	if( r == NULL )
	{
		logstring0( LOG_ERR, "Error", "Out of memory!" );
		exit( -1 );
	}

	return r;
}


/*
| "Secured" malloc
*/
extern void *
s_malloc( size_t size )
{
	void * r = malloc( size );

	if( r == NULL )
	{
		logstring0( LOG_ERR, "Error", "Out of memory!" );
		exit( -1 );
	}

	return r;
}


/*
| "Secured" realloc
*/
extern void *
s_realloc( void * ptr, size_t size )
{
	void * r = realloc( ptr, size );

	if( r == NULL )
	{
		logstring0( LOG_ERR, "Error", "Out of memory!" );
		exit( -1 );
	}

	return r;
}


/*
| "Secured" strdup
*/
extern char *
s_strdup( const char *src )
{
	char *s = strdup( src );

	if( s == NULL )
	{
		logstring0( LOG_ERR, "Error", "Out of memory!" );
		exit( -1 );
	}

	return s;
}

