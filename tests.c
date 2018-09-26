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

#define TEST_BUFFER 0
#define TEST_FD_FILE 1

#define MULTI_CONSUMERS 0
#define MULTI_PRODUCERS 1

struct tdata
{
    struct rb *rb;
    unsigned char *data;
    int fd;
    int len;
    int objsize;
    size_t buflen;
    int test_type;
};

struct multi_data
{
    struct rb *rb;
    const char *pipe;
    int fd;
};

static int t_rblen;
static int t_readlen;
static int t_writelen;
static int t_objsize;
static int t_multi_test_type;
static int multi;

static int t_num_producers;
static int t_num_consumers;
static int data[255];
static unsigned int multi_index;
static volatile unsigned int multi_index_count;

mt_defs();

#if ENABLE_THREADS
static pthread_mutex_t multi_mutex;
static pthread_mutex_t multi_mutex_count;

static void *consumer(void *arg)
{
    struct tdata *data = arg;
    size_t r = 0;

    while (r != data->buflen)
    {
        size_t left = data->buflen - r;
        left = left < data->len ? left : data->len;
        r += rb_read(data->rb, data->data + r * data->objsize, left);
    }

    return data;
}

static void *producer_file(void *arg)
{
    struct tdata *data = arg;
    size_t w = 0;

    while (w != data->buflen)
    {
        size_t left = data->buflen - w;
        left = left < data->len ? left : data->len;
        w += rb_posix_write(data->rb, data->fd, left);
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

static void *consumer_file(void *arg)
{
    struct tdata *data = arg;
    size_t r = 0;

    while (r != data->buflen)
    {
        size_t left = data->buflen - r;
        left = left < data->len ? left : data->len;
        r += rb_posix_read(data->rb, data->fd, left);
    }

    return data;
}

static void *multi_producer(void *arg)
{
    struct multi_data *d = arg;
    struct rb *rb = d->rb;
    int fd = d->fd;
    unsigned int index;

    for (;;)
    {
        if (fd == -1)
        {
            pthread_mutex_lock(&multi_mutex);
            index = multi_index++;
            pthread_mutex_unlock(&multi_mutex);

            if (index >= rb_array_size(data))
            {
                return NULL;
            }

            rb_write(rb, &index, 1);
        }
        else
        {
            if (rb_posix_write(rb, fd, 1) == -1)
            {
                if (errno == ECANCELED)
                {
                    /*
                     * stop, no more, rb is not valid any more
                     */

                    return NULL;
                }
            }
        }
    }
}

static void *multi_pipe_producer(void *arg)
{
    struct multi_data *d = arg;
    struct rb *rb = d->rb;
    unsigned int index;
    int fd;

#if defined(__OpenBSD__)
    fd = open(d->pipe, O_RDWR);
#else
    fd = open(d->pipe, O_WRONLY);
#endif

    for (;;)
    {
        pthread_mutex_lock(&multi_mutex);
        index = multi_index++;
        pthread_mutex_unlock(&multi_mutex);

        if (index >= rb_array_size(data))
        {
            close(fd);
            return NULL;
        }

        write(fd, &index, sizeof(index));
    }
}

static void *multi_consumer(void *arg)
{
    struct multi_data *d = arg;
    struct rb *rb = d->rb;
    int fd = d->fd;
    unsigned int index;

    for (;;)
    {
        int overflow;

        if (fd == -1)
        {
            if (rb_read(rb, &index, 1) == -1)
            {
                /*
                 * force exit received
                 */

                return NULL;
            }

            overflow = index >= rb_array_size(data);

            if (overflow)
            {
                continue;
            }

            data[index] = 1;
            pthread_mutex_lock(&multi_mutex_count);
            ++multi_index_count;
            pthread_mutex_unlock(&multi_mutex_count);

        }
        else
        {
            if (rb_posix_read(rb, fd, 1) == -1)
            {
                /*
                 * force exit received
                 */

                mt_fail(errno == ECANCELED);
                return NULL;
            }
        }
    }
}

static void *multi_pipe_consumer(void *arg)
{
    struct multi_data *d = arg;
    struct rb *rb = d->rb;
    unsigned int index;
    int fd;

#if defined(__OpenBSD__)
    fd = open(d->pipe, O_RDWR);
#else
    fd = open(d->pipe, O_RDONLY);
#endif

    for (;;)
    {
        unsigned int overflow;
        index = -1u;

        struct timeval tv;
        fd_set fds;
        int sact;

        tv.tv_sec = 0;
        tv.tv_usec = 1000;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        sact = select(fd + 1, &fds, NULL, NULL, &tv);

        if (sact == -1)
        {
            perror("select()");
            close(fd);
            return NULL;
        }

        if (sact == 0)
        {
            pthread_mutex_lock(&multi_mutex_count);
            index = multi_index_count;
            pthread_mutex_unlock(&multi_mutex_count);

            if (index >= rb_array_size(data))
            {
                /*
                 * we have consumed all there was to consume
                 */

                close(fd);
                return NULL;
            }

            continue;
        }


        if (read(fd, &index, sizeof(index)) == -1)
        {
            perror("read()");
            close(fd);
            return NULL;
        }

        overflow = index >= rb_array_size(data);

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
    struct multi_data tdata;
    unsigned int i, r;
    int count;

    multi_index = 0;
    multi_index_count = 0;
    count  = 0;
    memset(data, 0, sizeof(data));
    cons = malloc(t_num_consumers * sizeof(*cons));
    prod = malloc(t_num_producers * sizeof(*prod));

    tdata.rb = rb_new(8, sizeof(unsigned int), O_MULTITHREAD);
    tdata.fd = -1;

    pthread_mutex_init(&multi_mutex, NULL);
    pthread_mutex_init(&multi_mutex_count, NULL);
    for (i = 0; i != t_num_consumers; ++i)
    {
        pthread_create(&cons[i], NULL, multi_consumer, &tdata);
    }

    for (i = 0; i != t_num_producers; ++i)
    {
        pthread_create(&prod[i], NULL, multi_producer, &tdata);
    }

    /*
     * wait until all indexes has been consumed
     */
    while (count < rb_array_size(data))
    {
        int buf[16];

        pthread_mutex_lock(&multi_mutex_count);
        count = multi_index_count;
        pthread_mutex_unlock(&multi_mutex_count);

        /*
         * while waiting, we randomly peek into rb, and to make sure,
         * peeking won't make a difference
         */

        rb_recv(tdata.rb, buf, rand() % 16, MSG_PEEK);
    }

    rb_stop(tdata.rb);

    for (i = 0; i != t_num_consumers; ++i)
    {
        pthread_join(cons[i], NULL);
    }

    for (i = 0; i != t_num_producers; ++i)
    {
        pthread_join(prod[i], NULL);
    }

    rb_destroy(tdata.rb);

    for (r = 0, i = 0; i < rb_array_size(data); ++i)
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

static void multi_file_consumer_producer(void)
{
    pthread_t *prod;
    pthread_t *cons;
    pthread_t pipet;

    struct multi_data prod_data;
    struct multi_data cons_data;
    struct multi_data pipe_data;
    struct rb *rb;
    int fd;
    unsigned int i, r;
    int count;

    multi_index = 0;
    multi_index_count = 0;
    count = 0;
    memset(data, 0, sizeof(data));

    unlink("./rb-test-fifo");
    mkfifo("./rb-test-fifo", 0777);

    rb = rb_new(8, sizeof(unsigned int), O_MULTITHREAD);

    pthread_mutex_init(&multi_mutex, NULL);
    pthread_mutex_init(&multi_mutex_count, NULL);

    if (multi == MULTI_CONSUMERS)
    {
        /*
         *                   +-cons-+
         *                  /        \
         *          +------+          +------+
         * prod --> |  rb  |---cons---| pipe | --> pipet
         *          +------+          +------+
         *                  \        /
         *                   +-cons-+
         */
        prod = malloc(sizeof(*prod));
        cons = malloc(t_num_consumers * sizeof(*cons));

        pipe_data.pipe = "./rb-test-fifo";
        pthread_create(&pipet, NULL, multi_pipe_consumer, &pipe_data);

        prod_data.rb = rb;
        prod_data.fd = -1;
        pthread_create(prod, NULL, multi_producer, &prod_data);

        cons_data.rb = rb;
#if defined(__OpenBSD__)
        cons_data.fd = open("./rb-test-fifo", O_RDWR);
#else
        cons_data.fd = open("./rb-test-fifo", O_WRONLY);
#endif
        for (i = 0; i != t_num_consumers; ++i)
        {
            pthread_create(&cons[i], NULL, multi_consumer, &cons_data);
        }
    }
    else
    {
        prod = malloc(t_num_producers * sizeof(*prod));
        cons = malloc(sizeof(*cons));

        pipe_data.pipe = "./rb-test-fifo";
        pthread_create(&pipet, NULL, multi_pipe_producer, &pipe_data);

        cons_data.rb = rb;
        cons_data.fd = -1;
        pthread_create(cons, NULL, multi_consumer, &cons_data);

        prod_data.rb = rb;
#if defined(__OpenBSD__)
        prod_data.fd = open("./rb-test-fifo", O_RDWR);
#else
        prod_data.fd = open("./rb-test-fifo", O_RDONLY);
#endif
        for (i = 0; i != t_num_producers; ++i)
        {
            pthread_create(&prod[i], NULL, multi_producer, &prod_data);
        }
    }

    while(count < rb_array_size(data))
    {
        int buf[16];

        pthread_mutex_lock(&multi_mutex_count);
        count = multi_index_count;
        pthread_mutex_unlock(&multi_mutex_count);

        rb_recv(rb, buf, rand() % 16, MSG_PEEK);
    }

    rb_stop(rb);

    if (multi == MULTI_CONSUMERS)
    {
        for (i = 0; i != t_num_consumers; ++i)
        {
            pthread_join(cons[i], NULL);
        }
        pthread_join(*prod, NULL);
        pthread_join(pipet, NULL);
        close(cons_data.fd);
    }
    else
    {
        pthread_join(*cons, NULL);
        pthread_join(pipet, NULL);
        for (i = 0; i != t_num_producers; ++i)
        {
            pthread_join(prod[i], NULL);
        }
        close(prod_data.fd);
    }

    rb_destroy(rb);

    for (r = 0, i = 0; i < rb_array_size(data); ++i)
    {
        r += data[i] != 1;
    }

    mt_fail(r == 0);

    free(cons);
    free(prod);
    pthread_mutex_destroy(&multi_mutex);
    pthread_mutex_destroy(&multi_mutex_count);
}

static void multi_thread(void)
{
    pthread_t cons;
    pthread_t prod;
    pthread_t cons_file;
    pthread_t prod_file;

    size_t buflen = t_readlen > t_writelen ? t_readlen : t_writelen;
    unsigned char *send_buf = malloc(t_objsize * buflen);
    unsigned char *recv_buf = malloc(t_objsize * buflen);
    size_t i;
    int rc;
    int fd = -1;
    int fd2 = -1;

    struct rb *rb;
    struct rb *rb2;
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
    proddata.fd = -1;

    consdata.data = recv_buf;
    consdata.len = t_readlen;
    consdata.objsize = t_objsize;
    consdata.rb = rb;
    consdata.buflen = buflen;
    consdata.fd = -1;

    if (t_multi_test_type == TEST_FD_FILE)
    {
        fd2 = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644);
        fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || fd2 < 0)
        {
            perror("open");
            mt_assert(0);
        }

        proddata.fd = fd;
        consdata.fd = fd2;
        rb2 = rb_new(t_rblen, t_objsize, O_MULTITHREAD);
        consdata.rb = rb2;

        pthread_create(&cons_file, NULL, consumer_file, &proddata);
        pthread_create(&prod_file, NULL, producer_file, &consdata);
    }

    pthread_create(&cons, NULL, consumer, &consdata);
    pthread_create(&prod, NULL, producer, &proddata);

    pthread_join(cons, NULL);
    pthread_join(prod, NULL);

    if (t_multi_test_type == TEST_FD_FILE)
    {
        pthread_join(cons_file, NULL);
        pthread_join(prod_file, NULL);
        rb_destroy(rb2);
    }

    rc = memcmp(send_buf, recv_buf, t_objsize * buflen);
    mt_fail(rc == 0);

    if (rc)
    {
        printf("[%lu] a = %lu, b = %d, c = %d, d = %d\n",
                c, buflen, t_rblen, t_readlen, t_writelen);
    };

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

#if ENABLE_THREADS && ENABLE_POSIX_CALLS

static void *client(void *arg)
{
    struct sockaddr_in saddr;
    int fd;

    memset(&saddr, 0x00, sizeof(saddr));
    saddr.sin_addr.s_addr = htonl(0x7f000001ul); /* 127.0.0.1 */
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(*(unsigned short *)arg);

    if ((fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("socket()");
        return NULL;
    }

    for (;;)
    {
        if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
        {
            if (errno == ECONNREFUSED)
            {
                continue;
            }

            perror("socket()");
            close(fd);
            return NULL;
        }

        break;
    }

    if (write(fd, "msg", 3) != 3)
    {
        perror("write()");
        close(fd);
        return NULL;
    }

    close(fd);
    return NULL;
}

static void socket_disconnect(void)
{
    struct sockaddr_in saddr;
    struct rb *rb;
    int sfd;
    int cfd;
    pthread_t client_t;
    unsigned short port;

    rb = rb_new(16, 1, O_MULTITHREAD);

    while ((port = rand() & 0xffff) < 10000);
    memset(&saddr, 0x00, sizeof(saddr));
    saddr.sin_addr.s_addr = htonl(0x7f000001ul); /* 127.0.0.1 */
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket()");
        mt_assert(0);
    }

    if (bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
    {
        perror("bind()");
        close(sfd);
        mt_assert(0);
    }

    if (listen(sfd, 1) != 0)
    {
        perror("listen()");
        close(sfd);
        mt_assert(0);
    }

    pthread_create(&client_t, NULL, client, &port);

    if ((cfd = accept(sfd, NULL, NULL)) < 0)
    {
        perror("accept()");
        close(sfd);
        mt_assert(0);
    }

    mt_fail(rb_posix_write(rb, cfd, 6) == 3);
    mt_fail(rb_posix_write(rb, cfd, 6) == 0);

    pthread_join(client_t, NULL);
    rb_destroy(rb);
    close(sfd);
}

#endif

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

    for (i = 0; i != rb_array_size(v); ++i)
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

    /*
     * now with overlaped memory
     */

    memset(v, 0, sizeof(v));
    mt_fail(rb_discard(rb, 7) == 4);
    rb_write(rb, d, 6);
    rb_recv(rb, v, 6, MSG_PEEK);
    mt_fail(v[0] == 0);
    mt_fail(v[1] == 1);
    mt_fail(v[2] == 2);
    mt_fail(v[3] == 3);
    mt_fail(v[4] == 4);
    mt_fail(v[5] == 5);
    memset(v, 0, sizeof(v));
    rb_recv(rb, v, 6, MSG_PEEK);
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
            long w;
            if (written + writelen > buflen)
            {
                writelen = buflen - written;
            }

            w = rb_write(rb, send_buf + written * t_objsize, writelen);

            if (w == -1)
            {
                break;
            }

            written += w;
        }

        if (read != buflen)
        {
            long r;

            if (read + readlen > buflen)
            {
                readlen = buflen - read;
            }

            r = rb_read(rb, recv_buf + read * t_objsize, readlen);

            if (r == -1)
            {
                break;
            }

            read += r;
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

static void fd_write_single(void)
{
    int fd;
    int i;
    int pos;
    char data[128];
    char rdata[128];
    struct rb *rb;

    /*
     * prepare file with some content to read into rb
     */

    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("posix_read_single; prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != sizeof(data); ++i)
    {
        data[i] = rand() % 127;
    }

    write(fd, data, sizeof(data));
    lseek(fd, 0, SEEK_SET);

    rb = rb_new(sizeof(data), sizeof(char), 0);

    /*
     * now let's read some data from fd to ring buffer
     */

    mt_fail(rb_posix_write(rb, fd, 100) == 100);
    mt_fail(rb_read(rb, rdata, 100) == 100);
    mt_fail(memcmp(rdata, data, 100) == 0);

    mt_fail(rb_posix_write(rb, fd, 100) == 28);
    mt_fail(rb_read(rb, rdata, 100) == 28);
    mt_fail(memcmp(rdata, data + 100, 28) == 0);

    rb_destroy(rb);
    close(fd);
}

static void fd_write_single_multibyte(void)
{
    int fd;
    int i;
    int pos;
    int data[128];
    int rdata[128];
    struct rb *rb;

    /*
     * prepare file with some content to read into rb
     */

    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("posix_read_single; prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand();
    }

    write(fd, data, sizeof(data));
    lseek(fd, 0, SEEK_SET);

    rb = rb_new(128, sizeof(int), 0);

    mt_fail(rb_posix_write(rb, fd, 100) == 100);
    mt_fail(rb_read(rb, rdata, 100) == 100);

    for (i = 0; i != 100; ++i)
    {
        mt_fail(data[i] == rdata[i]);
    }

    mt_fail(rb_posix_write(rb, fd, 100) == 28);
    mt_fail(rb_read(rb, rdata, 100) == 28);
    for (i = 0; i != 28; ++i)
    {
        mt_fail(data[i + 100] == rdata[i]);
    }

    /*
     * we read all data from file, read() on our fd will return 0 (end of file),
     * so rb will return 0 as well
     */
    mt_fail(rb_posix_write(rb, fd, 10) == 0);

    rb_destroy(rb);
    close(fd);
}

static void fd_write_single_overlap(void)
{
    int fd;
    int i;
    int pos;
    int data[128];
    int rdata[128];
    struct rb *rb;

    /*
     * prepare file with some content to read into rb
     */

    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("posix_read_single; prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand();
    }

    write(fd, data, sizeof(data));
    lseek(fd, 0, SEEK_SET);

    rb = rb_new(16, sizeof(int), 0);

    /*
     * cause overlap condition
     */

    mt_fail(rb_posix_write(rb, fd, 10) == 10);
    mt_fail(rb_read(rb, rdata, 10) == 10);

    /*
     * and check overlap set
     */

    mt_fail(rb_posix_write(rb, fd, 15) == 15);
    mt_fail(rb_read(rb, rdata, 15) == 15);
    for (i = 0; i != 15; ++i)
    {
        mt_fail(data[i + 10] == rdata[i]);
    }

    rb_destroy(rb);
    close(fd);
}

static void fd_write_single_partial(void)
{
    int fd;
    int i;
    int pos;
    char data[128];
    char rdata[128];
    struct rb *rb;

    /*
     * prepare file with some content to read into rb
     */

    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("posix_read_single; prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand() % 127;
    }

    write(fd, data, sizeof(data));

    rb = rb_new(16, 1, 0);


    /*
     * partial read no overlap
     */

    lseek(fd, 120, SEEK_SET);
    mt_fail(rb_posix_write(rb, fd, 15) == 8);
    mt_fail(rb_read(rb, rdata, 15) == 8);
    mt_fail(memcmp(rdata, data + 120, 8) == 0);

    /*
     * partial read with overlap
     */

    lseek(fd, 118, SEEK_SET);
    mt_fail(rb_posix_write(rb, fd, 15) == 10);
    mt_fail(rb_read(rb, rdata, 15) == 10);
    mt_fail(memcmp(rdata, data + 118, 10) == 0);

    /*
     * partial read, overlap, with first read satisfied fully
     */

    mt_fok(rb_clear(rb, 0));
    lseek(fd, 113, SEEK_SET);
    mt_fail(rb_posix_write(rb, fd, 5) == 5);
    mt_fail(rb_discard(rb, 5) == 5);
    mt_fail(rb_posix_write(rb, fd, 15) == 10);
    mt_fail(rb_read(rb, rdata, 15) == 10);
    mt_fail(memcmp(rdata, data + 118, 10) == 0);

    close(fd);
    rb_destroy(rb);
}

static void fd_write_enosys(void)
{
    struct rb *rb;
    rb = rb_new(16, 1, 0);
    mt_ferr(rb_posix_write(rb, 0, 1), ENOSYS);
    mt_ferr(rb_posix_send(rb, 0, 1, 0), ENOSYS);
}

static void fd_read_single(void)
{
    struct rb *rb;
    int fd;
    int i;
    char data[128];
    char rdata[128];


    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand() % 127;
    }

    rb = rb_new(128, 1, 0);

    mt_fail(rb_write(rb, data, 100) == 100);
    mt_fail(rb_posix_read(rb, fd, 100) == 100);
    mt_fail(rb_write(rb, data + 100, 28) == 28);
    mt_fail(rb_posix_read(rb, fd, 28) == 28);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 128) == 128);
    mt_fail(memcmp(data, rdata, 128) == 0);
    mt_fail(read(fd, rdata, 1) == 0);

    rb_destroy(rb);
    close(fd);
}


static void fd_read_single_multibyte(void)
{
    struct rb *rb;
    int fd;
    int i;
    int data[128];
    int rdata[128];


    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand();
    }

    rb = rb_new(128, sizeof(int), 0);

    mt_fail(rb_write(rb, data, 100) == 100);
    mt_fail(rb_posix_read(rb, fd, 100) == 100);
    mt_fail(rb_write(rb, data + 100, 28) == 28);
    mt_fail(rb_posix_read(rb, fd, 28) == 28);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 128 * sizeof(int)) == 128 * sizeof(int));

    for (i = 0; i != 128; ++i)
    {
        mt_fail(rdata[i] == data[i]);
    }
    mt_fail(read(fd, rdata, 1) == 0);

    rb_destroy(rb);
    close(fd);

}

