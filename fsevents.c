/** fsevents.c from Lsyncd - Live (Mirror) Syncing Demon
 *
 * License: GPLv2 (see COPYING) or any later version
 *
 * Authors: Axel Kittenberger <axkibe@gmail.com>
 *          Damian Steward <damian.stewart@gmail.com>
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
    struct kfs_event_arg* args[FSE_MAX_ARGS];
};

/**
 * fsevents (cloned) filedescriptor
 */
static int fsevents_fd = -1;

/* event names */
/*static const char *eventNames[FSE_MAX_EVENTS] = {
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
};*/

/* argument names*/
/*static const char *argNames[] = {
	"UNKNOWN",
	"VNODE",
	"STRING",
	"PATH",
	"INT32",
	"INT64",
	"RAW",
	"INO",
	"UID",
	"DEV",
	"MODE",
	"GID",
	"FINFO",
};*/

/**
 * The read buffer
 */
static size_t const readbuf_size = 131072;
static char * readbuf = NULL;

/**
 * The event buffer
 */
static size_t const eventbuf_size = FSEVENT_BUFSIZ;
static char* eventbuf = NULL;

/**
 * Handles one fsevents event
 */
static void
handle_event(lua_State *L, struct kfs_event *event, ssize_t mlen)
{
	int32_t atype;
	const char *path = NULL;
	const char *trg  = NULL;
	const char *etype = NULL;
	int isdir = -1;

	if (event->type == FSE_EVENTS_DROPPED) {
		logstring("Fsevents", "Events dropped!");
		load_runner_func(L, "overflow");
		if (lua_pcall(L, 0, 0, -2)) {
			exit(-1); // ERRNO
		}
		lua_pop(L, 1);
		hup = 1;
		return;
	}

	atype  = event->type & FSE_TYPE_MASK;
	/*uint32_t aflags = FSE_GET_FLAGS(event->type);*/

	if ((atype < FSE_MAX_EVENTS) && (atype >= -1)) {
		/*printlogf(L, "Fsevents", "got event %s", eventNames[atype]);
		if (aflags & FSE_COMBINED_EVENTS) {
			logstring("Fsevents", "combined events");
		}
		if (aflags & FSE_CONTAINS_DROPPED_EVENTS) {
			logstring("Fsevents", "contains dropped events");
		}*/
	} else {
		printlogf(
			L,
			"Error",
			"unknown event(%d) in fsevents.",
			atype
		);

		exit(-1); // ERRNO
	}

	{
		/* assigns the expected arguments */
		int whichArg = 0;
		while (whichArg < FSE_MAX_ARGS) {
			struct kfs_event_arg * arg = event->args[whichArg++];
			if (arg->type == FSE_ARG_DONE) {
				break;
			}

			switch (arg->type) {
			case FSE_ARG_STRING :
				switch(atype) {
				case FSE_RENAME :
					if (path) {
						// for move events second string is target
						trg = (char *) &arg->data.str;
					}
					// fallthrough
				case FSE_CHOWN :
				case FSE_CONTENT_MODIFIED :
				case FSE_CREATE_FILE :
				case FSE_CREATE_DIR :
				case FSE_DELETE :
				case FSE_STAT_CHANGED :
					if (!path) path = (char *)&arg->data.str;
					break;
				}
				break;
			case FSE_ARG_MODE :
				switch(atype) {
				case FSE_RENAME :
				case FSE_CHOWN :
				case FSE_CONTENT_MODIFIED :
				case FSE_CREATE_FILE :
				case FSE_CREATE_DIR :
				case FSE_DELETE :
				case FSE_STAT_CHANGED :
					isdir = arg->data.mode & S_IFDIR ? 1 : 0;
					break;
				}
				break;
			}
		}
	}

	switch(atype) {
	case FSE_CHOWN :
	case FSE_STAT_CHANGED :
		etype = "Attrib";
		break;
	case FSE_CREATE_DIR :
	case FSE_CREATE_FILE :
		etype = "Create";
		break;
	case FSE_DELETE :
		etype = "Delete";
		break;
	case FSE_RENAME :
		etype = "Move";
		break;
	case FSE_CONTENT_MODIFIED :
		etype = "Modify";
		break;
	}

	if (etype) {
		if (!path) {
			printlogf(L, "Error", "Internal fail, fsevents, no path.");
			exit(-1);
		}
		if (isdir < 0) {
			printlogf(L, "Error", "Internal fail, fsevents, neither dir nor file.");
			exit(-1);
		}
		load_runner_func(L, "fsEventsEvent");
		lua_pushstring(L, etype);
		lua_pushboolean(L, isdir);
		l_now(L);
		lua_pushstring(L, path);
		if (trg) {
			lua_pushstring(L, trg);
		} else {
			lua_pushnil(L);
		}

   	 	if (lua_pcall(L, 5, 0, -7)) {
			exit(-1); // ERRNO
		}
		lua_pop(L, 1);
	}
}

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

	ptrdiff_t len = read (fsevents_fd, readbuf, readbuf_size);
	int err = errno;
	if (len == 0) {
		return;
	}
	if (len < 0) {
		if (err == EAGAIN) {
			/* nothing more */
			return;
		} else {
			printlogf(L, "Error", "Read fail on fsevents");
			exit(-1); // ERRNO
		}
	}
	{
		int off = 0;
		while (off < len && !hup && !term) {
			/* deals with alignment issues on 64 bit by copying data bit by bit */
			struct kfs_event* event = (struct kfs_event *) eventbuf;
			event->type = *(int32_t*)(readbuf+off);
			off += sizeof(int32_t);
			event->pid = *(pid_t*)(readbuf+off);
			off += sizeof(pid_t);
			/* arguments */
			int whichArg = 0;
			int eventbufOff = sizeof(struct kfs_event);
			size_t ptrSize = sizeof(void*);
			if ((eventbufOff % ptrSize) != 0) {
				eventbufOff += ptrSize-(eventbufOff%ptrSize);
			}
			while (off < len && whichArg < FSE_MAX_ARGS) {
				/* assign argument pointer to eventbuf based on 
				   known current offset into eventbuf */
				uint16_t argLen = 0;
				event->args[whichArg] = (struct kfs_event_arg *) (eventbuf + eventbufOff);
				/* copy type */
				uint16_t argType = *(uint16_t*)(readbuf + off);
				event->args[whichArg]->type = argType;
				off += sizeof(uint16_t);
				if (argType == FSE_ARG_DONE) {
					/* done */
					break;
				} else {
					/* copy data length */
					argLen = *(uint16_t *)(readbuf + off);
					event->args[whichArg]->len = argLen;
					off += sizeof(uint16_t);
					/* copy data */
					memcpy(&(event->args[whichArg]->data), readbuf + off, argLen);
					  off += argLen;
				}
				/* makes sure alignment is correct for 64 bit systems */
				size_t argStructLen = sizeof(uint16_t) + sizeof(uint16_t);
				if ((argStructLen % ptrSize) != 0) {
					argStructLen += ptrSize-(argStructLen % ptrSize);
				}
				argStructLen += argLen;
				if ((argStructLen % ptrSize) != 0) {
					argStructLen += ptrSize-(argStructLen % ptrSize);
				}
				eventbufOff += argStructLen;
				whichArg++;
			}
			handle_event(L, event, len);
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
	free(eventbuf);
	eventbuf = NULL;
}

/**
 * opens and initalizes fsevents.
 */
extern void
open_fsevents(lua_State *L)
{
	int8_t event_list[] = { // action to take for each event
		FSE_REPORT,  // FSE_CREATE_FILE
		FSE_REPORT,  // FSE_DELETE
		FSE_REPORT,  // FSE_STAT_CHANGED
		FSE_REPORT,  // FSE_RENAME
		FSE_REPORT,  // FSE_CONTENT_MODIFIED
		FSE_REPORT,  // FSE_EXCHANGE
		FSE_REPORT,  // FSE_FINDER_INFO_CHANGED
		FSE_REPORT,  // FSE_CREATE_DIR
		FSE_REPORT,  // FSE_CHOWN
		FSE_REPORT,  // FSE_XATTR_MODIFIED
		FSE_REPORT,  // FSE_XATTR_REMOVED
	};
	struct fsevent_clone_args fca = {
		.event_list = (int8_t *) event_list,
		.num_events = sizeof(event_list)/sizeof(int8_t),
		.event_queue_depth = EVENT_QUEUE_SIZE,
		.fd = &fsevents_fd,
	};
	int fd = open(DEV_FSEVENTS, O_RDONLY);
	int err = errno;
	printlogf(L, "Warn",
		"Using /dev/fsevents which is considered an OSX internal interface.");
	printlogf(L, "Warn",
		"Functionality might break across OSX versions (This is for 10.5.X)");
	printlogf(L, "Warn",
		"A hanging Lsyncd might cause Spotlight/Timemachine doing extra work.");

	if (fd < 0) {
		printlogf(L, "Error",
			"Cannot access %s monitor! (%d:%s)",
			DEV_FSEVENTS, err, strerror(err));
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
	eventbuf = s_malloc(eventbuf_size);
	// fd has been cloned, closes access fd
	close(fd);
	close_exec_fd(fsevents_fd);
	non_block_fd(fsevents_fd);
	observe_fd(fsevents_fd, fsevents_ready, NULL, fsevents_tidy, NULL);
}


