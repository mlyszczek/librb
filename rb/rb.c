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


#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if ENABLE_THREADS
#   include <fcntl.h>
#   include <pthread.h>
#   include <sys/socket.h>
#   include <sys/time.h>
#endif

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
#endif
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
    size_t          count,    /* number of elements to copy to buffer */
    unsigned long   flags     /* receiving options */
)
{
    size_t          rbcount;  /* number of elements in rb */
    size_t          cnte;     /* number of elements in rb until overlap */
    size_t          tail;     /* rb->tail copy in case we need to restore it */
    size_t          objsize;  /* size, in bytes, of single object in rb */
    unsigned char*  buf;      /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (count > (rbcount = rb_count_ns(rb)))
    {
        /*
         * Caller requested more data then is available, adjust count
         */

        count = rbcount;
    }

    if (count == 0)
    {
        errno = EAGAIN;
        return -1;
    }

    objsize = rb->object_size;
    tail = rb->tail;
    cnte = rb_count_end(rb);
    buf = buffer;

    if (count > cnte)
    {
        /*
         * Memory overlaps, copy data in two turns
         */

        memcpy(buf, rb->buffer + rb->tail * objsize, objsize * cnte);
        memcpy(buf + cnte * objsize, rb->buffer, (count - cnte) * objsize);
        rb->tail = count - cnte;
    }
    else
    {
        /*
         * Memory doesn't overlap, good we can do copying on one go
         */

        memcpy(buf, rb->buffer + rb->tail * objsize, count * objsize);
        rb->tail += count;
        rb->tail &= rb->count - 1;
    }

    if (flags & MSG_PEEK)
    {
        /*
         * Caller is just peeking, restore previous tail position
         */

        rb->tail = tail;
    }

    return count;
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
    size_t          count,   /* number of elements to copy to buffer */
    unsigned long   flags    /* receiving options */
)
{
    size_t          read;    /* number of elements read */
    unsigned char*  buf;     /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    read = 0;
    buf = buffer;

    while (count)
    {
        size_t  count_to_end;
        size_t  count_to_read;
        size_t  bytes_to_read;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


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

                if (read == 0)
                {
                    /*
                     * set errno only when we did not read any bytes from rb
                     * this is how standard posix read/send works
                     */

                    errno = EAGAIN;
                    return -1;
                }
                return read;
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
            return -1;
        }

        /*
         * Elements in memory can overlap, so we need to calculate how much
         * elements we can safel
         */

        count_to_end = rb_count_end(rb);
        count_to_read = count > count_to_end ? count_to_end : count;
        bytes_to_read = count_to_read * rb->object_size;

        memcpy(buf, rb->buffer + rb->tail * rb->object_size, bytes_to_read);

        /*
         * Adjust pointers and counts for the next read
         */

        buf += bytes_to_read;
        rb->tail += count_to_read;
        rb->tail &= rb->count - 1;
        read += count_to_read;
        count -= count_to_read;

        /*
         * Signal any threads that waits for space to put data in buffer
         */

        pthread_cond_signal(&rb->wait_room);
        pthread_mutex_unlock(&rb->lock);
    }

    return read;
}


#endif  /* ENABLE_THREADS */


/* ==========================================================================
    Function writes maximum count of data into ring buffer from buffer.   If
    there is not enough space to store all data from buffer,  function  will
    store as many as it can, and will return count of  objects  stored  into
    ring buffer. If buffer is full, function returns -1 and EAGAIN error.
   ========================================================================== */


static long rb_sends
(
    struct rb*            rb,       /* rb object */
    const void*           buffer,   /* location of data to put into rb */
    size_t                count,    /* number of elements to put on the rb */
    unsigned long         flags     /* receiving options */
)
{
    size_t                rbspace;  /* space left in rb */
    size_t                spce;     /* space left in rb until overlap */
    size_t                objsize;  /* size of a single element in rb */
    const unsigned char*  buf;      /* buffer treated as unsigned char */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    (void)flags;

    if (count > (rbspace = rb_space_ns(rb)))
    {
        /*
         * Caller wants to store more data then there is space available
         */

        count = rbspace;
    }

    if (count == 0)
    {
        errno = EAGAIN;
        return -1;
    }

    objsize = rb->object_size;
    spce = rb_space_end(rb);
    buf = buffer;

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

    return count;
}


/* ==========================================================================
    Writes count data pointed by buffer in to rb.  Function will block until
    count elements are stored into rb, unless blocking flag  is  set  to  1.
    When rb is full and there is still data to write, caller thread will  be
    put to sleep and will be waked up as soon as there is space in rb. count
    can be any size, it can be much bigger than rb size, just keep  in  mind
    if count is too big, time waiting for space might be significant.   When
    blocking flag is set to 1, and there is less  space  in  rb  than  count
    expects, function will copy as many elements as it can and  will  return
    with number of elements written to rb.   If  buffer  is  full,  function
    returns -1 and EAGAIN error.
   ========================================================================== */


