/*
| core.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/

#include "lsyncd.h"

#define SYSLOG_NAMES 1

#include <sys/select.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "log.h"
#include "mem.h"
#include "util.h"
#include "pipe.h"
#include "observe.h"
#include "time.h"

#ifdef WITH_INOTIFY
#include "inotify.h"
#endif

/*
| The Lua part of Lsyncd
*/
extern const char mantle_out[];
extern size_t mantle_size;

/*
| The Lua coded default sync implementations
*/
extern const char default_out[];
extern size_t default_size;

/*
| Makes sure there is one file system monitor.
*/
#ifndef WITH_INOTIFY
#	error "needing at least one notification system. please rerun cmake"
#endif

/*
| All monitors supported by this Lsyncd.
*/
static char *monitors[] = {

#ifdef WITH_INOTIFY
	"inotify",
#endif

	NULL,
};


/**
| Configuration parameters that matter to the core
*/
struct settings settings = {
	.log_file     = NULL,
	.log_syslog   = false,
	.log_ident    = NULL,
	.log_facility = LOG_USER,
	.log_level    = LOG_NOTICE
};


/*
| True if stdout and stderr are detected to
| be directed to /dev/null.
*/
bool no_output = false;


/*
| The config file loaded by Lsyncd.
*/
char * lsyncd_config_file = NULL;


/*
| False after first time Lsyncd started up.
|
| Configuration error messages are thus written to
| stdout/stderr only on first start.
*/
bool first_time = true;


/*
| Set by TERM or HUP signal handler
| telling Lsyncd should end or reset ASAP.
*/
volatile sig_atomic_t hup  = 0;
volatile sig_atomic_t term = 0;
volatile sig_atomic_t sigcode = 0;


/*
| The kernel's clock ticks per second.
*/
extern long clocks_per_sec;


/*
| signal handler
*/
void sig_child( int sig ) { /* nothing */ }


/*
| signal handler
*/
void
sig_handler( int sig )
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


/*:::::::::::::::::::.
::  Helper Routines
'::::::::::::::::::::*/


/*
| Variable which address is used as
| the cores index in the lua registry to
| index the mantle-core-interface
|
| Its value is used to determined if the
| mantle has registered itself already.
*/
static int mci = 0;


/*
| Dummy variable which address is used as
| the cores index n the lua registry to
| the lua runners error handler.
*/
static int callError;


/*
| A user observance became read-ready.
*/
static void
user_obs_ready(
	lua_State * L,
	int fd,
	void * extra
)
{
	// pushes the ready table on table
	lua_pushlightuserdata( L, ( void * ) user_obs_ready );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the error handler
	lua_pushlightuserdata( L, (void *) &callError );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the user func
	lua_pushnumber( L, fd );
	lua_gettable( L, -3 );

	// gives the ufunc the fd
	lua_pushnumber( L, fd );

	// calls the user function
	if( lua_pcall( L, 1, 0, -3 ) ) exit( -1 );

	lua_pop( L, 2 );
}


/*
| A user observance became write-ready
*/
static void
user_obs_writey(
	lua_State * L,
	int fd,
	void * extra
)
{
	// pushes the writey table on table
	lua_pushlightuserdata( L, (void *) user_obs_writey );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the error handler
	lua_pushlightuserdata(L, (void *) &callError);
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the user func
	lua_pushnumber( L, fd );
	lua_gettable( L, -3 );

	// gives the user func the fd
	lua_pushnumber( L, fd );

	// calls the user function
	if( lua_pcall( L, 1, 0, -3 ) ) exit(-1);

	lua_pop( L, 2 );
}

/*
| Tidies up a user observance
| FIXME - give the user a chance to do something in that case!
*/
static void
user_obs_tidy(
	int fd,
	void * extra
)
{
	close( fd );
}


/*:::::::::::::::::::::::::::::::.
::  Library calls for the mantle
'::::::::::::::::::::::::::::::::*/


int l_stackdump( lua_State* L );


