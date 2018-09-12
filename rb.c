/* ==========================================================================
    Licensed under BSD 2clause license. See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */


/* ==========================================================================
      _               __            __           __   ____ _  __
     (_)____   _____ / /__  __ ____/ /___   ____/ /  / __/(_)/ /___   _____
    / // __ \ / ___// // / / // __  // _ \ / __  /  / /_ / // // _ \ / ___/
   / // / / // /__ / // /_/ // /_/ //  __// /_/ /  / __// // //  __/(__  )
  /_//_/ /_/ \___//_/ \__,_/ \__,_/ \___/ \__,_/  /_/  /_//_/ \___//____/

   ========================================================================== */


#ifdef TRACE_LOG
#   define _GNU_SOURCE
#   include <stdio.h>
#   include <string.h>
#   include <syscall.h>
#   include <unistd.h>

#   define trace(x, ...) fprintf(stderr, "[%s:%d:%s():%ld] " x "\n", \
        __FILE__, __LINE__, __func__, syscall(SYS_gettid), ##__VA_ARGS__)
#else
#   define trace(x, ...)
#endif

#if HAVE_CONFIG_H
#   include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_ASSERT_H
#   include <assert.h>
#else /* HAVE_ASSERT_H */
#   define assert(x)
#endif /* HAVE_ASSERT_H */

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if ENABLE_THREADS
#   include <fcntl.h>
#   include <pthread.h>
#   include <sys/socket.h>
#   include <sys/time.h>
#   if ENABLE_POSIX_CALLS
#       include <signal.h>
#   endif /* ENABLE_POSIX_CALLS */
#endif /* ENABLE_THREADS */

#if ENABLE_POSIX_CALLS
#   include <unistd.h>
#endif /* ENABLE_POSIX_CALLS */

#include "rb.h"
#include "valid.h"


/* ==========================================================================
                  _                __           __
    ____   _____ (_)_   __ ____ _ / /_ ___     / /_ __  __ ____   ___   _____
   / __ \ / ___// /| | / // __ `// __// _ \   / __// / / // __ \ / _ \ / ___/
  / /_/ // /   / / | |/ // /_/ // /_ /  __/  / /_ / /_/ // /_/ //  __/(__  )
 / .___//_/   /_/  |___/ \__,_/ \__/ \___/   \__/ \__, // .___/ \___//____/
/_/                                              /____//_/
   ========================================================================== */


#if ENABLE_POSIX_CALLS

/*
 * sadly there is no portable pthread_t invalid value like '0', so  we  need
 * used field to know if field in blocked threads list is empty or not.
 */

struct blocked_threads
{
    pthread_t  thread;  /* blocked thread */
    int        valid;   /* if set, thread is valid */
};

#endif /* ENABLE_POSIX_CALLS */

/*
 * Ring buffer information.  This needs to be  hidden  in  c,  because  some
 * fields might not be accessible depending on compilation choices.  Imagine
 * these beautiful segfaults, when shared library would be compiled with all
 * fields, and application using library would  be  compiled  without  these
 * fields - it would allocate less memory on stack than it would be  needed.
 * We use malloc anyway to reserve memory for buffer, so it  is  not  a  big
 * deal to reserve memory also for this structure
 */


struct rb
{
    size_t           head;        /* pointer to buffer's head */
    size_t           tail;        /* pointer to buffer's tail */
    size_t           count;       /* maximum number of elements in buffer */
    size_t           object_size; /* size of a single object in buffer */
    unsigned long    flags;       /* flags used with buffer */
    unsigned char   *buffer;      /* pointer to ring buffer in memory */

#if ENABLE_THREADS

    pthread_mutex_t  lock;        /* mutex for concurrent access */
    pthread_cond_t   wait_data;   /* ca, will block if buffer is empty */
    pthread_cond_t   wait_room;   /* ca, will block if buffer is full */
    pthread_t        stop_thread; /* thread to force thread to exit send/recv */
    int              stopped_all; /* when set no threads are in send/recv */
    int              force_exit;  /* if set, library will stop all operations */

#   if ENABLE_POSIX_CALLS

    struct blocked_threads *blocked_threads; /* blocked threads in rb */
    int              curr_blocked;  /* current number of threads in read() */
    int              max_blocked;   /* size of blocked_threads array */
    int              signum;        /* signal to send when stopping blocked
                                       threads */

#   endif /* ENABLE_POSIX_CALLS */

#endif /* ENABLE_THREADS */
};


/* ==========================================================================
                                   _                __
                     ____   _____ (_)_   __ ____ _ / /_ ___
                    / __ \ / ___// /| | / // __ `// __// _ \
                   / /_/ // /   / / | |/ // /_/ // /_ /  __/
                  / .___//_/   /_/  |___/ \__,_/ \__/ \___/
                 /_/
               ____                     __   _
              / __/__  __ ____   _____ / /_ (_)____   ____   _____
             / /_ / / / // __ \ / ___// __// // __ \ / __ \ / ___/
            / __// /_/ // / / // /__ / /_ / // /_/ // / / /(__  )
           /_/   \__,_//_/ /_/ \___/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


/* ==========================================================================
    Calculates number of elements in ring buffer.  ns stands for not safe as
    in there are no checks.
   ========================================================================== */


static size_t rb_count_ns
(
    const struct rb  *rb  /* rb object */
)
{
    return (rb->head - rb->tail) & (rb->count - 1);
}


/* ==========================================================================
    Calculates how many elements can be pushed into ring buffer.   ns stands
    for nos safe as in there are no checks.
   ========================================================================== */


static size_t rb_space_ns
(
    const struct rb  *rb  /* rb object */
)
{
    return (rb->tail - (rb->head + 1)) & (rb->count - 1);
}


/* ==========================================================================
    Calculates number of elements in ring buffer until  the  end  of  buffer
    memory.  If elements don't overlap memory, function acts  like  rb_count
   ========================================================================== */


static size_t rb_count_end
(
    const struct rb  *rb  /* rb object */
)
{
    size_t            end;
    size_t            n;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    end = rb->count - rb->tail;
    n = (rb->head + end) & (rb->count - 1);

    return n < end ? n : end;
}


/* ==========================================================================
    Calculates how many elements can be  pushed  into  ring  buffer  without
    overlapping memory
   ========================================================================== */


static size_t rb_space_end
(
    const struct rb  *rb  /* rb object */
)
{
    size_t            end;
    size_t            n;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    end = rb->count - 1 - rb->head;
    n = (end + rb->tail) & (rb->count - 1);

    return n <= end ? n : end + 1;
}


/* ==========================================================================
    Checks if number x is exactly power of two number (ie.  1, 2, 4, 8,  16)
   ========================================================================== */


static int rb_is_power_of_two
(
    size_t  x  /* number to check */
)
{
    return (x != 0) && ((x & (~x + 1)) == x);
}


/* ==========================================================================
    Signal action handler. It's called when we signal blocked thread to exit
    blocked system call, it does nothing, it's here so we don't crash.
   ========================================================================== */


#if ENABLE_THREADS && ENABLE_POSIX_CALLS

static void rb_sigaction(int signum)
{
    return;
}

#endif /* ENABLE_THREADS && ENABLE_POSIX_CALLS */


