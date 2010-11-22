/** 
 * inotify.c from Lsyncd - Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * -----------------------------------------------------------------------
 *
 * Event interface for Lsyncd to Linux´ inotify.
 */

#include "lsyncd.h"

#ifndef HAVE_SYS_INOTIFY_H
#  error Missing <sys/inotify.h>; supply kernel-headers and rerun configure.
#endif

#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/**
 * The inotify file descriptor.
 */
static int inotify_fd = -1;

/**
 * TODO allow configure.
 */
static const uint32_t standard_event_mask = 
		IN_ATTRIB   | IN_CLOSE_WRITE | IN_CREATE     |
		IN_DELETE   | IN_DELETE_SELF | IN_MOVED_FROM |
		IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR;

/**
 * Adds an inotify watch
 * 
 * @param dir (Lua stack) path to directory
 * @return    (Lua stack) numeric watch descriptor
 */
static int
l_addwatch(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	lua_Integer wd = inotify_add_watch(inotify_fd, path, standard_event_mask);
	lua_pushinteger(L, wd);
	return 1;
}

/**
 * Removes an inotify watch
 * 
 * @param dir (Lua stack) numeric watch descriptor
 * @return    nil
 */
static int
l_rmwatch(lua_State *L)
{
	lua_Integer wd = luaL_checkinteger(L, 1);
	inotify_rm_watch(inotify_fd, wd);
	return 0;
}

static const luaL_reg linotfylib[] = {
		{"addwatch",   l_addwatch   },
		{"rmwatch",    l_rmwatch    },
		{NULL, NULL}
};

/**
 * Buffer for MOVE_FROM events.
 * Lsyncd buffers MOVE_FROM events to check if
 * they are followed by MOVE_TO events with identical cookie
 * then they are condensed into one move event to be sent to the
 * runner
 */
static struct inotify_event * move_event_buf = NULL;

/**
 * Memory allocated for move_event_buf
 */
static size_t move_event_buf_size = 0;

/**
 * true if the buffer is used.
 */
static bool move_event = false;

/**
 * Handles an inotify event.
 */
static void 
handle_event(lua_State *L, 
             struct inotify_event *event) 
{
	int event_type;

	/* used to execute two events in case of unmatched MOVE_FROM buffer */
	struct inotify_event *after_buf = NULL;
	if (event && (IN_Q_OVERFLOW & event->mask)) {
		/* and overflow happened, tells the runner */
		load_runner_func(L, "overflow");
		if (lua_pcall(L, 0, 0, -2)) {
			exit(-1); // ERRNO
		}
		lua_pop(L, 1);
		hup = 1;
		return;
	}
	/* cancel on ignored or resetting */
	if (event && (IN_IGNORED & event->mask)) {
		return;
	}
	if (event && event->len == 0) {
		/* sometimes inotify sends such strange events, 
		 * (e.g. when touching a dir */
		return;
	}

	if (event == NULL) {
		/* a buffered MOVE_FROM is not followed by anything, 
		   thus it is unary */
		event = move_event_buf;
		event_type = DELETE;
		move_event = false;
	} else if (move_event && 
	            ( !(IN_MOVED_TO & event->mask) || 
			      event->cookie != move_event_buf->cookie) ) {
		/* there is a MOVE_FROM event in the buffer and this is not the match
		 * continue in this function iteration to handler the buffer instead */
		after_buf = event;
		event = move_event_buf;
		event_type = DELETE;
		move_event = false;
	} else if ( move_event && 
	            (IN_MOVED_TO & event->mask) && 
			    event->cookie == move_event_buf->cookie ) {
		/* this is indeed a matched move */
		event_type = MOVE;
		move_event = false;
	} else if (IN_MOVED_FROM & event->mask) {
		/* just the MOVE_FROM, buffers this event, and wait if next event is 
		 * a matching MOVED_TO of this was an unary move out of the watched 
		 * tree. */
		size_t el = sizeof(struct inotify_event) + event->len;
		if (move_event_buf_size < el) {
			move_event_buf_size = el;
			move_event_buf = s_realloc(move_event_buf, el);
		}
		memcpy(move_event_buf, event, el);
		move_event = true;
		return;
	} else if (IN_MOVED_TO & event->mask) {
		/* must be an unary move-to */
		event_type = CREATE;
	} else if (IN_MOVED_FROM & event->mask) {
		/* must be an unary move-from */
		event_type = DELETE;
	} else if (IN_ATTRIB & event->mask) {
		/* just attrib change */
		event_type = ATTRIB;
	} else if (IN_CLOSE_WRITE & event->mask) {
		/* closed after written something */
		event_type = MODIFY;
	} else if (IN_CREATE & event->mask) {
		/* a new file */
		event_type = CREATE;
	} else if (IN_DELETE & event->mask) {
		/* rm'ed */
		event_type = DELETE;
	} else {
		logstring("Inotify", "skipped some inotify event.");
		return;
	}

