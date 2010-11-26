/** 
 * fsevents.c from Lsyncd - Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * -----------------------------------------------------------------------
 *
 * Event interface for MacOS 10(.5) /dev/fsevents interface.
 *
 * WARNING! AFAIK this interface is not strictly considered "public" API 
 * by Apple. Thus it might easily change between versions. Also its said,
 * altough every event receiver has its own message queue, the OS X kernel
 * only deletes a message after *all* registered receivers handled it. So
 * one receiver blocking overflows all receivers. So spotlight might have 
 * to do more stuff, when Lsyncd might cause an overflow. Use at own risk. 
 *
 * Special thanks go to Amit Singh and his fslogger demonstration that
 * showed how apples /dev/fsevents can be used.
 * http://osxbook.com/software/fslogger/
 */

#include "lsyncd.h"

/** 
 * registers fsevents functions.
 */
extern void
register_fsevents(lua_State *L) {
	// TODO
}

/** 
 * opens and initalizes fsevents.
 */
extern void
open_fsevents(lua_State *L) {
	// TODO
}

/** 
 * closes fsevents
 */
extern void
close_fsevents() {
	// TODO
}