/* ==========================================================================
    This function will add currently executing thread to the list of blocked
    threads. It will try to allocate more memory if it detects all slots are
    used up.

    On success 0 is returned, on error -1. Error can be returned only when
    realloc fails - that is there is not enough memory in the sytem.
   ========================================================================== */


#if ENABLE_THREADS && ENABLE_POSIX_CALLS

static int rb_add_blocked_thread
(
    struct rb  *rb  /* rb object */
)
{
    int         i;  /* just an iterator */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    if (rb->curr_blocked >= rb->max_blocked)
    {
        void *p;  /* new pointer for blocked threads */
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        /*
         * all slots for blocked threads are used up, we  need  to  allocate
         * more by doubling available memory
         */

        p = realloc(rb->blocked_threads,
            2 * rb->max_blocked * sizeof(struct blocked_threads));

        if (p == NULL)
        {
            /*
             * failed to realloc memory, we  return  error  without  chaning
             * anything in rb object
             */

            return -1;
        }

        /*
         * realocation was a success, we can now change rb object to reflect
         * new memory
         */

        rb->blocked_threads = p;
        rb->max_blocked *= 2;

        /*
         * one thing left, new memory we got from realloc contains  garbage,
         * so we need to initialize it to 0.  We just doubled  memory  size,
         * so only second half of memory needs to be zeroed.
         */

        memset(rb->blocked_threads + rb->max_blocked / 2, 0x00,
            rb->max_blocked / 2 * sizeof(struct blocked_threads));
        trace("i/increase blocked size; new size is %d", rb->max_blocked);
    }

    /*
     * there is at least one slot available for our blocked thread info
     */

    for (i = 0; i != rb->max_blocked; ++i)
    {
        /*
         * let's find free slot
         */

        if (rb->blocked_threads[i].valid)
        {
            /*
             * nope, that ain't it
             */

            continue;
        }

        /*
         * and here is our free slot, let's fill it with thread info
         */

        rb->blocked_threads[i].thread = pthread_self();
        rb->blocked_threads[i].valid = 1;
        rb->curr_blocked++;
        trace("i/slots used: %d, max %d", rb->curr_blocked, rb->max_blocked);
        return 0;
    }

    /*
     * I have *NO* idea how could we get here.  Anyway, let's  return  error
     * as we didn't add thread to the list
     */

    assert(0 && "rb_add_blocked_thread() error adding thread, all slots used?");
    return -1;
}

#endif /* ENABLE_THREADS && ENABLE_POSIX_CALLS */


/* ==========================================================================
    This will remove current thread from the list of  blocked  threads.   It
    shouldn't fail. If it does, there is logic error in the code.
   ========================================================================== */


#if ENABLE_THREADS && ENABLE_POSIX_CALLS

static int rb_del_blocked_thread
(
    struct rb  *rb            /* rb object */
)
{
    int         i;            /* just an iterator */
    pthread_t   curr_thread;  /* current thread */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    curr_thread = pthread_self();

    for (i = 0; i != rb->max_blocked; ++i)
    {
        if (rb->blocked_threads[i].valid == 0)
        {
            /*
             * empty slot, nothing to do
             */

            continue;
        }

        if (pthread_equal(curr_thread, rb->blocked_threads[i].thread))
        {
            /*
             * this is our slot, remove thread from the list,  there  is  no
             * need to set .thread field to 0, as 0 may still  be  valid  id
             */

            rb->blocked_threads[i].valid = 0;
            rb->curr_blocked--;
            trace("i/slots used %d max %d", rb->curr_blocked, rb->max_blocked);
            return 0;
        }
    }

    /*
     * couldn't find current thread on the list, shouldn't happen, but still
     * life can be surprising
     */

    assert(0 && "rb_del_blocked_thread() thread not found on the list");
    return -1;
}

#endif /* ENABLE_THREADS && ENABLE_POSIX_CALLS */


/* ==========================================================================
    Reads 'count' bytes of data from 'fd'  into  memory  pointed  by  'dst'.
    This function is basically read() but it first checks if read()  can  be
    called without blocking. It's like non-blocking read();

    Number of bytes read or -1 on error.
   ========================================================================== */

#if ENABLE_POSIX_CALLS

static long rb_nb_read
(
    int             fd,    /* file descriptor to check */
    void           *dst,   /* where data from read() should be stored */
    size_t          count  /* number of bytes to read */
)
{
    struct timeval  tv;    /* timeout for select() function */
    fd_set          fds;   /* fd set to check for activity */
    int             sact;  /* return value from select() */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /*
     * we simply want to check if data is available and don't want select()
     * to block
     */

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    sact = select(fd + 1, &fds, NULL, NULL, &tv);

    if (sact == -1)
    {
        /*
         * critical error, we're fucked... I mean caller is fucked
         */

        return -1;
    }

    if (sact == 0)
    {
        /*
         * no data to read immediately from 'fd' socket
         */

        errno = EAGAIN;
        return -1;
    }

    return read(fd, dst, count);
}

#endif /* ENABLE_THREADS && ENABLE_POSIX_CALLS */


/* ==========================================================================
    Writes 'cont' bytes of data from 'src' into file descriptor 'fd'. It's
    basically non blocking write().

    Returns number of bytes or -1 on error
   ========================================================================== */


#if ENABLE_POSIX_CALLS

static long rb_nb_write
(
    int             fd,    /* file descriptor to check */
    void           *src,   /* location to write data from */
    size_t          count  /* number of bytes to write */
)
{
    struct timeval  tv;    /* timeout for select() function */
    fd_set          fds;   /* fd set to check for activity */
    int             sact;  /* return value from select() */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /*
     * we simply want to check if data is available and don't want select()
     * to block
     */

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    sact = select(fd + 1, NULL, &fds, NULL, &tv);

    if (sact == -1)
    {
        /*
         * critical error, we're fucked... I mean caller is fucked
         */

        return -1;
    }

    if (sact == 0)
    {
        /*
         * no data to write immediately to 'fd' socket
         */

        errno = EAGAIN;
        return -1;
    }

    return write(fd, src, count);
}

#endif /* ENABLE_POSIX_CALLS */


/* ==========================================================================
    Function reads maximum count of data from rb  into  buffer.   When  user
    requested more data than there is in a buffer, function  will  copy  all
    data from rb and will return number of bytes copied.  When there  is  no
    data in buffer, function will return -1 and EAGAIN

    If MSG_PEEK flag is set, data will  be  copied  into  buffer,  but  tail
    pointer will not be moved, so consequent call  to  rb_recv  will  return
    same data.
   ========================================================================== */


