/** fsevents.c from Lsyncd - Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *
 * -----------------------------------------------------------------------
 *
 * Event interface for MacOS 10.5 (Leopard) /dev/fsevents interface.
 *
 * Special thanks go to Amit Singh and his fslogger demonstration that showed
 * how apples /dev/fsevents can be used.  http://osxbook.com/software/fslogger/
 *
 * -- WARNING -- Quoting http://www.osxbook.com/software/fslogger/ --
 *
 * The interface that fslogger [and thus Lsyncd] uses is private to Apple.
 * Currently, there is a caveat regarding the use of this interface by third
 * parties (including fslogger [and thus Lsyncd]). While the change
 * notification interface supports multiple clients, there is a single kernel
 * buffer for holding events that are to be delivered to one or more
 * subscribers, with the primary subscriber being Spotlight. Now, the kernel
 * must hold events until it has notified all subscribers that are interested
 * in them. Since there is a single buffer, a slow subscriber can cause it to
 * overflow. If this happens, events will be dropped — for all subscribers,
 * including Spotlight.  Consequently, Spotlight may need to look at the entire
 * volume to determine "what changed". 
 */

#include "lsyncd.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bsd/sys/fsevents.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


/* the fsevents pseudo-device */
#define DEV_FSEVENTS     "/dev/fsevents"

/* buffer for reading from the device */
#define FSEVENT_BUFSIZ   131072 
/* limited by MAX_KFS_EVENTS */
#define EVENT_QUEUE_SIZE 4096  
#define KFS_NUM_ARGS  FSE_MAX_ARGS

/* OS 10.5 structuce */
/* an event argument */
struct kfs_event_arg {
	/* argument type */
    u_int16_t  type; 

    /* size of argument data that follows this field */
    u_int16_t  len;
 
    union {
        struct vnode *vp;
        char    *str;
        void    *ptr;
        int32_t  int32;
        dev_t    dev;
        ino_t    ino;
        int32_t  mode;
        uid_t    uid;
        gid_t    gid;
        uint64_t timestamp;
    } data;
};

/* OS 10.5 structuce */
/* an event */
struct kfs_event {

	/* event type */
    int32_t  type;

	/* pid of the process that performed the operation */
    pid_t    pid;

	/* event arguments */
    struct kfs_event_arg args[KFS_NUM_ARGS]; 
};

/**
 * fsevents (cloned) filedescriptor
 */
static int fsevents_fd = -1;

static const luaL_reg lfseventslib[] = {
		{NULL, NULL}
};

// event names
static const char *eventNames[FSE_MAX_EVENTS] = {
	"CREATE_FILE",
	"DELETE",
	"STAT_CHANGED",
	"RENAME",
	"CONTENT_MODIFIED",
	"EXCHANGE",
	"FINDER_INFO_CHANGED",
	"CREATE_DIR",
	"CHOWN",
	"XATTR_MODIFIED",
	"XATTR_REMOVED",
};

static size_t const readbuf_size = 131072;
static char * readbuf = NULL;

/**
 * Called when fsevents has something to read 
 */
