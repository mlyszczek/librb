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

struct rb *rb_new(size_t count, size_t object_size, unsigned long flags);
struct rb *rb_init(size_t count, size_t object_size, unsigned long flags,
    void *mem);
long rb_read(struct rb *rb, void *buffer, size_t count);
long rb_recv(struct rb *rb, void *buffer, size_t count, unsigned long flags);
long rb_write(struct rb *rb, const void *buffer, size_t count);
long rb_send(struct rb *rb, const void *buffer, size_t count,
    unsigned long flags);
long rb_fd_read(struct rb *rb, int fd, size_t count);
long rb_fd_recv(struct rb *rb, int fd, size_t count, unsigned long flags);
long rb_fd_write(struct rb *rb, int fd, size_t count);
long rb_fd_send(struct rb *rb, int fd, size_t count, unsigned long flags);

int rb_clear(struct rb *rb, int clear);
int rb_destroy(struct rb *rb);
int rb_cleanup(struct rb *rb);
int rb_stop(struct rb *rb);
int rb_stop_signal(struct rb *rb, int signum);
long rb_discard(struct rb *rb, size_t count);
const char *rb_version(char *major, char *minor, char *patch);
long rb_count(struct rb *rb);
long rb_space(struct rb *rb);
size_t rb_header_size(void);

int rb_posix_read(struct rb *rb, int fd, size_t count);
int rb_posix_recv(struct rb *rb, int fd, size_t count, unsigned long flags);
int rb_posix_write(struct rb *rb, int fd, size_t count);
int rb_posix_send(struct rb *rb, int fd, size_t count, unsigned long flags);

#endif
