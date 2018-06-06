/*
| main.c from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Entry and main loop
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#include "feature.h"

// FIXME remove unneeded headers

#include <sys/select.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
#include "mci.h"
#include "mem.h"
#include "util.h"
#include "pipe.h"
#include "observe.h"
#include "time.h"
#include "signal.h"
#include "userobs.h"

#ifdef WITH_INOTIFY
#include "inotify.h"
#endif


/*
| Makes sure there is one file system monitor.
*/
#ifndef WITH_INOTIFY
#	error "needing at least one notification system. please rerun cmake"
#endif

/*
| All monitors supported by this Lsyncd.
*/
char *monitors[] = {

#ifdef WITH_INOTIFY
	"inotify",
#endif

	NULL,
};


/**
| Set to true to soft reset at earliest possilibity.
*/
bool softreset = false;


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
| Normal operation happens in here.
*/
static void
masterloop(
	lua_State *L
)
{
	while( !softreset )
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

	logstring( "Normal", "--- Soft Reset ---" );
	softreset = false;
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

	signal_init( );

#ifdef WITH_INOTIFY
	open_inotify( L );
#endif

	mci_load_mantle( L );

	mci_load_default( L );

	// checks if there is a "-help" or "--help"
	{
		// FIXME this should be done in mantle
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
			s = lua_tostring( L, -1 );

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
		lua_getglobal( L, "userenv" );
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
	mci_tidy( );
	signal_tidy( );

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

	while( true ) main1( argc, argv );

//	return -1;
}

