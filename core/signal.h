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


// Initializes signal handling.
extern void signal_init( );

#endif
