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
#include <sys/syscall.h>
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

    multi_index = 0;
    multi_index_count = 0;
    memset(data, 0, sizeof(data));
    cons = malloc(t_num_consumers * sizeof(*cons));
    prod = malloc(t_num_producers * sizeof(*prod));

    rb = rb_new(8, sizeof(unsigned int), 0);

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
    while (multi_index_count < sizeof(data)/sizeof(*data));

    rb_destroy(rb);

    for (i = 0; i != t_num_consumers; ++i)
    {
        pthread_join(cons[i], NULL);
    }

    for (i = 0; i != t_num_producers; ++i)
    {
        pthread_join(prod[i], NULL);
    }

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

    rb = rb_new(t_rblen, t_objsize, 0);

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
#endif

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
    int flags;
    size_t i;
    int rc;

    c++;

    for (i = 0; i != t_objsize * buflen; ++i)
    {
        send_buf[i] = i;
        recv_buf[i] = 0;
    }

    flags = 0;
#if ENABLE_THREADS
    flags = O_NONBLOCK | O_NONTHREAD;
#endif

    rb = rb_new(t_rblen, t_objsize, flags);

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

int main(void)
{
    unsigned int t_rblen_max = 1024;
    unsigned int t_readlen_max = 1024;
    unsigned int t_writelen_max = 1024;
    unsigned int t_objsize_max = 1024;

    unsigned int t_num_producers_max = 256;
    unsigned int t_num_consumers_max = 256;

    srand(time(NULL));

#if ENABLE_THREADS
    for (t_num_consumers = 2; t_num_consumers < t_num_consumers_max;
         t_num_consumers += rand() % 16)
    {
        for (t_num_producers = 2; t_num_producers < t_num_producers_max;
             t_num_producers += rand() % 16)
        {
            mt_run(multi_producers_consumers);
        }
    }
#endif

    //mt_return();

    for (t_rblen = 2; t_rblen < t_rblen_max; t_rblen *= 2)
    {
        for (t_readlen = 2; t_readlen < t_readlen_max;
             t_readlen += rand() % 512)
        {
            for (t_writelen = 2; t_writelen < t_writelen_max;
                 t_writelen += rand() % 512)
            {
                for (t_objsize = 2; t_objsize < t_objsize_max;
                     t_objsize += rand() % 512)
                {
#if ENABLE_THREADS
                    mt_run(multi_thread);
#endif
                    mt_run(single_thread);
                }
            }
        }
    }

    mt_return();
}
