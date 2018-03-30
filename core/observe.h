/*
| observe.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
|
| Handles observing file descriptors and the big select.
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_OBSERVE_H
#define LSYNCD_OBSERVE_H

// Sets the non-blocking flag for a file descriptor.
extern void non_block_fd(int fd);

// Sets the close-on-exit flag for a file descriptor.
extern void close_exec_fd(int fd);

// makes the core observe a file descriptor
extern void observe_fd(
	int fd,
	void (*ready) ( lua_State *, int fd, void * extra ),
	void (*writey)( lua_State *, int fd, void * extra ),
	void (*tidy)  ( int fd, void * extra ),
	void *extra
);

// makes the big select for all observed fds
extern void observe_select( lua_State * L, struct timespec const * timeout );

// tidies up all observances
extern void observe_tidy_all( );

// stops the core to observe a file descriptor
extern void nonobserve_fd( int fd );

#endif
