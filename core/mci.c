/*
| mci.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| The (generic) C part of the inteface between mantle and core.
|
| Note that some mci functions are provided directly
| in their respective core subsystems
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>  // FIXME abstract this away
#include <syslog.h>  // FIXME abstract this away
#include <string.h>
#include <unistd.h>

#define LUA_USE_APICHECK 1
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "log.h"
#include "mci.h"
#include "mem.h"
#include "observe.h"
#include "pipe.h"
#include "signal.h"
#include "time.h"
#include "userobs.h"
#include "util.h"

#ifdef WITH_INOTIFY
#  include "inotify.h"
#endif


#define CORE_LIBNAME "core"
#define INOTIFY_LIBNAME "inotify"


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
| True only on first time.
| FIXME do away.
*/
extern bool first_time;

/*
| Makes sure there is one file system monitor.
*/
#ifndef WITH_INOTIFY
#	error "needing at least one notification system. please rerun cmake"
#endif

/*
| All monitors supported by this Lsyncd.
*/
extern char *monitors[];


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
int callError;


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
l_exec( lua_State * L )
{
	// the binary to call
	const char *binary = luaL_checkstring( L, 1 );

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
			lua_checkstack( L, lua_gettop( L ) + lua_rawlen( L, i ) + 1 );

			// moves table to top of stack
			lua_pushvalue( L, i );
			lua_remove( L, i );
			argc--;
			tlen = lua_rawlen( L, -1 );

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

	return 0;
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
	char *adir = get_realpath( rdir );

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

	DIR *d = opendir( dirname );

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

		settings.log_file = s_strdup( file );
	}
	else if( !strcmp( command, "logfacility" ) )
	{
		if( lua_isstring( L, 2 ) )
		{
			const char * fname = luaL_checkstring( L, 2 );

			settings.log_facility = log_getFacility( L, fname );
		}
		else if (lua_isnumber(L, 2))
		{
			settings.log_facility = luaL_checknumber( L, 2 );
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
| The Lsnycd's core library.
*/
static const luaL_Reg corelib[ ] =
{
	{ "configure",      l_configure     },
	{ "exec",           l_exec          },
	{ "log",            l_log           },
	{ "mci",            l_mci           },
	{ "now",            l_now           },
	{ "nonobserve_fd",  l_nonobserve_fd },
	{ "observe_fd",     l_observe_fd    },
	{ "onsignal",       l_onsignal      },
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
	lua_setglobal( L, CORE_LIBNAME );

	register_jiffies( L );

#ifdef WITH_INOTIFY

	lua_getglobal( L, CORE_LIBNAME );
	register_inotify( L );
	lua_setfield( L, -2, INOTIFY_LIBNAME );
	lua_pop( L, 1 );

#endif

	if( lua_gettop( L ) )
	{
		logstring( "Error", "internal, stack not empty in lsyncd_register( )" );
		exit( -1 );
	}
}


/*
| Pushes a function from the mantle on the stack.
| As well as the callError handler.
| FIXME rename
*/
void
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
| Loads the mantle.
*/
void
mci_load_mantle(
	lua_State * L
)
{
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
}


/*
| Loads the default implementations.
*/
void
mci_load_default(
	lua_State * L
)
{
	// loads the default sync implementations
	if( luaL_loadbuffer( L, default_out, default_size, "default" ) )
	{
		printlogf(
			L, "Error",
			"loading default sync implementations: %s", lua_tostring( L, -1 )
		);

		exit( -1 );
	}

	// loads the user enivornment
	// the default sync implementations are actually not priviledged in any way
	lua_getglobal( L, "userENV" );
	lua_setupvalue( L, -2, 1 );

	// prepares the default sync implementations
	if( lua_pcall( L, 0, 0, 0 ) )
	{
		printlogf(
			L, "Error",
			"preparing default sync implementations: %s", lua_tostring( L, -1 )
		);

		exit( -1 );
	}
}

