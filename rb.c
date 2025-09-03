/* ==========================================================================
    Licensed under BSD 2clause license. See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ==========================================================================
 *                       ░▀█▀░█▀█░█▀▀░█░░░█░█░█▀▄░█▀▀░█▀▀
 *                       ░░█░░█░█░█░░░█░░░█░█░█░█░█▀▀░▀▀█
 *                       ░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀▀░░▀▀▀░▀▀▀
 * ========================================================================== */
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_CONFIG_H
#   include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef TRACE_LOG
#   define _GNU_SOURCE
#   include <stdio.h>
#   include <syscall.h>
#   include <unistd.h>

#   define trace(x) do                                                         \
    {                                                                          \
        printf("[%s:%d:%s():%ld]" , __FILE__, __LINE__, __func__,              \
            syscall(SYS_gettid));                                              \
        printf x ;                                                             \
        printf("\n");                                                          \
    }                                                                          \
    while (0)
#else
#   define trace(x)
#endif

#if HAVE_ASSERT_H
#   include <assert.h>
#else /* HAVE_ASSERT_H */
#   define assert(x)
#endif /* HAVE_ASSERT_H */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if ENABLE_THREADS
#   include <fcntl.h>
#   include <pthread.h>
#   include <sys/socket.h>
#   include <sys/time.h>
#endif /* ENABLE_THREADS */

#include "rb.h"
#include "valid.h"

/* ==========================================================================
 *               ░█▀▄░█▀▀░█▀▀░█░░░█▀█░█▀▄░█▀█░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *               ░█░█░█▀▀░█░░░█░░░█▀█░█▀▄░█▀█░░█░░░█░░█░█░█░█░▀▀█
 *               ░▀▀░░▀▀▀░▀▀▀░▀▀▀░▀░▀░▀░▀░▀░▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */
#define return_errno(R, E) do { errno = E; return R; } while (0)
#define RB_IS_GROWABLE(flags) (flags & RB_GROWABLE)
#define RB_IS_ROUNDABLE(flags) (flags & RB_ROUND_COUNT)

/* ==========================================================================
 *                     ░█▀▀░█░█░█▀█░█▀▀░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *                     ░█▀▀░█░█░█░█░█░░░░█░░░█░░█░█░█░█░▀▀█
 *                     ░▀░░░▀▀▀░▀░▀░▀▀▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */

/** =========================================================================
 * Calculates number of elements in ring buffer until the end of buffer
 * memory. If elements don't overlap memory, function acts like rb_count
 *
 * @param rb ring buffer object
 *
 * @return Number of elements in #rb until end of buffer memory
 * ========================================================================== */
static size_t rb_count_end(const struct rb *rb)
{
	size_t end;
	size_t n;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	end = rb->count - rb->tail;
	n = (rb->head + end) & (rb->count - 1);

	return n < end ? n : end;
}


/** =========================================================================
 * Calculates how many elements can be pushed into ring buffer
 * without overlapping memory
 *
 * @param rb ring buffer object
 *
 * @return Number of free elements in #rb until end of buffer
 * ========================================================================== */
static size_t rb_space_end(const struct rb *rb)
{
	size_t end;
	size_t n;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	end = rb->count - 1 - rb->head;
	n = (end + rb->tail) & (rb->count - 1);

	return n <= end ? n : end + 1;
}

/** =========================================================================
 * Converts #v number into nearest power of two that is larger than passed #v
 *
 * @param v value to convert to nearest power of 2
 *
 * @return #v converted to nearest power of 2
 * ========================================================================== */
