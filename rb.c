/*$2
 ===============================================================================
    Licensed under BSD 2-clause license. See LICENSE file for more information.
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 ===============================================================================
 */


/*$2- Included Files =========================================================*/


#include "rb.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef LIBRB_PTHREAD
#   include <pthread.h>
#   include <sys/socket.h>
#   include <fcntl.h>
#endif

#include "version.h"


/*$2- Private Functions ======================================================*/


/*
 -------------------------------------------------------------------------------
    Calculates number of elements in ring buffer until the end of buffer
    memory. If elements don't overlap memory, function acts like rb_count
 -------------------------------------------------------------------------------
 */

static size_t rb_count_end(
    const struct rb*    rb) /* rb object */
{
    /*~~~~~~~~*/
    size_t  end;
    size_t  n;
    /*~~~~~~~~*/

    end = rb->count - rb->tail;
    n = (rb->head + end) & (rb->count - 1);

    return n < end ? n : end;
}

/*
 -------------------------------------------------------------------------------
    Calculates how many elements can be pushed into ring buffer without
    overlapping memory
 -------------------------------------------------------------------------------
 */

static size_t rb_space_end(
    const struct rb*    rb) /* rb object */
{
    /*~~~~~~~~*/
    size_t  end;
    size_t  n;
    /*~~~~~~~~*/

    end = rb->count - 1 - rb->head;
    n = (end + rb->tail) & (rb->count - 1);

    return n <= end ? n : end + 1;
}

/*
 -------------------------------------------------------------------------------
    Checks if number x is exactly power of two number (ie. 1, 2, 4, 8, 16)
 -------------------------------------------------------------------------------
 */

static int rb_is_power_of_two(
    size_t  x)  /* number to check */
{
    return (x != 0) && ((x & (~x + 1)) == x);
}

/*
 -------------------------------------------------------------------------------
    Function reads maximum count of data from rb into buffer. When user
    requested more data than there is in a buffer, function will copy all data
    from rb and will return number of bytes copied.~
    If MSG_PEEK flag is set, data will be copied into buffer, but tail pointer
    will not be moved, so consequent call to rb_recv will return same data.
 -------------------------------------------------------------------------------
 */

static long rb_recvs(
    struct rb*      rb,     /* rb object */
    void*           buffer, /* buffer where received data will be copied to */
    size_t          count,  /* requested number of elements to copy to buffer */
    unsigned long   flags)  /* receiving options */
{
    /*~~~~~~~~~~~~~~~~~~~~~~*/
    size_t          rbcount;    /* number of elements in rb */
    size_t          cnte;       /* number of elements in rb until overlap */
    size_t          tail;       /* copy of rb->tail in case we need to restore
                                 * it */
    size_t          objsize;    /* size, in bytes, of single object in rb */
    unsigned char*  buf;        /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~~~~*/

    if (count > (rbcount = rb_count(rb)))
    {
        /* Caller requested more data then is available, adjust count */

        count = rbcount;
    }

    objsize = rb->object_size;
    tail = rb->tail;
    cnte = rb_count_end(rb);
    buf = buffer;

    if (count > cnte)
    {
        /* Memory overlaps, copy data in two turns */

        memcpy(buf, rb->buffer + rb->tail * objsize, objsize * cnte);
        memcpy(buf + cnte * objsize, rb->buffer, (count - cnte) * objsize);
        rb->tail = count - cnte;
    }
    else
    {
        /* Memory doesn't overlap, good we can do copying on one go */

        memcpy(buf, rb->buffer + rb->tail * objsize, count * objsize);
        rb->tail += count;
        rb->tail &= rb->count - 1;
    }

    if (flags & MSG_PEEK)
    {
        /* Caller is just peeking, restore previous tail position */

        rb->tail = tail;
    }

    return count;
}

#ifdef LIBRB_PTHREAD


/*
 -------------------------------------------------------------------------------
    Reads count data from rb into buffer. Function will block until count
    elements are stored into buffer, unless blocking flag is set to 1. When rb
    is exhausted and there is still data to read, caller thread will be put to
    sleep and will be waked up as soon as there is data in rb. count can be any
    size, it can be much bigger than rb size, just keep in mind if count is too
    big, time waiting for data might be significant.~
    When blocking flag is set to 1, and there is less data in rb than count
    expects, function will copy as many elements as it can (actually it will
    copy all of data that is in rb) and will return with number of elements
    stored in buffer.
 -------------------------------------------------------------------------------
 */