static void fd_read_single_overlap(void)
{
    struct rb *rb;
    int fd;
    int i;
    char data[128];
    char rdata[128];

    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand() % 127;
    }

    rb = rb_new(16, 1, 0);

    /*
     * cause overlap condition
     */

    mt_fail(rb_write(rb, data, 10) == 10);
    mt_fail(rb_posix_read(rb, fd, 10) == 10);

    /*
     * and check overlap set
     */

    mt_fail(rb_write(rb, data + 10, 15) == 15);
    mt_fail(rb_posix_read(rb, fd, 15) == 15);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 25) == 25);
    mt_fail(memcmp(rdata, data, 25) == 0);
    mt_fail(read(fd, rdata, 1) == 0);

    /*
     * another read to check rb is consistent
     */

    mt_fail(rb_write(rb, data + 25, 15) == 15);
    mt_fail(rb_posix_read(rb, fd, 15) == 15);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 40) == 40);
    mt_fail(memcmp(rdata, data, 40) == 0);
    mt_fail(read(fd, rdata, 1) == 0);

    rb_destroy(rb);
    close(fd);
}


static void fd_read_single_partial(void)
{
    struct rb *rb;
    int fd;
    int i;
    char data[128];
    char rdata[128];

    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = i;
    }

    rb = rb_new(16, 1, 0);

    /*
     * partial write no overlap
     */

    mt_fail(rb_write(rb, data, 10) == 10);
    mt_fail(rb_posix_read(rb, fd, 15) == 10);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 20) == 10);
    mt_fail(memcmp(rdata, data, 10) == 0);

    /*
     * partial write with overlap
     */

    mt_fail(rb_write(rb, data + 10, 15) == 15);
    mt_fail(rb_posix_read(rb, fd, 20) == 15);
    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 25) == 25);
    mt_fail(memcmp(rdata, data, 25) == 0);

    /*
     * partial write, overlap, with first write satisfied fully
     */

    mt_fok(rb_clear(rb, 0));
    mt_fail(rb_write(rb, data, 5) == 5);
    mt_fail(rb_discard(rb, 5) == 5);
    mt_fail(rb_write(rb, data + 25, 10) == 10);
    mt_fail(rb_posix_read(rb, fd, 15) == 10);
    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 35) == 35);
    mt_fail(memcmp(rdata, data, 35) == 0);

    rb_destroy(rb);
    close(fd);
}

