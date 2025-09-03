/* ==========================================================================
   Licensed under BSD 2clause license. See LICENSE file for more information
Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
========================================================================== */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#if ENABLE_THREADS
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <pthread.h>
#endif

#if HAVE_SYS_SELECT_H
#   include <sys/select.h>
#endif

#include "mtest.h"
#include "rb.h"

struct tdata
{
	struct rb *rb;
	unsigned char *data;
	int fd;
	size_t len;
	size_t objsize;
	size_t buflen;
	int test_type;
};

struct multi_data
{
	struct rb *rb;
	int fd;
	int objsize;
};

static unsigned t_rblen;
static unsigned t_readlen;
static unsigned t_writelen;
static unsigned t_objsize;

static int t_num_producers;
static int t_num_consumers;
static unsigned char data[250];
static unsigned int multi_index;
static volatile unsigned int multi_index_count;

#define rb_array_size(a) (sizeof(a)/sizeof(*(a)))
mt_defs();

#if ENABLE_THREADS
static pthread_mutex_t multi_mutex;
static pthread_mutex_t multi_mutex_count;

static void *consumer(void *arg)
{
	struct tdata *data = arg;
	size_t r = 0;

	while (r != data->buflen) {
		size_t left = data->buflen - r;
		left = left < data->len ? left : data->len;
		r += rb_read(data->rb, data->data + r * data->objsize, left);
	}

	return data;
}

static void *producer(void *arg)
{
	struct tdata *data = arg;
	size_t w = 0;

	while (w != data->buflen)
	{
		size_t left = data->buflen - w;
		left = left < data->len ? left : data->len;
		w += rb_write(data->rb, data->data + w * data->objsize, left);
	}

	return data;
}

static void *multi_producer(void *arg)
{
	struct multi_data *d = arg;
	struct rb *rb = d->rb;
	unsigned int index;

	index = 0;
	for (;;) {
		pthread_mutex_lock(&multi_mutex);
		index = multi_index++;
		pthread_mutex_unlock(&multi_mutex);

		if (index >= rb_array_size(data))
			return NULL;

		if (d->objsize == 1) {
			unsigned char i = index;
			rb_write(rb, &i, 1);
		} else
			rb_write(rb, &index, 1);
	}
}

static void *multi_consumer(void *arg)
{
	struct multi_data *d = arg;
	struct rb *rb = d->rb;
	unsigned int index;

	for (;;) {
		int overflow;

		long ret;

		if (d->objsize == 1) {
			unsigned char i;
			ret = rb_read(rb, &i, 1);
			index = i;
		} else
			ret = rb_read(rb, &index, 1);

		if (ret == -1)
			/* force exit received */
			return NULL;

		overflow = index >= rb_array_size(data);

		if (overflow)
			continue;

		data[index] = 1;
		pthread_mutex_lock(&multi_mutex_count);
		++multi_index_count;
		pthread_mutex_unlock(&multi_mutex_count);
	}
}

static void multi_producers_consumers(void)
{
	pthread_t *cons;
	pthread_t *prod;
	struct multi_data tdata;
	int i, r;
	unsigned count;

	multi_index = 0;
	multi_index_count = 0;
	count  = 0;
	memset(data, 0, sizeof(data));
	cons = malloc(t_num_consumers * sizeof(*cons));
	prod = malloc(t_num_producers * sizeof(*prod));

	tdata.rb = rb_new(8, sizeof(unsigned int), rb_multithread);
	tdata.fd = -1;
	tdata.objsize = sizeof(unsigned int);

	pthread_mutex_init(&multi_mutex, NULL);
	pthread_mutex_init(&multi_mutex_count, NULL);
	for (i = 0; i != t_num_consumers; ++i)
		pthread_create(&cons[i], NULL, multi_consumer, &tdata);

	for (i = 0; i != t_num_producers; ++i)
		pthread_create(&prod[i], NULL, multi_producer, &tdata);

	/* wait until all indexes has been consumed */
	while (count < rb_array_size(data)) {
		int buf[16];

		pthread_mutex_lock(&multi_mutex_count);
		count = multi_index_count;
		pthread_mutex_unlock(&multi_mutex_count);

		/* while waiting, we randomly peek into rb, and to make sure,
		 * peeking won't make a difference */
		rb_recv(tdata.rb, buf, rand() % 16, rb_peek);
	}

	rb_stop(tdata.rb);

	for (i = 0; i != t_num_consumers; ++i)
		pthread_join(cons[i], NULL);

	for (i = 0; i != t_num_producers; ++i)
		pthread_join(prod[i], NULL);

	rb_destroy(tdata.rb);

	for (r = 0, i = 0; i < (int)rb_array_size(data); ++i)
		r += (data[i] != 1);

	mt_fail(r == 0);

	if (r != 0)
		printf("num_consumers = %d, num_producers = %d\n",
			t_num_consumers, t_num_producers);

	free(cons);
	free(prod);
	pthread_mutex_destroy(&multi_mutex);
	pthread_mutex_destroy(&multi_mutex_count);
}