static long rb_recvt(
    struct rb*      rb,     /* rb object */
    void*           buffer, /* buffer where received data will be copied to */
    size_t          count,  /* requested number of elements to copy to buffer */
    unsigned long   flags)  /* receiving options */
{
    /*~~~~~~~~~~~~~~~~~~~*/
    size_t          read;   /* number of elements read */
    unsigned char*  buf;    /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~*/

    read = 0;
    buf = buffer;

    while (count)
    {
        /*~~~~~~~~~~~~~~~~~~*/
        size_t  count_to_end;
        size_t  count_to_read;
        size_t  bytes_to_read;
        /*~~~~~~~~~~~~~~~~~~*/

        pthread_mutex_lock(&rb->lock);

        while (rb_count(rb) == 0 && rb->force_exit == 0)
        {
            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                /*
                 * Socket is nonblocking or caller wants just this call to be
                 * nonblocking, either way, return
                 */

                pthread_mutex_unlock(&rb->lock);
                errno = EAGAIN;
                return read;
            }

            /* Buffer is empty, socket is blocking, wait for data */

            pthread_cond_wait(&rb->wait_data, &rb->lock);
        }

        if (rb->force_exit)
        {
            /* ring buffer is going down operations on buffer are not allowed */

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

        /* Adjust pointers and counts for the next read */

        buf += bytes_to_read;
        rb->tail += count_to_read;
        rb->tail &= rb->count - 1;
        read += count_to_read;
        count -= count_to_read;

        /* Signal any threads that waits for space to put data in buffer */

        pthread_cond_signal(&rb->wait_room);
        pthread_mutex_unlock(&rb->lock);
    }

    return read;
}

#endif

/*
 -------------------------------------------------------------------------------
    Function writes maximum count of data into ring buffer from buffer. If
    there is not enough space to store all data from buffer, function will
    store as many as it can, and will return count of objects stored into rin
    buffer
 -------------------------------------------------------------------------------
 */

static long rb_sends(
    struct rb*      rb,     /* rb object */
    const void*     buffer, /* location of data to put into rb */
    size_t          count,  /* requested number of elements to put on the rb */
    unsigned long   flags)  /* receiving options */
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    size_t                  rbspace;    /* space left in rb */
    size_t                  spce;       /* space left in rb until overlap */
    size_t                  objsize;    /* size of a single element in rb */
    const unsigned char*    buf;        /* buffer treated as unsigned char */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    (void)flags;

    if (count > (rbspace = rb_space(rb)))
    {
        /* Caller wants to store more data then there is space available */

        count = rbspace;
    }

    objsize = rb->object_size;
    spce = rb_space_end(rb);
    buf = buffer;

    if (count > spce)
    {
        /* Memory overlaps, copy data in two turns */

        memcpy(rb->buffer + rb->head * objsize, buf, spce * objsize);
        memcpy(rb->buffer, buf + spce * objsize, (count - spce) * objsize);
        rb->head = count - spce;
    }
    else
    {
        /* Memory doesn't overlap, good, we can do copying in one go */

        memcpy(rb->buffer + rb->head * objsize, buf, count * objsize);
        rb->head += count;
        rb->head &= rb->count - 1;
    }

    return count;
}

#ifdef LIBRB_PTHREAD


/*
 -------------------------------------------------------------------------------
    Writes count data pointed by buffer in to rb. Function will block until
    count elements are stored into rb, unless blocking flag is set to 1. When
    rb is full and there is still data to write, caller thread will be put to
    sleep and will be waked up as soon as there is space in rb. count can be
    any size, it can be much bigger than rb size, just keep in mind if count is
    too big, time waiting for space might be significant.~
    When blocking flag is set to 1, and there is less space in rb than count
    expects, function will copy as many elements as it can and will return with
    number of elements written to rb.
 -------------------------------------------------------------------------------
 */

long rb_sendt(
    struct rb*      rb,     /* rb object */
    const void*     buffer, /* location of data to put into rb */
    size_t          count,  /* requested number of elements to put on the rb */
    unsigned long   flags)  /* receiving options */
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    size_t                  written;    /* number of bytes written to rb */
    const unsigned char*    buf;        /* buffer treated as unsigned char type */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    written = 0;
    buf = buffer;

    while (count)
    {
        /*~~~~~~~~~~~~~~~~~~~*/
        size_t  count_to_end;
        size_t  count_to_write;
        size_t  bytes_to_write;
        /*~~~~~~~~~~~~~~~~~~~*/

        pthread_mutex_lock(&rb->lock);

        while (rb_space(rb) == 0 && rb->force_exit == 0)
        {
            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                pthread_mutex_unlock(&rb->lock);
                errno = EAGAIN;
                return written;
            }

            /* Buffer is full, wait for someone to read data and free space */

            pthread_cond_wait(&rb->wait_room, &rb->lock);
        }

        if (rb->force_exit == 1)
        {
            /* ring buffer is going down operations on buffer are not allowed */

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

        /* Adjust pointers and counts for next write */

        buf += bytes_to_write;
        rb->head += count_to_write;
        rb->head &= rb->count - 1;
        written += count_to_write;
        count -= count_to_write;

        /* Signal any threads that waits for data to read */

        pthread_cond_signal(&rb->wait_data);
        pthread_mutex_unlock(&rb->lock);
    }

    return written;
}

