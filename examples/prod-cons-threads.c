/* ==========================================================================
 *  Licensed under BSD 2clause license See LICENSE file for more information
 *  Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ==========================================================================
 *       ------------------------------------------------------------
 *      / This example uses threads. It is classic consumer-producer \
 *      | problem that thanks to rb internal thread awareness is     |
 *      | reduced to minimum. Note that there is no mutex in this    |
 *      | file, all synchronization is performed by rb. Program      |
 *      | prints blocks representing count elements in rb to         |
 *      | visualize how elements are popped and pushed. This example |
 *      \ also shows how growable buffer works.                      /
 *       ------------------------------------------------------------
 *           \
 *            \
 *                oO)-.                       .-(Oo
 *               /__  _\                     /_  __\
 *               \  \(  |     ()~()         |  )/  /
 *                \__|\ |    (-___-)        | /|__/
 *                '  '--'    ==`-'==        '--'  '
 * ==========================================================================
 *                       ░▀█▀░█▀█░█▀▀░█░░░█░█░█▀▄░█▀▀░█▀▀
 *                       ░░█░░█░█░█░░░█░░░█░█░█░█░█▀▀░▀▀█
 *                       ░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀▀░░▀▀▀░▀▀▀
 * ========================================================================== */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rb.h"
#include "common.h"

/* ==========================================================================
 *               ░█▀▄░█▀▀░█▀▀░█░░░█▀█░█▀▄░█▀█░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *               ░█░█░█▀▀░█░░░█░░░█▀█░█▀▄░█▀█░░█░░░█░░█░█░█░█░▀▀█
 *               ░▀▀░░▀▀▀░▀▀▀░▀▀▀░▀░▀░▀░▀░▀░▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */
static struct rb  *rb;  /* pointer to malloc()ed rb object */
static const char *bar =
"=============================================================================";
static const char *sbar =
"                                                                             ";

/* ==========================================================================
 *                     ░█▀▀░█░█░█▀█░█▀▀░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *                     ░█▀▀░█░█░█░█░█░░░░█░░░█░░█░█░█░█░▀▀█
 *                     ░▀░░░▀▀▀░▀░▀░▀▀▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */

/** =========================================================================
 * Consumer thread.
 *
 * @param arg small integer, to control how fast data is read from buffer
 * ========================================================================== */
static void *consumer(void *arg)
{
	struct timespec  req;
	int              sleep_ms;
	int              i;
	int              data;
	int              run_time;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	sleep_ms = *(int *)arg;
	req.tv_sec = sleep_ms / 1000;
	req.tv_nsec = 1000000L * (sleep_ms % 1000);

	run_time = sleep_ms ? 15 : 300;

	for (i = 0; i != run_time; ++i) {
		if (rb_read(rb, &data, 1) != 1)
			pdie("rb_read()");

		/* use random time */
		if (sleep_ms == 0) {
			req.tv_sec = 0;
			req.tv_nsec = 1000000L * (rand() % 50 + 20);
		}
		nanosleep(&req, NULL);
	}

	return NULL;
}

/** =========================================================================
 * Producer thread.
 *
 * @param arg small integer, to control how fast data is read from buffer
 * ========================================================================== */
static void *producer(void *arg)
{
	struct timespec  req;
	int              sleep_ms;
	int              i;
	int              run_time;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	sleep_ms = *(int *)arg;
	req.tv_sec = sleep_ms / 1000;
	req.tv_nsec = 1000000L * (sleep_ms % 1000);

	run_time = sleep_ms ? 15 : 300;

	for (i = 0; i != run_time; ++i) {
		if (rb_write(rb, &i, 1) != 1)
			pdie("rb_write()");

		/* use random time */
		if (sleep_ms == 0) {
			req.tv_sec = 0;
			req.tv_nsec = 1000000L * (rand() % 50 + 1);
		}
		nanosleep(&req, NULL);
	}

	return NULL;
}

/** =========================================================================
 * Thread prints current ring buffer status with "graphical" representation
 * of fullness.
 *
 * @param rb ring buffer object
 * ========================================================================== */
static void *print_rb_status(void *rb)
{
	char dummy;
	struct timespec  req;

	req.tv_sec = 0;
	req.tv_nsec = 1000 * 1000L;

	for (;;) {
		int count = rb_count(rb);
		int space = rb_space(rb);
		int size = count + space;

		fprintf(stderr, "%3d/%d [%.*s%.*s]\r",
			count, size, count, bar, space, sbar);

		/* do this until we can no longer access rb */
		if (rb_recv(rb, &dummy, 1, rb_peek) == -1 && errno == ECANCELED) {
			fprintf(stderr, "\n");
			return NULL;
		}

		nanosleep(&req, NULL);
	}
}

/** =========================================================================
 * Run example. Start consumer and producer threads with configured action
 * speed.
 *
 * @param cons_sleep sleep between consuming action, 0 random
 * @param prod_sleep sleep between production action, 0 random
 * ========================================================================== */
static void run(int cons_sleep, int prod_sleep)
{
	pthread_t cons_t, prod_t, print_t;
	int flags;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	flags = rb_multithread;
	if (!cons_sleep && !prod_sleep)
		flags |= rb_growable;

	if ((rb = rb_new(8, sizeof(int), flags)) == NULL)
		pdie("rb_new()");

	if (!cons_sleep && !prod_sleep) {
		rb_set_hard_max_count(rb, 64);
		printf("=====] growable, hard limit 64, production > consumption [=====\n");
		printf("=====] consumer delay: [25-75]ms, producer delay: [50-100]ms [=====\n");
	} else
		printf("=====] consumer delay: %dms, producer delay: %dms [=====\n",
			cons_sleep, prod_sleep);

	pthread_create(&print_t, NULL, print_rb_status, rb);
	printf("-----| spawning producer |-----\n");
	pthread_create(&prod_t, NULL, producer, &prod_sleep);
	if (prod_sleep > cons_sleep)
		sleep(5);
	printf("-----| spawning consumer |-----\n");
	pthread_create(&cons_t, NULL, consumer, &cons_sleep);

	pthread_join(cons_t, NULL);
	pthread_join(prod_t, NULL);
	rb_stop(rb);
	pthread_join(print_t, NULL);
	rb_destroy(rb);
}

/* ==========================================================================
 *                               ░█▄█░█▀█░▀█▀░█▀█
 *                               ░█░█░█▀█░░█░░█░█
 *                               ░▀░▀░▀░▀░▀▀▀░▀░▀
 * ========================================================================== */
int main(void)
{
	run(100, 500);
	run(500, 100);
	run(0, 0);

	return 0;
}
