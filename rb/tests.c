/* ==========================================================================
    Licensed under BSD 2clause license. See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

#include "config.h"
#include "rb.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include "mtest.h"

#include <sys/types.h>
#include <unistd.h>
struct tdata
{
    struct rb *rb;
    unsigned char *data;
    int len;
    int objsize;
    size_t buflen;
};

static int t_rblen;
static int t_readlen;
static int t_writelen;
static int t_objsize;

static int t_num_producers;
static int t_num_consumers;
static int data[128];
static pthread_mutex_t multi_mutex;
static pthread_mutex_t multi_mutex_count;
static unsigned int multi_index;
static volatile unsigned int multi_index_count;

mt_defs();

#if ENABLE_THREADS
static void *consumer(void *arg)
{
    struct tdata *data = arg;
    size_t read = 0;

    while (read != data->buflen)
    {
        size_t left = data->buflen - read;
        left = left < data->len ? left : data->len;
        read += rb_read(data->rb, data->data + read * data->objsize, left);
    }

    return data;
}

static void *producer(void *arg)
{
    struct tdata *data = arg;
    size_t written = 0;

    while (written != data->buflen)
    {
        size_t left = data->buflen - written;
        left = left < data->len ? left : data->len;
        written += rb_write(data->rb, data->data + written * data->objsize, left);
    }

    return data;
}

static void *multi_producer(void *arg)
{
    struct rb *rb = arg;
    unsigned int index;

    for (;;)
    {
        pthread_mutex_lock(&multi_mutex);
        index = multi_index++;
        pthread_mutex_unlock(&multi_mutex);

        if (index >= (sizeof(data)/sizeof(*data)))
        {
            return NULL;
        }

        rb_write(rb, &index, 1);
    }
}

static void *multi_consumer(void *arg)
{
    struct rb *rb = arg;
    unsigned int index;

    for (;;)
    {
        int overflow;

        if (rb_read(rb, &index, 1) == -1)
        {
            /*
             * force exit received
             */

            return NULL;
        }

        overflow = index >= sizeof(data)/sizeof(*data);

        if (overflow)
        {
            continue;
        }

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
    struct rb *rb;
    unsigned int i, r;
    int count;

    multi_index = 0;
    multi_index_count = 0;
    count  = 0;
    memset(data, 0, sizeof(data));
    cons = malloc(t_num_consumers * sizeof(*cons));
    prod = malloc(t_num_producers * sizeof(*prod));

    rb = rb_new(8, sizeof(unsigned int), O_MULTITHREAD);

    pthread_mutex_init(&multi_mutex, NULL);
    pthread_mutex_init(&multi_mutex_count, NULL);
    for (i = 0; i != t_num_consumers; ++i)
    {
        pthread_create(&cons[i], NULL, multi_consumer, rb);
    }

    for (i = 0; i != t_num_producers; ++i)
    {
        pthread_create(&prod[i], NULL, multi_producer, rb);
    }

    /*
     * wait until all indexes has been consumed
     */
    while (count < sizeof(data)/sizeof(*data))
    {
        int buf[16];

        pthread_mutex_lock(&multi_mutex_count);
        count = multi_index_count;
        pthread_mutex_unlock(&multi_mutex_count);

        /*
         * while waiting, we randomly peek into rb, and to make sure,
         * peeking won't make a difference
         */

        rb_recv(rb, buf, rand() % 16, MSG_PEEK);
    }

    rb_stop(rb);

    for (i = 0; i != t_num_consumers; ++i)
    {
        pthread_join(cons[i], NULL);
    }

    for (i = 0; i != t_num_producers; ++i)
    {
        pthread_join(prod[i], NULL);
    }

    rb_destroy(rb);

    for (r = 0, i = 0; i < sizeof(data)/sizeof(*data); ++i)
    {
        r += (data[i] != 1);
    }

    mt_fail(r == 0);

    if (r != 0)
    {
        printf("num_consumers = %d, num_producers = %d\n",
                t_num_consumers, t_num_producers);
    }

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

    for (i = 0; i != t_objsize * buflen; ++i)
    {
        send_buf[i] = i;
        recv_buf[i] = 0;
    }

    rb = rb_new(t_rblen, t_objsize, O_MULTITHREAD);

    proddata.data = send_buf;
    proddata.len = t_writelen;
    proddata.objsize = t_objsize;
    proddata.rb = rb;
    proddata.buflen = buflen;

    consdata.data = recv_buf;
    consdata.len = t_readlen;
    consdata.objsize = t_objsize;
    consdata.rb = rb;
    consdata.buflen = buflen;

    pthread_create(&cons, NULL, consumer, &consdata);
    pthread_create(&prod, NULL, producer, &proddata);

    pthread_join(cons, NULL);
    pthread_join(prod, NULL);

    rc = memcmp(send_buf, recv_buf, t_objsize * buflen);
    mt_fail(rc == 0);

    if (rc)
    {
        printf("[%lu] a = %lu, b = %d, c = %d, d = %d\n",
                c, buflen, t_rblen, t_readlen, t_writelen);
    };

    rb_destroy(rb);
    free(send_buf);
    free(recv_buf);
}

