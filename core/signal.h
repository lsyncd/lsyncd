/*
| signal.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Logging.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_SIGNAL_H
#define LSYNCD_SIGNAL_H

// set to 1 on hup signal or term signal
extern volatile sig_atomic_t hup;
extern volatile sig_atomic_t term;
extern volatile sig_atomic_t sigcode;


// initializes signal handling.
extern void signal_init( );

// registers a signal handler
int l_onsignal( lua_State *L );

// sends a signal
int l_kill( lua_State *L );

// notifies the mantle about signals
extern void signal_notify( lua_State *L );

#endif
