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

#define O_MULTITHREAD 0x10000000L
struct rb;

struct rb *rb_new(size_t, size_t, unsigned long);
long rb_read(struct rb *, void *, size_t);
long rb_recv(struct rb *, void *, size_t, unsigned long);
long rb_write(struct rb *, const void *, size_t);
long rb_send(struct rb *, const void *, size_t, unsigned long);

int rb_clear(struct rb *, int);
int rb_destroy(struct rb *);
int rb_stop(struct rb *);
long rb_discard(struct rb *, size_t);
const char *rb_version(char *, char *, char *);
long rb_count(struct rb *);
long rb_space(struct rb *);

#endif