/*
| Logs a message.
|
| Params on Lua stack:
|
|    1:  loglevel of massage
|    2:  the string to log
*/
static int
l_log( lua_State *L )
{
	const char * cat;     // log category
	const char * message; // log message
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
					lua_pushfstring(
						L,
						"(Table: %p)",
						lua_topointer( L, i )
					);

					lua_replace( L, i );
					break;

				case LUA_TBOOLEAN :
					if( lua_toboolean( L, i ) )
						{ lua_pushstring( L, "(true)"  ); }
					else
						{ lua_pushstring( L, "(false)" ); }

					lua_replace(L, i);
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


/*
| Executes a subprocess. Does not wait for it to return.
|
| Params on Lua stack:
|
|    1: Path to binary to call
|    2: List of string as arguments
|         or "<" in which case the next argument is a string
|         that will be piped on stdin.
|         The arguments will follow that one.
|
| Returns (Lua stack) the pid on success, 0 on failure.
*/
static int
l_exec( lua_State *L )
{
	// the binary to call
	const char *binary = luaL_checkstring(L, 1);

	// number of arguments
	int argc = lua_gettop( L ) - 1;

	// the pid spawned
	pid_t pid;

	// the arguments position in the lua arguments
	int li = 1;

	// the pipe to text
	char const * pipe_text = NULL;

	// the pipes length
	size_t pipe_len = 0;

	// the arguments
	char const ** argv;

	// pipe file descriptors
	int pipefd[ 2 ];

	int i;

	// expands tables
	// and removes nils
	for( i = 1; i <= lua_gettop( L ); i++ )
	{
		if( lua_isnil( L, i ) )
		{
			lua_remove( L, i );
			i--;
			argc--;
			continue;
		}

		if( lua_istable( L, i ) )
		{
			int tlen;
			int it;
			lua_checkstack( L, lua_gettop( L ) + lua_objlen( L, i ) + 1 );

			// moves table to top of stack
			lua_pushvalue( L, i );
			lua_remove( L, i );
			argc--;
			tlen = lua_objlen( L, -1 );

			for( it = 1; it <= tlen; it++ )
			{
				lua_pushinteger( L, it );
				lua_gettable( L, -2 );
				lua_insert( L, i );
				i++;
				argc++;
			}
			i--;
			lua_pop( L, 1 );
		}
	}

	// writes a log message (if needed).
	if( check_logcat( "Exec" ) <= settings.log_level )
	{
		lua_checkstack( L, lua_gettop( L ) + argc * 3 + 2 );
		lua_pushvalue( L, 1 );

		for( i = 1; i <= argc; i++ )
		{
			lua_pushstring( L, " [" );
			lua_pushvalue( L, i + 1 );
			lua_pushstring( L, "]" );
		}

		lua_concat( L, 3 * argc + 1 );

		// replaces midfile 0 chars by linefeed
		size_t len = 0;
		const char * cs = lua_tolstring( L, -1, &len );
		char * s = s_calloc( len + 1, sizeof( char ) );

		for( i = 0; i < len; i++ )
		{
			s[ i ] = cs[ i ] ? cs[ i ] : '\n';
		}

		logstring0( LOG_DEBUG, "Exec", s );

		free( s );

		lua_pop( L, 1 );
	}

	if( argc >= 2 && !strcmp( luaL_checkstring( L, 2 ), "<" ) )
	{
		// pipes something into stdin
		if( !lua_isstring( L, 3 ) )
		{
			logstring( "Error", "in spawn(), expected a string after pipe '<'" );

			exit( -1 );
		}

		pipe_text = lua_tolstring( L, 3, &pipe_len );

		if( strlen( pipe_text ) > 0 )
		{
			pipe_create( pipefd );
		}
		else
		{
			pipe_text = NULL;
		}

		argc -= 2;
		li += 2;
	}

	// prepares the arguments
	argv = s_calloc( argc + 2, sizeof( char * ) );

	argv[ 0 ] = binary;

	for( i = 1; i <= argc; i++ )
	{
		argv[i] = luaL_checkstring( L, i + li );
	}

	argv[ i ] = NULL;

	// the fork!
	pid = fork( );

	if( pid == 0 )
	{
		// replaces stdin for pipes
		if( pipe_text ) dup2( pipefd[ 0 ], STDIN_FILENO );

		// if lsyncd runs as a daemon and has a logfile it will redirect
		// stdout/stderr of child processes to the logfile.
		if( settings.log_file )
		{
			if( !freopen( settings.log_file, "a", stdout ) )
			{
				printlogf( L, "Error", "cannot redirect stdout to '%s'.", settings.log_file );
			}

			if( !freopen( settings.log_file, "a", stderr ) )
			{
				printlogf( L, "Error", "cannot redirect stderr to '%s'.", settings.log_file );
			}
		}

		execv( binary, ( char ** ) argv );

		// in a sane world execv does not return!
		printlogf( L, "Error", "Failed executing [ %s ]!", binary );

		exit( -1 );
	}

	if( pipe_text )
	{
		// closes read-end of pipe, this is for child process only
		close( pipefd[ 0 ] );

		pipe_write( pipefd, pipe_text, pipe_len );
	}

	free( argv );
	lua_pushnumber( L, pid );

	return 1;
}


/*
| Registers the mantle core interface with the core.
|
| Params on Lua stack:
|    1: The luacode mantle.
|
| Returns on Lua stack:
|    nothing
*/
static int
l_mci( lua_State *L )
{
	if( mci )
	{
		logstring( "Error", "Luacode interface already registered!" );
		exit( -1 );
	}

	mci = 1;

	lua_pushlightuserdata( L, (void *) &mci );

	// switches the passed mantle interface as parameter and the key &mci
	lua_insert( L, 1 );

	// saves the table of the mci in the lua registry
	lua_settable( L, LUA_REGISTRYINDEX );

	// saves the error function extras

	lua_pushlightuserdata( L, (void *) &callError );
	lua_pushlightuserdata( L, (void *) &mci );
	lua_gettable( L, LUA_REGISTRYINDEX );
	lua_pushstring( L, "callError"  );
	lua_gettable( L, -2 );
	lua_remove( L, -2 );
	lua_settable( L, LUA_REGISTRYINDEX );

	if( lua_gettop( L ) )
	{
		logstring( "Error", "internal, stack is dirty." );
		l_stackdump( L );
		exit( -1 );
	}
}


/*
| Converts a relative directory path to an absolute.
|
| Params on Lua stack:
|     1: a relative path to directory
|
| Returns on Lua stack:
|     The absolute path of directory
*/
static int
l_realdir( lua_State *L )
{
	luaL_Buffer b;
	const char *rdir = luaL_checkstring(L, 1);
	char *adir = get_realpath(rdir);

	if( !adir )
	{
		printlogf(
			L, "Error",
			"failure getting absolute path of [%s]",
			rdir
		);

		return 0;
	}

	{
		// makes sure its a directory
	    struct stat st;
	    if( stat( adir, &st ) )
		{
			printlogf(
				L, "Error",
				"cannot get absolute path of dir '%s': %s",
				rdir,
				strerror( errno )
			);

			free( adir );

			return 0;
		}

	    if( !S_ISDIR( st.st_mode ) )
		{
			printlogf(
				L, "Error",
				"cannot get absolute path of dir '%s': is not a directory",
				rdir
			);

			free( adir );

			return 0;
	    }
	}

	// returns absolute path with a concated '/'
	luaL_buffinit( L, &b );
	luaL_addstring( &b, adir );
	luaL_addchar( &b, '/' );
	luaL_pushresult( &b );

	free( adir );

	return 1;
}


/*
| Dumps the Lua stack.
| For debugging purposes.
*/
int
l_stackdump( lua_State * L )
{
	int i;
	int top = lua_gettop( L );

	printlogf( L, "Debug", "total on stack %d", top );

	for( i = 1; i <= top; i++ )
	{
		int t = lua_type( L, i );

		switch( t )
		{
			case LUA_TSTRING:

				printlogf(
					L, "Debug",
					"%d string: '%s'", i, lua_tostring( L,  i )
				);

				break;

			case LUA_TBOOLEAN:

				printlogf( L, "Debug",
					"%d boolean %s", i, lua_toboolean( L, i ) ? "true" : "false"
				);

				break;

			case LUA_TNUMBER:

				printlogf( L, "Debug", "%d number: %g", i, lua_tonumber( L, i ) );

				break;

			default:

				printlogf( L, "Debug", "%d %s", i, lua_typename( L, t ) );

				break;
		}
	}

	return 0;
}

/*
| Reads the directories entries.
|
| Params on Lua stack:
|     1: absolute path to directory
|
| Returns on Lua stack:
|     a table of directory names.
|     names are keys
|     values are boolean true on dirs.
*/
static int
l_readdir( lua_State *L )
{
	const char * dirname = luaL_checkstring( L, 1 );

	DIR *d;

	d = opendir( dirname );

	if( d == NULL )
	{
		printlogf( L, "Error", "cannot open dir [%s].", dirname );

		return 0;
	}

	lua_newtable( L );

	while( !hup && !term )
	{
		struct dirent *de = readdir( d );
		bool isdir;

		// finished?
		if( de == NULL ) break;

		// ignores . and ..
		if( !strcmp( de->d_name, "."  ) || !strcmp( de->d_name, ".." ) ) continue;

		if( de->d_type == DT_UNKNOWN )
		{
			// must call stat on some systems :-/
			// ( e.g. ReiserFS )
			char *entry = s_malloc( strlen( dirname ) + strlen( de->d_name ) + 2 );

			struct stat st;

			strcpy( entry, dirname );
			strcat( entry, "/" );
			strcat( entry, de->d_name );

			lstat( entry, &st );

			isdir = S_ISDIR( st.st_mode );

			free( entry );
		}
		else
		{
			// otherwise readdir can be trusted
			isdir = de->d_type == DT_DIR;
		}

		// adds this entry to the Lua table
		lua_pushstring( L, de->d_name );
		lua_pushboolean( L, isdir );
		lua_settable( L, -3 );
	}

	closedir( d );

	return 1;
}


/*
| Immediately terminates Lsyncd.
|
| Params on Lua stack:
|     1:  exitcode of Lsyncd.
|
| Does not return.
|
*/
int
l_terminate( lua_State *L )
{
	int exitcode = luaL_checkinteger( L, 1 );

	exit( exitcode );

	return 0;
}


/*
| Configures core parameters.
|
| Params on Lua stack:
|     1:   a string, configure option
|     2:   depends on Param 1
*/
static int
l_configure( lua_State *L )
{
	const char * command = luaL_checkstring( L, 1 );

	if( !strcmp( command, "running" ) )
	{
		// set by mantle after first initialize
		// from this on log to configurated log end instead of
		// stdout/stderr
		first_time = false;

		if( !settings.log_file )
		{
			settings.log_syslog = true;

			const char * log_ident = settings.log_ident ? settings.log_ident : "lsyncd";

			openlog( log_ident, 0, settings.log_facility );
		}

		logstring( "Normal", "--- Startup ---" );

	}
	else if( !strcmp( command, "logfile" ) )
	{
		const char * file = luaL_checkstring( L, 2 );

		if( settings.log_file )
		{
			free( settings.log_file );
		}

		settings.log_file =
			s_strdup( file );
	}
	else if( !strcmp( command, "logfacility" ) )
	{
		if( lua_isstring( L, 2 ) )
		{
			const char * fname = luaL_checkstring( L, 2 );
			int i;
			for( i = 0; facilitynames[ i ].c_name; i++ )
			{
				if( !strcasecmp( fname, facilitynames[ i ].c_name ) )
					{ break; }
			}

			if( !facilitynames[ i ].c_name )
			{
				printlogf(
					L, "Error",
					"Logging facility '%s' unknown.",
					fname
				);

				exit( -1 );
			}
			settings.log_facility = facilitynames[ i ].c_val;
		}
		else if (lua_isnumber(L, 2))
		{
			settings.log_facility = luaL_checknumber(L, 2);
		}
		else
		{
			printlogf( L, "Error", "Logging facility must be a number or string" );

			exit( -1 );
		}
	}
	else if( !strcmp( command, "logident" ) )
	{
		const char * ident = luaL_checkstring( L, 2 );

		if( settings.log_ident ) free( settings.log_ident );

		settings.log_ident = s_strdup( ident );
	}
	else
	{
		printlogf(
			L, "Error",
			"Internal error, unknown parameter in l_configure( %s )",
			command
		);

		exit( -1 );
	}

	return 0;
}

/*
| Allows user scripts to observe filedescriptors
|
| Params on Lua stack:
|     1: file descriptor
|     2: function to call when read  becomes ready
|     3: function to call when write becomes ready
*/
static int
l_observe_fd( lua_State *L )
{
	int fd = luaL_checknumber( L, 1 );
	bool ready  = false;
	bool writey = false;

	// Stores the user function in the lua registry.
	// It uses the address of the cores ready / writey functions
	// for the user as key
	if( !lua_isnoneornil( L, 2 ) )
	{
		lua_pushlightuserdata( L, (void *) user_obs_ready );

		lua_gettable( L, LUA_REGISTRYINDEX );

		if( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1  );
			lua_newtable( L );
			lua_pushlightuserdata( L, (void *) user_obs_ready );
			lua_pushvalue( L, -2 );
			lua_settable( L, LUA_REGISTRYINDEX );
		}

		lua_pushnumber( L, fd );
		lua_pushvalue( L,  2 );
		lua_settable( L, -3 );
		lua_pop( L,  1 );

		ready = true;
	}

	if( !lua_isnoneornil( L, 3 ) )
	{
		lua_pushlightuserdata( L, (void *) user_obs_writey );

		lua_gettable (L, LUA_REGISTRYINDEX );

		if( lua_isnil(L, -1) )
		{
			lua_pop               ( L, 1                        );
			lua_newtable          ( L                           );
			lua_pushlightuserdata ( L, (void *) user_obs_writey );
			lua_pushvalue         ( L, -2                       );
			lua_settable          ( L, LUA_REGISTRYINDEX        );
		}

		lua_pushnumber ( L, fd );
		lua_pushvalue  ( L,  3 );
		lua_settable   ( L, -3 );
		lua_pop        ( L,  1 );

		writey = true;
	}

	// tells the core to watch the fd
	observe_fd(
		fd,
		ready  ? user_obs_ready : NULL,
		writey ? user_obs_writey : NULL,
		user_obs_tidy,
		NULL
	);

	return 0;
}