static long rb_recvs
(
    struct rb*      rb,       /* rb object */
    void*           buffer,   /* buffer where received data will be copied */
    int             fd,       /* file descriptor where data will be copied */
    size_t          count,    /* number of elements to copy to buffer */
    unsigned long   flags     /* receiving options */
)
{
    size_t          rbcount;  /* number of elements in rb */
    size_t          cnte;     /* number of elements in rb until overlap */
    size_t          tail;     /* rb->tail copy in case we need to restore it */
    size_t          objsize;  /* size, in bytes, of single object in rb */
    unsigned char*  buf;      /* buffer treated as unsigned char type */
    long            w;        /* number of bytes wrote with write() */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    trace("i/fd: %d, count: %zu, flags: %lu", fd, count, flags);

    if (count > (rbcount = rb_count_ns(rb)))
    {
        /*
         * Caller requested more data then is available, adjust count
         */

        count = rbcount;
    }

    if (count == 0)
    {
        trace("e/eagain");
        errno = EAGAIN;
        return -1;
    }

    objsize = rb->object_size;
    tail = rb->tail;
    cnte = rb_count_end(rb);
    buf = buffer;

#if ENABLE_POSIX_CALLS

    if (buf)
    {

#endif /* ENABLE_POSIX_CALLS */

        if (count > cnte)
        {
            /*
             * Memory overlaps, copy data in two turns
             */

            memcpy(buf, rb->buffer + rb->tail * objsize, objsize * cnte);
            memcpy(buf + cnte * objsize, rb->buffer, (count - cnte) * objsize);
            rb->tail = flags & MSG_PEEK ? rb->tail : count - cnte;
        }
        else
        {
            /*
             * Memory doesn't overlap, good we can do copying on one go
             */

            memcpy(buf, rb->buffer + rb->tail * objsize, count * objsize);
            rb->tail += flags & MSG_PEEK ? 0 : count;
            rb->tail &= rb->count - 1;
        }

        trace("i/ret %zu", count);
        return count;

#if ENABLE_POSIX_CALLS

    }

    /*
     * copy data from buffer to fd
     */

    if (count > cnte)
    {
        long  tw;     /* total number of elements wrote to fd */
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        /*
         * memory overlaps, we'll need to copy data in two steps
         */

        w = rb_nb_write(fd, rb->buffer + rb->tail * objsize, objsize * cnte);
        if (w == -1)
        {
            trace("e/write() %s", strerror(errno));
            return -1;
        }

        /*
         * we operate on elements, and write() returns number of bytes read,
         * so here we convert number of bytes wrote into number of  elements
         * wrote
         */

        tw = w / objsize;

        if (tw != cnte)
        {
            /*
             * write() returned less bytes than we  wanted,  looks  like  fd
             * cannot accept more data right now, return  partial  write  to
             * the caller
             */

            rb->tail += flags & MSG_PEEK ? 0 : tw;
            rb->tail &= rb->count - 1;
            trace("i/ret %lu", tw);
            return tw;
        }

        w = rb_nb_write(fd, rb->buffer, (count - cnte) * objsize);
        if (w == -1)
        {
            trace("e/write() %s", strerror(errno));
            return -1;
        }

        tw += w / objsize;
        rb->tail = flags & MSG_PEEK ? rb->tail : w / objsize;
        trace("i/ret %lu", tw);
        return tw;
    }

    /*
     * write to fd without overlap
     */

    w = rb_nb_write(fd, rb->buffer + rb->tail * objsize, count * objsize);
    if (w == -1)
    {
        trace("e/write() %s", strerror(errno));
        return -1;
    }

    rb->tail += flags & MSG_PEEK ? 0 : w / objsize;
    rb->tail &= rb->count - 1;
    trace("i/ret %zu", w / objsize);
    return w / objsize;

#endif /* ENABLE_POSIX_CALLS */
}


/* ==========================================================================
    Reads count data  from  rb  into  buffer.   Function  will  block  until
    count elements are stored into buffer, unless blocking flag is set to 1.
    When rb is exhausted and there is still  data  to  read,  caller  thread
    will be put to sleep and will be waked up as soon as there  is  data  in
    rb.  count can be any size, it can be much bigger  than  rb  size,  just
    keep in mind if count is  too  big,  time  waiting  for  data  might  be
    significant.  When blocking flag is set to 1, and there is less data  in
    rb than count expects, function will copy as many  elements  as  it  can
    (actually it will copy all of data  that  is  in  rb)  and  will  return
    with   number   of   elements   stored   in    buffer. When there is  no
    data in buffer, function will return -1 and EAGAIN
   ========================================================================== */


#if ENABLE_THREADS


static long rb_recvt
(
    struct rb*      rb,      /* rb object */
    void*           buffer,  /* buffer where received data will be copied to */
    int             fd,
    size_t          count,   /* number of elements to copy to buffer */
    unsigned long   flags    /* receiving options */
)
{
    size_t          r;       /* number of elements read */
    unsigned char*  buf;     /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    r = 0;
    errno = 0;
    buf = buffer;

    trace("i/fd: %d, count: %zu, flags: %lu", fd, count, flags);
    while (count)
    {
        size_t  count_to_end;
        size_t  count_to_read;
        size_t  bytes_to_read;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        trace("lock");
        pthread_mutex_lock(&rb->lock);

        while (rb_count_ns(rb) == 0 && rb->force_exit == 0)
        {
            struct timespec ts;  /* timeout for pthread_cond_timedwait */
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


            /*
             * buffer is empty and no data can be  read,  we  wait  for  any
             * data or exit if 'rb' is nonblocking
             */

            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                pthread_mutex_unlock(&rb->lock);
                trace("unlock");

                if (r == 0)
                {
                    /*
                     * set errno only when we did not read any bytes from rb
                     * this is how standard posix read/send works
                     */

                    trace("e/eagain");
                    errno = EAGAIN;
                    return -1;
                }
                return r;
            }

            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5;

            /*
             * This happens only after calling rb_stop()
             *
             * on some very rare ocassions it is possible that signal  won't
             * reach out rb->wait_data conditional variable.  This shouldn't
             * happend, but yet it does.  Such behaviour may cause deadlock.
             * To prevent deadlock we wake this thread every now and then to
             * make sure program is running.  When everything works ok, this
             * has marginal impact on performance and when things go  south,
             * instead of deadlocking  we  stall  execution  for  maximum  5
             * seconds.
             *
             * TODO: look into this and try to make proper fix
             */

            pthread_cond_timedwait(&rb->wait_data, &rb->lock, &ts);
        }

        if (rb->force_exit)
        {
            /*
             * ring buffer is going down operations on buffer are not allowed
             */

            pthread_mutex_unlock(&rb->lock);
            trace("unlock");
            trace("i/force exit");
            return -1;
        }

        /*
         * Elements in memory can overlap, so we need to calculate how much
         * elements we can safel
         */

        count_to_end = rb_count_end(rb);
        count_to_read = count > count_to_end ? count_to_end : count;
        bytes_to_read = count_to_read * rb->object_size;

#   if ENABLE_POSIX_CALLS

        if (buf)
        {

#   endif /* ENABLE_POSIX_CALLS */

            memcpy(buf, rb->buffer + rb->tail * rb->object_size, bytes_to_read);
            buf += bytes_to_read;

#   if ENABLE_POSIX_CALLS

        }
        else
        {
            long            w;
            fd_set          fds;
            int             sact;
            struct timeval  tv;
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


            /*
             * check rb_sendt() function to why we do what we do  here  with
             * select(), it's the same thing  but  for  write()  instead  of
             * read().  In short, write() may block  and  that  could  cause
             * deadlock, and select() saves us from that
             */

            if (rb_add_blocked_thread(rb) == -1)
            {
                flags |= MSG_DONTWAIT;
            }

            pthread_mutex_unlock(&rb->lock);
            trace("unlock");

            tv.tv_sec = 0;
            tv.tv_usec = 0;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                sact = select(fd + 1, NULL, &fds, NULL, &tv);
            }
            else
            {
                sact = select(fd + 1, NULL, &fds, NULL, NULL);
            }

            trace("lock");
            pthread_mutex_lock(&rb->lock);
            rb_del_blocked_thread(rb);

            if (sact == -1)
            {
                trace("e/select() %s", strerror(errno));
                pthread_mutex_unlock(&rb->lock);
                trace("unlock");
                return -1;
            }

            if (sact == 0)
            {
                pthread_mutex_unlock(&rb->lock);
                trace("unlock");

                if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
                {

                    if (r == 0)
                    {
                        trace("w/select() timeout, eagain");
                        errno = EAGAIN;
                        return -1;
                    }

                    trace("i/select() timeout, ret %zu", r);
                    return r;
                }

                continue;
            }

            w = write(fd, rb->buffer + rb->tail * rb->object_size,
                bytes_to_read);

            if (w == -1)
            {
                trace("e/write() %s", strerror(errno));
                pthread_mutex_unlock(&rb->lock);
                trace("unlock");

                if (errno == EAGAIN)
                {
                    if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
                    {
                        /*
                         * write cannot be finished without blocking- EAGAIN
                         * and user requested non blocking operation, so  we
                         * return. We don't notify anyone here, as this loop
                         * didn't take anything from rb
                         */

                        trace("i/ret %zu", r ? r : -1);
                        return r ? r : -1;
                    }

                    /*
                     * looks like, passed fd is a non blocking  socket,  but
                     * caller  wants  blocking  operation,  so   we   simply
                     * continue but without notifying another threads as  no
                     * data has been read from rb
                     */

                    continue;
                }

                /*
                 * got some nasty error from write(), not much to do, return
                 * number of elements read - user must check errno to see if
                 * there was any error
                 */

                trace("i/ret %zu", r ? r : -1);
                return r ? r : -1;
            }

            /*
             * write() returned something meaningfull,  overwrite  count  to
             * read variable to what was actually read, so pointers are  set
             * properly
             */

            count_to_read = w;
       }

