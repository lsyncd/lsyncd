/*
| log.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
|
| Logging.
|
|
| This code assumes you have a 100 character wide display to view it (when tabstop is 4)
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "config.h"

#define SYSLOG_NAMES 1
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "log.h"
#include "mem.h"
#include "time.h"
#include "lsyncd.h"


// FIXME
extern bool first_time;
extern bool no_output;


/*
| A logging category
*/
struct logcat
{
	char *name;
	int priority;
};


/*
| A table of all enabled logging categories.
| Sorted by first letter for faster access.
*/
struct logcat * logcats[ 26 ] = { 0, };


/*
| Returns a logging facility number by name.
|
| Raises an error if not known.
*/
int
log_getFacility(
	lua_State * L,
	char const * fname
)
{
	int i;
	for( i = 0; facilitynames[ i ].c_name; i++ )
	{
		if( !strcasecmp( fname, facilitynames[ i ].c_name ) ) break;
	}

	if( !facilitynames[ i ].c_name )
	{
		printlogf( L, "Error", "Logging facility '%s' unknown.", fname );

		exit( -1 );
	}

	return facilitynames[ i ].c_val;
}


/*
| Returns a positive priority if category is configured to be logged or -1.
*/
int
check_logcat( const char *name )
{
	struct logcat *lc;

	if( name[ 0 ] < 'A' || name[ 0 ] > 'Z') return 99;

	lc = logcats[ name[ 0 ] - 'A' ];

	if( !lc ) return 99;

	while( lc->name )
	{
		if( !strcmp( lc->name, name ) ) return lc->priority;

		lc++;
	}

	return 99;
}


/*
| Adds a logging category
|
| Returns true if OK.
*/
bool add_logcat( const char *name, int priority )
{
	struct logcat *lc;

	if( !strcmp( "all", name ) )
	{
		settings.log_level = 99;

		return true;
	}

	if( !strcmp( "scarce", name ) )
	{
		settings.log_level = LOG_WARNING;

		return true;
	}

	// categories must start with a capital letter.
	if( name[ 0 ] < 'A' || name[ 0 ] > 'Z' ) return false;

	if( !logcats[ name[ 0 ]- 'A' ] )
	{
		// an empty capital letter
		lc = logcats[name[0]-'A'] = s_calloc(2, sizeof(struct logcat));
	}
	else
	{
		// length of letter list
		int ll = 0;

		// counts list length
		for( lc = logcats[ name[ 0 ] - 'A' ]; lc->name; lc++, ll++ );

		// enlarges list
		logcats[ name[ 0 ] - 'A'] =
			s_realloc( logcats[ name[ 0 ]-'A' ], ( ll + 2 ) * sizeof( struct logcat ) );

		// goes to the list end
		for( lc = logcats[ name[ 0 ] - 'A']; lc->name; lc++ )
		{
			// already there?
			if( !strcmp( name, lc->name ) ) return true;
		}
	}

	lc->name = s_strdup( name );
	lc->priority = priority;

	// terminates the list
	lc[ 1 ].name = NULL;
	return true;
}


/*
| Logs a string.
|
| Do not call this directly, but the macro logstring( )
| defined in lsyncd.h
*/
void
logstring0(
	int priority,        // the priority of the log message
	const char * cat,    // the category
	const char * message // the log message
)
{
	if( first_time )
	{
		// lsyncd is in it's intial configuration phase.
		// thus just print to normal stdout/stderr.
		if( priority >= LOG_ERR )
		{
			fprintf( stderr, "%s: %s\n", cat, message);
		}
		else
		{
			printf( "%s: %s\n", cat, message );
		}
		return;
	}

	// writes on console if not daemonized
	if( !no_output )
	{
		char ct[ 255 ];
		// gets current timestamp hour:minute:second
		time_t mtime;

		time( &mtime );

		strftime( ct, sizeof( ct ), "%T", localtime( &mtime ) );

		FILE * flog = priority <= LOG_ERR ? stderr : stdout;

		fprintf( flog, "%s %s: %s\n", ct, cat, message );
	}

	// writes to file if configured so
	if( settings.log_file )
	{
		FILE * flog = fopen( settings.log_file, "a" );

		char * ct;

		time_t mtime;

		// gets current timestamp day-time-year
		time( &mtime );

		ct = ctime( &mtime );

	 	// cuts trailing linefeed
		ct[ strlen( ct ) - 1] = 0;

		if( flog == NULL )
		{
			fprintf( stderr, "Cannot open logfile [%s]!\n", settings.log_file );

			exit( -1 );
		}

		fprintf( flog, "%s %s: %s\n", ct, cat, message );

		fclose( flog );
	}

	// sends to syslog if configured so
	if( settings.log_syslog )
	{
		syslog( priority, "%s, %s", cat, message );
	}

	return;
}


/*
| Lets the core print logmessages comfortably as formated string.
| This uses the lua_State for it easy string buffers only.
*/
void
printlogf0(
	lua_State * L,
	int priority,
	const char *cat,
	const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);
	logstring0(priority, cat, luaL_checkstring(L, -1));
	lua_pop(L, 1);
	return;
}


/*
| Frees logging stuff.
*/
void
log_free( )
{
	int ci;
	struct logcat *lc;

	for( ci = 'A'; ci <= 'Z'; ci++ )
	{
		for( lc = logcats[ ci - 'A' ]; lc && lc->name; lc++)
		{
			free( lc->name );
			lc->name = NULL;
		}

		if( logcats[ ci - 'A' ] )
		{
			free( logcats[ ci - 'A' ] );
			logcats[ ci - 'A' ] = NULL;
		}
	}
}


/*
| Logs a message.
|
| Params on Lua stack:
|
|    1:  loglevel of massage
|    2:  the string to log
*/
int l_log( lua_State * L )
{
	char const * cat;     // log category
	char const * message; // log message
	int priority;         // log priority

	cat = luaL_checkstring( L, 1 );
	priority = check_logcat( cat );

	// skips filtered messages
	if( priority > settings.log_level ) return 0;

	// replaces non string values
	{
		int i;
		int top = lua_gettop(L);
		for( i = 1; i <= top; i++ )
		{
			int t = lua_type( L, i );

			switch( t )
			{
				case LUA_TTABLE :

					lua_pushfstring( L, "(Table: %p)", lua_topointer( L, i ) );

					lua_replace( L, i );

					break;

				case LUA_TBOOLEAN :
					if( lua_toboolean( L, i ) )
						lua_pushstring( L, "(true)"  );
					else
						lua_pushstring( L, "(false)" );

					lua_replace( L, i );

					break;

				case LUA_TUSERDATA:
					{
						clock_t *c = ( clock_t * ) luaL_checkudata( L, i, "Lsyncd.jiffies" );

						double d = *c;
						d /= clocks_per_sec;
						lua_pushfstring( L, "(Timestamp: %f)", d );
						lua_replace( L, i );
					}
					break;

				case LUA_TNIL:
					lua_pushstring( L, "(nil)" );
					lua_replace( L, i );
					break;
			}
		}
	}

	// concates if there is more than one string parameter
	lua_concat( L, lua_gettop( L ) - 1 );

	message = luaL_checkstring( L, 2 );
	logstring0( priority, cat, message );

	return 0;
}