static void fd_read_single_peek(void)
{
    struct rb *rb;
    int fd;
    int i;
    char data[128];
    char rdata[128];


    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand() % 127;
    }

    rb = rb_new(32, 1, 0);

    mt_fail(rb_write(rb, data, 30) == 30);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 30) == 30);
    mt_fail(memcmp(data, rdata, 10) == 0);
    mt_fail(memcmp(data, rdata + 10, 10) == 0);
    mt_fail(memcmp(data, rdata + 20, 10) == 0);
    mt_fail(read(fd, rdata, 1) == 0);


    mt_fail(rb_posix_recv(rb, fd, 40, MSG_PEEK) == 30);
    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 60) == 60);
    mt_fail(memcmp(data, rdata, 10) == 0);
    mt_fail(memcmp(data, rdata + 10, 10) == 0);
    mt_fail(memcmp(data, rdata + 20, 10) == 0);
    mt_fail(memcmp(data, rdata + 30, 30) == 0);
    mt_fail(read(fd, rdata, 1) == 0);


    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 80) == 80);
    mt_fail(memcmp(data, rdata, 10) == 0);
    mt_fail(memcmp(data, rdata + 10, 10) == 0);
    mt_fail(memcmp(data, rdata + 20, 10) == 0);
    mt_fail(memcmp(data, rdata + 30, 30) == 0);
    mt_fail(memcmp(data, rdata + 60, 10) == 0);
    mt_fail(memcmp(data, rdata + 70, 10) == 0);
    mt_fail(read(fd, rdata, 1) == 0);

    rb_destroy(rb);
    close(fd);
}