#if ENABLE_THREADS

long rb_sendt
(
    struct rb*            rb,       /* rb object */
    const void*           buffer,   /* location of data to put into rb */
    size_t                count,    /* number of elements to put on the rb */
    unsigned long         flags     /* receiving options */
)
{
    size_t                written;  /* number of bytes written to rb */
    const unsigned char*  buf;      /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    written = 0;
    buf = buffer;

    while (count)
    {
        size_t  count_to_end;
        size_t  count_to_write;
        size_t  bytes_to_write;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        pthread_mutex_lock(&rb->lock);

        while (rb_space_ns(rb) == 0 && rb->force_exit == 0)
        {
            struct timespec ts;  /* timeout for pthread_cond_timedwait */
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


            /*
             * buffer is full and no new data can be  pushed,  we  wait  for
             * room or exit if 'rb' is nonblocking
             */

            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                pthread_mutex_unlock(&rb->lock);

                if (written == 0)
                {
                    /*
                     * set errno only when we did not read any bytes from rb
                     * this is how standard posix read/send works
                     */

                    errno = EAGAIN;
                    return -1;
                }

                return written;
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
            return -1;
        }

        /*
         * Count might be too large to store it in one burst, we calculate
         * how many elements can we store before needing to overlap memor
         */

        count_to_end = rb_space_end(rb);
        count_to_write = count > count_to_end ? count_to_end : count;
        bytes_to_write = count_to_write * rb->object_size;

        memcpy(rb->buffer + rb->head * rb->object_size, buf, bytes_to_write);

        /*
         * Adjust pointers and counts for next write
         */

        buf += bytes_to_write;
        rb->head += count_to_write;
        rb->head &= rb->count - 1;
        written += count_to_write;
        count -= count_to_write;

        /*
         * Signal any threads that waits for data to read
         */

        pthread_cond_signal(&rb->wait_data);
        pthread_mutex_unlock(&rb->lock);
    }

    return written;
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
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    rb = arg;
    stopped = 0;

    pthread_mutex_lock(&rb->lock);
    rb->force_exit = 1;
    pthread_mutex_unlock(&rb->lock);

    /*
     * Send cond signal, until all threads exits read/send functions.
     */

    while (stopped != 1)
    {
        pthread_mutex_lock(&rb->lock);
        pthread_cond_signal(&rb->wait_data);
        pthread_cond_signal(&rb->wait_room);
        stopped = rb->stopped_all;
        pthread_mutex_unlock(&rb->lock);
    }

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
         * when working in non multi-threaded mode, force O_NONBLOCK flag,
         * and return, as we don't need to init pthread elements.
         */

        rb->flags |= O_NONBLOCK;
        return 0;
    }

    /*
     * Multithreaded environment
     */

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

    pthread_mutex_lock(&rb->lock);
    if (rb->stopped_all == 0)
    {
        rb->stopped_all = 1;
        pthread_mutex_unlock(&rb->lock);
        pthread_join(rb->stop_thread, NULL);
    }
    else
    {
        pthread_mutex_unlock(&rb->lock);
    }

    pthread_cond_destroy(&rb->wait_data);
    pthread_cond_destroy(&rb->wait_room);
    pthread_mutex_destroy(&rb->lock);

    return 0;
}

#endif

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
        return rb_recvs(rb, buffer, count, flags);
    }

    pthread_mutex_lock(&rb->lock);
    if (rb->force_exit)
    {
        pthread_mutex_unlock(&rb->lock);
        errno = ECANCELED;
        return -1;
    }
    pthread_mutex_unlock(&rb->lock);

    if (flags & MSG_PEEK)
    {
        /*
         * when called is just peeking, we can simply call function for
         * single thread, as it will not modify no data, and will not cause
         * deadlock
         */

        pthread_mutex_lock(&rb->lock);
        count = rb_recvs(rb, buffer, count, flags);
        pthread_mutex_unlock(&rb->lock);
        return count;
    }

    return rb_recvt(rb, buffer, count, flags);
#else
    return rb_recvs(rb, buffer, count, flags);
#endif
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
        return rb_sends(rb, buffer, count, flags);
    }

    pthread_mutex_lock(&rb->lock);
    if (rb->force_exit)
    {
        pthread_mutex_unlock(&rb->lock);
        errno = ECANCELED;
        return -1;
    }
    pthread_mutex_unlock(&rb->lock);

    return rb_sendt(rb, buffer, count, flags);
#else
    return rb_sends(rb, buffer, count, flags);
#endif
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
        return 0;
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
        pthread_mutex_lock(&rb->lock);
    }
#endif

    count = rb_count_ns(rb);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        pthread_mutex_unlock(&rb->lock);
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
        pthread_mutex_lock(&rb->lock);
    }
#endif

    space = rb_space_ns(rb);

#if ENABLE_THREADS
    if ((rb->flags & O_NONBLOCK) == 0)
    {
        pthread_mutex_unlock(&rb->lock);
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