static unsigned long rb_nearest_power_if_two(unsigned long v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

/** =========================================================================
 * Checks if number x is exactly power of two number (ie 1, 2, 4, 8, 16)
 *
 * @param x number to check
 *
 * @return 1 if x is power of two number, 0 if not
 * ========================================================================== */
static int rb_is_power_of_two(size_t x)
{
	return (x != 0) && ((x & (~x + 1)) == x);
}

/** =========================================================================
 * Grow ring buffer #rb 2 times. If memory is wrapped around, it will be
 * relocated so ring buffer is still in valid stated after we're done here.
 * If #realloc() will not give us more memory, ring buffer stays unmodified.
 *
 * @param rb ring buffer to grow
 *
 * @return 0 on success, otherwise -1 is returned
 *
 * @exception ENOMEM #realloc() failed to give us requested memory
 * ========================================================================== */
static int rb_grow(struct rb *rb)
{
	size_t new_count;
	void *new_buffer;
	size_t count_to_end = rb_count_end(rb);

	new_count = rb->count * 2;
	new_buffer = realloc(rb->buffer, new_count * rb->object_size);
	if (new_buffer == NULL)
		return_errno(-1, ENOMEM);

	rb->buffer = new_buffer;
	rb->count = new_count;

	/* if head is ahead of tail, memory is not wrapping around,
	 * so we are effectively done */
	if (rb->head >= rb->tail)
		return 0;

	/* memory is wrapped like:
	 *
	 *    4 5 6 - - - 1 2 3
	 *        H       T
	 *
	 * numbers represent bytes as they were put on queue, '-' is empty space.
	 * We must copy 4 5 6 to front, so we get
	 *
	 *    - - - - - - 1 2 3 4 5 6
	 *                T         H
	 *
	 * We just grew buffer 2 times, so we are certain we will not overflow
	 * the buffer with our copy */
	memcpy(rb->buffer + (rb->tail + count_to_end) * rb->object_size, rb->buffer,
		rb->head * rb->object_size);

	rb->head += rb->tail + count_to_end;
	return 0;
}

/** =========================================================================
 * Initializes #rb object. Buffer and #rb object must already be allocated.
 *
 * @param rb ring buffer to initialize
 * @param buf memory buffer where data shall be stored
 * @param count number of elements that buffer can hold
 * @param object_size size, in bytes, of a single object
 * @param flags flags to create buffer with
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
static int rb_init_p(struct rb *rb, void *buf, size_t count,
		size_t object_size, unsigned long flags)
{
#if ENABLE_THREADS
	int e; /* errno value from pthread function */
#endif
	VALID(EINVAL, rb);
	VALID(EINVAL, buf);
	VALID(EINVAL, rb_is_power_of_two(count));

	trace(("count: %zu, objsize: %zu, flags: %lu", count, object_size, flags));

	rb->buffer = buf;
	rb->head = 0;
	rb->tail = 0;
	rb->count = count;
	rb->object_size = object_size;
	rb->flags = flags;

#if ENABLE_THREADS == 0
	/*
	 * multi threaded operations are not allowed when library is compiled
	 * without threads
	 */
	VALID(ENOSYS, (flags & O_MULTITHREAD) == 0);

	return 0;
#else
	if ((flags & O_MULTITHREAD) == 0) {
		/* when working in *non* multi-threaded mode, force O_NONBLOCK flag,
		 * and return, as we don't need to init pthread elements. */
		rb->flags |= O_NONBLOCK;
		return 0;
	}

	/* Multi threaded environment */

	rb->stopped_all = -1;
	rb->force_exit = 0;

	VALIDGO(e, error_lock,  (e = pthread_mutex_init(&rb->lock, NULL)) == 0);
	VALIDGO(e, error_rlock, (e = pthread_mutex_init(&rb->rlock, NULL)) == 0);
	VALIDGO(e, error_wlock, (e = pthread_mutex_init(&rb->wlock, NULL)) == 0);
	VALIDGO(e, error_data,  (e = pthread_cond_init(&rb->wait_data, NULL)) == 0);
	VALIDGO(e, error_room,  (e = pthread_cond_init(&rb->wait_room, NULL)) == 0);

	return 0;

error_room:
	pthread_cond_destroy(&rb->wait_data);
error_data:
	pthread_mutex_destroy(&rb->wlock);
error_wlock:
	pthread_mutex_destroy(&rb->rlock);
error_rlock:
	pthread_mutex_destroy(&rb->lock);
error_lock:
	errno = e;
	return -1;
#endif

	return 0;
}