/*
| Removes a user observance
|
| Params on Lua stack:
|     1:  exitcode of Lsyncd.
*/
extern int
l_nonobserve_fd( lua_State *L )
{
	int fd = luaL_checknumber( L, 1 );

	// removes the read function
	lua_pushlightuserdata( L, (void *) user_obs_ready );
	lua_gettable( L, LUA_REGISTRYINDEX );

	if( !lua_isnil( L, -1 ) )
	{
		lua_pushnumber ( L, fd );
		lua_pushnil    ( L     );
		lua_settable   ( L, -2 );
	}
	lua_pop( L, 1 );

	lua_pushlightuserdata( L, (void *) user_obs_writey );
	lua_gettable( L, LUA_REGISTRYINDEX );
	if ( !lua_isnil( L, -1 ) )
	{
		lua_pushnumber ( L, fd );
		lua_pushnil    ( L     );
		lua_settable   ( L, -2 );
	}
	lua_pop( L, 1 );

	nonobserve_fd( fd );
	return 0;
}


/*
| The Lsnycd's core library.
*/
static const luaL_Reg corelib[] =
{
	{ "configure",      l_configure     },
	{ "exec",           l_exec          },
	{ "log",            l_log           },
	{ "mci",            l_mci           },
	{ "now",            l_now           },
	{ "nonobserve_fd",  l_nonobserve_fd },
	{ "observe_fd",     l_observe_fd    },
	{ "readdir",        l_readdir       },
	{ "realdir",        l_realdir       },
	{ "stackdump",      l_stackdump     },
	{ "terminate",      l_terminate     },
	{ NULL,             NULL            }
};


