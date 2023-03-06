/*
| kqueue.c from Lsyncd - Live (Mirror) Syncing Demon
|
| License: GPLv2 (see COPYING) or any later version
|
| Authors: Youcai <omegacoleman@gmail.com>
|
| -----------------------------------------------------------------------
|
| Event interface for Lsyncd to BSD/MacOS's kqueue.
*/

#include "lsyncd.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#ifdef O_EVTONLY
#define LSYNC_OPEN_FOR_EVT O_EVTONLY
#else
#define LSYNC_OPEN_FOR_EVT O_RDONLY
#endif

#ifdef O_SYMLINK
#define LSYNC_OPEN_SYMLINK O_SYMLINK
#else
#define LSYNC_OPEN_SYMLINK 0
#endif

#define LSYNC_OPEN_FLAGS (LSYNC_OPEN_FOR_EVT | LSYNC_OPEN_SYMLINK)

static const uint32_t NUM_WATCH_ENTRY_MAX = 2048;

static int kqueue_fd = -1;
static int in_after_fork = 0;

static const unsigned int vnode_events = NOTE_DELETE | NOTE_EXTEND | NOTE_RENAME | NOTE_WRITE | NOTE_ATTRIB | NOTE_LINK | NOTE_REVOKE;

struct kqueue_watch_entry {
	int fd;
	char *path;
	bool is_dir;
	int refcnt;
	struct kqueue_watch_entry* prev;
	struct kqueue_watch_entry* next;
};

struct kqueue_watch_entry* kqueue_watch_list_head;
struct kqueue_watch_entry* kqueue_watch_list_tail;

static void assign_kqueue_watch_entry(lua_State* L, struct kqueue_watch_entry *ent) {
	struct kevent change;
retry:
	EV_SET( &change, ent->fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR, vnode_events, 0, ent );
	int ret = kevent( kqueue_fd, &change, 1, 0, 0, 0 );
	if ( ret == -1 ) {
		if ( errno == EINTR ) {
			goto retry;
		}
		printlogf(
			L,

			"Error",
			"Failed to add kqueue watch entry ( %d : %s )",
			errno, strerror(errno)
		);
		exit( -1 );
	}
}

static struct kqueue_watch_entry *add_kqueue_watch_entry(lua_State* L, const char* path, bool is_dir) {
	struct kqueue_watch_entry *ent = (struct kqueue_watch_entry *) malloc(sizeof(struct kqueue_watch_entry));
	ent->prev = kqueue_watch_list_head;
	ent->next = kqueue_watch_list_head->next;
	ent->prev->next = ent;
	ent->next->prev = ent;
	ent->refcnt = 1;

	ent->fd = open( path, LSYNC_OPEN_FLAGS );

	if ( ent->fd == -1 ) {
		printlogf(
			L,
			"Error",
			"Failed to open file %s for watching ( %d : %s )",
			path, errno, strerror(errno)
		);
		goto err;
	}
	ent->path = strdup(path);
	ent->is_dir = is_dir;
	assign_kqueue_watch_entry(L, ent);
	return ent;
err:
	free( ent );
	return NULL;
}

static void remove_kqueue_watch_entry(struct kqueue_watch_entry *ent) {
	ent->refcnt --;
	if (ent->refcnt > 0) {
		return;
	}
	ent->prev->next = ent->next;
	ent->next->prev = ent->prev;

	(void)close(ent->fd); // this will assume EV_DELETE
	free(ent->path);
	free(ent);
}

/*
| Adds an kqueue watch
|
| param path        (Lua stack) path to watch
| param isdir       (Lua stack) is path a dir
|
| returns           (Lua stack) watch descriptor
*/
static int
l_addwatch( lua_State *L )
{
	const char *path  = luaL_checkstring( L, 1 );
	bool is_dir = lua_toboolean( L, 2 );
	struct kqueue_watch_entry *ent = add_kqueue_watch_entry( L, path, is_dir );
	lua_pushinteger( L, (intptr_t)ent );
	return 1;
}

/*
* Removes an kqueue watch.
*
* param wd (Lua stack) watch descriptor
*
* return    nil
*/
static int
l_rmwatch( lua_State *L )
{
	intptr_t wd = luaL_checkinteger( L, 1 );
	remove_kqueue_watch_entry( (struct kqueue_watch_entry *)wd );
	return 0;
}

static void trigger_callback(lua_State *L, intptr_t wd, const char* event, bool isdir, bool reused) {
	load_runner_func( L, "kqueueEvent" );
	lua_pushstring( L, event );
	lua_pushinteger( L, wd );
	l_now( L );
	lua_pushboolean( L, isdir );
	lua_pushboolean( L, reused );
	if( lua_pcall( L, 5, 0, -7 ) ) exit( -1 );
	lua_pop( L, 1 );
}

/*
| Handles an kqueue event.
*/
static void
handle_event(
	lua_State *L,
	struct kevent *event
)
{
	struct kqueue_watch_entry *ent = (struct kqueue_watch_entry *)event->udata;
	bool reused = true;

	if ( event->fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE) ) {
		reused = false;
	}
	if ( (event->fflags & (NOTE_WRITE | NOTE_LINK)) && ent->is_dir ) {
		reused = false;
	}

	char ev_s[256] = "";