static void
fsevents_ready(lua_State *L, struct observance *obs)
{
	if (obs->fd != fsevents_fd) {
		logstring("Error", "Internal, fsevents_fd != ob->fd");
		exit(-1); // ERRNO
	}
	while(true) {
		ptrdiff_t len; 
		int err;
		len = read (fsevents_fd, readbuf, readbuf_size);
		err = errno;
		if (len == 0) {
			/* nothing more */
			break;
		}
		if (len < 0) {
			if (err == EAGAIN) {
				/* nothing more inotify */
				break;
			} else {
				printlogf(L, "Error", "Read fail on fsevents");
				exit(-1); // ERRNO
			}
		}
		{
			int off = 0;
			int32_t atype;
			uint32_t aflags;

			while (off < len && !hup && !term) {
				struct kfs_event *event = (struct kfs_event *) &readbuf[off];
				off += sizeof(int32_t) + sizeof(pid_t);

				if (event->type == FSE_EVENTS_DROPPED) {
					logstring("Fsevents", "Events dropped!");
       				load_runner_func(L, "overflow");
        			if (lua_pcall(L, 0, 0, -2)) {
            			exit(-1); // ERRNO
        			}
        			lua_pop(L, 1);
        			hup = 1;
					off += sizeof(u_int16_t);
					continue;
				}
				atype  = event->type & FSE_TYPE_MASK;
				aflags = FSE_GET_FLAGS(event->type);

				if ((atype < FSE_MAX_EVENTS) && (atype >= -1)) {
					printlogf(L, "Fsevents", "got event %s", eventNames[atype]);
					if (aflags & FSE_COMBINED_EVENTS) {
						logstring("Fsevents", "combined events");
					}
					if (aflags & FSE_CONTAINS_DROPPED_EVENTS) {
						logstring("Fsevents", "contains dropped events");
					}
				} else {
					printlogf(L, "Error", "unknown event(%d) in fsevents.", 
						atype);
					exit(-1); // ERRNO
				}
			}
		}
	}
}

/**
 * Called to close/tidy fsevents 
 */
static void
fsevents_tidy(struct observance *obs)
{
	if (obs->fd != fsevents_fd) {
		logstring("Error", "Internal, fsevents_fd != ob->fd");
		exit(-1); // ERRNO
	}
	close(fsevents_fd);
	free(readbuf);
	readbuf = NULL;
}


/** 
 * registers fsevents functions.
 */
extern void
register_fsevents(lua_State *L) {
	lua_pushstring(L, "fsevents");
	luaL_register(L, "fsevents", lfseventslib);
}

/** 
 * opens and initalizes fsevents.
 */
extern void
open_fsevents(lua_State *L) 
{
	return;

	int8_t event_list[] = { // action to take for each event
    	FSE_REPORT,  /* FSE_CREATE_FILE         */
		FSE_REPORT,  /* FSE_DELETE              */
		FSE_REPORT,  /* FSE_STAT_CHANGED        */
		FSE_REPORT,  /* FSE_RENAME              */
		FSE_REPORT,  /* FSE_CONTENT_MODIFIED    */
		FSE_REPORT,  /* FSE_EXCHANGE            */
		FSE_REPORT,  /* FSE_FINDER_INFO_CHANGED */
		FSE_REPORT,  /* FSE_CREATE_DIR          */
		FSE_REPORT,  /* FSE_CHOWN               */
		FSE_REPORT,  /* FSE_XATTR_MODIFIED      */
		FSE_REPORT,  /* FSE_XATTR_REMOVED       */
	};
	struct fsevent_clone_args fca = {
		.event_list = (int8_t *) event_list,
		.num_events = sizeof(event_list)/sizeof(int8_t),
		.event_queue_depth = EVENT_QUEUE_SIZE,
		.fd = &fsevents_fd,
	};
	int fd = open(DEV_FSEVENTS, O_RDONLY);
	if (fd < 0) {
		printlogf(L, "Error", 
			"Cannot access %s monitor! (%d:%s)", 
			DEV_FSEVENTS, errno, strerror(errno));
		exit(-1); // ERRNO
	}

    if (ioctl(fd, FSEVENTS_CLONE, (char *)&fca) < 0) {
		printlogf(L, "Error", 
			"Cannot control %s monitor! (%d:%s)", 
			DEV_FSEVENTS, errno, strerror(errno));
        exit(-1); // ERRNO
    }
	
	if (readbuf) {
		logstring("Error", 
			"internal fail, inotify readbuf!=NULL in open_inotify()") 
		exit(-1); // ERRNO
	}
	readbuf = s_malloc(readbuf_size);

	/* fd has been cloned, closes access fd */
    close(fd);
	close_exec_fd(fsevents_fd);
	non_block_fd(fsevents_fd);
	observe_fd(fsevents_fd, fsevents_ready, NULL, fsevents_tidy, NULL);
}