/*
| Registers the Lsyncd's core library.
*/
void
register_core( lua_State *L )
{
	lua_newtable( L );
	luaL_setfuncs( L, corelib, 0 );
	lua_setglobal( L, LSYNCD_CORE_LIBNAME );

	register_jiffies( L );

#ifdef WITH_INOTIFY

	lua_getglobal( L, LSYNCD_CORE_LIBNAME );
	register_inotify( L );
	lua_setfield( L, -2, LSYNCD_INOTIFY_LIBNAME );
	lua_pop( L, 1 );

#endif

	if( lua_gettop( L ) )
	{
		logstring( "Error", "internal, stack not empty in lsyncd_register( )" );
		exit( -1 );
	}
}


/*:::::::::::::::.
::  Lsyncd Core
'::::::::::::::::*/


/*
| Pushes a function from the mantle on the stack.
| As well as the callError handler.
*/
extern void
load_mci(
	lua_State * L,
	const char * name
)
{
	printlogf( L, "Call", "%s( )", name );

	// pushes the error handler
	lua_pushlightuserdata( L, (void *) &callError );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the function
	lua_pushlightuserdata( L, (void *) &mci );
	lua_gettable( L, LUA_REGISTRYINDEX );
	lua_pushstring( L, name );
	lua_gettable( L, -2 );
	lua_remove( L, -2 );
}


