/* ==========================================================================
 *  Licensed under BSD 2clause license See LICENSE file for more information
 *  Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ==========================================================================
 *       -------------------------------------------------------------
 *      / This example shows how to use librb as simple event queue.  \
 *      | Idea is that multiple threads gather different events from  |
 *      | different sources and pass them to rb object and main       |
 *      | thread reads them and process them one by one. This is nice |
 *      \ way to process data asynchronously without polling.         /
 *       -------------------------------------------------------------
 *         \
 *          \
 *              .--.
 *             |o_o |
 *             |:_/ |
 *            //   \ \
 *           (|     | )
 *          /'\_   _/`\
 *          \___)=(___/
 * ==========================================================================
 *                       ░▀█▀░█▀█░█▀▀░█░░░█░█░█▀▄░█▀▀░█▀▀
 *                       ░░█░░█░█░█░░░█░░░█░█░█░█░█▀▀░▀▀█
 *                       ░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀▀░░▀▀▀░▀▀▀
 * ========================================================================== */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "rb.h"
#include "common.h"

/* ==========================================================================
 *               ░█▀▄░█▀▀░█▀▀░█░░░█▀█░█▀▄░█▀█░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *               ░█░█░█▀▀░█░░░█░░░█▀█░█▀▄░█▀█░░█░░░█░░█░█░█░█░▀▀█
 *               ░▀▀░░▀▀▀░▀▀▀░▀▀▀░▀░▀░▀░▀░▀░▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */

enum event_type {
	EVENT_KBD,
	EVENT_NET,
	EVENT_SIGNAL
};

struct event {
	enum event_type  type;
	long             datalen;
	union {
		void        *data;
		int          signal;
	};
};

static struct rb  *queue;  /* rb object, that will act as a queue */

/* ==========================================================================
 *                     ░█▀▀░█░█░█▀█░█▀▀░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *                     ░█▀▀░█░█░█░█░█░░░░█░░░█░░█░█░█░█░▀▀█
 *                     ░▀░░░▀▀▀░▀░▀░▀▀▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */

/** =========================================================================
 * Thread waits for keyboard input on stdio and sends event once new line
 * is read, or we would overflow buffer
 * ========================================================================== */
static void *kbd_event(void *)
{
	char          c;
	char         *edata;
	int           i;
	struct event  event;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	for (;;) {
		event.type = EVENT_KBD;
		event.data = malloc(1024);
		edata = event.data;
		i = 0;

		/* read keyboard input and put them in event data */
		while ((c = getchar()) != '\n') {
			edata[i++] = c;
			if (i == 1023)
				/* overflow imminent, send what we got */
				break;
		}

		edata[i++] = '\0';
		event.datalen = i;

		/* Write one event to rb object, which is our event queue. Function
		 * will return either 1 (1 element has been written) or -1 on error.
		 * If there is not enough space on #queue, function will block until
		 * there is space for 1 element. */
		if (rb_write(queue, &event, 1) == 1)
			/* ok, we do not free event.data, it's consumer responsibility */
			continue;

		/* we did not put event on queue, so we must free data ourselves */
		free(event.data);

		/* When errno is ECANCELED that means rb_stop() has
		 * been called and rb object should not be used any more */
		if (errno == ECANCELED)
			return NULL;

		perror("rb_write()");
		return NULL;
	}
}

/** =========================================================================
 * Starts server at 127.0.0.1:23894, and creates net event with data for
 * processing
 * ========================================================================== */
static void *net_event(void *)
{
	int sfd; /* server file descriptor */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	sfd = start_tcp_server(23894);

	for (;;) {
		int           cfd;    /* file descriptor of the connected client */
		struct event  event;  /* event to send to event queue */
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

		if ((cfd = accept(sfd, NULL, NULL)) < 0)
			pdie("accept()");

		event.type = EVENT_NET;
		event.data = malloc(1024);
		event.datalen = read(cfd, event.data, 1023);

		if (event.datalen == -1) {
			perror("read()");
			free(event.data);
			continue;
		}

		*((char *)event.data + event.datalen++) = '\0';

		/* Write one event to rb object, which is our event queue. Function
		 * will return either 1 (1 element has been written) or -1 on error.
		 * If there is not enough space on #queue, function will block until
		 * there is space for 1 element. */
		if (rb_write(queue, &event, 1) == 1)
			/* ok, we do not free event.data, it's consumer responsibility */
			continue;

		/* we did not put event on queue, so we must free data ourselves */
		free(event.data);
		close(cfd);
		close(sfd);

		/* When errno is ECANCELED that means rb_stop() has
		 * been called and rb object should not be used any more */
		if (errno == ECANCELED)
			return NULL;

		perror("rb_write()");
		return NULL;
	}
}

/** =========================================================================
 * Signal handler, forwards signal to event queue for processing
 *
 * @param signum signal that has been received
 * ========================================================================== */
