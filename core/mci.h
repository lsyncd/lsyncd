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

int l_stackdump( lua_State* L );

void register_core( lua_State *L );

void mci_load_mantle( lua_State *L );

void mci_load_default( lua_State *L );

#endif
