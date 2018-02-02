/* ==========================================================================
    Licensed under BSD 2clause license. See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */


#ifndef LIBRB_H
#define LIBRB_H 1

#include <stddef.h>

#ifndef MSG_PEEK
#define MSG_PEEK 2
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 00004000
#endif

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif

#define O_MULTITHREAD 0x00010000L
struct rb;

typedef long (*rb_send_f)(struct rb *, const void *, size_t , unsigned long);
typedef long (*rb_recv_f)(struct rb *, void *, size_t, unsigned long);

struct rb *rb_new(size_t, size_t, unsigned long);
long rb_read(struct rb *, void *, size_t);
long rb_recv(struct rb *, void *, size_t, unsigned long);
long rb_write(struct rb *, const void *, size_t);
long rb_send(struct rb *, const void *, size_t, unsigned long);

int rb_clear(struct rb *, int);
int rb_destroy(struct rb *);
int rb_stop(struct rb *);
size_t rb_discard(struct rb *, size_t);
const char *rb_version(char *, char *, char *);
size_t rb_count(const struct rb *);
size_t rb_space(const struct rb *);

#endif