static void multi_thread(void)
{
	pthread_t cons;
	pthread_t prod;

	size_t buflen = t_readlen > t_writelen ? t_readlen : t_writelen;
	unsigned char *send_buf = malloc(t_objsize * buflen);
	unsigned char *recv_buf = malloc(t_objsize * buflen);
	size_t i;
	int rc;

	struct rb *rb;
	struct tdata consdata;
	struct tdata proddata;
	static unsigned long c;
	c++;

	for (i = 0; i != t_objsize * buflen; ++i) {
		send_buf[i] = i;
		recv_buf[i] = 0;
	}

	rb = rb_new(t_rblen, t_objsize, rb_multithread);

	proddata.data = send_buf;
	proddata.len = t_writelen;
	proddata.objsize = t_objsize;
	proddata.rb = rb;
	proddata.buflen = buflen;
	proddata.fd = -1;

	consdata.data = recv_buf;
	consdata.len = t_readlen;
	consdata.objsize = t_objsize;
	consdata.rb = rb;
	consdata.buflen = buflen;
	consdata.fd = -1;

	pthread_create(&cons, NULL, consumer, &consdata);
	pthread_create(&prod, NULL, producer, &proddata);

	pthread_join(cons, NULL);
	pthread_join(prod, NULL);

	rc = memcmp(send_buf, recv_buf, t_objsize * buflen);
	mt_fail(rc == 0);

	if (rc)
		printf("[%lu] a = %lu, b = %d, c = %d, d = %d\n",
			c, buflen, t_rblen, t_readlen, t_writelen);

	close(proddata.fd);
	close(consdata.fd);
	rb_destroy(rb);
	free(send_buf);
	free(recv_buf);
}

static void multithread_eagain(void)
{
	char s[6] = {0, 1, 2, 3, 4, 5};
	char d[6];
	struct rb *rb;

	rb = rb_new(4, 1, rb_multithread | rb_nonblock);
	rb_write(rb, s, sizeof(s));
	mt_ferr(rb_write(rb, s, sizeof(s)), EAGAIN);
	rb_read(rb, d, sizeof(d));
	mt_ferr(rb_read(rb, d, sizeof(d)), EAGAIN);
	rb_destroy(rb);
}
#endif

static void multithread_flag(void)
{
	struct rb *rb;

	rb = rb_new(4, 1, rb_multithread);

#if ENABLE_THREADS
	mt_assert(rb != NULL);
	rb_destroy(rb);
#else
	mt_assert(rb == NULL);
	mt_assert(errno == ENOSYS);
#endif
}

static void nonblocking_flag(void)
{
	struct rb *rb;
	char s[6] = {0, 1, 2, 3, 4, 5};
	char e[3] = {0, 1, 2};
	char d[6];
	int r;

#if ENABLE_THREADS
	rb = rb_new(4, 1, rb_nonblock | rb_multithread);
	r = rb_write(rb, s, sizeof(s));
	mt_fail(r == 3);
	r = rb_read(rb, d, sizeof(d));
	mt_fail(r == 3);
	mt_fok(memcmp(d, e, sizeof(e)));
	rb_destroy(rb);
	memset(d, 0, sizeof(d));
#endif

	rb = rb_new(4, 1, 0);
	r = rb_write(rb, s, sizeof(s));
	mt_fail(r == 3);
	r = rb_read(rb, d, sizeof(d));
	mt_fail(r == 3);
	mt_fok(memcmp(d, e, sizeof(e)));
	rb_destroy(rb);
}