#   endif /* ENABLE_POSIX_CALLS */

        /*
         * Adjust pointers and counts for the next read
         */

        rb->tail += count_to_read;
        rb->tail &= rb->count - 1;
        r += count_to_read;
        count -= count_to_read;

        /*
         * Signal any threads that waits for space to put data in buffer
         */

        pthread_cond_signal(&rb->wait_room);
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }

    trace("i/ret %zu", r);
    return r;
}


#endif  /* ENABLE_THREADS */


/* ==========================================================================
    Function writes maximum count of data into ring buffer  from  buffer  or
    file/socket described by fd.  If there is not enough space to store  all
    data from buffer, function will store as many as it can, and will return
    count of objects stored into ring buffer.  If buffer is  full,  function
    returns -1 and EAGAIN error.

    Either buffer or fd can be passed, never both!
   ========================================================================== */


static long rb_sends
(
    struct rb*            rb,       /* rb object */
    const void*           buffer,   /* location of data to put into rb */
    int                   fd,       /* file descriptor to read data from */
    size_t                count,    /* number of elements to put on the rb */
    unsigned long         flags     /* receiving options */
)
{
    size_t                rbspace;  /* space left in rb */
    size_t                spce;     /* space left in rb until overlap */
    size_t                objsize;  /* size of a single element in rb */
    const unsigned char*  buf;      /* buffer treated as unsigned char */
    long                  r;        /* number of bytes read from fd */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    (void)flags;
    trace("i/fd: %d, count: %zu, flags: %lu", fd, count, flags);

    if (count > (rbspace = rb_space_ns(rb)))
    {
        /*
         * Caller wants to store more data then there is space available
         */

        count = rbspace;
    }

    if (count == 0)
    {
        trace("e/eagain");
        errno = EAGAIN;
        return -1;
    }

    objsize = rb->object_size;
    spce = rb_space_end(rb);
    buf = buffer;

#if ENABLE_POSIX_CALLS

    if (buf)
    {

#endif /* ENABLE_POSIX_CALLS */

        if (count > spce)
        {
            /*
             * Memory overlaps, copy data in two turns
             */

            memcpy(rb->buffer + rb->head * objsize, buf, spce * objsize);
            memcpy(rb->buffer, buf + spce * objsize, (count - spce) * objsize);
            rb->head = count - spce;
        }
        else
        {
            /*
             * Memory doesn't overlap, good, we can do copying in one go
             */

            memcpy(rb->buffer + rb->head * objsize, buf, count * objsize);
            rb->head += count;
            rb->head &= rb->count - 1;
        }

        trace("i/ret %zu", count);
        return count;

#if ENABLE_POSIX_CALLS

    }

    /*
     * use file descriptor as a source of data to put into rb
     */

    if (count > spce)
    {
        long  tr;     /* total number of elements read from fd */
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        r = rb_nb_read(fd, rb->buffer + rb->head * objsize, spce * objsize);

        if (r == -1)
        {
            trace("e/read() %s", strerror(errno));
            return -1;
        }

        /*
         * we operate on elements, and read() returns number of bytes read,
         * so here we convert number of bytes read into number of elements
         * read.
         */

        tr = r / objsize;

        if (tr != spce)
        {
            /*
             * read() returned less bytes than we wanted, fd  is  empty,  no
             * need for another call and we didn't overlap memory
             */

            rb->head += tr;
            rb->head &= rb->count -1;
            trace("i/ret %ld", tr);
            return tr;
        }

        r = rb_nb_read(fd, rb->buffer, (count - spce) * objsize);

        if (r == -1)
        {
            trace("e/read() %s", strerror(errno));
            return -1;
        }

        /*
         * since we overlaped and put data to rb->head, new rb->head pointer
         * is simply moved by the number of elements read from the read()
         */

        tr += r / objsize;
        rb->head = r / objsize;

        /*
         * and we return number of elements totaly read from read()
         */

        trace("i/ret %ld", tr);
        return tr;
    }

    /*
     * read from fd when memory does not overlap and we can do read in a
     * single read
     */

    r = rb_nb_read(fd, rb->buffer + rb->head * objsize, count * objsize);

    if (r == -1)
    {
        trace("e/read() %s", strerror(errno));
        return -1;
    }

    rb->head += r / objsize;
    rb->head &= rb->count -1;
    trace("i/ret %zu", r / objsize);
    return r / objsize;

#endif /* ENABLE_POSIX_CALLS */
}


/* ==========================================================================
    Writes count data pointed by buffer or fd into rb.  Function will  block
    until count elements are stored into rb, unless blocking flag is set  to
    1.  When rb is full and there is still data to write, caller thread will
    be put to sleep and will be waked up as soon as there is  space  in  rb.
    count can be any size, it can be much bigger than rb size, just keep  in
    mind if count is too big, time waiting for space might  be  significant.
    When blocking flag is set to 1, and there is less space in rb than count
    expects, function will copy as many elements as it can and  will  return
    with number of elements written to rb.   If  buffer  is  full,  function
    returns -1 and EAGAIN error.

    Either buffer or fd can be set, never both!
   ========================================================================== */


#if ENABLE_THREADS

