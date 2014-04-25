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

#ifndef __FSEVENTS_API_H
#define __FSEVENTS_API_H

void fsevents_api_add_path (const char* dir_path);
void fsevents_api_start_thread (int latency_seconds);
void fsevents_api_stop_thread (void);
int fsevents_api_getfd (void);

#endif // __FSEVENTS_API_H
