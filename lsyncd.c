/*
| lsyncd.c   Live (Mirror) Syncing Demon
| ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
|
| This is Lsyncd's core.
|
| It contains as minimal as possible glues to the operating system needed
| for Lsyncd's operation. All high-level logic is coded (when feasable)
| into lsyncd.lua
|
| This code assumes you have a 100 character wide display to view it (when tabstop is 4)
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
|
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

/*
| The Lua part of Lsyncd
*/
extern const char runner_out[];
extern size_t runner_size;

extern const char defaults_out[];
extern size_t defaults_size;

/*
| Makes sure there is one file system monitor.
*/
#ifndef WITH_INOTIFY
#ifndef WITH_FSEVENTS
#	error "needing at least one notification system. please rerun cmake"
#endif
#endif

/*
| All monitors supported by this Lsyncd.
*/
static char *monitors[] = {

#ifdef WITH_INOTIFY
	"inotify",
#endif

#ifdef WITH_FSEVENTS
	"fsevents",
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
	.log_level    = LOG_NOTICE,
	.nodaemon     = false,
};


/*
| True when Lsyncd daemonized itself.
*/
static bool is_daemon = false;


/*
| The config file loaded by Lsyncd.
*/
char * lsyncd_config_file = NULL;


/*
| False after first time Lsyncd started up.
|
| Configuration error messages are thus written to
| stdout/stderr only on first start.
|
| All other resets (HUP or monitor OVERFLOW) run with 'insist'
| implictly turned on and thus Lsyncd does not failing on a non
| responding target.
*/
static bool first_time = true;


/*
| Set by TERM or HUP signal handler
| telling Lsyncd should end or reset ASAP.
*/
volatile sig_atomic_t hup  = 0;
volatile sig_atomic_t term = 0;
volatile sig_atomic_t sigcode = 0;
int pidfile_fd = 0;


/*
| The kernel's clock ticks per second.
*/
static long clocks_per_sec;


/**
 * signal handler
 */
void
sig_child(int sig) {
	// nothing
}


/**
 * signal handler
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


/*
| Non glibc builds need a real tms structure for the times( ) call
*/
#ifdef __GLIBC__
	static struct tms * dummy_tms = NULL;
#else
	static struct tms  _dummy_tms;
	static struct tms * dummy_tms = &_dummy_tms;
#endif