long rb_sendt
(
    struct rb*            rb,       /* rb object */
    const void*           buffer,   /* location of data to put into rb */
    int                   fd,       /* file descriptor to read data from */
    size_t                count,    /* number of elements to put on the rb */
    unsigned long         flags     /* receiving options */
)
{
    size_t                w;        /* number of elements written to rb */
    const unsigned char*  buf;      /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    w = 0;
    buf = buffer;
    trace("i/fd: %d, count: %zu, flags: %lu", fd, count, flags);

    while (count)
    {
        size_t  count_to_end;
        size_t  count_to_write;
        size_t  bytes_to_write;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        trace("lock");
        pthread_mutex_lock(&rb->lock);

        while (rb_space_ns(rb) == 0 && rb->force_exit == 0)
        {
            struct timespec ts;  /* timeout for pthread_cond_timedwait */
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


            /*
             * buffer is full and no new data can be  pushed,  we  wait  for
             * room or exit if 'rb' is nonblocking
             */

            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                pthread_mutex_unlock(&rb->lock);
                trace("unlock");

                if (w == 0)
                {
                    /*
                     * set errno only when we did not read any bytes from rb
                     * this is how standard posix read/send works
                     */

                    errno = EAGAIN;
                    return -1;
                }

                trace("i/ret %zu", w);
                return w;
            }

            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5;

            /*
             * This happens only after calling rb_stop()
             *
             * on some very rare ocassions it is possible that signal  won't
             * reach out rb->wait_room conditional variable.  This shouldn't
             * happend, but yet it does.  Such behaviour may cause deadlock.
             * To prevent deadlock we wake this thread every now and then to
             * make sure program is running.  When everything works ok, this
             * has marginal impact on performance and when things go  south,
             * instead of deadlocking  we  stall  execution  for  maximum  5
             * seconds.
             *
             * TODO: look into this and try to make proper fix
             */

            pthread_cond_timedwait(&rb->wait_room, &rb->lock, &ts);
        }

        if (rb->force_exit == 1)
        {
            /*
             * ring buffer is going down operations on buffer are not allowed
             */

            pthread_mutex_unlock(&rb->lock);
            trace("unlock");
            trace("i/force exit");
            return -1;
        }

        /*
         * Count might be too large to store it in one burst, we calculate
         * how many elements can we store before needing to overlap memor
         */

        count_to_end = rb_space_end(rb);
        count_to_write = count > count_to_end ? count_to_end : count;
        bytes_to_write = count_to_write * rb->object_size;

#   if ENABLE_POSIX_CALLS

        if (buf)
        {

#   endif /* ENABLE_POSIX_CALLS */

            memcpy(rb->buffer + rb->head * rb->object_size,
                buf, bytes_to_write);
            buf += bytes_to_write;

#   if ENABLE_POSIX_CALLS

        }
        else
        {
            long            r;     /* number of bytes read from read() */
            fd_set          fds;   /* watch set for select() */
            int             sact;  /* select() return code */
            struct timeval  tv;    /* timeout for select() */
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

            /*
             * add current thread to the list of possible locked threads in
             * read() so we can interrupt them on rb_stop()
             */

            if (rb_add_blocked_thread(rb) == -1)
            {
                /*
                 * we couldn't add blockedthread, very unlikely, but we set
                 * this call to be non blocking to avoid deadlock
                 */

                flags |= MSG_DONTWAIT;
            }

            /*
             * read() may block, and read() uses rb directly, so blocking in
             * read() here would cause  massive  deadlock  as  rb  mutex  is
             * locked here.  To prevent bad things from happening we need to
             * make sure read() can be invoked without blocking, for that we
             * will use good old fashioned select() and  we  can  do  it  in
             * unlocked state, so when we  wait  in  select(),  some  thread
             * calling rb_write() could write to rb
             */

            pthread_mutex_unlock(&rb->lock);
            trace("unlock");

            tv.tv_sec = 0;
            tv.tv_usec = 0;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                /*
                 * call select() without blocking (timeout = 0)
                 */

                sact = select(fd + 1, &fds, NULL, NULL, &tv);
            }
            else
            {
                /*
                 * call select() without timeout, so it block indefinitely
                 */

                sact = select(fd + 1, &fds, NULL, NULL, NULL);
            }

            trace("lock");
            pthread_mutex_lock(&rb->lock);
            rb_del_blocked_thread(rb);

            if (sact == -1)
            {
                trace("e/select() %s", strerror(errno));
                pthread_mutex_unlock(&rb->lock);
                trace("unlock");
                return -1;
            }

            if (sact == 0)
            {
                /*
                 * timeout in select() occured
                 */

                pthread_mutex_unlock(&rb->lock);
                trace("unlock");

                if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
                {
                    /*
                     * in non blocking mode, we return what has been already
                     * put into rb (or -1 if nothing has been stored in rb)
                     */

                    if (w == 0)
                    {
                        errno = EAGAIN;
                        trace("w/select() timeout, eagain");
                        return -1;
                    }

                    trace("i/select() timeout, return %zu", w);
                    return w;
                }

                /*
                 * okay, so open group defines when select() is called  with
                 * timeout set to NULL,  select()  will  block  until  event
                 * occurs
                 *
                 * > If the timeout argument is  a  null  pointer,  select()
                 * > blocks until an event causes one of  the  masks  to  be
                 * > returned with a valid (non-zero) value
                 * http://pubs.opengroup.org/onlinepubs/7908799/xsh/select.html
                 *
                 * freebsd also will block indefiniately:
                 *
                 * > If  timeout  is  a  nil  pointer,   the  select  blocks
                 * > indefinitely.
                 * http://nixdoc.net/man-pages/FreeBSD/man4/man2/select.2.html
                 *
                 * But linux man page states that select() only *CAN*  block
                 * indefinitely, not *MUST*
                 *
                 * > If timeout is NULL  (no timeout),  select()  can  block
                 * > indefinitely.
                 * http://man7.org/linux/man-pages/man2/select.2.html
                 *
                 * Taking that into  considaration,  even  though  it's  not
                 * fully posix compliant, we expect  select()  to  return  0
                 * when timeout  was  set  to  NULL,  it  won't  harm  posix
                 * implementation of select(), but will save our asses  from
                 * Linux
                 */

                continue;
            }

            /*
             * now we are sure read() won't block
             */

            r = read(fd, rb->buffer + rb->head * rb->object_size,
                bytes_to_write);

            if (r == -1)
            {
                pthread_mutex_unlock(&rb->lock);
                trace("unlock");

                if (errno == EAGAIN)
                {
                    if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
                    {
                        /*
                         * write cannot be finished without blocking- EAGAIN
                         * and user requested non blocking operation, so  we
                         * return. We don't notify anyone here, as this loop
                         * didn't take anything from rb
                         */

                        trace("w/read() eagain, ret: %zu", w ? w : -1);
                        return w ? w : -1;
                    }

                    /*
                     * looks like, passed fd is a non blocking  socket,  but
                     * caller  wants  blocking  operation,  so   we   simply
                     * continue but without notifying another threads as  no
                     * data has been read from rb
                     */

                    continue;
                }

                /*
                 * got some nasty error from write(), not much to do, return
                 * number of elements read - user must check errno to see if
                 * there was any error
                 */

                trace("e/read() %s, ret: %zu", strerror(errno), w ? w : -1);
                return w ? w : -1;
            }

            /*
             * write() returned something meaningfull,  overwrite  count  to
             * read variable to what was actually read, so pointers are  set
             * properly
             */

            count_to_write = r;
        }

#   endif /* ENABLE_POSIX_CALLS */

        /*
         * Adjust pointers and counts for next write
         */

        rb->head += count_to_write;
        rb->head &= rb->count - 1;
        w += count_to_write;
        count -= count_to_write;

        /*
         * Signal any threads that waits for data to read
         */

        pthread_cond_signal(&rb->wait_data);
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }

    trace("i/ret %zu", w);
    return w;
}


