/*
| fsevent-api-test.c
|
| License: GPLv2 (see COPYING) or any later version
|
| Authors: David Gauchard <gauchard@laas.fr>
|
*/


#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "fsevents-api.h"

int main (void)
{
	int fd;
	size_t status;
	
	fsevents_api_add_path("/tmp");
	fsevents_api_start_thread(1);
	fd = fsevents_api_getfd();

	printf("you can play now in /tmp:\n");
	
	do
	{
		uint32_t flags, len, newlen;
		char path[1024];
		char newpath[1024];
		
		if ((status = read(fd, &flags, sizeof(uint32_t))) != sizeof(uint32_t))
			break;
		if ((status = read(fd, &len, sizeof(uint32_t))) != sizeof(uint32_t))
			break;
		if ((status = read(fd, path, len + 1)) != len + 1)
			break;
		if ((status = read(fd, &newlen, sizeof(uint32_t))) != sizeof(uint32_t))
			break;
		if ((status = read(fd, newpath, newlen + 1)) != newlen + 1)
			break;
		
		printf("0x%08x '%s' new: '%s'\n", flags, path, newpath);

	} while (1);
	
	printf("last status = %li (%s)\n", status, strerror(status));
		
	fsevents_api_stop_thread();
	
	return 0;
}