/*
| Normal operation happens in here.
*/
static void
masterloop(lua_State *L)
{
	while( true )
	{
		bool have_alarm;
		bool force_alarm   = false;
		clock_t cnow       = now( );
		clock_t alarm_time = 0;

		// memory usage debugging
		// lua_gc( L, LUA_GCCOLLECT, 0 );
		// printf(
		//     "gccount: %d\n",
		//     lua_gc( L, LUA_GCCOUNT, 0 ) * 1024 + lua_gc( L, LUA_GCCOUNTB, 0 ) );

		//
		// queries the mantle about the soonest alarm
		//
		load_mci( L, "getAlarm" );

		if( lua_pcall( L, 0, 1, -2 ) ) exit( -1 );

		if( lua_type( L, -1 ) == LUA_TBOOLEAN)
		{
			have_alarm = false;

			force_alarm = lua_toboolean( L, -1 );
		}
		else
		{
			have_alarm = true;

			alarm_time = *( ( clock_t * ) luaL_checkudata( L, -1, "Lsyncd.jiffies" ) );
		}

		lua_pop( L, 2 );

		if(
			force_alarm ||
			( have_alarm && time_before_eq( alarm_time, cnow ) )
		)
		{
			// there is a delay that wants to be handled already thus instead
			// of reading/writing from observances it jumps directly to
			// handling

			// TODO: Actually it might be smarter to handle observances
			// anyway. since event queues might overflow.
			logstring( "Masterloop", "immediately handling delays." );
		}
		else
		{
			// uses select( ) to determine what happens next:
			//   a) a new event on an observance
			//   b) an alarm on timeout
			//   c) the return of a child process
			struct timespec tv;

			if( have_alarm )
			{
				// TODO use trunc instead of long conversions
				double d = ( ( double )( alarm_time - cnow ) ) / clocks_per_sec;
				tv.tv_sec  = d;
				tv.tv_nsec = ( ( d - ( long ) d ) ) * 1000000000.0;

				printlogf(
					L, "Masterloop",
					"going into select ( timeout %f seconds )",
					d
				);
			}
			else
			{
				logstring( "Masterloop", "going into select ( no timeout )" );
			}

			observe_select( L, have_alarm ? &tv : NULL );
		}

		// collects possibly zombified child processes
		while( 1 )
		{
			int status;
			pid_t pid = waitpid( 0, &status, WNOHANG );

			// no more zombies
			if( pid <= 0 ) break;

			// calls the mantle to handle the collection
			load_mci( L, "collectProcess" );
			lua_pushinteger( L, pid );
			lua_pushinteger( L, WEXITSTATUS( status ) );

			if( lua_pcall( L, 2, 0, -4 ) ) exit(-1);

			lua_pop( L, 1 );
		}

		// reacts on HUP signals
		if( hup )
		{
			load_mci( L, "hup" );
			if( lua_pcall( L, 0, 0, -2 ) ) exit( -1 );
			lua_pop( L, 1 );

			hup = 0;
		}

		// reacts on TERM and INT signals
		if( term == 1 )
		{
			load_mci( L, "term" );
			lua_pushnumber( L, sigcode );
			if( lua_pcall( L, 1, 0, -3 ) ) exit( -1 );
			lua_pop( L, 1 );

			term = 2;
		}

		// lets the mantle do stuff every cycle,
		// like starting new processes, writing the statusfile etc.
		load_mci( L, "cycle" );

		l_now( L );

		if( lua_pcall( L, 1, 1, -3 ) ) exit( -1 );

		if( !lua_toboolean( L, -1 ) )
		{
			// cycle told core to break mainloop
			lua_pop( L, 2 );
			return;
		}

		lua_pop( L, 2 );

		if( lua_gettop( L ) )
		{
			logstring( "Error", "internal, stack is dirty." );
			l_stackdump( L );
			exit( -1 );
		}
	}
}