static void fd_read_single_peek_overlap(void)
{
    struct rb *rb;
    int fd;
    int i;
    char data[128];
    char rdata[128];


    if ((fd = open("./rb-test-file", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        perror("prepare; open");
        mt_assert(0);
    }

    for (i = 0; i != 128; ++i)
    {
        data[i] = rand() % 127;
    }

    rb = rb_new(32, 1, 0);

    mt_fail(rb_write(rb, data, 16) == 16);
    mt_fail(rb_discard(rb, 16) == 16);

    mt_fail(rb_write(rb, data, 30) == 30);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 30) == 30);
    mt_fail(memcmp(data, rdata, 10) == 0);
    mt_fail(memcmp(data, rdata + 10, 10) == 0);
    mt_fail(memcmp(data, rdata + 20, 10) == 0);
    mt_fail(read(fd, rdata, 1) == 0);


    mt_fail(rb_posix_recv(rb, fd, 40, MSG_PEEK) == 30);
    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 60) == 60);
    mt_fail(memcmp(data, rdata, 10) == 0);
    mt_fail(memcmp(data, rdata + 10, 10) == 0);
    mt_fail(memcmp(data, rdata + 20, 10) == 0);
    mt_fail(memcmp(data, rdata + 30, 30) == 0);
    mt_fail(read(fd, rdata, 1) == 0);


    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);
    mt_fail(rb_posix_recv(rb, fd, 10, MSG_PEEK) == 10);

    lseek(fd, 0, SEEK_SET);
    mt_fail(read(fd, rdata, 80) == 80);
    mt_fail(memcmp(data, rdata, 10) == 0);
    mt_fail(memcmp(data, rdata + 10, 10) == 0);
    mt_fail(memcmp(data, rdata + 20, 10) == 0);
    mt_fail(memcmp(data, rdata + 30, 30) == 0);
    mt_fail(memcmp(data, rdata + 60, 10) == 0);
    mt_fail(memcmp(data, rdata + 70, 10) == 0);
    mt_fail(read(fd, rdata, 1) == 0);

    rb_destroy(rb);
    close(fd);
}

