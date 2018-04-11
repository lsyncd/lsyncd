/*
| inotify.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Event interface for Lsyncd to Linux´ inotify.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_INOTIFY_H
#define LSYNCD_INOTIFY_H

#ifndef WITH_INOTIFY
#	error "Do not include inotify.h when not configured to use inotify."
#endif

extern void register_inotify(lua_State *L);
extern void open_inotify(lua_State *L);

#endif
