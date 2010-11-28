/** 
 * fsevents.c from Lsyncd - Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * -----------------------------------------------------------------------
 *
 * Interface to Linux' new filesystem monitor - fanotify.
 */
#include "lsyncd.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


#ifdef HAVE_SYS_FANOTIFY_H
#  include <sys/fanotify.h>
#else
#  warning sys/fanotify.h not found, using own syscalls as workaround.
#  include "fanotify-syscall.h"
#endif

/**
 * The fanotify file descriptor.
 */
static int fanotify_fd = -1;

/**
 * Cores fanotify functions.
 */
static const luaL_reg lfanotifylib[] = {
		{NULL, NULL}
};

/** 
 * registers fanoitfy functions.
 */
extern void
register_fanotify(lua_State *L) {
	lua_pushstring(L, "fanotify");
	luaL_register(L, "fanotify", lfanotifylib);
}

/**
 * Called by function pointer from when the inotify file descriptor 
 * became ready. Reads it contents and forward all received events
 * to the runner.
 */
static void
fanotify_ready(lua_State *L, struct observance *ob)
{
	// TODO
}

/** 
 * closes fanoitfy
 */
extern void
fanotify_tidy(struct observance *ob) {
	if (ob->fd != fanotify_fd) {
		logstring("Error", "Internal, fanotify_fd != ob->fd");
		exit(-1); // ERRNO
	}
	close(fanotify_fd);
}


/** 
 * opens and initalizes fanotify.
 */
extern void
open_fanotify(lua_State *L) {
	fanotify_fd = fanotify_init(O_CLOEXEC | O_NONBLOCK, O_RDONLY);
	if (fanotify_fd < 0) {
		printlogf(L, "Error", 
			"Cannot access fanotify monitor! (%d:%s)", 
			errno, strerror(errno));
		exit(-1); // ERRNO
	}

	observe_fd(fanotify_fd, fanotify_ready, NULL, fanotify_tidy, NULL);
}