/*
| The effective main for one run.
|
| HUP signals may cause several runs of the one main.
*/
int
main1( int argc, char *argv[] )
{
	// the Lua interpreter
	lua_State * L;

	int argp = 1;

	// load Lua
	L = luaL_newstate( );

	luaL_openlibs( L );

	{
		// checks the lua version
		const char * version;
		int major, minor;
		lua_getglobal( L, "_VERSION" );
		version = luaL_checkstring( L, -1 );

		if( sscanf( version, "Lua %d.%d", &major, &minor ) != 2 )
		{
			fprintf( stderr, "cannot parse lua library version!\n" );

			exit (-1 );
		}

		if( major < 5 || ( major == 5 && minor < 2 ) )
		{
			fprintf( stderr, "Lua library is too old. Needs 5.2 at least" );

			exit( -1 );
		}

		lua_pop( L, 1 );
	}

	{
		// logging is prepared quite early
		int i = 1;
		add_logcat( "Normal", LOG_NOTICE );
		add_logcat( "Warn", LOG_WARNING );
		add_logcat( "Error", LOG_ERR );

		while( i < argc )
		{
			if( strcmp( argv[ i ], "-log"  ) && strcmp( argv[ i ], "--log" ) )
			{
				// arg is neither -log or --log
				i++;
				continue;
			}

			// -(-)log was last argument
			if( ++i >= argc ) break;

			if( !add_logcat( argv[ i ], LOG_NOTICE ) )
			{
				printlogf(
					L, "Error",
					"'%s' is not a valid logging category",
					argv[ i ]
				);

				exit( -1 );
			}
		}
	}

	// registers Lsycnd's core library
	register_core( L );

	if( check_logcat( "Debug" ) <= settings.log_level )
	{
		// printlogf doesnt support %ld :-(
		printf( "kernels clocks_per_sec=%ld\n", clocks_per_sec );
	}

	// loads the lsyncd mantle
	if( luaL_loadbuffer( L, mantle_out, mantle_size, "mantle" ) )
	{
		printlogf( L, "Error", "loading mantle: %s", lua_tostring( L, -1 ) );
		exit( -1 );
	}

	// prepares the luacode executing the script
	if( lua_pcall( L, 0, 0, 0 ) )
	{
		printlogf( L, "Error", "preparing mantle: %s", lua_tostring( L, -1 ) );
		exit( -1 );
	}

	{
		// asserts the Lsyncd's version matches
		// double checks the if mantle version is the same as core version
		const char *lversion;

		lua_getglobal( L, "lsyncd_version" );
		lversion = luaL_checkstring( L, -1 );

		if( strcmp( lversion, PACKAGE_VERSION ) )
		{
			printlogf(
				L, "Error",
				"Version mismatch luacode is '%s', but core is '%s'",
				lversion, PACKAGE_VERSION
			);
			exit( -1 );
		}

		lua_pop( L, 1 );
	}

	// loads the default sync implementations
	if( luaL_loadbuffer( L, default_out, default_size, "default" ) )
	{
		printlogf( L, "Error",
			"loading default sync implementations: %s", lua_tostring( L, -1 ) );
		exit( -1 );
	}

	// loads the user enivornment
	// the default sync implementations are actually not priviledged in any way
	lua_getglobal( L, "userENV" );
	lua_setupvalue( L, -2, 1 );

	// prepares the default sync implementations
	if( lua_pcall( L, 0, 0, 0 ) )
	{
		printlogf( L, "Error",
			"preparing default sync implementations: %s", lua_tostring( L, -1 ) );
		exit( -1 );
	}

	// checks if there is a "-help" or "--help"
	{
		int i;
		for( i = argp; i < argc; i++ )
		{
			if ( !strcmp( argv[ i ],  "-help" ) || !strcmp( argv[ i ], "--help" ) )
			{
				load_mci( L, "help" );

				if( lua_pcall( L, 0, 0, -2 ) ) exit( -1 );

				lua_pop( L, 1 );

				exit( 0 );
			}
		}
	}

	// starts the option parser in Lua script
	{
		int idx = 1;
		const char *s;

		// creates a table with all remaining argv option arguments
		load_mci( L, "configure" );
		lua_newtable( L );

		while( argp < argc )
		{
			lua_pushnumber( L, idx++ );
			lua_pushstring( L, argv[ argp++ ] );
			lua_settable( L, -3 );
		}

		// creates a table with the cores event monitor interfaces
		idx = 0;
		lua_newtable( L );

		while( monitors[ idx ] )
		{
			lua_pushnumber( L, idx + 1 );
			lua_pushstring( L, monitors[ idx++ ] );
			lua_settable( L, -3 );
		}

		if( lua_pcall( L, 2, 1, -4 ) ) exit( -1 );

		if( first_time )
		{
			// If not first time, simply retains the config file given
			s = lua_tostring(L, -1);

			if( s ) lsyncd_config_file = s_strdup( s );
		}

		lua_pop( L, 2 );
	}

	// checks existence of the config file
	if( lsyncd_config_file )
	{
		struct stat st;

		// gets the absolute path to the config file
		// so in case of HUPing the daemon, it finds it again
		char * apath = get_realpath( lsyncd_config_file );
		if( !apath )
		{
			printlogf( L, "Error", "Cannot find config file at '%s'.", lsyncd_config_file );

			exit( -1 );
		}

		free( lsyncd_config_file );

		lsyncd_config_file = apath;

		if( stat( lsyncd_config_file, &st ) )
		{
			printlogf( L, "Error", "Cannot find config file at '%s'.", lsyncd_config_file );

			exit( -1 );
		}

		// loads and executes the config file
		if( luaL_loadfile( L, lsyncd_config_file ) )
		{
			printlogf(
				L, "Error",
				"error loading %s: %s", lsyncd_config_file, lua_tostring( L, -1 )
			);

			exit( -1 );
		}


		// loads the user enivornment
		lua_getglobal( L, "userENV" );
		lua_setupvalue( L, -2, 1 );

		if( lua_pcall( L, 0, LUA_MULTRET, 0) )
		{
			printlogf(
				L, "Error",
				"error preparing %s: %s", lsyncd_config_file, lua_tostring( L, -1 )
			);

			exit( -1 );
		}
	}

#ifdef WITH_INOTIFY
	open_inotify( L );
#endif

	// adds signal handlers
	// listens to SIGCHLD, but blocks it until pselect( )
	// opens the signal handler up
	{
		sigset_t set;
		sigemptyset( &set );
		sigaddset( &set, SIGCHLD );
		signal( SIGCHLD, sig_child );
		sigprocmask( SIG_BLOCK, &set, NULL );

		signal( SIGHUP,  sig_handler );
		signal( SIGTERM, sig_handler );
		signal( SIGINT,  sig_handler );
	}

	// runs initializations from mantle
	// it will set the configuration and add watches
	{
		load_mci( L, "initialize" );
		lua_pushboolean( L, first_time );

		if( lua_pcall( L, 1, 0, -3 ) ) exit( -1 );

		lua_pop( L, 1 );
	}

	//
	// enters the master loop
	//
	masterloop( L );

	// cleanup
	observe_tidy_all( );

	// frees logging categories
	log_free( );

	lua_close( L );

	return 0;
}


/*
| Main
*/
int
main( int argc, char * argv[ ] )
{
	// gets a kernel parameter
	clocks_per_sec = sysconf( _SC_CLK_TCK );

	setlinebuf( stdout );
	setlinebuf( stderr );

	while( !term )
	{
		main1( argc, argv );
	}

	// exits with error code responding to the signal it died for
	// FIXME this no longer holds true to systemd recommendations
	return 128 + sigcode;
}

