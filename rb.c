/*****************************************************************************
 * Licensed under BSD 3-clause license. See LICENSE file for more information.
 * Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 ****************************************************************************/

/*****************************************************************************
 * Included Files
 ****************************************************************************/

#include "rb.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "version.h"

/*****************************************************************************
 * Private Functions
 ****************************************************************************/

/*****************************************************************************
 * Name: rb_count_end
 *
 * Description:
 *   Calculates number of elements in ring buffer until the end of buffer
 *   memory. If elements don't overlap memory, function acts like rb_count
 ****************************************************************************/

static size_t rb_count_end(const struct rb *rb)
{
  size_t end = rb->count - rb->tail;
  size_t n = (rb->head + end) & (rb->count - 1);
  return n < end ? n : end;
}

/*****************************************************************************
 * Name: rb_space_end
 *
 * Description:
 *   Calculates how many elements can be pushed into ring buffer without
 *   overlapping memory
 ****************************************************************************/

static size_t rb_space_end(const struct rb *rb)
{
  size_t end = rb->count - 1 - rb->head;
  size_t n = (end + rb->tail) & (rb->count - 1);
  return n <= end ? n : end + 1;
}

/*****************************************************************************
 * Name: rb_is_power_of_two
 *
 * Description:
 *   Checks if number x is exactly power of two number (ie. 1, 2, 4, 8, 16)
 *
 * Input Parameters:
 *   x: number to check
 *
 * Return Values:
 *   0: x is NOT a power of two number
 *   1: x is a power of two number
 ****************************************************************************/

static int rb_is_power_of_two(size_t x)
{
  return ((x != 0) && ((x & (~x + 1)) == x));
}

/*****************************************************************************
 * Name: rb_recv
 *
 * Description:
 *   Function reads maximum count of data from rb into buffer. When user
 *   requested more data than there is in a buffer, function will copy all
 *   data from rb and will return number of bytes copied. If MSG_PEEK flag is
 *   set, data will be copied into buffer, but tail pointer will not be moved,
 *   so consequent call to rb_recv will return same data.
 *
 * Input Parameters:
 *   rb: ring buffer object
 *   buffer: memory where copy data from ring buffer
 *   count: maximum count of objects read into buffer
 *   flags:
 *     MSG_PEEK: just copy data without removing them from ring buffer
 *
 * Return Values:
 *   Number of bytes copied into buffer, function cannot fail
 ****************************************************************************/