static void singlethread_eagain(void)
{
	char s[6] = {0, 1, 2, 3, 4, 5};
	char d[6];
	struct rb *rb;

	rb = rb_new(4, 1, 0);
	rb_write(rb, s, sizeof(s));
	mt_ferr(rb_write(rb, s, sizeof(s)), EAGAIN);
	rb_read(rb, d, sizeof(d));
	mt_ferr(rb_read(rb, d, sizeof(d)), EAGAIN);
	rb_destroy(rb);
}

static void peeking(void)
{
	struct rb *rb;
	int v[8];
	int d[8];
	int i;

	for (i = 0; i != 8; ++i)
	{
		d[i] = i;
	}

	memset(v, 0, sizeof(v));
	mt_assert(rb = rb_new(8, sizeof(int), 0));

	rb_write(rb, d, 4);
	rb_recv(rb, v, 2, rb_peek);
	mt_fail(v[0] == 0);
	mt_fail(v[1] == 1);
	mt_fail(v[2] == 0);
	mt_fail(v[3] == 0);
	memset(v, 0, sizeof(v));
	rb_recv(rb, v, 6, rb_peek);
	mt_fail(v[0] == 0);
	mt_fail(v[1] == 1);
	mt_fail(v[2] == 2);
	mt_fail(v[3] == 3);
	mt_fail(v[4] == 0);
	mt_fail(v[5] == 0);

	/* now with overlapped memory */

	memset(v, 0, sizeof(v));
	mt_fail(rb_discard(rb, 7) == 4);
	rb_write(rb, d, 6);
	rb_recv(rb, v, 6, rb_peek);
	mt_fail(v[0] == 0);
	mt_fail(v[1] == 1);
	mt_fail(v[2] == 2);
	mt_fail(v[3] == 3);
	mt_fail(v[4] == 4);
	mt_fail(v[5] == 5);
	memset(v, 0, sizeof(v));
	rb_recv(rb, v, 6, rb_peek);
	mt_fail(v[0] == 0);
	mt_fail(v[1] == 1);
	mt_fail(v[2] == 2);
	mt_fail(v[3] == 3);
	mt_fail(v[4] == 4);
	mt_fail(v[5] == 5);

	rb_destroy(rb);
}

static void single_thread(void)
{
	size_t read;
	size_t written;
	size_t buflen = t_readlen > t_writelen ? t_readlen : t_writelen;
	unsigned long writelen = t_writelen;
	unsigned long readlen = t_readlen;

	unsigned char *send_buf = malloc(t_objsize * buflen);
	unsigned char *recv_buf = malloc(t_objsize * buflen);

	struct rb *rb;
	static unsigned long c;
	size_t i;
	int rc;

	c++;

	for (i = 0; i != t_objsize * buflen; ++i) {
		send_buf[i] = i;
		recv_buf[i] = 0;
	}

	rb = rb_new(t_rblen, t_objsize, 0);

	written = 0;
	read = 0;

	while (written != buflen || read != buflen) {
		if (written != buflen) {
			long w;
			if (written + writelen > buflen)
				writelen = buflen - written;

			w = rb_write(rb, send_buf + written * t_objsize, writelen);

			if (w == -1)
				break;

			written += w;
		}

		if (read != buflen) {
			long r;

			if (read + readlen > buflen)
				readlen = buflen - read;

			r = rb_read(rb, recv_buf + read * t_objsize, readlen);

			if (r == -1)
				break;

			read += r;
		}
	}

	rc = memcmp(send_buf, recv_buf, buflen * t_objsize);
	mt_fail(rc == 0);

	if (rc)
		printf("[%lu] a = %lu, b = %d, c = %d, d = %d\n",
			c, buflen, t_rblen, t_readlen, t_writelen);

	free(send_buf);
	free(recv_buf);
	rb_destroy(rb);
}

