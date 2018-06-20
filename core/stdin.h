/*
| stdinh from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Reads a config file from stdin and buffers it.
|
| On every run of Lsyncd a config file from stdin is
| read only once, in case of a HUP soft reset the buffered
| version is used.
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_STDIN_H
#define LSYNCD_STDIN_H


/*
| Reads a config file from stdin.
| Or returns an already read file.
*/
extern char const * read_stdin( lua_State *L );


/*
| Lua wrapper to read_stdin( ).
*/
extern int l_stdin( lua_State *L );


#endif