#endif


/*$2- Public Functions =======================================================*/


/*
 -------------------------------------------------------------------------------
    Initializes ring buffer and allocates all necessary resources. Newly
    created rb will be stored in sturct pointer by 'rb'. In case of an function
    error, state of 'rb' object is undefinied
 -------------------------------------------------------------------------------
 */

int rb_new(
    struct rb*      rb,             /* ring buffer to initialize */
    size_t          count,          /* number of elements (not bytes) that
                                     * buffer can hold */
    size_t          object_size,    /* size, in bytes, of a single object */
    unsigned long   flags)          /* flags to create buffer with */
{
    /*~~*/
#ifdef LIBRB_PTHREAD
    int e;  /* holds errno value */
#endif
    /*~~*/

    if (rb == NULL || rb_is_power_of_two(count) == 0)
    {
        errno = EINVAL;
        return -1;
    }

    if ((rb->buffer = malloc(count * object_size)) == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

    rb->head = 0;
    rb->tail = 0;
    rb->count = count;
    rb->object_size = object_size;
    rb->force_exit = 0;
    rb->flags = flags;

#ifndef LIBRB_PTHREAD
    return 0;
#else
    if (flags & O_NONTHREAD)
    {
        if (!(flags & O_NONBLOCK))
        {
            /* O_NONBLOCK is not set, but it should if O_NONTHREAD is used */

            e = EINVAL;
            goto error;
        }

        rb->recv = rb_recvs;
        rb->send = rb_sends;

        return 0;
    }

    /* Multithreaded environment */

    rb->recv = rb_recvt;
    rb->send = rb_sendt;

    if (pthread_mutex_init(&rb->lock, NULL))
    {
        e = errno;
        goto error;
    }

    if (pthread_cond_init(&rb->wait_data, NULL))
    {
        e = errno;
        goto error;
    }

    if (pthread_cond_init(&rb->wait_room, NULL))
    {
        e = errno;
        goto error;
    }

    return 0;

error:
    pthread_mutex_destroy(&rb->lock);
    pthread_cond_destroy(&rb->wait_data);
    pthread_cond_destroy(&rb->wait_room);

    free(rb->buffer);
    rb->buffer = NULL;

    errno = e;
    return -1;
#endif
}

/*
 -------------------------------------------------------------------------------
    Reads maximum of count elements from rb and stores them into buffer.~
    If rb is O_NONTHREAD or O_NONBLOCK, function will never block, and cannot
    guarantee writing count elements into buffer. If there is not enough data
    in ring buffer, function will read whole buffer and return with elements
    read.~
    If rb is threaded and blocking, function will block (sleep) caller thread
    until all count elements were copied into buffer.~
    Function is equivalent to call rb_recv with flags == 0
 -------------------------------------------------------------------------------
 */

long rb_read(
    struct rb*  rb,     /* rb object */
    void*       buffer, /* location where data from rb will be stored */
    size_t      count)  /* requested number of data from rb */
{
    if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#ifdef LIBRB_PTHREAD
    return rb->recv(rb, buffer, count, 0);
#else
    return rb_recvs(rb, buffer, count, 0);
#endif
}

/*
 -------------------------------------------------------------------------------
    Same as rb_read but also accepts flags
 -------------------------------------------------------------------------------
 */

long rb_recv(
    struct rb*      rb,     /* rb object */
    void*           buffer, /* location where data from rb will be stored */
    size_t          count,  /* requested number of data from rb */
    unsigned long   flags)  /* operation flags */
{
    if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#ifdef LIBRB_PTHREAD
    if (flags & MSG_PEEK && (rb->flags & O_NONTHREAD) != O_NONTHREAD)
    {
        errno = EINVAL;
        return -1;
    }

    return rb->recv(rb, buffer, count, flags);
#else
    return rb_recvs(rb, buffer, count, flags);
#endif
}

/*
 -------------------------------------------------------------------------------
    Writes maximum count data from buffer into rb.~
    If rb is O_NONTHREAD or O_NONBLOCK, function will never block, but also
    cannot guarantee that count elements will be copied from buffer. If there
    is not enough space in rb, function will store as many elements as it can,
    and return with number of elements stored into rb.~
    If rb is multithreaded, and blocking function will block (sleep) caller
    until count elements have been stored into rb.~
    Function os equivalent to call rb_send with flags == 0
 -------------------------------------------------------------------------------
 */

long rb_write(
    struct rb*      rb,     /* rb object */
    const void*     buffer, /* data to be put into rb */
    size_t          count)  /* requested number of elements to be put into rb */
{
    if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#ifdef LIBRB_PTHREAD
    return rb->send(rb, buffer, count, 0);
#else
    return rb_sends(rb, buffer, count, 0);
#endif
}

/*
 -------------------------------------------------------------------------------
    Same as rb_write but also accepts flags
 -------------------------------------------------------------------------------
 */

long rb_send(
    struct rb*      rb,     /* rb object */
    const void*     buffer, /* data to be put into rb */
    size_t          count,  /* requested number of elements to be put into r */
    unsigned long   flags)  /* operation flags */
{
    if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#ifdef LIBRB_PTHREAD
    return rb->send(rb, buffer, count, flags);
#else
    return rb_sends(rb, buffer, count, flags);
#endif
}

/*
 -------------------------------------------------------------------------------
    Clears all data in the buffer
 -------------------------------------------------------------------------------
 */

int rb_clear(
    struct rb*  rb) /* rb object */
{
    if (rb == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#ifdef LIBRB_PTHREAD
    pthread_mutex_lock(&rb->lock);
#endif

    rb->head = 0;
    rb->tail = 0;

#ifdef LIBRB_PTHREAD
    pthread_mutex_unlock(&rb->lock);
#endif

    return 0;
}

/*
 -------------------------------------------------------------------------------
    Frees resources allocated by rb_new. Also it stops any blocked rb_read or
    rb_write functions so they can return.~
    Below only applies to library working in threaded environment~
    When rb_read and rb_write work in another threads and you want to join them
    after stopping rb, you should call this function before joining threads. If
    you do it otherwise, threads calling rb_write or rb_read can be in locked
    state, waiting for resources, and threads might never return to call this
    function. You have been warned
 -------------------------------------------------------------------------------
 */

int rb_destroy(
    struct rb*  rb) /* rb object */
{
    if (rb == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (rb->buffer == NULL)
    {
        return 0;
    }

#ifdef LIBRB_PTHREAD
    if (rb->flags & O_NONTHREAD)
    {
        free(rb->buffer);
        rb->buffer = NULL;
        return 0;
    }

    rb->force_exit = 1;

    /*
     * Signal locked threads in conditional variables to return, so callers
     * can exit
     */

    pthread_cond_signal(&rb->wait_data);
    pthread_cond_signal(&rb->wait_room);

    /*
     * To prevent any pending operations on buffer, we lock before freeing
     * memory, so there is no crash on buffer destr
     */

    pthread_mutex_lock(&rb->lock);
    free(rb->buffer);
    rb->buffer = NULL;
    pthread_mutex_unlock(&rb->lock);

    pthread_mutex_destroy(&rb->lock);
    pthread_cond_destroy(&rb->wait_data);
    pthread_cond_destroy(&rb->wait_room);
#else
    free(rb->buffer);
    rb->buffer = NULL;
#endif

    return 0;
}

/*
 -------------------------------------------------------------------------------
    Returns version of the library
 -------------------------------------------------------------------------------
 */

const char* rb_version(
    char*   major,  /* major version info will be stored here */
    char*   minor,  /* minor version info will be stored here */
    char*   patch)  /* patch version info will be stored here */
{
    if (major && minor && patch)
    {
        /*~~~~~~~~~~~~~~~~~~~~*/
        char    version[11 + 1];    /* strtok modified input string, so we need
                                     * copy of version */
        /*~~~~~~~~~~~~~~~~~~~~*/

        strcpy(version, VERSION);

        strcpy(major, strtok(version, "."));
        strcpy(minor, strtok(NULL, "."));
        strcpy(patch, strtok(NULL, "."));
    }

    return VERSION;
}

/*
 -------------------------------------------------------------------------------
    Calculates number of elements in ring buffer
 -------------------------------------------------------------------------------
 */

size_t rb_count(
    const struct rb*    rb) /* rb object */
{
    return (rb->head - rb->tail) & (rb->count - 1);
}

/*
 -------------------------------------------------------------------------------
    Calculates how many elements can be pushed into ring buffer
 -------------------------------------------------------------------------------
 */

size_t rb_space(
    const struct rb*    rb) /* rb object */
{
    return (rb->tail - (rb->head + 1)) & (rb->count - 1);
}