static void sig_event(int signum)
{
	struct event  event;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	event.type = EVENT_SIGNAL;
	event.signal = signum;
	event.datalen = 1;

	/* Send event to queue, but make sure to send it with #rb_dontwait here.
	 * Signals are special, they are like interrupts, and should NEVER block
	 * execution */
	if (rb_send(queue, &event, 1, rb_dontwait) == 1)
		return;

	/* queue is full and it's really not wise to block signal, so event
	 * will not be delivered in that case. You may want to have a flag
	 * which you could check for queue overrun in that case, but that's
	 * not part of this example, here we simply drop event */
	if (errno != EAGAIN)
		pdie("rb_send()");
}

/* ==========================================================================
 *                                  ┏┳┓┏━┓╻┏┓╻
 *                                  ┃┃┃┣━┫┃┃┗┫
 *                                  ╹ ╹╹ ╹╹╹ ╹
 * ========================================================================== */
int main(void)
{
	pthread_t         kbd_event_t;  /* thread listening for keyboard events */
	pthread_t         net_event_t;  /* thread listening for network events */
	sigset_t          signal_mask;  /* signal mask for newly created threads */
	struct sigaction  sa;           /* sigaction for signal handling */
	long              pid;          /* process id */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	/* create rb object that can hold 127 (yes, 127) elements
	 * each with size of struct event. */
	if ((queue = rb_new(128, sizeof(struct event), rb_multithread)) == NULL)
		pdie("rb_new()");

	/* Create threads that will generate us some events to process. Mask
	 * signals for producer queues as we want only main thread to process
	 * signals. */
	sigemptyset(&signal_mask);
	sigaddset(&signal_mask, SIGINT);
	sigaddset(&signal_mask, SIGTERM);
	sigaddset(&signal_mask, SIGUSR1);
	sigaddset(&signal_mask, SIGUSR2);
	sigaddset(&signal_mask, SIGHUP);
	pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);

	pthread_create(&kbd_event_t, NULL, kbd_event, NULL);
	pthread_create(&net_event_t, NULL, net_event, NULL);

	/* unblock signals and install handler for some of the signals we will
	 * be processing. */
	pthread_sigmask (SIG_UNBLOCK, &signal_mask, NULL);
	memset(&sa, 0x00, sizeof(sa));
	sa.sa_handler = sig_event;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	pid = (long)getpid();
	printf(
		"======================================================================"
		"program is up, you can send some messages with following commands\n"
		"to see program in action (not all might be available on your system)\n"
		"for network activity do\n"
		"    1) telnet 127.0.0.1 23894\n"
		"    2) echo \"test message\" | nc -w1 127.0.0.1 23894\n"
		"    3) echo \"test message\" > /dev/tcp/127.0.0.1/23894\n"
		"\n"
		"for signal activity do\n"
		"    1) kill -SIGUSR1 %ld\n"
		"    2) kill -SIGUSR2 %ld\n"
		"    3) kill -SIGHUP %ld\n"
		"\n"
		"or you can simply type with you keyboard to stdin and hit enter\n"
		"\n"
		"to interrupt program either send SIGTERM signal or hit ctrl-c\n"
		"======================================================================"
		"\n\n",
		pid, pid, pid);

	for (;;) {
		struct event  event;  /* event read from queue */
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

		/* read event from the queue, function will lock and sleep until
		 * someone pushes event into the queue */

		if (rb_read(queue, &event, 1) == -1)
			pdie("rb_read()");

		/* event received */
		switch (event.type) {
		case EVENT_KBD:
			printf("---> keyboard event received data len: %zu, data %s\n",
				event.datalen, (char *)event.data);
			free(event.data);
			break;

		case EVENT_NET:
			printf("---> network event received data len: %zu, data %s\n",
				event.datalen, (char *)event.data);
			free(event.data);
			break;

		case EVENT_SIGNAL:
			printf("---> received signal %s\n", strsignal(event.signal));

			if (event.signal == SIGTERM || event.signal == SIGINT) {
				fprintf(stderr, "terminating\n");
				goto stop;
			}
			break;

		default:
			fprintf(stderr, "unexpected event type: %d\n", event.type);
			exit(1);
		}
	}

stop:
	/* stop queue, instruct event producers to exit */
	rb_stop(queue);

	/* send cancellation request to threads, rb_stop() will wake threads
	 * that are blocked on ring buffer semaphore/mutexes but if threads
	 * are locked on posix calls (read(2)) then rb_stop() will not wake
	 * them up, pthread_cancel() will. */
	printf("Stopping threads\n");
	pthread_cancel(kbd_event_t);
	pthread_cancel(net_event_t);

	pthread_join(kbd_event_t, NULL);
	printf("Keyboard thread stopped\n");
	pthread_join(net_event_t, NULL);
	printf("Network thread stopped\n");

	rb_destroy(queue);
	return 0;
}