static void discard(void)
{
	char s[8] = "0123456";
	char d[8];
	int i;
	struct rb  *rb;

	for (i = 0; i != 2; i++) {
		int flags;

#if ENABLE_THREADS
		flags = i ? rb_multithread : 0;
#else
		/* yup, if ENABLE_THREADS is 0, same code will be executed twice...
		 * it's not a bug, it's a feature! MORE TESTS NEVER HURT! */

		flags = 0;
#endif
		rb = rb_new(8, 1, flags);
		rb_write(rb, s, 6);
		mt_fail(rb_discard(rb, 3) == 3);
		rb_read(rb, d, 3);
		mt_fok(memcmp(d, "345", 3));
		rb_clear(rb, 0);

		rb_write(rb, s, 6);
		rb_read(rb, d, 2);
		mt_fail(rb_discard(rb, 2) == 2);
		rb_read(rb, d, 2);
		mt_fok(memcmp(d, "45", 2));
		rb_clear(rb, 0);

		/* overlap cases */
		rb_write(rb, s, 7);
		rb_read(rb, d, 5);
		rb_write(rb, s, 5);
		mt_fail(rb_discard(rb, 3) == 3);
		rb_read(rb, d, 3);
		mt_fok(memcmp(d, "123", 3));
		rb_clear(rb, 0);

		rb_write(rb, s, 7);
		rb_read(rb, d, 5);
		rb_write(rb, s, 5);
		mt_fail(rb_discard(rb, 2) == 2);
		rb_read(rb, d, 3);
		mt_fok(memcmp(d, "012", 3));
		rb_clear(rb, 0);

		rb_write(rb, s, 7);
		rb_read(rb, d, 5);
		rb_write(rb, s, 5);
		mt_fail(rb_discard(rb, 4) == 4);
		rb_read(rb, d, 3);
		mt_fok(memcmp(d, "234", 3));
		rb_clear(rb, 0);

		rb_write(rb, s, 3);
		mt_fail(rb_discard(rb, 10) == 3);

		rb_destroy(rb);
	}
}

static void count_and_space(void)
{
	char d[4];
	struct rb  *rb;

	rb = rb_new(16, 1, 0);

	mt_fail(rb_space(rb) == 15);
	mt_fail(rb_count(rb) == 0);

	rb_write(rb, "123", 3);

	mt_fail(rb_space(rb) == 12);
	mt_fail(rb_count(rb) == 3);

	rb_write(rb, "1234567", 7);

	mt_fail(rb_space(rb) == 5);
	mt_fail(rb_count(rb) == 10);

	rb_read(rb, d, 4);

	mt_fail(rb_space(rb) == 9);
	mt_fail(rb_count(rb) == 6);

	rb_discard(rb, 5);

	mt_fail(rb_space(rb) == 14);
	mt_fail(rb_count(rb) == 1);

	rb_discard(rb, 999);

	mt_fail(rb_space(rb) == 15);
	mt_fail(rb_count(rb) == 0);

	rb_destroy(rb);
}

static void bad_count_value(void)
{
	struct rb *rb;
	struct rb rb2;
	unsigned char  rbs[10240];

	rb = rb_new(6, 1, 0);
	mt_fail(errno == EINVAL);
	mt_fail(rb == NULL);
	rb_init(&rb2, rbs, 6, 1, 0);
	mt_fail(errno == EINVAL);
}

static void enomem(void)
{
	struct rb  *rb;

	rb = rb_new(0x10000000000 - 0x1000, 1, 0);
	mt_fail(errno = ENOMEM);
	mt_fail(rb == NULL);
	mt_ferr(rb_destroy(rb), EINVAL);
}

static void einval_on_init(void)
{
	struct rb rb;
	char buf;

	mt_ferr(rb_init(&rb, &buf, 8, 1, rb_growable), EINVAL);
	mt_ferr(rb_init(&rb, &buf, 8, 1, rb_round_count), EINVAL);
	mt_ferr(rb_init(NULL, &buf, 8, 1, 0), EINVAL);
	mt_ferr(rb_init(&rb, NULL, 8, 1, 0), EINVAL);
	mt_ferr(rb_init(&rb, &buf, 7, 1, 0), EINVAL);
	mt_ferr(rb_init(&rb, &buf, 0, 1, 0), EINVAL);
}

