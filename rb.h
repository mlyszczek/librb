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
 * Public Types
 ****************************************************************************/

struct rb
{
  size_t head;
  size_t tail;
  size_t count;
  size_t object_size;

  uint8_t *buffer;

  pthread_mutex_t lock;
  pthread_cond_t wait_data;
  pthread_cond_t wait_room;

  uint8_t force_exit;
  uint8_t non_blocking;
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

int rb_new(struct rb *rb, size_t count, size_t object_size, int non_blocking);

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

ssize_t rb_read(struct rb *rb, void *buffer, size_t count);

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

ssize_t rb_write(struct rb *rb, const void *buffer, size_t count);

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

void rb_destroy(struct rb *rb);

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