static void multithread_eagain(void)
{
    char s[6] = {0, 1, 2, 3, 4, 5};
    char d[6];
    struct rb *rb;

    rb = rb_new(4, 1, O_MULTITHREAD | O_NONBLOCK);
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

    rb = rb_new(4, 1, O_MULTITHREAD);

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
    rb = rb_new(4, 1, O_NONBLOCK | O_MULTITHREAD);
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


static void invalid_read_write(void)
{
}

static void invalid_stop(void)
{
    struct rb *rb;
    rb = rb_new(4, 1, 0);
    mt_ferr(rb_stop(rb), EINVAL);
    rb_destroy(rb);
}

static void peeking(void)
{
    struct rb *rb;
    int v[8];
    int d[8];
    int i;

    for (i = 0; i != sizeof(v)/sizeof(*v); ++i)
    {
        d[i] = i;
    }

    memset(v, 0, sizeof(v));
    mt_assert(rb = rb_new(8, sizeof(int), 0));

    rb_write(rb, d, 4);
    rb_recv(rb, v, 2, MSG_PEEK);
    mt_fail(v[0] == 0);
    mt_fail(v[1] == 1);
    mt_fail(v[2] == 0);
    mt_fail(v[3] == 0);
    memset(v, 0, sizeof(v));
    rb_recv(rb, v, 6, MSG_PEEK);
    mt_fail(v[0] == 0);
    mt_fail(v[1] == 1);
    mt_fail(v[2] == 2);
    mt_fail(v[3] == 3);
    mt_fail(v[4] == 0);
    mt_fail(v[5] == 0);

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

    for (i = 0; i != t_objsize * buflen; ++i)
    {
        send_buf[i] = i;
        recv_buf[i] = 0;
    }

    rb = rb_new(t_rblen, t_objsize, 0);

    written = 0;
    read = 0;

    while (written != buflen || read != buflen)
    {
        if (written != buflen)
        {
            if (written + writelen > buflen)
            {
                writelen = buflen - written;
            }

            written += rb_write(rb, send_buf + written * t_objsize, writelen);
        }

        if (read != buflen)
        {
            if (read + readlen > buflen)
            {
                readlen = buflen - read;
            }

            read+= rb_read(rb, recv_buf + read * t_objsize, readlen);
        }
    }

    rc = memcmp(send_buf, recv_buf, buflen * t_objsize);
    mt_fail(rc == 0);

    if (rc)
    {
        printf("[%lu] a = %lu, b = %d, c = %d, d = %d\n",
                c, buflen, t_rblen, t_readlen, t_writelen);
    }

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

    for (i = 0; i != 1; i++)
    {
        int flags;

#if ENABLE_THREADS
        flags = i ? O_MULTITHREAD : 0;
#else
        /*
         * yup, if ENABLE_THREADS is 0, same code will be executed twice...
         * it's not a bug, it's a feature! MORE TESTS NEVER HURT!
         */

        flags = 0;
#endif
        rb = rb_new(8, 1, 0);
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
    unsigned char  rbs[10240];

    rb = rb_new(6, 1, 0);
    mt_fail(errno == EINVAL);
    mt_fail(rb == NULL);
    rb = rb_init(6, 1, 0, rbs);
    mt_fail(errno == EINVAL);
    mt_fail(rb == NULL);
}

static void enomem(void)
{
    struct rb  *rb;

    rb = rb_new((size_t)1 << ((sizeof(size_t) * 8) - 1), 1, 0);
    mt_fail(errno = ENOMEM);
    mt_fail(rb == NULL);
    mt_ferr(rb_destroy(rb), EINVAL);
}

static void einval(void)
{
    struct rb *rb;
    int v;
    unsigned char  rbs[10240];

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

    unsigned char  buf[rb_header_size() + 4 * sizeof(m)];
    struct rb  *rb;

    mt_assert(rb = rb_init(4, sizeof(m), 0, buf));
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

#if ENABLE_THREADS
    mt_assert(rb = rb_init(4, sizeof(m), O_MULTITHREAD, buf));
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

int main(void)
{
    unsigned int t_rblen_max = 128;
    unsigned int t_readlen_max = 128;
    unsigned int t_writelen_max = 128;
    unsigned int t_objsize_max = 128;

    unsigned int t_num_producers_max = 8;
    unsigned int t_num_consumers_max = 8;

    srand(time(NULL));

#if ENABLE_THREADS
    for (t_num_consumers = 1; t_num_consumers <= t_num_consumers_max;
         t_num_consumers++)
    {
        for (t_num_producers = 1; t_num_producers <= t_num_producers_max;
             t_num_producers++)
        {
            mt_run(multi_producers_consumers);
        }
    }
#endif

    for (t_rblen = 2; t_rblen < t_rblen_max; t_rblen *= 2)
    {
        for (t_readlen = 2; t_readlen < t_readlen_max;
             t_readlen += rand() % 128)
        {
            for (t_writelen = 2; t_writelen < t_writelen_max;
                 t_writelen += rand() % 128)
            {
                for (t_objsize = 2; t_objsize < t_objsize_max;
                     t_objsize += rand() % 128)
                {
#if ENABLE_THREADS
                    mt_run(multi_thread);
#endif
                    mt_run(single_thread);
                }
            }
        }
    }

    mt_run(peeking);
    mt_run(bad_count_value);
    mt_run(invalid_read_write);
    mt_run(multithread_flag);
    mt_run(nonblocking_flag);
    mt_run(singlethread_eagain);
    mt_run(discard);
    mt_run(count_and_space);
    mt_run(einval);
    /* mt_run(enomem); */
    mt_run(stack_init);

#if ENABLE_THREADS
    mt_run(multithread_eagain);
#endif

    mt_return();
}
