/*
| time.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Time keeping.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_TIME_H
#define LSYNCD_TIME_H

// time comparisons - wrap around safe
#define time_after(a,b)         ((long)(b) - (long)(a) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)


// Initializes time management.
extern void time_first_init( );


// Returns the current time.
extern clock_t now( );


// Puts the time difference between 't2' (later) and 't1' into 'tv'
extern double time_diff( long t2, long t1, struct timespec * tv );


// Returns (on Lua stack) the current kernels clock state( jiffies ).
extern int l_now( lua_State *L );


// Registers the jiffies meta table in a Lua state.
extern void register_jiffies( lua_State *L );


// Checks if the function argument 'arg' on Lua stack is a jiffie
// and returns the value converted to seconds.
extern double check_jiffies_arg ( lua_State *L, int arg );


#endif