/** =========================================================================
 * Initializes new ring buffer object like rb_new but does not use dynamic
 * memory allocation, you must instead provide pointers to struct rb, and
 * buffer where data will be stored
 *
 * @param rb ring buffer to initialize
 * @param buf memory buffer where data shall be stored
 * @param count number of elements that buffer can hold
 * @param object_size size, in bytes, of a single object
 * @param flags flags to create buffer with
 *
 * @return 0 on success, otherwise -1 is returned
 * 
 * @exception EINVAL RB_ROUND_COUNT flag passed
 * @exception EINVAL RB_GROWABLE flag passed
 * ========================================================================== */
int rb_init(struct rb *rb, void *buf, size_t count, size_t object_size,
	unsigned long flags)
{
	VALID(EINVAL, !RB_IS_ROUNDABLE(flags));
	VALID(EINVAL, !RB_IS_GROWABLE(flags));
	VALID(EINVAL, rb);
	VALID(EINVAL, buf);

	return rb_init_p(rb, buf, count, object_size, flags);
}

/** =========================================================================
 * Initializes ring buffer and allocates all necessary resources.
 *
 * Newly created rb will returned as a pointer. In case of an function
 * error, NULL will be returned
 *
 * @param count number of elements that buffer can hold
 * @param object_size size, in bytes, of a single object
 * @param flags flags to create buffer with
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
struct rb *rb_new(size_t count, size_t object_size, unsigned long flags)
{
	struct rb *rb;  /* pointer to newly created buffer */
	void      *buf; /* buffer to hold data in ring buffer */
	int       e;    /* error */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	rb = NULL;
	buf = NULL;
	e = -1;

	if (flags & RB_ROUND_COUNT)
		count = rb_nearest_power_if_two(count);

	if ((rb = malloc(sizeof(*rb))) == NULL)
		goto error;

	if ((buf = malloc(count * object_size)) == NULL)
		goto error;

	if (rb_init_p(rb, buf, count, object_size, flags) == 0)
		return rb;

	e = errno;

error:
	free(buf);
	free(rb);
	errno = e != -1 ? e : ENOMEM;
	return NULL;
}

/** =========================================================================
 * Reads maximum of count elements from rb and stores them into buffer.
 *
 * Function will never block, and cannot guarantee writing count elements
 * into buffer. If there is not enough data in ring buffer, function will
 * read whatever is in the ring buffer and return with only elements read.
 *
 * Function also accepts flags.
 * - MSG_PEEK: do normal read operation, but do not remove read data from
 *   ring buffer, calling recv() with this flag multiple times wille yield
 *   same results (provided that no new data is copied to ring buffer)
 *
 * @param rb ring buffer object
 * @param buffer location where data from rb will be stored
 * @param count requested number of data from rb
 * @param flags read flags
 *
 * @return Number of bytes copied to #buffer
 * @return -1 when no data could be copied to #buffer (rb is empty)
 * @exception EAGAIN ring buffer is empty, nothing copied to #buffer
 * ========================================================================== */
static long rb_recvs(struct rb *rb, void *buffer, size_t count, unsigned long flags)
{
	size_t          rbcount;  /* number of elements in rb */
	size_t          cnte;     /* number of elements in rb until overlap */
	size_t          objsize;  /* size, in bytes, of single object in rb */
	unsigned char*  buf;      /* buffer treated as unsigned char type */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count == 0)
		return 0;

	if (rb_count(rb) == 0)
		return_errno(-1, EAGAIN);

	if (count > (size_t)LONG_MAX)
		/* function cannot read more than LONG_MAX count of  elements,  trim
		 * users count to acceptable value */
		count = LONG_MAX;

	if (count > (rbcount = rb_count(rb)))
		/* Caller requested more data then is available, adjust count */
		count = rbcount;

	objsize = rb->object_size;
	cnte = rb_count_end(rb);
	buf = buffer;

	if (count > cnte) {
		/* Memory overlaps, copy data in two turns */
		memcpy(buf, rb->buffer + rb->tail * objsize, objsize * cnte);
		memcpy(buf + cnte * objsize, rb->buffer, (count - cnte) * objsize);
		rb->tail = flags & MSG_PEEK ? rb->tail : count - cnte;
	} else {
		/* Memory doesn't overlap, good we can do copying on one go */
		memcpy(buf, rb->buffer + rb->tail * objsize, count * objsize);
		rb->tail += flags & MSG_PEEK ? 0 : count;
		rb->tail &= rb->count - 1;
	}

	return count;
}

