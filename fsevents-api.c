/*
| fsevent-api.c from Lsyncd - Live (Mirror) Syncing Demon
|
| License: GPLv2 (see COPYING) or any later version
|
| Authors: David Gauchard <gauchard@laas.fr>
|
| inspired from https://github.com/ggreer/fsevents-tools
|
| -----------------------------------------------------------------------
|
| Event interface for Lsyncd to OSX´ fsevents
*/

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <pthread.h>

#include "fsevents-api.h"

static CFMutableArrayRef paths = NULL;
static FSEventStreamRef stream = NULL;
static pthread_t fsevents = NULL;
static int thread_pipe[2] = { -1, -1 };
static int fsevents_latency_seconds;
static char* oldpath = NULL;
static uint32_t oldlen = 0;
static const char* empty = "";

static void event_cb (ConstFSEventStreamRef streamRef,
		  void *ctx,
		  size_t count,
		  void *paths,
		  const FSEventStreamEventFlags flags[],
		  const FSEventStreamEventId ids[])
{
	size_t i;
	
	(void)streamRef;
	(void)ctx;
	(void)ids;

//	printf("%li\n", count);

	for (i = 0; i < count; i++)
	{
		const char *path = ((const char **)paths)[i];
		uint32_t len = strlen(path);
		uint32_t flag = flags[i];
		
//		printf("0x%08x %s\n", flags[i], path);
		
		if (flags[i] & kFSEventStreamEventFlagItemRenamed && !oldpath)
		{
			asprintf(&oldpath, "%s", path);
			oldlen = len;
			continue;
		}
		
		if ((flags[i] & kFSEventStreamEventFlagItemRenamed) == 0 && oldpath)
		{
			fprintf(stderr, "fsevents thread: inconsistency in rename/move event (flags=0x%08x path='%s' oldpath='%s')\n", flag, path, oldpath);
			pthread_exit(NULL);
		}
		
		if (   write(thread_pipe[1], &flag, sizeof(uint32_t)) != sizeof(uint32_t)
		    || write(thread_pipe[1], oldpath? &oldlen: &len, sizeof(uint32_t))           != sizeof(uint32_t)
		    || write(thread_pipe[1], oldpath? oldpath: path, (oldpath? oldlen: len) + 1) != (oldpath? oldlen: len) + 1
		    || write(thread_pipe[1], oldpath? &len: &oldlen, sizeof(uint32_t))           != sizeof(uint32_t)
		    || write(thread_pipe[1], oldpath? path: empty,   (oldpath? len: 0)      + 1) != (oldpath? len: 0)      + 1
		   )
		{
			fprintf(stderr, "fsevents thread: pipe error (flags=0x%08x path='%s')\n", flag, path);
			pthread_exit(NULL);
		}
		
		if (oldpath)
		{
			free(oldpath);
			oldpath = NULL;
			oldlen = 0;
		}
	}
}

void fsevents_api_add_path (const char* dir_path)
{
	if (!paths)
		paths = CFArrayCreateMutable(NULL, 0, NULL);
	CFArrayAppendValue(paths, CFStringCreateWithCString(NULL, dir_path, kCFStringEncodingUTF8));
}

static void fsevents_api_loop (int latency_seconds)
{
	FSEventStreamContext ctx =
	{
		0,
		NULL, // ptr
		NULL,
		NULL,
		NULL
	};
	stream = FSEventStreamCreate(NULL, &event_cb, &ctx, paths, kFSEventStreamEventIdSinceNow, latency_seconds, kFSEventStreamCreateFlagFileEvents);
	FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	if (FSEventStreamStart(stream))
		CFRunLoopRun();

}

static void* fsevents_api_loop_in_thread (void* unused)
{
	(void)unused;
	
	fsevents_api_loop(fsevents_latency_seconds);
	return NULL;
}

void fsevents_api_start_thread (int latency_seconds)
{
	int status;
	
	fsevents_latency_seconds = latency_seconds;
	
	if (pipe(thread_pipe) != 0)
	{
		fprintf(stderr, "fsevent-api: pipe creation error (%s)\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if ((status = pthread_create(&fsevents, NULL, fsevents_api_loop_in_thread, NULL)) != 0)
	{
		fprintf(stderr, "fsevent-api: thread start error (%s)\n", strerror(status));
		exit(EXIT_FAILURE);
	}
}

void fsevents_api_stop_thread (void)
{
	if (stream)
		FSEventStreamStop(stream);
	stream = NULL;
	if (fsevents)
		pthread_cancel(fsevents);
	fsevents = NULL;
	if (thread_pipe[0] >= 0)
		close(thread_pipe[0]);
	thread_pipe[0] = -1;
	if (thread_pipe[1] >= 0)
		close(thread_pipe[1]);
	thread_pipe[1] = -1;
}

int fsevents_api_getfd (void)
{
	return thread_pipe[0];
}
