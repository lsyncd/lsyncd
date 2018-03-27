/*
| smem.h from Lsyncd - Live (Mirror) Syncing Demon
|
|
| Simple "secured" memory management.
|
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/

#ifndef LSYNCD_SMEM_H
#define LSYNCD_SMEM_H

extern void * s_calloc( size_t nmemb, size_t size );
extern void * s_malloc( size_t size );
extern void * s_realloc( void *ptr, size_t size );
extern char * s_strdup( const char *src );

#endif