#if ENABLE_THREADS
/** =========================================================================
 * Reads count data from #rb into #buffer. Function will block until
 * any data is stored into #buffer, unless non blocking #flag is set to 1.
 * When #rb is exhausted and there is still data to read, caller thread
 * will be put to sleep and will be waked up as soon as there is data in
 * #rb. If #rb is non blocking or #flag is O_NONBLOCK when there is no
 * data in buffer, function will return -1 and EAGAIN
 *
 *
 * @param rb ring buffer to read data from
 * @param buffer location where data shall be copied to
 * @param count requested number of elements to copy to #buffer
 * @param flags single call flags
 *
 * @return number of elements copied to #buffer, this may be less than #count
 * @return -1 when there was an error
 *
 * @exception EAGAIN #rb is non blocking and there was no data on the ring
 *            buffer
 * ========================================================================== */
static long rb_recvt(struct rb *rb, void *buffer, size_t count,
	unsigned long flags)
{
	size_t          r;       /* number of elements read */
	unsigned char   *buf;    /* buffer treated as unsigned char type */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	r = 0;
	errno = 0;
	buf = buffer;

	/* globally lock read mutex, we don't want to let multiple readers to
	 * read from single rb, this may lead to situation when T1 reads part of
	 * data, then T2 comes in reads some data, and then T1 comes back and
	 * reads more data, and now T1 read data that is not continuous. Very
	 * bad when reading full packets */

	trace(("i/read lock"));
	pthread_mutex_lock(&rb->rlock);
	trace(("i/count: %zu, flags: %lu", count, flags));
	while (count) {
		size_t count_to_end;
		size_t count_to_read;
		size_t bytes_to_read;
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

		trace(("i/rb lock"));
		pthread_mutex_lock(&rb->lock);

		while (rb_count(rb) == 0 && rb->force_exit == 0) {
			struct timespec ts;  /* timeout for pthread_cond_timedwait */
			/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

			/* buffer is empty and no data can be read, we wait for any
			 * data or exit if #rb is non blocking. If we managed to read
			 * some data previously, let's bail with what we have. */
			if (r || rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT) {
				pthread_mutex_unlock(&rb->lock);
				trace(("i/rb unlock"));
				pthread_mutex_unlock(&rb->rlock);
				trace(("read unlock"));

				if (r == 0) {
					/* set errno only when we did not read any bytes from rb
					 * this is how standard posix read/send works */
					trace(("e/eagain"));
					errno = EAGAIN;
					return -1;
				}
				return r;
			}

			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 5;

			/* This happens only after calling rb_stop()
			 *
			 * on some very rare occasions it is possible that signal won't
			 * reach out rb->wait_data conditional variable. This shouldn't
			 * happen, but yet it does. Such behavior may cause deadlock.
			 * To prevent deadlock we wake this thread every now and then to
			 * make sure program is running. When everything works ok, this
			 * has marginal impact on performance and when things go south,
			 * instead of deadlocking we stall execution for maximum 5 seconds.
			 *
			 * TODO: look into this and try to make proper fix */

			pthread_cond_timedwait(&rb->wait_data, &rb->lock, &ts);
		}

		if (rb->force_exit) {
			/* ring buffer is going down operations on buffer are not allowed */
			pthread_mutex_unlock(&rb->lock);
			trace(("i/rb unlock"));
			pthread_mutex_unlock(&rb->rlock);
			trace(("read unlock"));
			trace(("i/force exit"));
			errno = ECANCELED;
			return -1;
		}

		count_to_end = rb_count_end(rb);
		count_to_read = count > count_to_end ? count_to_end : count;
		bytes_to_read = count_to_read * rb->object_size;

		memcpy(buf, rb->buffer + rb->tail * rb->object_size, bytes_to_read);
		buf += bytes_to_read;

		/* Adjust pointers and counts for the next read */
		rb->tail += count_to_read;
		rb->tail &= rb->count - 1;
		r += count_to_read;
		count -= count_to_read;

		/* Signal any threads that waits for space to put data in buffer */
		pthread_cond_signal(&rb->wait_room);
		pthread_mutex_unlock(&rb->lock);
		trace(("i/rb unlock"));
	}

	pthread_mutex_unlock(&rb->rlock);
	trace(("read unlock"));
	trace(("i/ret %zu", r));
	return r;
}
#endif  /* ENABLE_THREADS */