/* ==========================================================================
    This function simply signals all  conditional  variables  to  force  any
    locked thread to exit from read/send functions
   ========================================================================== */


static void *rb_stop_thread
(
    void       *arg       /* disguised rb object */
)
{
    struct rb  *rb;       /* ring buffer object */
    int         stopped;  /* copy of rb->stopped_all */
    int         i;        /* i stands for iterator dude! */

#   if ENABLE_POSIX_CALLS

    struct sigaction sa;  /* signal action info */
    struct sigaction osa; /* Office of Secret Actions... kidding, it's old sa */
    time_t           now; /* current time in seconds */
    time_t           prev;/* previous time */

#   endif /* ENABLE_POSIX_CALLS */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    rb = arg;
    stopped = 0;
    trace("starting");

    trace("lock");
    pthread_mutex_lock(&rb->lock);
    rb->force_exit = 1;
    pthread_mutex_unlock(&rb->lock);
    trace("unlock");

#   if ENABLE_POSIX_CALLS

    now = 0;
    prev = 0;

    /*
     * we need to install action handler, so sending signal won't kill  kill
     * application
     */

    memset(&sa, 0x00, sizeof(sa));
    memset(&osa, 0x00, sizeof(osa));
    sa.sa_handler = rb_sigaction;

    /*
     * install signal action for user specified signal (or default one if he
     * didn't define it
     */

    if (sigaction(rb->signum, &sa, NULL) == -1)
    {
        /*
         * good job user, he just passed wrong signal (like SIGKILL) or
         * something that is not supported on this platform, let's try to
         * save his sorry ass by installing default sigaction.
         */

        trace("e/sigaction() %s", strerror(errno));
        sigaction(SIGUSR1, &sa, &osa);
    }

#   endif /* ENABLE_POSIX_CALLS */

    /*
     * Send cond signal, until all threads exits read/send functions.   This
     * loop will finish once user calls rb_cleanup().  It's his job to check
     * if all threads finished before calling rb_cleanup()
     */

    while (stopped != 1)
    {
        trace("lock");
        pthread_mutex_lock(&rb->lock);
        pthread_cond_signal(&rb->wait_data);
        pthread_cond_signal(&rb->wait_room);
        stopped = rb->stopped_all;

#   if ENABLE_POSIX_CALLS

        /*
         * send signal to all threads locked in  system  call,  signal  will
         * make sytem call to interrupt
         */

        now = time(NULL);

        if (now != prev)
        {
            prev = now;
            trace("i/sending kill");
            for (i = 0; i != rb->max_blocked; ++i)
            {
                if (rb->curr_blocked == 0)
                {
                    /*
                     * no threads in blocked state, no need for iteration
                     */

                    break;
                }

                if (rb->blocked_threads[i].valid == 0)
                {
                    /*
                     * empty slot
                     */

                    continue;
                }

                pthread_kill(rb->blocked_threads[i].thread, rb->signum);
            }
        }

#   endif /* ENABLE_POSIX_CALLS */

        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }

#   if  ENABLE_POSIX_CALLS

    /*
     * if we overwritten user's sigaction, now it's time to restore it
     */

    if (((osa.sa_flags & SA_SIGINFO) == 0 && osa.sa_handler) ||
        ((osa.sa_flags & SA_SIGINFO) && osa.sa_sigaction))
    {
        /*
         * SIGUSR1 is the only signal we could overwrite
         */

        sigaction(SIGUSR1, &osa, NULL);
    }

#   endif /* ENABLE_POSIX_CALLS */

    trace("return NULL");
    return NULL;
}


#endif  /* ENABLE_THREADS */


/* ==========================================================================
    Initializes rb buffer to known state. Does not initialize rb->buffer
   ========================================================================== */


static int rb_init_p
(
    struct rb     *rb,           /* rb object to init */
    size_t         count,        /* number of elements that buffer can hold */
    size_t         object_size,  /* size, in bytes, of a single object */
    unsigned long  flags         /* flags to create buffer with */
)
{
#if ENABLE_THREADS
    int            e;            /* errno value from pthread function */
#endif

    VALID(EINVAL, rb_is_power_of_two(count) == 1);
    trace("init rb %p", rb);

    rb->head = 0;
    rb->tail = 0;
    rb->count = count;
    rb->object_size = object_size;
    rb->flags = flags;

#if ENABLE_THREADS == 0
    /*
     * multithreaded operations are not allowed when library is compiled
     * without threads
     */
    VALID(ENOSYS, (flags & O_MULTITHREAD) == 0);

    return 0;
#else
    if ((flags & O_MULTITHREAD) == 0)
    {
        /*
         * when working in non multi-threaded mode, force  O_NONBLOCK  flag,
         * and return, as we don't need to init pthread elements.
         */

        rb->flags |= O_NONBLOCK;
        return 0;
    }

    /*
     * Multithreaded environment
     */

#if ENABLE_POSIX_CALLS

    /*
     * it may happen that rb will be blocked in read() or  write()  function
     * and the only way to interrupt such blocked syscall is  to  send  kill
     * signal to blocked thread. We start with assumption max 2 threads will
     * concurently try to access rb  object  (most  common  case)  and  will
     * increase it when needed
     */

    rb->max_blocked = 2;
    rb->curr_blocked = 0;
    rb->signum = SIGUSR1;
    rb->blocked_threads = calloc(rb->max_blocked,
        sizeof(struct blocked_threads));

    if (rb->blocked_threads == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

#endif /* ENABLE_POSIX_CALLS */

    rb->stopped_all = -1;
    rb->force_exit = 0;

    VALIDGO(e, error_lock, (e = pthread_mutex_init(&rb->lock, NULL)) == 0);
    VALIDGO(e, error_data, (e = pthread_cond_init(&rb->wait_data, NULL)) == 0);
    VALIDGO(e, error_room, (e = pthread_cond_init(&rb->wait_room, NULL)) == 0);

    return 0;

error_room:
    pthread_cond_destroy(&rb->wait_data);
error_data:
    pthread_mutex_destroy(&rb->lock);
error_lock:
    errno = e;
    return -1;
#endif
}


/* ==========================================================================
    Cleans up resources allocated by pthread stuff
   ========================================================================== */

#if ENABLE_THREADS

static int rb_cleanup_p
(
    struct rb  *rb  /* rb object */
)
{
    /*
     * check if user called rb_stop, if not (rb->stopped will be -1), we trust
     * caller made sure all threads are stopped before calling destroy.
     */

    trace("lock");
    pthread_mutex_lock(&rb->lock);

#   if ENABLE_POSIX_CALLS

    free(rb->blocked_threads);

#   endif /* ENABLE_POSIX_CALLS */

    if (rb->stopped_all == 0)
    {
        rb->stopped_all = 1;
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
        pthread_join(rb->stop_thread, NULL);
    }
    else
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }

    pthread_cond_destroy(&rb->wait_data);
    pthread_cond_destroy(&rb->wait_room);
    pthread_mutex_destroy(&rb->lock);

    return 0;
}

#endif /* ENABLE_THREADS */

