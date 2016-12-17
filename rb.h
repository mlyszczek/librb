/*****************************************************************************
 * Licensed under BSD 3-clause license. See LICENSE file for more information.
 * Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 ****************************************************************************/

#ifndef LIBRB_H
#define LIBRB_H 1

/*****************************************************************************
 * Included Files
 ****************************************************************************/

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/*****************************************************************************
 * Public Defines
 ****************************************************************************/

#define O_NONTHREAD (0x00010000)

/*****************************************************************************
 * Public Types
 ****************************************************************************/

struct rb;

typedef ssize_t (*rb_send_f)(struct rb *rb, const void *buffer, size_t len,
                             int flags);
typedef ssize_t (*rb_recv_f)(struct rb *rb, void *buffer, size_t len, int flags);

struct rb
{
  size_t head;
  size_t tail;
  size_t count;
  size_t object_size;
  uint32_t flags;

  uint8_t *buffer;

  pthread_mutex_t lock;
  pthread_cond_t wait_data;
  pthread_cond_t wait_room;

  rb_send_f send;
  rb_recv_f recv;

  uint8_t force_exit;
};

/*****************************************************************************
 * Public Functions
 ****************************************************************************/

/*****************************************************************************
 * Name: rb_new
 *
 * Description:
 *   Initializes ring buffer and alocates necessary resources
 *
 * Input Parameters:
 *   rb: ring buffer structure where data will be stored
 *   count: number of elements (not bytes!) that buffer can hold. It MUST be a
 *     value that is a power of two (ie 16, 32 or 4096)
 *   object_size: size, in bytes, of the objects that will be put into buffer
 *     Once declared cannot be changed, and data must always be of this size,
 *     or undefinied behaviours will occur.
 *   flags:
 *     O_NONBLOCK: if set function will not block when resources are not
 *       available. Otherwise thread will go to sleep until resoures are
 *       available again (just like any blocking socket)
 *     O_NONTHREAD: ring buffer will not use any of pthread functions and will
 *       work entirely in userspace - note: no sleeping and blocking calls
 *       possible, must be passed with O_NONBLOCK
 *
 * Return Values:
 *   0: success
 *  -1: failure
 *
 * errno:
 *   EINVAL: rb is null or count is not a power of two number
 *
 *   Function can also return errno values from:
 *     * malloc
 *     * pthread_mutex_init
 *     * pthread_cond_init
 *   Check proper man pages for details
 ****************************************************************************/

int rb_new(struct rb *rb, size_t count, size_t object_size, uint32_t flags);

/*****************************************************************************
 * Name: rb_read
 *
 * Description:
 *   Reads maximum of count elements from rb and stores them into buffer.
 *
 *   If rb is O_NONTHREAD or O_NONBLOCK, function will never block, and cannot
 *   guarantee writing count elements into buffer. If there is not enough data
 *   in ring buffer, function will read whole buffer and return with elements
 *   read.
 *
 *   If rb is threaded and blocking, function will block (sleep) caller thread
 *   until all count elements were copied into buffer.
 *
 *   Function is equivalent to call rb_recv with flags == 0
 *
 * Input Parameters:
 *   rb: ring buffer object
 *   buffer: place where data will be copied from ring buffer
 *   count: maximum count of elements to be copied to buffer
 *
 * Return:
 *   count: all requested data were read into buffer
 *  <count: only part of requested count elements were copied into buffer
 *      -1: error, see errno
 *
 * errno:
 *   EINVAL: input parameters are invalid
 *   EAGAIN: when return is <count
 ****************************************************************************/

ssize_t rb_read(struct rb *rb, void *buffer, size_t count);

/*****************************************************************************
 * Name: rb_recv
 *
 * Description:
 *   Same as rb_read but also accepts flags
 *
 * Input Parameters:
 *   rb: ring buffer object
 *   buffer: place where data will be copied from ring buffer
 *   count: maximum count of elements to be copied to buffer
 *   flags: flags...
 *     MSG_PEEK: copies data into buffer, but doesn't remove them from buffer,
 *       so subsequent call will return same data. MSG_PEEK can only be used
 *       with O_NONTHREAD.
 *
 * Return:
 *   count: all requested data were read into buffer
 *  <count: only part of requested count elements were copied into buffer
 *      -1: error, see errno
 *
 * errno:
 *   EINVAL: input parameters are invalid
 *   EAGAIN: when return is <count
 ****************************************************************************/

ssize_t rb_recv(struct rb *rb, void *buffer, size_t count, int flags);

/*****************************************************************************
 * Name: rb_write
 *
 * Description:
 *   Writes maximum count data from buffer into rb.
 *
 *   If rb is O_NONTHREAD or O_NONBLOCK, function will never block, but also
 *   cannot guarantee that count elements will be copied from buffer. If there
 *   is not enough space in rb, function will store as many elements as it
 *   can, and return with number of elements stored into rb.
 *
 *   If rb is multithreaded, and blocking function will block (sleep) caller
 *   until count elements have been stored into rb.
 *
 *   Function os equivalent to call rb_send with flags == 0
 *
 * Input Parameters:
 *   rb: ring buffer object
 *   buffer: data to copy into rb
 *   count: number of element to copy into buffer
 *
 * Return Values:
 *   count: all data has been commited into rb
 *  <count: not all data has been stored into rb
 *      -1: buffer got destroy signal during copying.
 *
 * errno:
 *   EINVAL: parameters are invalid
 *   EAGAIN: in case of return value <count
 ****************************************************************************/

ssize_t rb_write(struct rb *rb, void *buffer, size_t count);

/*****************************************************************************
 * Name: rb_send
 *
 * Description:
 *   Same as rb_write but also accepts flags
 *
 * Input Parameters:
 *   rb: ring buffer object
 *   buffer: data to copy into rb
 *   count: number of element to copy into buffer
 *   flags:
 *     MSG_DONTWAIT: acts like O_NONBLOCK but only for this one call
 *
 * Return Values:
 *   count: all data has been commited into rb
 *  <count: not all data has been stored into rb
 *      -1: buffer got destroy signal during copying.
 *
 * errno:
 *   EINVAL: parameters are invalid
 *   EAGAIN: in case of return value <count
 ****************************************************************************/

ssize_t rb_send(struct rb *rb, void *buffer, size_t count, int flags);

/*****************************************************************************
 * Name: rb_destroy
 *
 * Description:
 *   Frees resources allocated by rb_new. Also it stops any blocked rb_read or
 *   rb_write functions so they can return.
 *
 *   Below only applies to library working in threaded environment
 *
 *   When rb_read and rb_write work in
 *   another threads and you want to join them after stopping rb, you should
 *   call this function before joining threads. If you do it otherwise,
 *   threads calling rb_write or rb_read can be in locked state, waiting for
 *   resources, and threads might never return to call this function. You have
 *   been warned
 *
 * Input Parameters:
 *   rb: ring buffer object to destroy
 *
 * Return Values:
 *   0: success
 *  -1: error + errno
 *
 * Errno:
 *   EINVAL: rb is not correct
 ****************************************************************************/

int rb_destroy(struct rb *rb);

/*****************************************************************************
 * Name: rb_version
 *
 * Description:
 *   Returns version of the library
 *
 * Output Parameters:
 *   major: Major version (API/ABI changes)
 *   minor: Minor version (Added functions)
 *   patch: Patch version (bug fixes)
 *
 *   Function also returns pointer to static allocated pointer to char where
 *   full version string is stored
 ****************************************************************************/

const char *rb_version(char *major, char *minor, char *patch);

#endif /* LIBRB_H */