/** =========================================================================
 * Reads maximum of count elements from rb and stores them into buffer.
 *
 * Function will never block, and cannot guarantee writing count elements
 * into buffer. If there is not enough data in ring buffer, function will
 * read whatever is in the ring buffer and return with only elements read.
 *
 * @param rb ring buffer object
 * @param buffer location where data from rb will be stored
 * @param count requested number of data from rb
 *
 * @return Number of bytes copied to #buffer
 * @return -1 when no data could be copied to #buffer (rb is empty)
 * @exception EAGAIN ring buffer is empty, nothing copied to #buffer
 * ========================================================================== */
long rb_read(struct rb *rb, void *buffer, size_t count)
{
	return rb_recv(rb, buffer, count, 0);
}

/** =========================================================================
 * Same as rb_read but also accepts #flags
 *
 * @param rb ring buffer object
 * @param buffer location where data from rb will be stored
 * @param count requested number of data from rb
 * @param flags single call flags
 *
 * @return Number of bytes copied to #buffer
 * @return -1 on error
 *
 * @exception EAGAIN ring buffer is empty, nothing copied to #buffer
 * @exception EINVAL invalid parameter passed
 * ========================================================================== */
long rb_recv(struct rb *rb, void *buffer, size_t count, unsigned long flags)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count == 0)
		return 0;

	if (count > (size_t)LONG_MAX)
		/* function cannot read more than LONG_MAX count of elements, trim
		 * users count to acceptable value */
		count = LONG_MAX;

#if ENABLE_THREADS
	if ((rb->flags & O_MULTITHREAD) == 0)
		return rb_recvs(rb, buffer, count, flags);

	trace(("i/rb lock"));
	pthread_mutex_lock(&rb->lock);
	if (rb->force_exit) {
		pthread_mutex_unlock(&rb->lock);
		trace(("i/rb unlock"));
		errno = ECANCELED;
		return -1;
	}
	pthread_mutex_unlock(&rb->lock);
	trace(("i/rb unlock"));

	if (flags & MSG_PEEK) {
		/* when called is just peeking, we can simply call function for
		 * single thread, as it will not modify data, and will not cause
		 * deadlock */
		trace(("i/rb lock"));
		pthread_mutex_lock(&rb->lock);
		count = rb_recvs(rb, buffer, count, flags);
		pthread_mutex_unlock(&rb->lock);
		trace(("i/rb unlock"));
		return count;
	}

	return rb_recvt(rb, buffer, count, flags);
#else
	return rb_recvs(rb, buffer, count, flags);
#endif
}

#if ENABLE_THREADS
/** =========================================================================
 * Writes #count data pointed by #buffer into #rb. Function will block
 * until there is space on #rb, unless non-blocking flag is set to 1.
 * When #rb is full and there is still data to write, caller thread will
 * be put to sleep and will be waked up as soon as there is space in rb.
 * When non blocking flag is set to 1, and there is less space in #rb than
 * requested #count, function will copy as many elements as it can and will
 * return with number of elements written to #rb. If #rb is full, function
 * returns -1 and EAGAIN error.

 * @param rb ring buffer to write to
 * @param buffer pointer to memory from which data shall be copied from
 * @param count requested number of bytes to copy to #rb
 * @param flags single call flags
 *
 * @return number of elements copied to ring buffer
 * @return -1 on errors
 *
 * @exception EAGAIN ring buffer is full, and non blocking operation has been
 *            requested
 * ========================================================================== */