static void fd_read_enosys(void)
{
    struct rb *rb;
    rb = rb_new(16, 1, 0);
    mt_ferr(rb_posix_read(rb, 0, 1), ENOSYS);
    mt_ferr(rb_posix_recv(rb, 0, 1, 0), ENOSYS);
    rb_destroy(rb);
}


int main(void)
{
    srand(time(NULL));
    //mt_return();
    unsigned int t_rblen_max = 32;
    unsigned int t_readlen_max = 32;
    unsigned int t_writelen_max = 32;
    unsigned int t_objsize_max = 32;

    unsigned int t_num_producers_max = 8;
    unsigned int t_num_consumers_max = 8;
    char name[128];

#if ENABLE_THREADS
    for (t_num_consumers = 1; t_num_consumers <= t_num_consumers_max;
         t_num_consumers++)
    {
        for (t_num_producers = 1; t_num_producers <= t_num_producers_max;
             t_num_producers++)
        {
            sprintf(name, "multi_producers_consumers producers: %d "
                "consumers %d", t_num_producers, t_num_consumers);
            mt_run_named(multi_producers_consumers, name);

#   if ENABLE_POSIX_CALLS

            sprintf(name, "multi_file_consumer_producer: MULTI_CONSUMERS "
                "prod: %d, cons: %d", t_num_producers, t_num_consumers);
            multi = MULTI_CONSUMERS;
            mt_run_named(multi_file_consumer_producer, name);

            sprintf(name, "multi_file_consumer_producer: MULTI_PRODUCERS "
                "prod: %d, cons: %d", t_num_producers, t_num_consumers);
            multi = MULTI_PRODUCERS;
            mt_run_named(multi_file_consumer_producer, name);

#   endif

        }
    }
#endif

    for (t_rblen = 2; t_rblen <= t_rblen_max; t_rblen *= 2)
    {
        for (t_readlen = 2; t_readlen <= t_readlen_max;
             t_readlen *= 2)
        {
            for (t_writelen = 2; t_writelen <= t_writelen_max;
                 t_writelen *= 2)
            {
                for (t_objsize = 2; t_objsize <= t_objsize_max;
                     t_objsize *= 2)
                {
                    t_multi_test_type = TEST_BUFFER;

#if ENABLE_THREADS

                    sprintf(name, "multi_thread with buffer %3d, %3d, %3d, %3d",
                        t_rblen, t_readlen, t_writelen, t_objsize);
                    mt_run_named(multi_thread, name);

#endif

                    sprintf(name, "singl_thread with buffer %3d, %3d, %3d, %3d",
                        t_rblen, t_readlen, t_writelen, t_objsize);
                    mt_run_named(single_thread, name);

#if ENABLE_POSIX_CALLS

                    t_multi_test_type = TEST_FD_FILE;

#   if ENABLE_THREADS

                    sprintf(name, "multi_thread with file %3d, %3d, %3d, %3d",
                            t_rblen, t_readlen, t_writelen, t_objsize);
                    mt_run_named(multi_thread, name);

#   endif

                    sprintf(name, "singl_thread with file %3d, %3d, %3d, %3d",
                            t_rblen, t_readlen, t_writelen, t_objsize);
                    mt_run_named(single_thread, name);

#endif

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

#if ENABLE_POSIX_CALLS
    mt_run(fd_write_single);
    mt_run(fd_write_single_multibyte);
    mt_run(fd_write_single_overlap);
    mt_run(fd_write_single_partial);
    mt_run(fd_read_single);
    mt_run(fd_read_single_multibyte);
    mt_run(fd_read_single_overlap);
    mt_run(fd_read_single_partial);
    mt_run(fd_read_single_peek);
    mt_run(fd_read_single_peek_overlap);
#else
    mt_run(fd_write_enosys);
    mt_run(fd_read_enosys);
#endif

#if ENABLE_THREADS && ENABLE_POSIX_CALLS
    mt_run(socket_disconnect);
#endif

    unlink("./rb-test-file");
    unlink("./rb-test-fifo");
    mt_return();
}
