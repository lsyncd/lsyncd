/*
| userobs.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
|
| Allows user Lua scripts to observe file descriptors.
|
| They have to be opened by some other utility tough,
| for example lua-posix.
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_USEROBS_H
#define LSYNCD_USEROBS_H

// Allows user scripts to observe filedescriptors.
// To be called from Lua.
extern int l_observe_fd( lua_State *L );

// Removes a user observance.
// To be called from Lua.
extern int l_nonobserve_fd( lua_State *L );

#endif