#define EV_NOTE_TO_CB(__ev, __ev_s) \
	if ( event->fflags & __ev ) { \
		strncat( ev_s, ";" __ev_s, sizeof(__ev_s) + 1 ); \
	}

	EV_NOTE_TO_CB( NOTE_DELETE, "Delete" );
	EV_NOTE_TO_CB( NOTE_EXTEND, "Extend" );
	EV_NOTE_TO_CB( NOTE_RENAME, "Rename" );
	EV_NOTE_TO_CB( NOTE_WRITE, "Write" );
	EV_NOTE_TO_CB( NOTE_ATTRIB, "Attrib" );
	EV_NOTE_TO_CB( NOTE_LINK, "Link" );
	EV_NOTE_TO_CB( NOTE_REVOKE, "Revoke" );

#undef EV_NOTE_TO_CB

	trigger_callback( L, (intptr_t) ent, ev_s + 1, ent->is_dir, reused );

	if (reused) {
		assign_kqueue_watch_entry(L, ent);
	}
}

static const luaL_Reg lkqueuelib[ ] =
{
	{ "addwatch",   l_addwatch   },
	{ "rmwatch",    l_rmwatch    },
	{ NULL, NULL}
};

extern void
register_kqueue( lua_State *L )
{
	lua_compat_register( L, LSYNCD_KQUEUELIBNAME, lkqueuelib );
}

static void
kqueue_ready(
	lua_State *L,
	struct observance *obs
)
{
	// sanity check
	if( obs->fd != kqueue_fd )
	{
		logstring( "Error", "internal failure, obs->fd != kqueue_fd" );
		exit( -1 );
	}

	// read all events, call handle_event
	struct kevent ev_list[1024];
	int count;
	struct timespec no_block;
	no_block.tv_sec = 0;
	no_block.tv_nsec = 0;
retry:
	count = kevent(kqueue_fd, 0, 0, ev_list, 1024, &no_block);
	if (count == -1) {
		if (errno == EINTR) {
			goto retry;
		}
		printlogf(
			L,
			"Error",
			"Failed to retrieve kevents ( %d : %s )",
			errno, strerror(errno)
		);
		exit( -1 );
	}
	if (count == 0) {
		return;
	}
	for (int i = 0; i < count; i++) { // hold a ref of associated entries, preventing them from being recycled amid following loop
		struct kqueue_watch_entry *ent = (struct kqueue_watch_entry *)ev_list[i].udata;
		ent->refcnt ++;
	}
	for (int i = 0; i < count; i++) {
		handle_event(L, &ev_list[i]);
	}
	for (int i = 0; i < count; i++) {
		struct kqueue_watch_entry *ent = (struct kqueue_watch_entry *)ev_list[i].udata;
		remove_kqueue_watch_entry(ent);
	}
	goto retry; // loop until kevent returns zero
}

static void
kqueue_tidy(
	struct observance *obs
)
{
	if ( in_after_fork ) {
		return;
	}

	// sanity check
	if( obs->fd != kqueue_fd )
	{
		logstring( "Error", "internal failure, obs->fd != kqueue_fd" );
		exit( -1 );
	}

	for ( struct kqueue_watch_entry *ent = kqueue_watch_list_head->next; ent != kqueue_watch_list_tail; ent = ent->next ) {
		(void)close( ent->fd );
		free( ent->prev );
	}
	free( kqueue_watch_list_tail );
	kqueue_watch_list_head = NULL;
	kqueue_watch_list_tail = NULL;
	(void)close( kqueue_fd );
}

extern void
kqueue_after_fork( lua_State *L )
{
	in_after_fork = 1;
#ifndef HAS_RFORK
	nonobserve_fd(kqueue_fd); // child process don't get to share the queue
	kqueue_fd = kqueue( );

	if( kqueue_fd < 0 )
	{
		printlogf(
			L,
			"Error",
			"Cannot open kqueue ( %d : %s )",
			errno, strerror(errno)
		);
		exit( -1 );
	}

	printlogf(
		L, "Kqueue",
		"Kqueue(forked) fd = %d",
		kqueue_fd
	);

	// re-assign entrys to the new queue
	for ( struct kqueue_watch_entry *ent = kqueue_watch_list_head->next; ent != kqueue_watch_list_tail; ent = ent->next ) {
		assign_kqueue_watch_entry( L, ent );
	}

	close_exec_fd( kqueue_fd );
	observe_fd( kqueue_fd, kqueue_ready, NULL, kqueue_tidy, NULL );
#endif
	in_after_fork = 0;
}

extern void
open_kqueue( lua_State *L )
{
	kqueue_fd = kqueue( );

	if( kqueue_fd < 0 )
	{
		printlogf(
			L,
			"Error",
			"Cannot open kqueue ( %d : %s )",
			errno, strerror(errno)
		);
		exit( -1 );
	}

	printlogf(
		L, "Kqueue",
		"Kqueue fd = %d",
		kqueue_fd
	);

	kqueue_watch_list_head = (struct kqueue_watch_entry *) malloc( sizeof(struct kqueue_watch_entry) );
	kqueue_watch_list_tail = (struct kqueue_watch_entry *) malloc( sizeof(struct kqueue_watch_entry) );
	kqueue_watch_list_head->next = kqueue_watch_list_tail;
	kqueue_watch_list_tail->prev = kqueue_watch_list_head;
	kqueue_watch_list_head->prev = NULL;
	kqueue_watch_list_tail->next = NULL;
	kqueue_watch_list_head->fd = -1;
	kqueue_watch_list_tail->fd = -1;

	close_exec_fd( kqueue_fd );
	observe_fd( kqueue_fd, kqueue_ready, NULL, kqueue_tidy, NULL );
}


