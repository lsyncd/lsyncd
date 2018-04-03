/*
| log.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
|
| Logging.
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_LOG_H
#define LSYNCD_LOG_H

// Returns a logging facility number by name.
extern int log_getFacility( lua_State * L, char const * fname);

// Adds a logging category
extern bool add_logcat( char const * name, int priority );

// Returns the positive priority if name is configured to be logged, or -1
extern int check_logcat( char const * name );

// logs a string
#define logstring(cat, message) \
	{int p; if ((p = check_logcat(cat)) <= settings.log_level) \
	{logstring0(p, cat, message);}}
extern void logstring0( int priority, char const * cat, char const * message );

// logs a formated string
#define printlogf(L, cat, ...) \
	{int p; if ((p = check_logcat(cat)) <= settings.log_level)  \
	{printlogf0(L, p, cat, __VA_ARGS__);}}

extern void
printlogf0(
	lua_State *L,
	int priority,
	char const * cat,
	char const * fmt,
	...
) __attribute__ ( ( format( printf, 4, 5 ) ) );

// Frees logging stuff
extern void log_free( );


extern int l_log( lua_State * L );

#endif
