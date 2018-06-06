/*
| pipe.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Manages the pipes used to communicate with spawned subprocesses (usually rsync).
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef LSYNCD_PIPE_H
#define LSYNCD_PIPE_H


/*
| Creates a pipe.
|
| Sets the write end non blocking and close on exec.
*/
extern void pipe_create( int pipefd[ 2 ] );


/*
| Writes to a pipe and handles observing for further writing
| if it's buffer is fully filled on first try.
|
| This may be used only once for every pipe managed by Lsyncd!
*/
extern void pipe_write(
	int pipedf[ 2 ],        // the pipe file descriptors
	char const * pipe_text, // text to pipe
	size_t pipe_len         // the pipe's text length
);


#endif