/* ==========================================================================
                                        __     __ _
                         ____   __  __ / /_   / /(_)_____
                        / __ \ / / / // __ \ / // // ___/
                       / /_/ // /_/ // /_/ // // // /__
                      / .___/ \__,_//_.___//_//_/ \___/
                     /_/
               ____                     __   _
              / __/__  __ ____   _____ / /_ (_)____   ____   _____
             / /_ / / / // __ \ / ___// __// // __ \ / __ \ / ___/
            / __// /_/ // / / // /__ / /_ / // /_/ // / / /(__  )
           /_/   \__,_//_/ /_/ \___/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


/* ==========================================================================
    Initializes new ring buffer object like rb_new but does not use dynamic
    memory allocation, but uses memory pointer by mem.
   ========================================================================== */


struct rb *rb_init
(
    size_t         count,        /* number of elements that buffer can hold */
    size_t         object_size,  /* size, in bytes, of a single object */
    unsigned long  flags,        /* flags to create buffer with */
    void          *mem           /* memory area to use for rb object */
)
{
    struct rb     *rb;           /* treat passed mem as rb object */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    VALIDR(EINVAL, NULL, mem);

    rb = mem;
    rb->buffer = (unsigned char *)rb + sizeof(*rb);

    if (rb_init_p(rb, count, object_size, flags) == 0)
    {
        return rb;
    }

    return NULL;
}


/* ==========================================================================
    Initializes ring buffer and allocates all  necessary  resources.   Newly
    created rb  will  returned  as  a  pointer.   In  case  of  an  function
    error, NULL will be returned
   ========================================================================== */


struct rb *rb_new
(
    size_t         count,        /* number of elements that buffer can hold */
    size_t         object_size,  /* size, in bytes, of a single object */
    unsigned long  flags         /* flags to create buffer with */
)
{
    struct rb     *rb;           /* pointer to newly created buffer */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    if ((rb = malloc(sizeof(*rb))) == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }

    if ((rb->buffer = malloc(count * object_size)) == NULL)
    {
        free(rb);
        errno = ENOMEM;
        return NULL;
    }

    if (rb_init_p(rb, count, object_size, flags) == 0)
    {
        return rb;
    }

    free(rb->buffer);
    free(rb);
    return NULL;
}


/* ==========================================================================
    Reads maximum of count elements from rb and  stores  them  into  buffer.

    If rb is working in single  thread  mode  or  O_NONBLOCK  flag  is  set,
    function will never block, and cannot guarantee writing  count  elements
    into buffer.  If there is not enough data in ring buffer, function  will
    read whole buffer and return with elements read.

    If rb is threaded and  blocking,  function  will  block  (sleep)  caller
    thread until all count  elements  were  copied  into  buffer.   Function
    is   equivalent   to   call   rb_recv   with flags   ==   0
   ========================================================================== */


long rb_read
(
    struct rb  *rb,      /* rb object */
    void       *buffer,  /* location where data from rb will be stored */
    size_t      count    /* requested number of data from rb */
)
{
    return rb_recv(rb, buffer, count, 0);
}


/* ==========================================================================
    Same as rb_read but also accepts flags
   ========================================================================== */


long rb_recv
(
    struct rb     *rb,      /* rb object */
    void          *buffer,  /* location where data from rb will be stored */
    size_t         count,   /* requested number of data from rb */
    unsigned long  flags    /* operation flags */
)
{
    VALID(EINVAL, rb);
    VALID(EINVAL, buffer);
    VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
    if ((rb->flags & O_MULTITHREAD) == 0)
    {
        return rb_recvs(rb, buffer, -1, count, flags);
    }

    trace("lock");
    pthread_mutex_lock(&rb->lock);
    if (rb->force_exit)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
        errno = ECANCELED;
        return -1;
    }
    pthread_mutex_unlock(&rb->lock);
    trace("unlock");

    if (flags & MSG_PEEK)
    {
        /*
         * when called is just peeking, we can simply call function for
         * single thread, as it will not modify no data, and will not cause
         * deadlock
         */

        trace("lock");
        pthread_mutex_lock(&rb->lock);
        count = rb_recvs(rb, buffer, -1, count, flags);
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
        return count;
    }

    return rb_recvt(rb, buffer, -1, count, flags);
#else
    return rb_recvs(rb, buffer, -1, count, flags);
#endif
}


/* ==========================================================================
    Same as rb_read, but data is copied to file descriptor 'fd'  instead  of
    user provided buffer
   ========================================================================== */


long rb_fd_read
(
    struct rb  *rb,    /* rb object */
    int         fd,    /* file descriptor data from rb will be copied to */
    size_t      count  /* requested number of elements to be copied from rb */
)
{
    return rb_fd_recv(rb, fd, count, 0);
}


/* ==========================================================================
    Same as rb_fd_read but also accepts flags
   ========================================================================== */


long rb_fd_recv
(
    struct rb     *rb,    /* rb object */
    int            fd,    /* file descriptor data from rb will be copied to */
    size_t         count, /* requested number of elements to be copied from rb*/
    unsigned long  flags  /* operation flags */
)
{
#if ENABLE_POSIX_CALLS

    VALID(EINVAL, rb);
    VALID(EINVAL, rb->buffer);
    VALID(EINVAL, fd >= 0);

#   if ENABLE_THREADS

    if ((rb->flags & O_MULTITHREAD) == 0)
    {
        return rb_recvs(rb, NULL, fd, count, flags);
    }

    VALID(EINVAL, rb->object_size == 1);
    trace("lock");
    pthread_mutex_lock(&rb->lock);
    if (rb->force_exit)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
        errno = ECANCELED;
        return -1;
    }
    pthread_mutex_unlock(&rb->lock);
    trace("unlock");

    if (flags & MSG_PEEK)
    {
        /*
         * when called is just peeking, we can simply call function for
         * single thread, as it will not modify no data, and will not cause
         * deadlock
         */

        trace("lock");
        pthread_mutex_lock(&rb->lock);
        count = rb_recvs(rb, NULL, fd, count, flags);
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
        return count;
    }

    return rb_recvt(rb, NULL, fd, count, flags);

#   else /* ENABLE_THREADS */

    return rb_recvs(rb, NULL, fd, count, flags);

#   endif /* ENABLE_THREADS */

#else /* ENABLE_POSIX_CALLS */

    /*
     * function is no implemented
     */

    errno = ENOSYS;
    return -1;

#endif /* ENABLE_POSIX_CALLS */
}


/* ==========================================================================
    Writes maximum count data from buffer into rb.

    If rb is working in single  thread  mode  or  O_NONBLOCK  flag  is  set,
    function will never block, but also cannot guarantee that count elements
    will be copied from buffer. If there is not enough space in rb, function
    will store as many elements  as  it  can,  and  return  with  number  of
    elements stored into rb.

    If rb is multithreaded, and in blocking mode function will block (sleep)
    caller until count elements have been stored into rb.

    Function   os   equivalent   to   call   rb_send   with   flags   ==   0
   ========================================================================== */


long rb_write
(
    struct rb   *rb,      /* rb object */
    const void  *buffer,  /* data to be put into rb */
    size_t       count    /* requested number of elements to be put into rb */
)
{
    return rb_send(rb, buffer, count, 0);
}


/* ==========================================================================
    Same as rb_write but also accepts flags
   ========================================================================== */


