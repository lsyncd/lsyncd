/*
| util.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
|
| Small commonly used utils by Lsyncd.
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_UTIL_H
#define LSYNCD_UTIL_H

// Returns the absolute path of a path.
// This is a wrapper to various C-Library differences.
extern char * get_realpath( char const * rpath );

// Sets the non-blocking flag on a file descriptor.
extern void non_block_fd( int fd );

// Sets the close-on-exit flag on a file descriptor.
extern void close_exec_fd( int fd );

#endif