static ssize_t rb_recvs(struct rb *rb, void *buffer, size_t count, int flags)
{
  size_t rbcount;
  size_t cnte;
  size_t tail;
  size_t objsize;
  uint8_t *buf = (uint8_t *)buffer;

  if (count > (rbcount = rb_count(rb)))
    {
      /* Caller requested more data then is available, adjust count */

      count = rbcount;
    }

  objsize = rb->object_size;
  tail = rb->tail;
  cnte = rb_count_end(rb);

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

/*****************************************************************************
 * Name: rb_read
 *
 * Description:
 *   Reads count data from rb into buffer. Function will block until count
 *   elements are stored into buffer, unless blocking flag is set to 1. When
 *   rb is exhausted and there is still data to read, caller thread will be
 *   put to sleep and will be waked up as soon as there is data in rb. count
 *   can be any size, it can be much bigger than rb size, just keep in mind if
 *   count is too big, time waiting for data might be significant.
 *
 *   When blocking flag is set to 1, and there is less data in rb than count
 *   expects, function will copy as many elements as it can (actually it
 *   will copy all of data that is in rb) and will return with number of
 *   elements stored in buffer.
 *
 * Input Parameters:
 *   rb: ring buffer object to work on
 *   buffer: place where you want data to be copied to
 *   count: number fo elements (not bytes) you want to be copied
 *   flags:
 *     MSG_DONTWAIT: same as setting ring buffer to O_NONBLOCK but works only
 *     for single call.
 *
 * Return Values:
 *   count: all requested data were read into buffer
 *   <count: only part of requested count elements were copied into buffer
 *   -1: this value is returned when buffer object is being destroyed
 *
 * errno:
 *   EAGAIN: in case of return value <count
 ****************************************************************************/

static ssize_t rb_recvt(struct rb *rb, void *buffer, size_t count, int flags)
{
  size_t read = 0;
  uint8_t *buf = (uint8_t *)buffer;

  while (count)
    {
      size_t count_to_end;
      size_t count_to_read;
      size_t bytes_to_read;

      pthread_mutex_lock(&rb->lock);

      while (rb_count(rb) == 0 && rb->force_exit == 0)
        {
          if (rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT)
            {
              /* Socket is nonblocking or caller wants just this call to be
               * nonblocking, either way, return */

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

      /* Elements in memory can overlap, so we need to calculate how much
       * elements we can safely read */

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

/*****************************************************************************
 * Name: rb_send
 *
 * Description:
 *   Function writes maximum count of data into ring buffer from buffer. If
 *   there is not enough space to store all data from buffer, function will
 *   store as many as it can, and will return count of objects stored into rin
 *   buffer
 *
 * Input Parameters:
 *   rb: ring buffer object
 *   buffer: data to copy to ring buffer
 *   count: maximum count of objects to copy to ring buffer
 *   flags: non right now
 *
 * Return Values:
 *   Number of objects stored into ring buffer. Function cannot fail
 ****************************************************************************/

static ssize_t rb_sends(struct rb *rb, const void *buffer, size_t count,
                        int flags)
{
  size_t rbspace;
  size_t spce;
  size_t objsize;
  const uint8_t *buf = (const uint8_t *)buffer;

  if (count > (rbspace = rb_space(rb)))
    {
      /* Caller wants to store more data then there is space available */

      count = rbspace;
    }

  objsize = rb->object_size;
  spce = rb_space_end(rb);

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

/*****************************************************************************
 * Name: rb_write
 *
 * Description:
 *   Writes count data pointed by buffer in to rb. Function will block until
 *   count elements are stored into rb, unless blocking flag is set to 1. When
 *   rb is full and there is still data to write, caller thread will be put to
 *   sleep and will be waked up as soon as there is space in rb. count can be
 *   any size, it can be much bigger than rb size, just keep in mind if count
 *   is too big, time waiting for space might be significant.
 *
 *   When blocking flag is set to 1, and there is less space in rb than count
 *   expects, function will copy as many elements as it can and will return
 *   with number of elements written to rb.
 *
 * Input Parameters:
 *   rb: ring buffer object to work on
 *   buffer: location from where data are stored into rb
 *   count: number of elements caller desires to store into rb
 *   flags:
 *     MSG_DONTWAIT acts like rb was O_NONBLOCK, but only for this call
 *
 * Return Values:
 *   count: all requested data were written into rb
 *   <count: only part of requested count elements were copied into rb
 *   -1: this value is returned when buffer object is being destroyed
 *
 * errno:
 *   EAGAIN: in case of return value <count
 ****************************************************************************/

ssize_t rb_sendt(struct rb *rb, const void *buffer, size_t count, int flags)
{
  size_t written = 0;
  const uint8_t *buf = (const uint8_t *)buffer;

  while (count)
    {
      size_t count_to_end;
      size_t count_to_write;
      size_t bytes_to_write;

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

      /* Count might be too large to store it in one burst, we calculate how
       * many elements can we store before needing to overlap memory */

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

int rb_new(struct rb *rb, size_t count, size_t object_size, uint32_t flags)
{
  int errnos;

  if (rb == NULL || rb_is_power_of_two(count) == 0)
    {
      errno = EINVAL;
      return -1;
    }

  if ((rb->buffer = malloc(count * object_size)) == NULL)
    {
      errnos = errno;
      goto error;
    }

  rb->head = 0;
  rb->tail = 0;
  rb->count = count;
  rb->object_size = object_size;
  rb->force_exit = 0;
  rb->flags = flags;

  if (flags & O_NONTHREAD)
    {
      if (!(flags & O_NONBLOCK))
        {
          /* O_NONBLOCK is not set, but it should if O_NONTHREAD is used */

          errnos = EINVAL;
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
      errnos = errno;
      goto error;
    }

  if (pthread_cond_init(&rb->wait_data, NULL))
    {
      errnos = errno;
      goto error;
    }

  if (pthread_cond_init(&rb->wait_room, NULL))
    {
      errnos = errno;
      goto error;
    }

  return 0;

error:
  pthread_mutex_destroy(&rb->lock);
  pthread_cond_destroy(&rb->wait_data);
  pthread_cond_destroy(&rb->wait_room);

  free(rb->buffer);
  rb->buffer = NULL;

  errno = errnos;
  return -1;
}

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

ssize_t rb_read(struct rb *rb, void *buffer, size_t count)
{
  if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  return rb->recv(rb, buffer, count, 0);
}

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
 *   flags:
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

ssize_t rb_recv(struct rb *rb, void *buffer, size_t count, int flags)
{
  if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  if (flags & MSG_PEEK && (rb->flags & O_NONTHREAD) != O_NONTHREAD)
    {
      errno = EINVAL;
      return -1;
    }

  return rb->recv(rb, buffer, count, flags);
}

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

ssize_t rb_write(struct rb *rb, void *buffer, size_t count)
{
   if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

   return rb->send(rb, buffer, count, 0);
}

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
 *     MSG_DONTWAIT acts like rb was O_NONBLOCK, but only for this call
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

ssize_t rb_send(struct rb *rb, void *buffer, size_t count, int flags)
{
  if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  return rb->send(rb, buffer, count, flags);
}

/*****************************************************************************
 * Name: rb_clear
 *
 * Description:
 *   Clears all data in the buffer
 ****************************************************************************/

int rb_clear(struct rb *rb)
{
  if (rb == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  rb->head = 0;
  rb->tail = 0;
  return 0;
}

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

int rb_destroy(struct rb *rb)
{
  if (rb == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  if (rb->flags & O_NONTHREAD)
    {
      free(rb->buffer);
      rb->buffer = NULL;
      return 0;
    }

  rb->force_exit = 1;

  /* Signal locked threads in conditional variables to return, so callers can
   * exit */

  pthread_cond_signal(&rb->wait_data);
  pthread_cond_signal(&rb->wait_room);

  /* To prevent any pending operations on buffer, we lock before freeing
   * memory, so there is no crash on buffer destroy */

  pthread_mutex_lock(&rb->lock);
  free(rb->buffer);
  rb->buffer = NULL;
  pthread_mutex_unlock(&rb->lock);

  pthread_mutex_destroy(&rb->lock);
  pthread_cond_destroy(&rb->wait_data);
  pthread_cond_destroy(&rb->wait_room);

  return 0;
}

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

const char *rb_version(char *major, char *minor, char *patch)
{
  if (major && minor && patch)
    {
      /* strtok modified input string, so we need copy of version */

      char version[11 + 1];
      strcpy(version, VERSION);

      strcpy(major, strtok(version, "."));
      strcpy(minor, strtok(NULL, "."));
      strcpy(patch, strtok(NULL, "."));
    }

  return VERSION;
}

/*****************************************************************************
 * Name: rb_count
 *
 * Description:
 *   Calculates number of elements in ring buffer
 ****************************************************************************/

size_t rb_count(const struct rb *rb)
{
  return (rb->head - rb->tail) & (rb->count - 1);
}

/*****************************************************************************
 * Name: rb_space
 *
 * Description:
 *   Calculates how many elements can be pushed into ring buffer
 ****************************************************************************/

size_t rb_space(const struct rb *rb)
{
  return (rb->tail - (rb->head + 1)) & (rb->count -1 );
}