static void einval(void)
{
	struct rb *rb;
	int v;

	rb = rb_new(4, 1, 0);
	mt_ferr(rb_read(NULL, &v, 1), EINVAL);
	mt_ferr(rb_read(rb, NULL, 1), EINVAL);
	mt_ferr(rb_write(NULL, &v, 1), EINVAL);
	mt_ferr(rb_write(rb, NULL, 1), EINVAL);
	mt_ferr(rb_recv(NULL, &v, 1, 0), EINVAL);
	mt_ferr(rb_recv(rb, NULL, 1, 0), EINVAL);
	mt_ferr(rb_send(NULL, &v, 1, 0), EINVAL);
	mt_ferr(rb_send(rb, NULL, 1, 0), EINVAL);
	mt_ferr(rb_destroy(NULL), EINVAL);
	mt_ferr(rb_discard(NULL, 1), EINVAL);
	mt_ferr(rb_count(NULL), EINVAL);
	mt_ferr(rb_space(NULL), EINVAL);
#if ENABLE_THREADS
	mt_ferr(rb_stop(NULL), EINVAL);
#else
	mt_ferr(rb_stop(NULL), ENOSYS);
#endif
	rb_destroy(rb);
}

static void stack_init(void)
{
	struct msg
	{
		int a;
		int b;
	} m;

	unsigned char  buf[4 * sizeof(m)];
	struct rb rb2;
	struct rb  *rb = &rb2;

	mt_assert(rb_init(&rb2, buf, 4, sizeof(m), 0) == 0);
	m.a = 1;
	m.b = 2;
	rb_write(rb, &m, 1);
	m.a = 4;
	m.b = 3;
	rb_write(rb, &m, 1);
	m.a = 8;
	m.b = 7;
	rb_write(rb, &m, 1);
	mt_fail(rb_space(rb) == 0);
	rb_read(rb, &m, 1);
	mt_fail(m.a == 1);
	mt_fail(m.b == 2);
	rb_read(rb, &m, 1);
	mt_fail(m.a == 4);
	mt_fail(m.b == 3);
	rb_read(rb, &m, 1);
	mt_fail(m.a == 8);
	mt_fail(m.b == 7);

#if ENABLE_THREADS
	mt_assert(rb_init(&rb2, buf, 4, sizeof(m), rb_multithread) == 0);
	m.a = 1;
	m.b = 2;
	rb_write(rb, &m, 1);
	m.a = 4;
	m.b = 3;
	rb_write(rb, &m, 1);
	m.a = 8;
	m.b = 7;
	rb_write(rb, &m, 1);
	mt_fail(rb_space(rb) == 0);
	rb_read(rb, &m, 1);
	mt_fail(m.a == 1);
	mt_fail(m.b == 2);
	rb_read(rb, &m, 1);
	mt_fail(m.a == 4);
	mt_fail(m.b == 3);
	rb_read(rb, &m, 1);
	mt_fail(m.a == 8);
	mt_fail(m.b == 7);
	rb_cleanup(rb);
#endif
}

static void grow(void)
{
	const char *buf = "123456";
	char rdbuf[8] = { 0 };
	struct rb *rb = rb_new(4, 1, rb_growable);
	mt_assert(rb);

	mt_fail(rb_write(rb, "123", 3));
	mt_fail(rb_write(rb, "456", 3));
	mt_fail(rb_read(rb, rdbuf, 6));
	mt_fail(strcmp(rdbuf, buf) == 0);

	rb_destroy(rb);
}

static void grow_warped(void)
{
	char rdbuf[16] = { 0 };
	struct rb *rb = rb_new(8, 1, rb_growable);
	mt_assert(rb);

	mt_fail(rb_write(rb, "1234567", 7));
	mt_fail(rb_read(rb, rdbuf, 4));
	mt_fail(rb_write(rb, "890", 3));
	mt_fail(rb_write(rb, "abcde", 5));
	mt_fail(rb_read(rb, rdbuf, 11));
	mt_fail(strcmp(rdbuf, "567890abcde") == 0);

	rb_destroy(rb);
}

