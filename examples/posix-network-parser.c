/* ==========================================================================
 *  Licensed under BSD 2clause license See LICENSE file for more information
 *  Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ==========================================================================
 *       -------------------------------------------------------------
 *      / This example shows how one can use rb_claim/commit to read  \
 *      | network data in a trivial loop, and then process them in    |
 *      | separate thread in another simple loop. Normally you would  |
 *      | have to deal with mutexes; and memory buffer overlapping    |
 *      | and all of that crap. With librb code can be reduced to     |
 *      | minimum. With rb_claim/commit API you can pass buffer       |
 *      | directly to syscall read() to not perform unnecessary       |
 *      \ copying of data from driver->local_buffer->ring_buffer      /
 *       -------------------------------------------------------------
 *       \     /\  ___  /\
 *        \   // \/   \/ \\
 *           ((    O O    ))
 *            \\ /     \ //
 *             \/  | |  \/
 *              |  | |  |
 *              |  | |  |
 *              |   o   |
 *              | |   | |
 *              |m|   |m|
 * ==========================================================================
 *                       ░▀█▀░█▀█░█▀▀░█░░░█░█░█▀▄░█▀▀░█▀▀
 *                       ░░█░░█░█░█░░░█░░░█░█░█░█░█▀▀░▀▀█
 *                       ░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀▀░░▀▀▀░▀▀▀
 * ========================================================================== */
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "rb.h"
#include "common.h"

/* ==========================================================================
 *                     ░█▀▀░█░█░█▀█░█▀▀░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *                     ░█▀▀░█░█░█░█░█░░░░█░░░█░░█░█░█░█░▀▀█
 *                     ░▀░░░▀▀▀░▀░▀░▀▀▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */

/** =========================================================================
 * Function reads data stored in #rb buffer (#arg argument), processes it
 * and prints to stdout.
 *
 * @param arg ring buffer object
 * ========================================================================== */
static void *parser(void *arg)
{
	for (;;) {
		unsigned char d;  /* byte of data read from rb */

		/* Wait for data to arrive. rb_read() will block thread until data
		 * arrives */
		if (rb_read(arg, &d, sizeof(d)) != 1) {
			if (errno == ECANCELED)
				/* main thread is going down, not an error, just information
				 * we should not use rb object anymore, in this case we
				 * return from the function. This error will happen after
				 * other thread calls #rb_stop() function. */
				return NULL;

			/* log, other unexpected error */
			perror("rb_read()");
			return NULL;
		}

		/* "parse" data received from network, we just dump received data
		 * as hex */
		if (d == '\n')
			printf("\n");
		else if (d == '\r')
			printf("x%02x(\\r) ", d);
		else
			printf("x%02x(%c) ", d, d);
	}
}

/* ==========================================================================
 *                               ░█▄█░█▀█░▀█▀░█▀█
 *                               ░█░█░█▀█░░█░░█░█
 *                               ░▀░▀░▀░▀░▀▀▀░▀░▀
 * ========================================================================== */
int main(void)
{
	int         cfd;       /* server file descriptor */
	int         sfd;       /* connected client file descriptor */
	pthread_t   parser_t;  /* thread that parses messages from network */
	struct rb  *rb;        /* pointer to malloc()ed rb object */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	/* Create new ring buffer, that can hold 127 elements, each of size 1.
	 * Make ring buffer multi-thread aware */
	if ((rb = rb_new(128, 1, rb_multithread)) == NULL)
		pdie("rb_new()");

	sfd = start_tcp_server(23893);

	printf(
		"======================================================================\n"
		"server is up, you can send some messages with following commands\n"
		"to see program in action (not all might be available on your system\n"
		"    1) telnet 127.0.0.1 23893\n"
		"    2) echo \"test message\" | nc -w1 127.0.0.1 23893\n"
		"    3) echo \"test message\" > /dev/tcp/127.0.0.1/23893\n"
		"======================================================================"
		"\n\n");

	if ((cfd = accept(sfd, NULL, NULL)) < 0)
		pdie("accept()");

	/* create a thread, that will parse data received from network */
	pthread_create(&parser_t, NULL, parser, rb);

	/* start a loop, that will read data from network, and will put it
	 * on ring buffer, so that #parser thread can do something with it */
	for (;;) {
		ssize_t nread;
		void *buffer;
		size_t count;
		size_t objsize;
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

		/* Claim buffer for writing. Reader thread will still be able to read
		 * data from a queue without being locked out. Function will give us
		 * location where we can start writing data and how big buffer is */
		if (rb_write_claim(rb, &buffer, &count, &objsize, 0))
			pdie("rb_write_claim()");

		/* Read data from server socket, and place it directly in ring
		 * buffer's buffer. #buffer is #count * #objsize long, but we know
		 * that #objsize == 1, so we can just pass #count */
		nread = read(cfd, buffer, count);

		if (nread == -1)
			pdie("read()");

		if (nread == 0) {
			fprintf(stderr, "Connection closed by remote client\n");
			break;
		}

		/* Tell rb how many elements (or in this case bytes) have been
		 * written to #buffer. This will also wake any block reader thread */
		rb_write_commit(rb, nread);
	}

	close(cfd);

	/* call rb_stop(), so parser thread can exit from blocked rb_read(). */
	rb_stop(rb);

	/* wait for thread to finish it's job before we destroy rb object */
	pthread_join(parser_t, NULL);

	/* now we are sure no thread is locked in any rb_* functions, so we can
	 * safely destroy the object */
	rb_destroy(rb);

	return 0;
}
