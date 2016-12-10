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

#include "version.h"

/*****************************************************************************
 * Private Functions
 ****************************************************************************/

/*****************************************************************************
 * Name: rb_count
 *
 * Description:
 *   Calculates number of elements in ring buffer
 ****************************************************************************/

static size_t rb_count(const struct rb *rb)
{
  return (rb->head - rb->tail) & (rb->count - 1);
}

/*****************************************************************************
 * Name: rb_space
 *
 * Description:
 *   Calculates how many elements can be pushed into ring buffer
 ****************************************************************************/

static size_t rb_space(const struct rb *rb)
{
  return (rb->tail - (rb->head + 1)) & (rb->count -1 );
}

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
 *   non_blocking: if set to 1, function will not block when resources are not
 *     available. Otherwise thread will go to sleep until resoures are
 *     available again (just like any blocking socket)
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
 *   Check proper man pages for details *
 ****************************************************************************/

int rb_new(struct rb *rb, size_t count, size_t object_size, int non_blocking)
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
  rb->non_blocking = non_blocking;

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
 *
 * Return Values:
 *   count: all requested data were read into buffer
 *   <count: only part of requested count elements were copied into buffer
 *   -1: this value is returned when buffer object is being destroyed
 *
 * errno:
 *   EAGAIN: in case of return value <count
 ****************************************************************************/

ssize_t rb_read(struct rb *rb, void *buffer, size_t count)
{
  size_t read;
  uint8_t *buf;

  if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  read = 0;
  buf = (uint8_t *)buffer;

  while (count)
    {
      size_t count_to_end;
      size_t count_to_read;
      size_t bytes_to_read;

      pthread_mutex_lock(&rb->lock);

      while (rb_count(rb) == 0 && rb->force_exit == 0)
        {
          if (rb->non_blocking == 1)
            {
              pthread_mutex_unlock(&rb->lock);
              errno = EAGAIN;
              return read;
            }

          /* Buffer is empty, wait for data */

          pthread_cond_wait(&rb->wait_data, &rb->lock);
        }

      if (rb->force_exit == 1)
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
 *
 * Return Values:
 *   count: all requested data were written into rb
 *   <count: only part of requested count elements were copied into rb
 *   -1: this value is returned when buffer object is being destroyed
 *
 * errno:
 *   EAGAIN: in case of return value <count
 ****************************************************************************/

ssize_t rb_write(struct rb *rb, const void *buffer, size_t count)
{
  size_t written;
  const uint8_t *buf;

  if (rb == NULL || buffer == NULL || rb->buffer == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  written = 0;
  buf = (const uint8_t *)buffer;

  while (count)
    {
      size_t count_to_end;
      size_t count_to_write;
      size_t bytes_to_write;

      pthread_mutex_lock(&rb->lock);

      while (rb_space(rb) == 0 && rb->force_exit == 0)
        {
          if (rb->non_blocking == 1)
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
 * Name: rb_destroy
 *
 * Description:
 *   Frees resources allocated by rb_new. Also it stops any blocked rb_read or
 *   rb_write functions so they can return. When rb_read and rb_write work in
 *   another threads and you want to join them after stopping rb, you should
 *   call this function before joining threads. If you do it otherwise,
 *   threads calling rb_write or rb_read can be in locked state, waiting for
 *   resources, and threads might never return to call this function. You have
 *   been warned
 *
 * Input Parameters:
 *   rb: ring buffer object to destroy
 ****************************************************************************/

void rb_destroy(struct rb *rb)
{
  if (rb == NULL)
    {
      errno = EINVAL;
      return;
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