static long rb_sendt(struct rb *rb, const void *buffer, size_t count,
	unsigned long flags)
{
	size_t                w;   /* number of elements written to rb */
	const unsigned char  *buf; /* buffer treated as unsigned char type */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	w = 0;
	buf = buffer;
	trace(("i/fd: %d, count: %zu, flags: %lu", fd, count, flags));
	trace(("i/write lock"));
	pthread_mutex_lock(&rb->wlock);

	while (count) {
		size_t  count_to_end;
		size_t  count_to_write;
		size_t  bytes_to_write;
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

		trace(("i/rb lock"));
		pthread_mutex_lock(&rb->lock);

		while (rb_space(rb) == 0 && rb->force_exit == 0) {
			struct timespec ts;  /* timeout for pthread_cond_timedwait */
			/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

			if (RB_IS_GROWABLE(rb->flags)) {
				if (rb_grow(rb)) {
					pthread_mutex_unlock(&rb->lock);
					trace(("i/rb unlock"));
					return_errno(-1, ENOMEM);
				} else
					continue;
			}

			/* buffer is full and no new data can be pushed, we wait  for
			 * room or exit if 'rb' is non blocking */
			if (w || rb->flags & O_NONBLOCK || flags & MSG_DONTWAIT) {
				pthread_mutex_unlock(&rb->lock);
				trace(("i/rb unlock"));
				pthread_mutex_unlock(&rb->wlock);
				trace(("i/write unlock"));

				if (w == 0) {
					/* set errno only when we did not read any bytes from rb
					 * this is how standard posix read/send works */
					errno = EAGAIN;
					return -1;
				}

				trace(("i/ret %zu", w));
				return w;
			}

			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 5;

			/* This happens only after calling rb_stop()
			 *
			 * on some very rare occasions it is possible that signal won't
			 * reach out rb->wait_room conditional variable. This shouldn't
			 * happen, but yet it does. Such behavior may cause deadlock.
			 * To prevent deadlock we wake this thread every now and then to
			 * make sure program is running. When everything works ok, this
			 * has marginal impact on performance and when things go south,
			 * instead of deadlocking we stall execution for maximum 5
			 * seconds.
			 *
			 * TODO: look into this and try to make proper fix */
			pthread_cond_timedwait(&rb->wait_room, &rb->lock, &ts);
		}

		if (rb->force_exit == 1) {
			/* ring buffer is going down operations on buffer are not allowed */
			pthread_mutex_unlock(&rb->lock);
			trace(("i/rb unlock"));
			pthread_mutex_unlock(&rb->wlock);
			trace(("i/write unlock"));
			trace(("i/force exit"));
			errno = ECANCELED;
			return -1;
		}

		/* Count might be too large to store it in one burst, we
		 * calculate how many elements can we store before needing to
		 * overlap memory */
		count_to_end = rb_space_end(rb);
		count_to_write = count > count_to_end ? count_to_end : count;
		bytes_to_write = count_to_write * rb->object_size;

		memcpy(rb->buffer + rb->head * rb->object_size, buf, bytes_to_write);
		buf += bytes_to_write;

		/* Adjust pointers and counts for next write */
		rb->head += count_to_write;
		rb->head &= rb->count - 1;
		w += count_to_write;
		count -= count_to_write;

		/* Signal any threads that waits for data to read */
		pthread_cond_signal(&rb->wait_data);
		pthread_mutex_unlock(&rb->lock);
		trace(("i/rb unlock"));
	}

	pthread_mutex_unlock(&rb->wlock);
	trace(("i/write unlock"));
	trace(("i/ret %zu", w));
	return w;
}
#endif

/** =========================================================================
 * Function writes maximum count of data into ring buffer from buffer
 *
 * If there is not enough space to store all data from buffer, function will
 * store as many as it can, and will return count of objects stored into
 * ring buffer. If buffer is full, function returns -1 and EAGAIN error.
 *
 * @param rb ring buffer object
 * @param buffer location of data to put into rb
 * @param count number of elements to put on the rb
 * @param flags sending options
 *
 * @return On success function will return number of bytes copied to #buffer
 * @return On error -1 is returned
 * @exception EAGAIN ring buffer is full, cannot copy anything to it
 * ========================================================================== */