/*
| Returns the absolute path of a path.
|
| This is a wrapper to various C-Library differences.
*/
char *
get_realpath( const char * rpath )
{
	// uses c-library to get the absolute path
#ifdef __GLIBC__
	// in case of GLIBC the task is easy.
	return realpath( rpath, NULL );
#else
#	warning having to use old style realpath()
	// otherwise less so and requires PATH_MAX limit
	char buf[ PATH_MAX] ;
	char *asw = realpath( rpath, buf );
	if( !asw )
		{ return NULL; }

	return s_strdup( asw );
#endif
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
(              Logging                      )
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


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
static struct logcat *
logcats[ 26 ] = { 0, };


/*
| Returns a positive priority if category is configured to be logged or -1.
*/
extern int
check_logcat( const char *name )
{
	struct logcat *lc;

	if( name[ 0 ] < 'A' || name[ 0 ] > 'Z')
		{ return 99; }

	lc = logcats[ name[ 0 ] - 'A' ];

	if( !lc )
		{ return 99; }

	while( lc->name )
	{
		if( !strcmp( lc->name, name ) )
			{ return lc->priority; }

		lc++;
	}

	return 99;
}


/*
| Adds a logging category
|
| Returns true if OK.
*/
static bool
add_logcat( const char *name, int priority )
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
	if( name[ 0 ] < 'A' || name[ 0 ] > 'Z' )
	{
		return false;
	}

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
		for( lc = logcats[name[0]-'A']; lc->name; lc++ )
			{ ll++; }

		// enlarges list
		logcats[ name[ 0 ] - 'A'] =
			s_realloc(
				logcats[ name[ 0 ]-'A' ],
				( ll + 2 ) * sizeof( struct logcat )
			);

		// goes to the list end
		for( lc = logcats[ name[ 0 ] - 'A']; lc->name; lc++ )
		{
			if( !strcmp( name, lc->name ) )
			{
				// already there
				return true;
			}
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
extern void
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
	if( !is_daemon )
	{
		char ct[ 255 ];
		// gets current timestamp hour:minute:second
		time_t mtime;

		time( &mtime );

		strftime( ct, sizeof( ct ), "%T", localtime( &mtime ) );

		FILE * flog = priority <= LOG_ERR ? stderr : stdout;

		fprintf(
			flog,
			"%s %s: %s\n",
			ct, cat, message
		);
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
			fprintf(
				stderr,
				"Cannot open logfile [%s]!\n",
				settings.log_file
			);

			exit( -1 );
		}

		fprintf(
			flog,
			"%s %s: %s\n",
			ct, cat, message
		);

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
extern void
printlogf0(lua_State *L,
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


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
(       Simple memory management            )
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


// FIXME call the Lua garbace collector in case of out of memory

/*
| "Secured" calloc
*/
extern void *
s_calloc( size_t nmemb, size_t size )
{
	void * r = calloc( nmemb, size );

	if( r == NULL )
	{
		logstring0(
			LOG_ERR,
			"Error",
			"Out of memory!"
		);

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
		logstring0(
			LOG_ERR,
			"Error",
			"Out of memory!"
		);

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
		logstring0(
			LOG_ERR,
			"Error",
			"Out of memory!"
		);

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
		logstring0(
			LOG_ERR,
			"Error",
			"Out of memory!"
		);

		exit( -1 );
	}

	return s;
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
(           Pipes  Management               )
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


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
| writeable again
*/
static void
pipe_writey(
	lua_State * L,
	struct observance * observance
)
{
	int fd = observance->fd;

	struct pipemsg *pm = (struct pipemsg * ) observance->extra;

	int len = write(
		fd,
		pm->text + pm->pos,
		pm->tlen - pm->pos
	);

	pm->pos += len;

	if( len < 0 )
	{
		logstring( "Normal", "broken pipe." );
		nonobserve_fd( fd );
	}
	else if( pm->pos >= pm->tlen )
	{
		logstring( "Exec", "finished pipe." );
		nonobserve_fd(fd);
	}
}


/*
| Called when cleaning up a pipe.
*/
static void
pipe_tidy( struct observance * observance )
{
	struct pipemsg *pm = ( struct pipemsg * ) observance->extra;

	close( observance->fd );
	free( pm->text );
	free( pm );
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
(           Helper Routines                 )
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


/*
| Dummy variable of which it's address is used as
| the cores index in the lua registry to
| the lua runners function table in the lua registry.
*/
static int runner;


/*
| Dummy variable of which it's address is used as
| the cores index n the lua registry to
| the lua runners error handler.
*/
static int callError;


/*
| Sets the close-on-exit flag of a file descriptor.
*/
extern void
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


/*
| Sets the non-blocking flag of a file descriptor.
*/
extern void
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
| Writes a pid file.
*/
static void
write_pidfile
(
	lua_State *L,
	const char *pidfile
)
{
	pidfile_fd = open( pidfile, O_CREAT | O_RDWR, 0644 );

	fcntl( pidfile_fd, F_SETFD, FD_CLOEXEC );

	char buf[ 127 ];

	if( pidfile_fd < 0 )
	{
		printlogf(
			L, "Error",
			"Cannot create pidfile; '%s'",
			pidfile
		);

		exit( -1 );
	}

	int rc = lockf( pidfile_fd, F_TLOCK, 0 );

	if( rc < 0 )
	{
		printlogf(
			L, "Error",
			"Cannot lock pidfile; '%s'",
			pidfile
		);

		exit( -1 );
	}

	snprintf( buf, sizeof( buf ), "%i\n", getpid( ) );

	write( pidfile_fd, buf, strlen( buf ) );

	//fclose( f );
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
(             Observances                   )
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


/*
| List of file descriptor watches.
*/
static struct observance * observances = NULL;
static int observances_len             = 0;
static int observances_size            = 0;


/*
| List of file descriptors to not observe.
|
| While working for the oberver lists, it may
| not be altered, thus nonobserve stores the
| delayed removals.
*/
static int * nonobservances    = NULL;
static int nonobservances_len  = 0;
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
	void ( * ready  ) (lua_State *, struct observance * ),
	void ( * writey ) (lua_State *, struct observance * ),
	void ( * tidy   ) (struct observance * ),
	void *extra
)
{
	int pos;

	// looks if the fd is already there as pos or
	// stores the position to insert the new fd in pos
	for( pos = 0; pos < observances_len; pos++)
	{
		if( fd <= observances[ pos ].fd )
			{ break; }
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
		logstring(
			"Error",
			"New observances in ready/writey handlers not yet supported"
		);

		exit( -1 );
	}

	if( !tidy )
	{
		logstring(
			"Error",
			"internal, tidy() in observe_fd() must not be NULL."
		);
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
		(observances_len - pos) * sizeof(struct observance)
	);

	observances_len++;

	observances[ pos ].fd     = fd;
	observances[ pos ].ready  = ready;
	observances[ pos ].writey = writey;
	observances[ pos ].tidy   = tidy;
	observances[ pos ].extra  = extra;
}


/*
| Makes the core no longer watch a filedescriptor.
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
			nonobservances = s_realloc(
				nonobservances,
				nonobservances_size * sizeof( int )
			);
		}

		nonobservances[ nonobservances_len - 1 ] = fd;
		return;
	}

	// looks for the fd
	for( pos = 0; pos < observances_len; pos++ )
	{
		if( observances[ pos ].fd == fd )
			{ break; }
	}

	if( pos >= observances_len )
	{
		logstring(
			"Error",
			"internal fail, not observance file descriptor in nonobserve"
		);

		exit( -1 );
	}

	// tidies up the observance
	observances[ pos ].tidy( observances + pos );

	// and moves the list down
	memmove(
		observances + pos,
		observances + pos + 1,
		(observances_len - pos) * sizeof( struct observance )
	);

	observances_len--;
}


/*
| A user observance became read-ready.
*/
static void
user_obs_ready(
	lua_State * L,
	struct observance * obs
)
{
	int fd = obs->fd;

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
	if( lua_pcall( L, 1, 0, -3 ) )
	{
		exit( -1 );
	}

	lua_pop( L, 2 );
}


/*
| A user observance became write-ready
*/
static void
user_obs_writey(
	lua_State * L,
	struct observance * obs
)
{
	int fd = obs->fd;

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
	if( lua_pcall( L, 1, 0, -3 ) )
	{
		exit(-1);
	}

	lua_pop( L, 2 );
}

/*
| Tidies up a user observance
| FIXME - give the user a chance to do something in that case!
*/
static void
user_obs_tidy( struct observance *obs )
{
	close( obs->fd );
}


/******************************.
* Library calls for the runner *
'******************************/


static void daemonize( lua_State *L, const char *pidfile );

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
	if( priority > settings.log_level )
	{
		return 0;
	}

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
						clock_t *c = ( clock_t * )
							luaL_checkudata( L, i, "Lsyncd.jiffies" );

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
| Returns (on Lua stack) the current kernels
| clock state (jiffies)
*/
extern int
l_now(lua_State *L)
{
	clock_t * j = lua_newuserdata( L, sizeof( clock_t ) );
	luaL_getmetatable( L, "Lsyncd.jiffies" );
	lua_setmetatable( L, -2 );
	*j = times( dummy_tms );
	return 1;
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

		logstring0(
			LOG_DEBUG, "Exec",
			s
		);

		free( s );

		lua_pop( L, 1 );
	}

	if( argc >= 2 && !strcmp( luaL_checkstring( L, 2 ), "<" ) )
	{
		// pipes something into stdin
		if( !lua_isstring( L, 3 ) )
		{
			logstring(
				"Error",
				"in spawn(), expected a string after pipe '<'"
			);

			exit( -1 );
		}

		pipe_text = lua_tolstring( L, 3, &pipe_len );

		if( strlen( pipe_text ) > 0 )
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
		if( pipe_text )
		{
			dup2( pipefd[ 0 ], STDIN_FILENO );
		}

		// if lsyncd runs as a daemon and has a logfile it will redirect
		// stdout/stderr of child processes to the logfile.
		if( is_daemon && settings.log_file )
		{
			if( !freopen( settings.log_file, "a", stdout ) )
			{
				printlogf(
					L, "Error",
					"cannot redirect stdout to '%s'.",
					settings.log_file
				);
			}

			if( !freopen( settings.log_file, "a", stderr ) )
			{
				printlogf(
					L, "Error",
					"cannot redirect stderr to '%s'.",
					settings.log_file
				);
			}
		}

		execv( binary, ( char ** ) argv );

		// in a sane world execv does not return!
		printlogf(
			L, "Error",
			"Failed executing [ %s ]!",
			binary
		);

		exit( -1 );
	}

	if( pipe_text )
	{
		int len;

		// first closes read-end of pipe, this is for child process only
		close( pipefd[ 0 ] );

		// starts filling the pipe
		len = write( pipefd[ 1 ], pipe_text, pipe_len );

		if( len < 0 )
		{
			logstring( "Normal", "immediatly broken pipe." );
			close( pipefd[ 1 ] );
		}
		else if( len == pipe_len )
		{
			// usual and best case, the pipe accepted all input -> close
			close( pipefd[ 1 ] );
			logstring( "Exec", "one-sweeped pipe" );
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

			observe_fd(
				pipefd[ 1 ],
				NULL,
				pipe_writey,
				pipe_tidy,
				pm
			);
		}
	}

	free( argv );
	lua_pushnumber( L, pid );

	return 1;
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

	printlogf(
		L, "Debug",
		"total in stack %d",
		top
	);

	for( i = 1; i <= top; i++ )
	{
		int t = lua_type( L, i );
		switch( t )
		{
			case LUA_TSTRING:

				printlogf(
					L, "Debug",
					"%d string: '%s'",
					i, lua_tostring( L,  i )
				);

				break;

			case LUA_TBOOLEAN:

				printlogf(
					L, "Debug",
					"%d boolean %s",
					i, lua_toboolean( L, i ) ? "true" : "false"
				);

				break;

			case LUA_TNUMBER:

				printlogf(
					L, "Debug",
					"%d number: %g",
					i, lua_tonumber( L, i )
				);

				break;

			default:

				printlogf(
					L, "Debug",
					"%d %s",
					i, lua_typename( L, t )
				);

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
		printlogf(
			L, "Error", "cannot open dir [%s].",
			dirname
		);

		return 0;
	}

	lua_newtable( L );

	while( !hup && !term )
	{
		struct dirent *de = readdir( d );
		bool isdir;

		if( de == NULL ) // finished
		{
			break;
		}

		// ignores . and ..
		if(
			!strcmp( de->d_name, "."  )
			|| !strcmp( de->d_name, ".." )
		)
		{
			continue;
		}

		if( de->d_type == DT_UNKNOWN )
		{
			// must call stat on some systems :-/
			// ( e.g. ReiserFS )
			char *entry = s_malloc(
				strlen( dirname ) +
				strlen( de->d_name ) +
				2
			);

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
| Terminates Lsyncd.
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
		// set by runner after first initialize
		// from this on log to configurated log end instead of
		// stdout/stderr
		first_time = false;

		if( !settings.nodaemon && !settings.log_file )
		{
			settings.log_syslog = true;

			const char * log_ident =
				settings.log_ident
				? settings.log_ident
				: "lsyncd";

			openlog( log_ident, 0, settings.log_facility );
		}

		if( !settings.nodaemon && !is_daemon )
		{
			logstring( "Normal", "--- Startup, daemonizing ---" );

			daemonize( L, settings.pidfile );
		}
		else
		{
			logstring( "Normal", "--- Startup ---" );
		}

	}
	else if( !strcmp( command, "nodaemon" ) )
	{
		settings.nodaemon = true;
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
	else if( !strcmp( command, "pidfile" ) )
	{
		const char * file = luaL_checkstring( L, 2 );

		if( settings.pidfile )
		{
			free( settings.pidfile );
		}

		settings.pidfile = s_strdup( file );
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
			printlogf(
				L, "Error",
				"Logging facility must be a number or string"
			);

			exit( -1 );
		}
	}
	else if( !strcmp( command, "logident" ) )
	{
		const char * ident = luaL_checkstring( L, 2 );

		if( settings.log_ident )
		{
			free( settings.log_ident );
		}

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
	// It uses the address of the cores ready/write functions
	// for the user as key
	if( !lua_isnoneornil( L, 2 ) )
	{
		lua_pushlightuserdata( L, (void *) user_obs_ready );

		lua_gettable( L, LUA_REGISTRYINDEX );

		if( lua_isnil( L, -1 ) )
		{
			lua_pop               ( L, 1                       );
			lua_newtable          ( L                          );
			lua_pushlightuserdata ( L, (void *) user_obs_ready );
			lua_pushvalue         ( L, -2                      );
			lua_settable          ( L, LUA_REGISTRYINDEX       );
		}

		lua_pushnumber ( L, fd );
		lua_pushvalue  ( L,  2 );
		lua_settable   ( L, -3 );
		lua_pop        ( L,  1 );

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
| The Lsnycd's core library
*/
static const luaL_Reg lsyncdlib[] =
{
	{ "configure",      l_configure     },
	{ "exec",           l_exec          },
	{ "log",            l_log           },
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
| Adds a number in seconds to a jiffy timestamp.
*/
static int
l_jiffies_add( lua_State *L )
{
	clock_t *p1 = ( clock_t * ) lua_touserdata( L, 1 );
	clock_t *p2 = ( clock_t * ) lua_touserdata( L, 2 );

	if( p1 && p2 )
	{
		logstring( "Error", "Cannot add two timestamps!" );
		exit( -1 );
	}

	{
		clock_t a1  =
			p1 ? *p1 :  luaL_checknumber( L, 1 ) * clocks_per_sec;

		clock_t a2  =
			p2 ? *p2 :  luaL_checknumber( L, 2 ) * clocks_per_sec;

		clock_t *r  =
			( clock_t * ) lua_newuserdata( L, sizeof( clock_t ) );

		luaL_getmetatable( L, "Lsyncd.jiffies" );
		lua_setmetatable( L, -2 );
		*r = a1 + a2;
		return 1;
	}
}


/*
| Subracts two jiffy timestamps resulting in a number in seconds
| or substracts a jiffy by a number in seconds resulting a jiffy timestamp.
*/
static int
l_jiffies_sub( lua_State *L )
{
	clock_t *p1 = ( clock_t * ) lua_touserdata( L, 1 );
	clock_t *p2 = ( clock_t * ) lua_touserdata( L, 2 );

	if( p1 && p2 )
	{
		// substracting two timestamps result in a timespan in seconds
		clock_t a1  = *p1;
		clock_t a2  = *p2;
		lua_pushnumber(L, ((double) (a1 -a2)) / clocks_per_sec);
		return 1;
	}

	// makes a timestamp earlier by NUMBER seconds
	clock_t a1  = p1 ? *p1 :  luaL_checknumber( L, 1 ) * clocks_per_sec;
	clock_t a2  = p2 ? *p2 :  luaL_checknumber( L, 2 ) * clocks_per_sec;

	clock_t *r  = (clock_t *) lua_newuserdata( L, sizeof( clock_t ) );
	luaL_getmetatable( L, "Lsyncd.jiffies" );
	lua_setmetatable( L, -2 );

	*r = a1 - a2;

	return 1;
}


/*
| Compares two jiffy timestamps
*/
static int
l_jiffies_eq( lua_State *L )
{
	clock_t a1 = ( *( clock_t * ) luaL_checkudata( L, 1, "Lsyncd.jiffies" ) );
	clock_t a2 = ( *( clock_t * ) luaL_checkudata( L, 2, "Lsyncd.jiffies" ) );

	lua_pushboolean( L, a1 == a2 );

	return 1;
}


/*
* True if jiffy1 timestamp is eariler than jiffy2 timestamp
*/
static int
l_jiffies_lt( lua_State *L )
{
	clock_t a1 = ( *( clock_t * ) luaL_checkudata( L, 1, "Lsyncd.jiffies" ) );
	clock_t a2 = ( *( clock_t * ) luaL_checkudata( L, 2, "Lsyncd.jiffies" ) );

	lua_pushboolean( L, time_before( a1, a2 ) );

	return 1;
}


/*
| True if jiffy1 before or equals jiffy2
*/
static int
l_jiffies_le(lua_State *L)
{
	clock_t a1 = ( *( clock_t * ) luaL_checkudata( L, 1, "Lsyncd.jiffies" ) );
	clock_t a2 = ( *( clock_t * ) luaL_checkudata( L, 2, "Lsyncd.jiffies" ) );

	lua_pushboolean( L, ( a1 == a2 ) || time_before( a1, a2 ) );
	return 1;
}


/*
| Registers the Lsyncd's core library.
*/
void
register_lsyncd( lua_State *L )
{
	lua_compat_register( L, LSYNCD_LIBNAME, lsyncdlib );
	lua_setglobal( L, LSYNCD_LIBNAME );

	// creates the metatable for the jiffies ( timestamps ) userdata
	luaL_newmetatable( L, "Lsyncd.jiffies" );
	int mt = lua_gettop( L );

	lua_pushcfunction( L, l_jiffies_add );
	lua_setfield( L, mt, "__add" );

	lua_pushcfunction( L, l_jiffies_sub );
	lua_setfield( L, mt, "__sub" );

	lua_pushcfunction( L, l_jiffies_lt );
	lua_setfield( L, mt, "__lt" );

	lua_pushcfunction( L, l_jiffies_le );
	lua_setfield( L, mt, "__le" );

	lua_pushcfunction( L, l_jiffies_eq );
	lua_setfield( L, mt, "__eq" );

	lua_pop( L, 1 ); // pop(mt)

#ifdef WITH_INOTIFY

	lua_getglobal( L, LSYNCD_LIBNAME );
	register_inotify( L );
	lua_setfield( L, -2, LSYNCD_INOTIFYLIBNAME );
	lua_pop( L, 1 );

#endif

	if( lua_gettop( L ) )
	{
		logstring(
			"Error",
			"internal, stack not empty in lsyncd_register( )"
		);

		exit( -1 );
	}
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
(             Lsyncd Core                   )
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


/*
| Pushes a function from the runner on the stack.
| As well as the callError handler.
*/
extern void
load_runner_func(
	lua_State * L,
	const char * name
)
{
	printlogf( L, "Call", "%s( )", name );

	// pushes the error handler
	lua_pushlightuserdata( L, (void *) &callError );
	lua_gettable( L, LUA_REGISTRYINDEX );

	// pushes the function
	lua_pushlightuserdata( L, (void *) &runner );
	lua_gettable( L, LUA_REGISTRYINDEX );
	lua_pushstring( L, name );
	lua_gettable( L, -2 );
	lua_remove( L, -2 );
}


/*
| Daemonizes.
|
| Lsyncds own implementation over daemon(0, 0) since
|   a) OSX keeps bugging about it being deprecated
|   b) for a reason, since blindly closing stdin/out/err
|      is unsafe, since they might not have existed and
|      might actually close the monitors fd!
*/
static void
daemonize(
	lua_State *L,       // the lua state
	const char *pidfile // if not NULL write pidfile
)
{
	pid_t pid, sid;

	pid = fork( );

	if( pid < 0 )
	{
		printlogf(
			L, "Error",
			"Failure in daemonize at fork: %s",
			strerror( errno )
		);

		exit( -1 );
	}

	if( pid > 0 )
	{
		 // parent process returns to shell
	 	exit( 0 );
	}

	if( pidfile )
	{
		write_pidfile( L, pidfile );
	}

	// detaches the new process from the parent process
	sid = setsid( );

	if( sid < 0 )
	{
		printlogf(
			L, "Error",
			"Failure in daemonize at setsid: %s",
			strerror( errno )
		);

		exit( -1 );
	}

	// goes to root dir
	if( chdir( "/" ) < 0 )
	{
		printlogf(
			L, "Error",
			"Failure in daemonize at chdir( \"/\" ): %s",
			strerror( errno )
		);

		exit( -1 );
	}

	// does what clibs daemon( 0, 0 ) cannot do,
	// checks if there were no stdstreams and it might close used fds
	if( observances_len && observances->fd < 3 )
	{
		printlogf(
			L, "Normal",
			"daemonize not closing stdin/out/err, since there seem to none."
		);

		return;
	}

	// disconnects stdstreams
	if (
		!freopen( "/dev/null", "r", stdin  ) ||
		!freopen( "/dev/null", "w", stdout ) ||
		!freopen( "/dev/null", "w", stderr )
	)
	{
		printlogf(
			L, "Error",
			"Failure in daemonize at freopen( /dev/null, std[in|out|err] )"
		);
	}

	is_daemon = true;
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
		clock_t now        = times( dummy_tms );
		clock_t alarm_time = 0;

		// memory usage debugging
		// lua_gc( L, LUA_GCCOLLECT, 0 );
		// printf(
		//     "gccount: %d\n",
		//     lua_gc( L, LUA_GCCOUNT, 0 ) * 1024 + lua_gc( L, LUA_GCCOUNTB, 0 ) );

		//
		// queries the runner about the soonest alarm
		//
		load_runner_func( L, "getAlarm" );

		if( lua_pcall( L, 0, 1, -2 ) )
		{
			exit( -1 );
		}

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
			( have_alarm && time_before_eq( alarm_time, now ) )
		)
		{
			// there is a delay that wants to be handled already thus instead
			// of reading/writing from observances it jumps directly to
			// handling

			// TODO: Actually it might be smarter to handler observances
			// eitherway. since event queues might overflow.
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
				// TODO use trunc instead of long converstions
				double d   = ( (double )( alarm_time - now ) ) / clocks_per_sec;
				tv.tv_sec  = d;
				tv.tv_nsec = ( (d - ( long ) d) ) * 1000000000.0;
				printlogf(
					L, "Masterloop",
					"going into select ( timeout %f seconds )",
					d
				);
			}
			else
			{
				logstring(
					"Masterloop",
					"going into select ( no timeout )"
				);
			}

			// time for Lsyncd to try to put itself to rest into the big select( )
			// this configures:
			//    timeouts,
			//    filedescriptors and
			//    signals
			// that will wake Lsyncd
			{
				fd_set rfds;
				fd_set wfds;
				sigset_t sigset;
				int pi, pr;

				sigemptyset( &sigset );
				FD_ZERO( &rfds );
				FD_ZERO( &wfds );

				for( pi = 0; pi < observances_len; pi++ )
				{
					struct observance *obs = observances + pi;
					if ( obs->ready  )
					{
						FD_SET( obs->fd, &rfds );
					}

					if ( obs->writey )
					{
						FD_SET( obs->fd, &wfds );
					}
				}

				if( !observances_len )
				{
					logstring(
						"Error",
						"Internal fail, no observances, no monitor!"
					);

					exit( -1 );
				}

				// the great select, this is the very heart beat of Lsyncd
				// that puts Lsyncd to sleep until anything worth noticing
				// happens

				pr = pselect(
					observances[ observances_len - 1 ].fd + 1,
					&rfds,
					&wfds,
					NULL,
					have_alarm ? &tv : NULL,
					&sigset
				);

				// something happened!

				if (pr >= 0)
				{
					// walks through the observances calling ready/writey
					observance_action = true;

					for( pi = 0; pi < observances_len; pi++ )
					{
						struct observance *obs = observances + pi;

						// Checks for signals
						if( hup || term )
						{
							break;
						}

						// a file descriptor became read-ready
						if( obs->ready && FD_ISSET( obs->fd, &rfds ) )
						{
							obs->ready(L, obs);
						}

						// Checks for signals, again, better safe than sorry
						if ( hup || term )
						{
							break;
						}

						// FIXME breaks on multiple nonobservances in one beat
						if(
							nonobservances_len > 0 &&
							nonobservances[ nonobservances_len - 1 ] == obs->fd
						)
						{
							continue;
						}

						// a file descriptor became write-ready
						if( obs->writey && FD_ISSET( obs->fd, &wfds ) )
						{
							obs->writey( L, obs );
						}
					}

					observance_action = false;

					// works through delayed nonobserve_fd() calls
					for (pi = 0; pi < nonobservances_len; pi++)
					{
						nonobserve_fd( nonobservances[ pi ] );
					}

					nonobservances_len = 0;
				}
			}
		}

		// collects zombified child processes
		while( 1 )
		{
			int status;
			pid_t pid = waitpid( 0, &status, WNOHANG );

			if (pid <= 0)
			{
				// no more zombies
				break;
			}

			// calls the runner to handle the collection
			load_runner_func( L, "collectProcess" );
			lua_pushinteger( L, pid );
			lua_pushinteger( L, WEXITSTATUS( status ) );

			if ( lua_pcall( L, 2, 0, -4 ) )
				{ exit(-1); }

			lua_pop( L, 1 );
		}

		// reacts on HUP signals
		if( hup )
		{
			load_runner_func( L, "hup" );

			if( lua_pcall( L, 0, 0, -2 ) )
			{
				exit( -1 );
			}

			lua_pop( L, 1 );

			hup = 0;
		}

		// reacts on TERM and INT signals
		if( term == 1 )
		{
			load_runner_func( L, "term" );

			lua_pushnumber( L, sigcode );

			if( lua_pcall( L, 1, 0, -3 ) )
			{
				exit( -1 );
			}

			lua_pop( L, 1 );

			term = 2;
		}

		// lets the runner do stuff every cycle,
		// like starting new processes, writing the statusfile etc.
		load_runner_func( L, "cycle" );

		l_now( L );

		if( lua_pcall( L, 1, 1, -3 ) )
		{
			exit( -1 );
		}

		if( !lua_toboolean( L, -1 ) )
		{
			// cycle told core to break mainloop
			lua_pop( L, 2 );
			return;
		}
		lua_pop( L, 2 );

		if( lua_gettop( L ) )
		{
			logstring(
				"Error",
				"internal, stack is dirty."
			);
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

	// the runner file
	char * lsyncd_runner_file = NULL;

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

		if(
			sscanf(
				version,
				"Lua %d.%d",
				&major, &minor
			) != 2
		)
		{
			fprintf(
				stderr,
				"cannot parse lua library version!\n"
			);
			exit (-1 );
		}

		if(
			major < 5 ||
			(major == 5 && minor < 1)
		) {
			fprintf(
				stderr,
				"Lua library is too old. Needs 5.1 at least"
			);
			exit( -1 );
		}

		lua_pop( L, 1 );
	}

	{
		// logging is prepared quite early
		int i = 1;
		add_logcat( "Normal", LOG_NOTICE  );
		add_logcat( "Warn",   LOG_WARNING );
		add_logcat( "Error",  LOG_ERR     );

		while( i < argc )
		{
			if(
				strcmp( argv[ i ], "-log"  ) &&
				strcmp( argv[ i ], "--log" )
			)
			{
				// arg is neither -log or --log
				i++;
				continue;
			}

			if( ++i >= argc )
			{
				// -(-)log was last argument
				break;
			}

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
	register_lsyncd( L );

	if( check_logcat( "Debug" ) <= settings.log_level )
	{
		// printlogf doesnt support %ld :-(
		printf(
			"kernels clocks_per_sec=%ld\n",
			clocks_per_sec
		);
	}

	// checks if the user overrode the default runner file
	if(
		argp < argc &&
		!strcmp( argv[ argp ], "--runner" )
	)
	{
		if (argp + 1 >= argc)
		{
			logstring(
				"Error",
				"Lsyncd Lua-runner file missing after --runner "
			);

			exit( -1 );
		}

		lsyncd_runner_file = argv[ argp + 1 ];
		argp += 2;
	}

	if( lsyncd_runner_file )
	{
		// checks if the runner file exists
		struct stat st;

		if( stat( lsyncd_runner_file, &st ) )
		{
			printlogf(
				L, "Error",
				"Cannot see a runner at '%s'.",
				lsyncd_runner_file
			);
			exit( -1 );
		}

		// loads the runner file
		if( luaL_loadfile(L, lsyncd_runner_file ) )
		{
			printlogf(
				L, "Error",
				"error loading '%s': %s",
				lsyncd_runner_file,
				lua_tostring( L, -1 )
			);

			exit( -1 );
		}

	}
	else
	{
		// loads the runner from binary
		if( luaL_loadbuffer( L, runner_out, runner_size, "runner" ) )
		{
			printlogf(
				L, "Error",
				"error loading precompiled runner: %s",
				lua_tostring( L, -1 )
			);

			exit( -1 );
		}
	}

	// prepares the runner executing the script
	{
		if( lua_pcall( L, 0, LUA_MULTRET, 0 ) )
		{
			printlogf(
				L, "Error",
				"preparing runner: %s",
				lua_tostring( L, -1 )
			);

			exit( -1 );
		}

		lua_pushlightuserdata( L, (void *) & runner );

		// switches the value ( result of preparing ) and the key &runner
		lua_insert( L, 1 );

		// saves the table of the runners functions in the lua registry
		lua_settable( L, LUA_REGISTRYINDEX );

		// saves the error function extras

		// &callError is the key
		lua_pushlightuserdata ( L, (void *) &callError );

		// &runner[ callError ] the value
		lua_pushlightuserdata ( L, (void *) &runner    );
		lua_gettable          ( L, LUA_REGISTRYINDEX   );
		lua_pushstring        ( L, "callError"         );
		lua_gettable          ( L, -2                  );
		lua_remove            ( L, -2                  );

		lua_settable          ( L, LUA_REGISTRYINDEX   );
	}

	// asserts the Lsyncd's version matches
	// between runner and core
	{
		const char *lversion;

		lua_getglobal( L, "lsyncd_version" );
		lversion = luaL_checkstring( L, -1 );

		if( strcmp( lversion, PACKAGE_VERSION ) )
		{
			printlogf(
				L, "Error",
				"Version mismatch '%s' is '%s', but core is '%s'",
				lsyncd_runner_file ? lsyncd_runner_file : "( internal runner )",
				lversion, PACKAGE_VERSION
			);

			exit( -1 );
		}

		lua_pop( L, 1 );
	}

	// loads the defaults from binary
	{
		if( luaL_loadbuffer( L, defaults_out, defaults_size, "defaults" ) )
		{
			printlogf(
				L, "Error",
				"loading defaults: %s",
				lua_tostring( L, -1 )
			);

			exit( -1 );
		}

		// prepares the defaults
		if( lua_pcall( L, 0, 0, 0 ) )
		{
			printlogf(
				L, "Error",
				"preparing defaults: %s",
				lua_tostring( L, -1 )
			);
			exit( -1 );
		}
	}

	// checks if there is a "-help" or "--help"
	{
		int i;
		for( i = argp; i < argc; i++ )
		{
			if (
				!strcmp( argv[ i ],  "-help" ) ||
				!strcmp( argv[ i ], "--help" )
			)
			{
				load_runner_func( L, "help" );

				if( lua_pcall( L, 0, 0, -2 ) )
				{
					exit( -1 );
				}

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
		load_runner_func( L, "configure" );
		lua_newtable( L );

		while( argp < argc )
		{
			lua_pushnumber ( L, idx++          );
			lua_pushstring ( L, argv[ argp++ ] );
			lua_settable   ( L, -3             );
		}

		// creates a table with the cores event monitor interfaces
		idx = 0;
		lua_newtable( L );

		while( monitors[ idx ] )
		{
			lua_pushnumber ( L, idx + 1           );
			lua_pushstring ( L, monitors[ idx++ ] );
			lua_settable   ( L, -3                );
		}

		if( lua_pcall( L, 2, 1, -4 ) )
		{
			exit( -1 );
		}

		if( first_time )
		{
			// If not first time, simply retains the config file given
			s = lua_tostring(L, -1);
			if( s )
			{
				lsyncd_config_file = s_strdup( s );
			}
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
			printlogf(
				L, "Error",
				"Cannot find config file at '%s'.",
				lsyncd_config_file
			);

			exit( -1 );
		}

		free( lsyncd_config_file );
		lsyncd_config_file = apath;

		if( stat( lsyncd_config_file, &st ) )
		{
			printlogf(
				L, "Error",
				"Cannot find config file at '%s'.",
				lsyncd_config_file
			);

			exit( -1 );
		}

		// loads and executes the config file
		if( luaL_loadfile( L, lsyncd_config_file ) )
		{
			printlogf(
				L, "Error",
				"error loading %s: %s",
				lsyncd_config_file,
				lua_tostring( L, -1 )
			);

			exit( -1 );
		}

		if( lua_pcall( L, 0, LUA_MULTRET, 0) )
		{
			printlogf(
				L, "Error",
				"error preparing %s: %s",
				lsyncd_config_file,
				lua_tostring( L, -1 )
			);

			exit( -1 );
		}
	}

#ifdef WITH_INOTIFY
	open_inotify( L );
#endif

#ifdef WITH_FSEVENTS
	open_fsevents( L );
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

	// runs initializations from runner
	// it will set the configuration and add watches
	{
		load_runner_func( L, "initialize" );
		lua_pushboolean( L, first_time );

		if( lua_pcall( L, 1, 0, -3 ) )
		{
			exit( -1 );
		}

		lua_pop( L, 1 );
	}

	//
	// enters the master loop
	//
	masterloop( L );

	//
	// cleanup
	//

	// tidies up all observances
	{
		int i;
		for( i = 0; i < observances_len; i++ )
		{
			struct observance *obs = observances + i;
			obs->tidy( obs );
		}

		observances_len    = 0;
		nonobservances_len = 0;
	}

	// frees logging categories
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

			if( logcats[ci - 'A' ] )
			{
				free( logcats[ ci - 'A' ] );
				logcats[ ci - 'A' ] = NULL;
			}
		}
	}

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

	while( !term ) {
		main1( argc, argv );
	}

	if( pidfile_fd > 0 )
	{
		close( pidfile_fd );
	}

	if( settings.pidfile )
	{
		remove( settings.pidfile );
	}

	// exits with error code responding to the signal it died for
	return 128 + sigcode;
}