static void grow_multiple_times(void)
{
	char rdbuf[32] = { 0 };
	struct rb *rb = rb_new(4, 1, rb_growable);
	mt_fail(rb_write(rb, "123", 3));
	mt_fail(rb_write(rb, "12345678901234567890", 20));
	mt_fail(rb_read(rb, rdbuf, 32));
	mt_fail(strcmp(rdbuf, "12312345678901234567890") == 0);

	rb_destroy(rb);
}

static void grow_multi_warped(void)
{
	char rdbuf[256] = { 0 };
	char ascii[128];
	struct rb *rb = rb_new(8, 1, rb_growable);
	mt_assert(rb);

	for (int i = 0; i <= 126; i++)
		ascii[i] = (char)(i + 1);
	ascii[127] = '\0';

	mt_fail(rb_write(rb, "1234567", 7));
	mt_fail(rb_read(rb, rdbuf, 4));
	mt_fail(rb_write(rb, "890", 3));
	mt_fail(rb_write(rb, "abcde", 5));
	mt_fail(rb_write(rb, ascii, 128));
	mt_fail(rb_read(rb, rdbuf, 11));
	mt_fail(strcmp(rdbuf, "567890abcde") == 0);
	mt_fail(rb_read(rb, rdbuf, 128));
	mt_fail(strcmp(rdbuf, ascii) == 0);

	rb_destroy(rb);
}

static void round_count(void)
{
	struct rb *rb;

	rb = rb_new(6, 1, rb_round_count);
	mt_fail(rb->count == 8);
	rb_destroy(rb);
	rb = rb_new(201, 1, rb_round_count);
	mt_fail(rb->count == 256);
	rb_destroy(rb);
}

int main(void)
{
	srand(time(NULL));
	unsigned int t_rblen_max = 32;
	unsigned int t_readlen_max = 32;
	unsigned int t_writelen_max = 32;
	unsigned int t_objsize_max = 32;

	int t_num_producers_max = 8;
	int t_num_consumers_max = 8;

	char name[128];

#if ENABLE_THREADS
	for (t_num_consumers = 1; t_num_consumers <= t_num_consumers_max; t_num_consumers++) {
		for (t_num_producers = 1; t_num_producers <= t_num_producers_max; t_num_producers++) {
			sprintf(name, "multi_producers_consumers producers: %d "
				"consumers %d", t_num_producers, t_num_consumers);
			mt_run_named(multi_producers_consumers, name);
		}
	}
#endif

	for (t_rblen = 2; t_rblen <= t_rblen_max; t_rblen *= 2) {
		for (t_readlen = 2; t_readlen <= t_readlen_max; t_readlen *= 2) {
			for (t_writelen = 2; t_writelen <= t_writelen_max; t_writelen *= 2) {
				for (t_objsize = 2; t_objsize <= t_objsize_max; t_objsize *= 2) {
#if ENABLE_THREADS
					sprintf(name, "multi_thread with buffer %3d, %3d, %3d, %3d",
						t_rblen, t_readlen, t_writelen, t_objsize);
					mt_run_named(multi_thread, name);
#endif

					sprintf(name, "single_thread with buffer %3d, %3d, %3d, %3d",
						t_rblen, t_readlen, t_writelen, t_objsize);
					mt_run_named(single_thread, name);
				}

				t_objsize = 1;
			}
		}
	}

	mt_run(peeking);
	mt_run(bad_count_value);
	mt_run(multithread_flag);
	mt_run(nonblocking_flag);
	mt_run(singlethread_eagain);
	mt_run(discard);
	mt_run(count_and_space);
	mt_run(einval);
	mt_run(einval_on_init);
	mt_run(enomem);
	mt_run(stack_init);
	mt_run(grow);
	mt_run(grow_warped);
	mt_run(grow_multiple_times);
	mt_run(grow_multi_warped);
	mt_run(round_count);

#if ENABLE_THREADS
	mt_run(multithread_eagain);
#endif

	mt_return();
}