long rb_send
(
    struct rb     *rb,      /* rb object */
    const void    *buffer,  /* data to be put into rb */
    size_t         count,   /* requested number of elements to be put into r */
    unsigned long  flags    /* operation flags */
)
{
    VALID(EINVAL, rb);
    VALID(EINVAL, buffer);
    VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
    if ((rb->flags & O_MULTITHREAD) == 0)
    {
        return rb_sends(rb, buffer, -1, count, flags);
    }

    trace("lock");
    pthread_mutex_lock(&rb->lock);
    if (rb->force_exit)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
        errno = ECANCELED;
        return -1;
    }
    pthread_mutex_unlock(&rb->lock);
    trace("unlock");

    return rb_sendt(rb, buffer, -1, count, flags);
#else
    return rb_sends(rb, buffer, -1, count, flags);
#endif
}


/* ==========================================================================
    Same as rb_write, but data is copied from file descriptor  'fd'  instead
    of user provided buffer.
   ========================================================================== */


long rb_fd_write
(
    struct rb   *rb,      /* rb object */
    int          fd,      /* file descriptor from which copy data to buffer */
    size_t       count    /* requested number of elements to be put into rb */
)
{
    return rb_fd_send(rb, fd, count, 0);
}


/* ==========================================================================
    Same as rb_fd_write but also accepts 'flags'
   ========================================================================== */


long rb_fd_send
(
    struct rb     *rb,      /* rb object */
    int            fd,      /* file descriptor from which copy data to buffer */
    size_t         count,   /* requested number of elements to be put into r */
    unsigned long  flags    /* operation flags */
)
{
#if ENABLE_POSIX_CALLS

    VALID(EINVAL, rb);
    VALID(EINVAL, rb->buffer);
    VALID(EINVAL, fd >= 0);

#   if ENABLE_THREADS

    if ((rb->flags & O_MULTITHREAD) == 0)
    {
        return rb_sends(rb, NULL, fd, count, flags);
    }

    VALID(EINVAL, rb->object_size == 1);
    trace("lock");
    pthread_mutex_lock(&rb->lock);
    if (rb->force_exit)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
        errno = ECANCELED;
        return -1;
    }
    pthread_mutex_unlock(&rb->lock);
    trace("unlock");

    return rb_sendt(rb, NULL, fd, count, flags);

#   else /* ENABLE_THREADS */

    return rb_sends(rb, NULL, fd, count, flags);

#   endif /* ENABLE_THREADS */

#else /* ENABLE_POSIX_CALLS */

    /*
     * function is not implemented
     */

    errno = ENOSYS;
    return -1;

#endif /* ENABLE_POSIX_CALLS */
}


/* ==========================================================================
    Clears all data in the buffer
   ========================================================================== */


int rb_clear
(
    struct rb  *rb,    /* rb object */
    int         clear  /* if set to 1, also clears memory */
)
{
    VALID(EINVAL, rb);
    VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        trace("lock");
        pthread_mutex_lock(&rb->lock);
    }
#endif

    if (clear)
    {
        memset(rb->buffer, 0x00, rb->count * rb->object_size);
    }

    rb->head = 0;
    rb->tail = 0;

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }
#endif

    return 0;
}


/* ==========================================================================
    Frees resources allocated by rb_new. Due to pthread nature this function
    should be called *only*  when no other threads are working on rb object,
    and rb object was allocated with rb_new.
   ========================================================================== */


int rb_destroy
(
    struct rb  *rb  /* rb object */
)
{
    int         e;  /* error code to return */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    VALID(EINVAL, rb);
    VALID(EINVAL, rb->buffer);
    e = 0;

#if ENABLE_THREADS
    if ((rb->flags & O_MULTITHREAD) == 0)
    {
        free(rb->buffer);
        free(rb);
        return e;
    }

    e = rb_cleanup_p(rb);
#endif

    free(rb->buffer);
    free(rb);

    return e;
}


/* ==========================================================================
    Same as rb_destroy but should be caled only when rb object was allocated
    with rb_init function
   ========================================================================== */


int rb_cleanup
(
    struct rb  *rb  /* rb object */
)
{
    VALID(EINVAL, rb);

#if ENABLE_THREADS
    if (rb->flags & O_MULTITHREAD)
    {
        return rb_cleanup_p(rb);
    }
#endif

    return 0;
}


/* ==========================================================================
    Simply starts rb_stop_thread  that will force all threads to exit any
    rb_* public functions.
   ========================================================================== */


int rb_stop
(
    struct rb  *rb  /* rb object */
)
{
#if ENABLE_THREADS
    int         e;  /* errno value */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    VALID(EINVAL, rb);
    VALID(EINVAL, rb->flags & O_MULTITHREAD);

    rb->stopped_all = 0;
    if ((e = pthread_create(&rb->stop_thread, NULL, rb_stop_thread, rb)) != 0)
    {
        errno = e;
        return -1;
    }

    return 0;
#else
    errno = ENOSYS;
    return -1;
#endif
}


/* ==========================================================================
    Function that discards data from tail of buffer.  This works  just  like
    rb_reads function, but is way faster as there  is  no  copying  involved
   ========================================================================== */


long rb_discard
(
    struct rb  *rb,       /* rb object */
    size_t      count     /* number of elements to discard */
)
{
    size_t      rbcount;  /* number of elements in rb */
    size_t      cnte;     /* number of elements in rb until overlap */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    VALID(EINVAL, rb);
    VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        trace("lock");
        pthread_mutex_lock(&rb->lock);
    }
#endif

    cnte = rb_count_end(rb);
    rbcount = rb_count_ns(rb);

    if (count > rbcount)
    {
        count = rbcount;
    }

    if (count > cnte)
    {
        rb->tail = count - cnte;
    }
    else
    {
        rb->tail += count;
        rb->tail &= rb->count -1;
    }

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }
#endif

    return (long)count;
}


/* ==========================================================================
    Returns version of the library
   ========================================================================== */


const char* rb_version
(
    char*  major,            /* major version info will be stored here */
    char*  minor,            /* minor version info will be stored here */
    char*  patch             /* patch version info will be stored here */
)
{
    char   version[11 + 1];  /* copy of VERSION */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (major && minor && patch)
    {
        strcpy(version, VERSION);

        strcpy(major, strtok(version, "."));
        strcpy(minor, strtok(NULL, "."));
        strcpy(patch, strtok(NULL, "."));
    }

    return VERSION;
}


/* ==========================================================================
    Returns number of elements in buffer.
   ========================================================================== */


long rb_count
(
    struct rb  *rb      /* rb object */
)
{
    size_t      count;  /* number of elements in buffer */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    VALID(EINVAL, rb);
    VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        trace("lock");
        pthread_mutex_lock(&rb->lock);
    }
#endif

    count = rb_count_ns(rb);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }
#endif

    return (long)count;
}


/* ==========================================================================
    Return number of free space in buffer
   ========================================================================== */


long rb_space
(
    struct rb  *rb      /* rb object */
)
{
    size_t      space;  /* number of free slots in buffer */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    VALID(EINVAL, rb);
    VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        trace("lock");
        pthread_mutex_lock(&rb->lock);
    }
#endif

    space = rb_space_ns(rb);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        pthread_mutex_unlock(&rb->lock);
        trace("unlock");
    }
#endif

    return (long)space;
}


/* ==========================================================================
    Return size of rb struct for current implementation.  This size  may  be
    different depending on compilation flags and/or architecture
   ========================================================================== */


size_t rb_header_size(void)
{
    return sizeof(struct rb);
}
