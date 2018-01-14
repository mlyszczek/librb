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


#include "config.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#if ENABLE_THREADS
#   include <pthread.h>
#   include <sys/socket.h>
#   include <fcntl.h>
#endif

#include "rb.h"


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
    int              force_exit;  /* if set, library will stop all operations */

#if ENABLE_THREADS
    pthread_mutex_t  lock;        /* mutex for concurrent access */
    pthread_cond_t   wait_data;   /* ca, will block if buffer is empty */
    pthread_cond_t   wait_room;   /* ca, will block if buffer is full */
    pthread_cond_t   exit_cond;   /* signal cond when thread exits recv/send */

    rb_send_f        send;         /* function pointer with implementation */
    rb_recv_f        recv;         /* function pointer with implementation */

    int              tinsend;      /* number of threads inside send function */
    int              tinread;      /* number of threads inside recv function */
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
    data   from   rb   and   will   return   number   of    bytes    copied.

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

    if (count > (rbcount = rb_count(rb)))
    {
        /*
         * Caller requested more data then is available, adjust count
         */

        count = rbcount;
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
    with   number   of   elements   stored   in    buffer.
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

            /*
             * Buffer is empty, socket is blocking, wait for data
             */

            pthread_cond_wait(&rb->wait_data, &rb->lock);
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
    rin buffer
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

    if (count > (rbspace = rb_space(rb)))
    {
        /*
         * Caller wants to store more data then there is space available
         */

        count = rbspace;
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
    with number of elements written to rb.
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

        while (rb_space(rb) == 0 && rb->force_exit == 0)
        {
            if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
                pthread_mutex_unlock(&rb->lock);
                errno = EAGAIN;
                return written;
            }

            /*
             * Buffer is full, wait for someone to read data and free space
             */

            pthread_cond_wait(&rb->wait_room, &rb->lock);
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
    This function is called only when threads are enabled.  When  rb_destroy
    is called, it will call this function in separate detached thread.  This
    function will cleanup all resources once it makes sure all threads doing
    some operations on rb object are finished.   First  check  is  to  count
    threads in read or send, and if that count is zero it proceeds  to  next
    check.

    Next check: every thread that exits  read/send  functions,  will  signal
    conditional variable rb->exit_cond.  If  function  don't  get  any  such
    signal withing 30 seconds, it assumes all threads exited and it is  safe
    to free all memory.  We need to do this, as there is small  chance  that
    some thread will enter read/send and then immediately cotext switch will
    occur, before thread can  increment  thred  counter  leading  to  memory
    dealocation (as thread counter is 0), and right after thread wakes  back
    up, it will operate on unallocated memory.

    When  that   condition   is   met,   function   frees   all   resources.

    Bugs:

    When calling rb_destroy on threaded object, and then exiting main thread
    within 30 seconds, sanitizing tools will report memory  leak.   This  is
    false positive, as rb_destroy_async didn't reach free()  code  yet,  and
    while it is detached, it is impossible to check  if  function  finished.

   ========================================================================== */


static void *rb_destroy_async(void *arg)
{
    int         i;   /* counter for exit signal */
    struct rb  *rb;  /* ring buffer object */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    rb = arg;

    pthread_mutex_lock(&rb->lock);
    rb->force_exit = 1;
    pthread_mutex_unlock(&rb->lock);

    /*
     * Send cond signal, until all threads exits read/send functions.
     */

    for (;;)
    {
        pthread_mutex_lock(&rb->lock);

        if (rb->tinread == 0)
        {
            pthread_mutex_unlock(&rb->lock);
            break;
        }

        pthread_cond_signal(&rb->wait_data);
        pthread_mutex_unlock(&rb->lock);
    }

    for (;;)
    {
        pthread_mutex_lock(&rb->lock);

        if (rb->tinsend == 0)
        {
            pthread_mutex_unlock(&rb->lock);
            break;
        }

        pthread_cond_signal(&rb->wait_room);
        pthread_mutex_unlock(&rb->lock);
    }

    for (i = 0; i != 5;)
    {
        struct timespec ts;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        pthread_mutex_lock(&rb->lock);

        if (pthread_cond_timedwait(&rb->exit_cond, &rb->lock, &ts) == ETIMEDOUT)
        {
            /*
             * we increment counter only when no exit_cond signal has been
             * received
             */

            ++i;
        }
        else
        {
            /*
             * signal received, set counter to 0
             */

            i = 0;
        }

        pthread_mutex_unlock(&rb->lock);
    }

    pthread_mutex_lock(&rb->lock);
    pthread_cond_destroy(&rb->wait_data);
    pthread_cond_destroy(&rb->wait_room);
    pthread_cond_destroy(&rb->exit_cond);
    memset(&rb->wait_data, 0, sizeof(rb->wait_data));
    memset(&rb->wait_room, 0, sizeof(rb->wait_room));
    pthread_mutex_unlock(&rb->lock);
    pthread_mutex_destroy(&rb->lock);

    memset(&rb->lock, 0, sizeof(rb->lock));
    free(rb->buffer);
    free(rb);

    return NULL;
}


#endif  /* ENABLE_THREADS */


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
#if ENABLE_THREADS
    int            e;            /* holds errno value */
#endif
    struct rb     *rb;           /* pointer to newly created buffer */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    if (rb_is_power_of_two(count) == 0)
    {
        errno = EINVAL;
        return NULL;
    }

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

    rb->head = 0;
    rb->tail = 0;
    rb->count = count;
    rb->object_size = object_size;
    rb->force_exit = 0;
    rb->flags = flags;

#if ENABLE_THREADS == 0
    return rb;
#else
    if (flags & O_NONTHREAD)
    {
        if (!(flags & O_NONBLOCK))
        {
            /*
             * O_NONBLOCK is not set, but it should if O_NONTHREAD is used
             */

            e = EINVAL;
            goto error;
        }

        rb->recv = rb_recvs;
        rb->send = rb_sends;

        return rb;
    }

    /*
     * Multithreaded environment
     */

    rb->recv = rb_recvt;
    rb->send = rb_sendt;
    rb->tinread = 0;
    rb->tinsend = 0;

    if (pthread_mutex_init(&rb->lock, NULL))
    {
        e = errno;
        goto error;
    }

    if (pthread_cond_init(&rb->exit_cond, NULL))
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

    return rb;

error:
    pthread_mutex_destroy(&rb->lock);
    pthread_cond_destroy(&rb->wait_data);
    pthread_cond_destroy(&rb->wait_room);
    pthread_cond_destroy(&rb->exit_cond);

    free(rb->buffer);
    free(rb);

    errno = e;
    return NULL;
#endif
}


/* ==========================================================================
    Reads maximum of count elements from rb and  stores  them  into  buffer.

    If rb is O_NONTHREAD or  O_NONBLOCK,  function  will  never  block,  and
    cannot guarantee writing count elements into buffer.  If  there  is  not
    enough data  in  ring  buffer,  function  will  read  whole  buffer  and
    return with elements read.

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
    if (rb == NULL || rb->force_exit || buffer == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#if ENABLE_THREADS

    pthread_mutex_lock(&rb->lock);
    rb->tinread++;
    pthread_mutex_unlock(&rb->lock);

    count = rb->recv(rb, buffer, count, flags);

    pthread_mutex_lock(&rb->lock);
    rb->tinread--;
    pthread_cond_signal(&rb->exit_cond);
    pthread_mutex_unlock(&rb->lock);

    return count;
#else
    return rb_recvs(rb, buffer, count, flags);
#endif
}


/* ==========================================================================
    Writes maximum count data from buffer into rb.

    If rb is O_NONTHREAD or O_NONBLOCK, function will never block, but  also
    cannot guarantee that count elements will be  copied  from  buffer.   If
    there is not enough space in rb, function will store as many elements as
    it  can,  and  return  with  number  of   elements   stored   into   rb.

    If rb is multithreaded, and blocking function will block (sleep)  caller
    until count elements have been stored into rb.

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
    if (rb == NULL || rb->force_exit || buffer == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#if ENABLE_THREADS
    pthread_mutex_lock(&rb->lock);
    rb->tinsend++;
    pthread_mutex_unlock(&rb->lock);

    count = rb->send(rb, buffer, count, flags);

    pthread_mutex_lock(&rb->lock);
    rb->tinsend--;
    pthread_cond_signal(&rb->exit_cond);
    pthread_mutex_unlock(&rb->lock);

    return count;
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
    if (rb == NULL || rb->buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

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
    Frees resources allocated by rb_new.  Also it stops any blocked  rb_read
    or rb_write functions so they can return.

    Below only applies to  library  working  in  threaded  environment  When
    rb_read and rb_write work in  another  threads  and  you  want  to  join
    them after stopping rb, you should call  this  function  before  joining
    threads.   If  you  do  it  otherwise,  threads  calling   rb_write   or
    rb_read can be in locked  state,  waiting  for  resources,  and  threads
    might never  return  to  call  this  function.   You  have  been  warned
   ========================================================================== */


int rb_destroy
(
    struct rb *rb  /* rb object */
)
{
#if ENABLE_THREADS
    pthread_t destroy_thread;
#endif

    if (rb == NULL)
    {
        errno = EINVAL;
        return -1;
    }

#if ENABLE_THREADS
    if (rb->flags & O_NONTHREAD)
    {
        free(rb->buffer);
        free(rb);
        return 0;
    }

    pthread_create(&destroy_thread, NULL, rb_destroy_async, rb);
    pthread_detach(destroy_thread);

#else
    free(rb->buffer);
    free(rb);
#endif

    return 0;
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
    Calculates number of elements in ring buffer
   ========================================================================== */


size_t rb_count
(
    const struct rb  *rb  /* rb object */
)
{
    return (rb->head - rb->tail) & (rb->count - 1);
}


/* ==========================================================================
    Calculates how many elements can be pushed into ring buffer
   ========================================================================== */


size_t rb_space
(
    const struct rb  *rb  /* rb object */
)
{
    return (rb->tail - (rb->head + 1)) & (rb->count - 1);
}