	/* and hands over to runner */
	load_runner_func(L, "inotifyEvent"); 
	switch(event_type) {
	case ATTRIB : lua_pushstring(L, "Attrib"); break;
	case MODIFY : lua_pushstring(L, "Modify"); break;
	case CREATE : lua_pushstring(L, "Create"); break;
	case DELETE : lua_pushstring(L, "Delete"); break;
	case MOVE   : lua_pushstring(L, "Move");   break;
	default : 
		logstring("Error", "Internal: unknown event in handle_event()"); 
		exit(-1);	// ERRNO
	}
	if (event_type != MOVE) {
		lua_pushnumber(L, event->wd);
	} else {
		lua_pushnumber(L, move_event_buf->wd);
	}
	lua_pushboolean(L, (event->mask & IN_ISDIR) != 0);
	lua_pushinteger(L, times(NULL));
	if (event_type == MOVE) {
		lua_pushstring(L, move_event_buf->name);
		lua_pushnumber(L, event->wd);
		lua_pushstring(L, event->name);
	} else {
		lua_pushstring(L, event->name);
		lua_pushnil(L);
		lua_pushnil(L);
	}
	if (lua_pcall(L, 7, 0, -9)) {
		exit(-1); // ERRNO
	}
	lua_pop(L, 1);
	/* if there is a buffered event executes it */
	if (after_buf) {
		logstring("Inotify", "handling buffered event.");
		handle_event(L, after_buf);
	}
}

/** 
 * buffer to read inotify events into
 */
static size_t readbuf_size = 2048;
static char * readbuf = NULL;

/**
 * Called by function pointer from when the inotify file descriptor 
 * became ready. Reads it contents and forward all received events
 * to the runner.
 */
static void
inotify_ready(lua_State *L, int fd, void *extra)
{
	while(true) {
		size_t len; 
		do {
			len = read (inotify_fd, readbuf, readbuf_size);
			if (len < 0 && errno == EINVAL) {
				/* kernel > 2.6.21 indicates that way that way that
				 * the buffer was too small to fit a filename.
				 * double its size and try again. When using a lower
				 * kernel and a filename > 2KB appears lsyncd
				 * will fail. (but does a 2KB filename really happen?)
				 */
				readbuf_size *= 2;
				readbuf = s_realloc(readbuf, readbuf_size);
				continue;
			}
		} while(0);
		if (len == 0) {
			/* nothing more inotify */
			break;
		}
		if (len < 0) {
			if (errno == EAGAIN) {
				/* nothing more inotify */
				break;
			} else {
				printlogf(L, "Error", "Read fail on inotify");
				exit(-1); // ERRNO
			}
		}
		{
			int i = 0;
			while (i < len && !hup && !term) {
				struct inotify_event *event = 
					(struct inotify_event *) &readbuf[i];
				handle_event(L, event);
				i += sizeof(struct inotify_event) + event->len;
			}
		}
		if (!move_event) {
			/* give it a pause if not endangering splitting a move */
			break;
		}
	}

	/* checks if there is an unary MOVE_FROM left in the buffer */
	if (move_event) {
		logstring("Inotify", "handling unary move from.");
		handle_event(L, NULL);	
	}
}

/** 
 * registers inotify functions.
 */
extern void
register_inotify(lua_State *L) {
	lua_pushstring(L, "inotify");
	luaL_register(L, "inotify", linotfylib);
}

/** 
 * opens and initalizes inotify.
 */
extern void
open_inotify(lua_State *L) {
	if (readbuf) {
		logstring("Error", 
			"internal fail, inotify readbuf!=NULL in open_inotify()") 
		exit(-1); // ERRNO
	}
	readbuf = s_malloc(readbuf_size);

	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		printlogf(L, "Error", 
			"Cannot create inotify instance! (%d:%s)", 
			errno, strerror(errno));
		exit(-1); // ERRNO
	}

	close_exec_fd(inotify_fd);
	non_block_fd(inotify_fd);
	observe_fd(inotify_fd, inotify_ready, NULL, NULL);
}

/** 
 * closes inotify 
 */
extern void
close_inotify() {
	close(inotify_fd);
	free(readbuf);
	readbuf = NULL;
}