long rb_sends(struct rb *rb, const void *buffer, size_t count, unsigned long flags)
{
	size_t                rbspace;  /* space left in rb */
	size_t                spce;     /* space left in rb until overlap */
	size_t                objsize;  /* size of a single element in rb */
	const unsigned char*  buf;      /* buffer treated as unsigned char */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	(void)flags;

	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count == 0)
		return 0;

	if (!RB_IS_GROWABLE(rb->flags) && rb_space(rb) == 0)
		return_errno(-1, EAGAIN);

	if (count > (size_t)LONG_MAX)
		/* function cannot read more than LONG_MAX count of elements, trim
		 * users count to acceptable value */
		count = LONG_MAX;

	while (count > (rbspace = rb_space(rb))) {
		/* Caller wants to store more data than there is space available */
		if (RB_IS_GROWABLE(rb->flags)) {
			if (rb_grow(rb))
				return_errno(-1, ENOMEM);
		} else {
			count = rbspace;
		}
	}

	objsize = rb->object_size;
	spce = rb_space_end(rb);
	buf = buffer;

	if (count > spce) {
		/* Memory overlaps, copy data in two turns */
		memcpy(rb->buffer + rb->head * objsize, buf, spce * objsize);
		memcpy(rb->buffer, buf + spce * objsize, (count - spce) * objsize);
		rb->head = count - spce;
	} else {
		/* Memory doesn't overlap, good, we can do copying in one go */
		memcpy(rb->buffer + rb->head * objsize, buf, count * objsize);
		rb->head += count;
		rb->head &= rb->count - 1;
	}

	return count;
}

/** =========================================================================
 * Same as #rb_write but also accepts flags
 *
 * @param rb ring buffer object
 * @param buffer location of data to put into rb
 * @param count number of elements to put on the rb
 * @param flags single call flag
 *
 * @return On success function will return number of bytes copied to #buffer
 * @return On error -1 is returned
 *
 * @exception EAGAIN ring buffer is full, cannot copy anything to it
 * ========================================================================== */
long rb_send(struct rb *rb, const void *buffer, size_t count,
	unsigned long  flags)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count > (size_t)LONG_MAX)
		/* function cannot read more than LONG_MAX count of elements,
		 * trim users count to acceptable value */
		count = LONG_MAX;

#if ENABLE_THREADS
	if ((rb->flags & O_MULTITHREAD) == 0)
		return rb_sends(rb, buffer, count, flags);

	trace(("i/rb lock"));
	pthread_mutex_lock(&rb->lock);
	if (rb->force_exit) {
		pthread_mutex_unlock(&rb->lock);
		trace(("i/rb unlock"));
		errno = ECANCELED;
		return -1;
	}
	pthread_mutex_unlock(&rb->lock);
	trace(("i/rb unlock"));

	return rb_sendt(rb, buffer, count, flags);
#else
	return rb_sends(rb, buffer, count, flags);
#endif
}

/** =========================================================================
 * Function writes maximum count of data into ring buffer from buffer
 *
 * If there is not enough space to store all data from buffer, function will
 * store as many as it can, and will return count of objects stored into
 * ring buffer. If buffer is full, function returns -1 and EAGAIN error.
 *
 * Works same way as rb_send() with flags set to 0.
 *
 * @param rb ring buffer object
 * @param buffer location of data to put into rb
 * @param count number of elements to put on the rb
 *
 * @return On success function will return number of bytes copied to #buffer
 * @return On error -1 is returned
 *
 * @exception EAGAIN ring buffer is full, cannot copy anything to it
 * ========================================================================== */
long rb_write(struct rb *rb, const void *buffer, size_t count)
{
    return rb_send(rb, buffer, count, 0);
}

/** =========================================================================
 * Clears all data in the buffer
 *
 * Normally function do quick clean - only sets head and tail variables to
 * indicate buffer is free. If #clear is set to 1, function will also zero
 * out all ring buffer memory - in case you want to remove some confidential
 * data from memory.
 *
 * @param rb ring buffer object
 * @param clear if set to 1, will also clear memory
 *
 * @return 0 on success, otherwise -1 is returned
 * @exception EINVAL invalid ring buffer object passed
 * ========================================================================== */
int rb_clear(struct rb *rb, int clear)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
	if ((rb->flags & O_NONBLOCK) == 0) {
		trace(("i/rb lock"));
		pthread_mutex_lock(&rb->lock);
	}
#endif

	if (clear)
		memset(rb->buffer, 0x00, rb->count * rb->object_size);

	rb->head = 0;
	rb->tail = 0;

