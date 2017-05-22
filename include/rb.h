/* ==========================================================================
    Licensed under BSD 2clause license. See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */


#ifndef LIBRB_H
#define LIBRB_H 1

#include <stddef.h>

#ifdef LIBRB_PTHREAD

#include <pthread.h>
#include <sys/socket.h>

#define O_NONTHREAD 0x00010000L

struct rb;

typedef long (*rb_send_f)(struct rb *rb, const void *buffer, size_t len,
                          unsigned long flags);
typedef long (*rb_recv_f)(struct rb *rb, void *buffer, size_t len,
                          unsigned long flags);

#endif

#ifndef MSG_PEEK
#define MSG_PEEK 2
#endif

struct rb
{
    size_t head;
    size_t tail;
    size_t count;
    size_t object_size;
    unsigned long flags;

    unsigned char *buffer;

#ifdef LIBRB_PTHREAD
    pthread_mutex_t lock;
    pthread_cond_t wait_data;
    pthread_cond_t wait_room;

    rb_send_f send;
    rb_recv_f recv;
#endif

    int force_exit;
};

int rb_new(struct rb *rb, size_t count, size_t object_size, unsigned long flags);
long rb_read(struct rb *rb, void *buffer, size_t count);
long rb_recv(struct rb *rb, void *buffer, size_t count, unsigned long flags);
long rb_write(struct rb *rb, const void *buffer, size_t count);
long rb_send(struct rb *rb, const void *buffer, size_t count, unsigned long flags);

int rb_clear(struct rb *rb);
int rb_destroy(struct rb *rb);
const char *rb_version(char *major, char *minor, char *patch);
size_t rb_count(const struct rb *rb);
size_t rb_space(const struct rb *rb);

#endif
