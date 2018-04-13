/*
| mci.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
| The (generic) C part of the inteface between mantle and core.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_MCI_H
#define LSYNCD_MCI_H

// FIXME doc

extern int l_stackdump( lua_State* L );

extern void register_core( lua_State *L );

// Pushes a runner function and the runner error handler onto Lua stack
extern void load_mci(lua_State *L, const char *name);

extern void mci_load_mantle( lua_State *L );

extern void mci_load_default( lua_State *L );

#endif