#if ENABLE_THREADS
	if ((rb->flags & O_NONBLOCK) == 0) {
		pthread_mutex_unlock(&rb->lock);
		trace(("i/rb unlock"));
	}
#endif

	return 0;
}

/** =========================================================================
 * Makes all rb_read/write and family functions to wake up and return with
 * error. You can call this function as many times as you want to make sure
 * all your threads are unlocked and not using #rb anymore before you destroy
 * the ring buffer.
 *
 * It only makes sense to call this when #rb is in multi-threaded mode
 *
 * @param rb ring buffer to finish
 *
 * @return 0 on success, otherwise -1 is returned
 * 
 * @exception ENOSYS #rb has been compiled without multi-thread support
 * @exception EINVAL #rb is invalid or is not a in a multi-thread mode
 * ========================================================================== */
int rb_stop(struct rb *rb)
{
	VALID(EINVAL, rb);

#if ENABLE_THREADS
	VALID(EINVAL, rb->flags & O_MULTITHREAD);

	trace(("i/rb lock"));
	pthread_mutex_lock(&rb->lock);
	rb->force_exit = 1;

	/* signal all conditional variables, #force_exit is set, so all
	 * threads should just exit theirs rb_write/read functions. */
	pthread_cond_broadcast(&rb->wait_data);
	pthread_cond_broadcast(&rb->wait_room);

	pthread_mutex_unlock(&rb->lock);
	trace(("i/rb unlock"));

	return 0;
#else
	errno = ENOSYS;
	return -1;
#endif
}

/** =========================================================================
 * Clean up stack allocated ring buffer. Should only be called when #rb has
 * been created with #rb_init() function. If you have multi-threaded #rb,
 * you must first make sure no other thread is using #rb object. You can
 * wake up locked threads with #rb_stop() function.
 *
 * @param rb ring buffer to cleanup
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
int rb_cleanup(struct rb *rb)
{
#if ENABLE_THREADS
	if ((rb->flags & O_MULTITHREAD) == 0)
		return 0;

	pthread_cond_destroy(&rb->wait_data);
	pthread_cond_destroy(&rb->wait_room);
	pthread_mutex_destroy(&rb->lock);
	pthread_mutex_destroy(&rb->rlock);
	pthread_mutex_destroy(&rb->wlock);
#endif

	return 0;
}

/** =========================================================================
 * Same as #rb_cleanup() but for objects created with #rb_new() instead
 *
 * @param rb ring buffer object to destroy
 *
 * @return 0 on success, otherwise -1 is returned
 * @exception EINVAL invalid ring buffer object passed
 * ========================================================================== */
int rb_destroy(struct rb *rb)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

	rb_cleanup(rb);
	free(rb->buffer);
	free(rb);
	return 0;
}

/** =========================================================================
 * Function that discards data from tail of buffer. This works just like
 * rb_reads function, but is way faster as there is no copying involved
 *
 * @param rb ring buffer object
 * @param count number of elements to discard
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
long rb_discard(struct rb *rb, size_t count)
{
	size_t rbcount;  /* number of elements in rb */
	size_t cnte;     /* number of elements in rb until overlap */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

	cnte = rb_count_end(rb);
	rbcount = rb_count(rb);

	if (count > rbcount)
		count = rbcount;

	if (count > cnte)
		rb->tail = count - cnte;
	else {
		rb->tail += count;
		rb->tail &= rb->count -1;
	}

	return (long)count;
}

/** =========================================================================
 * Returns number of elements in buffer
 *
 * @param rb ring buffer object
 *
 * @return on success will return number of elements in buffer
 * @return on error -1 is returned with errno
 * @exception EINVAL invalid #rb object passed
 * ========================================================================== */
long rb_count(struct rb *rb)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

	return (rb->head - rb->tail) & (rb->count - 1);
}

/** =========================================================================
 * Returns number of free elements in buffer
 *
 * @param rb ring buffer object
 *
 * @return on success will return number of free elements in buffer
 * @return on error -1 is returned with errno
 * @exception EINVAL invalid #rb object passed
 * ========================================================================== */
long rb_space(struct rb *rb)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

	return (rb->tail - (rb->head + 1)) & (rb->count - 1);
}
