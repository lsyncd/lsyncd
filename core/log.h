/*
| log.h from Lsyncd - Live (Mirror) Syncing Demon
|
|
| Logging
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_LOG_H
#define LSYNCD_LOG_H

// Adds a logging category
extern bool add_logcat( const char *name, int priority );

// Returns the positive priority if name is configured to be logged, or -1
extern int check_logcat( const char *name );

// logs a string
#define logstring(cat, message) \
	{int p; if ((p = check_logcat(cat)) <= settings.log_level) \
	{logstring0(p, cat, message);}}
extern void logstring0( int priority, const char *cat, const char *message );

// logs a formated string
#define printlogf(L, cat, ...) \
	{int p; if ((p = check_logcat(cat)) <= settings.log_level)  \
	{printlogf0(L, p, cat, __VA_ARGS__);}}

extern void
printlogf0(
	lua_State *L,
	int priority,
	  const char *cat,
	  const char *fmt,
	  ...
) __attribute__ ( ( format( printf, 4, 5 ) ) );

// Frees logging stuff
extern void log_free( );

#endif
