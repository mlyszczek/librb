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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_CONFIG_H
#   include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_ASSERT_H
#   include <assert.h>
#else /* HAVE_ASSERT_H */
#   define assert(x)
#endif /* HAVE_ASSERT_H */

#if ENABLE_THREADS
#   include <fcntl.h>
#   include <pthread.h>
#   include <sys/socket.h>
#   include <sys/time.h>
#endif /* ENABLE_THREADS */

#undef TRACE_LOG
#ifdef TRACE_LOG
#   define _GNU_SOURCE
#   include <stdio.h>
#   include <syscall.h>
#   include <unistd.h>
#   include <time.h>

	pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
	struct rb *trace_rb;
#   define trace(...) do {                                                     \
		pthread_mutex_lock(&trace_lock);                                       \
		fprintf(stderr, "%ld [%s:%-5d%-20s%-6ld][rb h:%-6zu t:%-6zu c:%6zu,%6zu/%-6zu] ",                       \
			clock(), __FILE__, __LINE__, __func__, syscall(SYS_gettid),                 \
			trace_rb->head, trace_rb->tail, rb_space(trace_rb), rb_count(trace_rb), trace_rb->count);                                    \
		fprintf(stderr, __VA_ARGS__);                                                             \
		fprintf(stderr, "\n");                                                          \
		pthread_mutex_unlock(&trace_lock);                                     \
	} while (0)
#   define trace_init(RB) { trace_rb = RB; }
#else
#   define trace(...)
#   define trace_init(RB)
#endif


#include "rb.h"
#include "valid.h"

/* ==========================================================================
 *               ░█▀▄░█▀▀░█▀▀░█░░░█▀█░█▀▄░█▀█░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *               ░█░█░█▀▀░█░░░█░░░█▀█░█▀▄░█▀█░░█░░░█░░█░█░█░█░▀▀█
 *               ░▀▀░░▀▀▀░▀▀▀░▀▀▀░▀░▀░▀░▀░▀░▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */
#define return_errno(R, E) do { errno = E; return R; } while (0)
#define lock(L) do { trace("i/lock " #L); pthread_mutex_lock(&L); } while (0)
#define unlock(L) do { pthread_mutex_unlock(&L); trace("i/unlock " #L); } while (0)
#define RB_IS_GROWABLE(rb) (rb->flags & rb_growable)
#define RB_IS_ROUNDABLE(rb) (rb->flags & rb_round_count)
#define RB_IS_DYNAMIC(rb) (rb->flags & rb_dynamic)
#define RB_IS_BLOCKING(rb, flags) (!(rb->flags & rb_nonblock) && !(flags & rb_dontwait))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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
 * Check if #len is valid size for uint8, uint16, uint32 or - if supported -
 * uint64.
 *
 * @param len length to check
 *
 * @return 1 #len has valid size of integer
 * @return 0 #len has invalid size of integer
 * ========================================================================== */
static int rb_is_uint_size(size_t len)
{
#ifdef UINT64_MAX
	size_t max_len = sizeof(uint64_t);
#else
	size_t max_len = sizeof(uint32_t);
#endif

	if (len > max_len)
		return 0;

	if (rb_is_power_of_two(len))
		return 1;

	return 0;
}

/** =========================================================================
 * Returns the number of leading 0-bits in x, starting at the most
 * significant bit position. If x is 0, the result is undefined.
 *
 * For example, if #len is 1 (0b1) it will return 0, for #len 4 (0b100)
 * function will return 2
 *
 * @return Returns the number of leading 0-bits in x
 * ========================================================================== */
static unsigned rb_ctz(size_t len)
{
	return __builtin_ctz(len);
}

/** =========================================================================
 * Get size of data length information that resides before actual data.
 * For non-dynamic ring buffers, this will return 0
 *
 * @param rb ring buffer object
 *
 * @return 0 #rb object is not dynamic
 * @return size in bytes of data length information
 * ========================================================================== */
static int rb_dynamic_len_size(struct rb *rb)
{
	return RB_IS_DYNAMIC(rb) ? 1 << rb->object_size : 0;
}

/** =========================================================================
 * Wait for conditional variable to get signaled
 * ========================================================================== */
static void rb_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	struct timespec ts;  /* timeout for pthread_cond_timedwait */

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
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 5;
	pthread_cond_timedwait(cond, mutex, &ts);
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
	size_t objsize;

	new_count = rb->count * 2;
	objsize = RB_IS_DYNAMIC(rb) ? 1 : rb->object_size;
	new_buffer = realloc(rb->buffer, new_count * objsize);
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
	memcpy(rb->buffer + (rb->tail + count_to_end) * objsize, rb->buffer,
		rb->head * objsize);

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
 *
 * @exception EINVAL rb is dynamic, but object_size is not valid integer size
 * ========================================================================== */
static int rb_init_p(struct rb *rb, void *buf, size_t count,
		size_t object_size, enum rb_flags flags)
{
#if ENABLE_THREADS
	int e; /* errno value from pthread function */
#endif
	VALID(EINVAL, rb);
	VALID(EINVAL, buf);
	VALID(EINVAL, rb_is_power_of_two(count));

	trace_init(rb);
	trace("count: %zu, objsize: %zu, flags: %u", count, object_size, flags);

	if (flags & rb_dynamic)
		if (rb_is_uint_size(object_size) == 0)
			return_errno(-1, EINVAL);

	rb->buffer = buf;
	rb->head = 0;
	rb->tail = 0;
	rb->count = count;
	rb->flags = flags;
	rb->object_size = RB_IS_DYNAMIC(rb) ? rb_ctz(object_size) : object_size;

#if ENABLE_THREADS == 0
	/*
	 * multi threaded operations are not allowed when library is compiled
	 * without threads
	 */
	VALID(ENOSYS, (flags & rb_multithread) == 0);

	return 0;
#else
	if ((flags & rb_multithread) == 0) {
		/* when working in *non* multi-threaded mode, force rb_nonblock flag,
		 * and return, as we don't need to init pthread elements. */
		rb->flags |= rb_nonblock;
		return 0;
	}

	/* Multi threaded environment */

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
 * @exception EINVAL rb_round_count flag passed
 * @exception EINVAL rb_growable flag passed
 * ========================================================================== */
int rb_init(struct rb *rb, void *buf, size_t count, size_t object_size,
	enum rb_flags flags)
{
	VALID(EINVAL, !(flags & rb_round_count));
	VALID(EINVAL, !(flags & rb_growable));
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
struct rb *rb_new(size_t count, size_t object_size, enum rb_flags flags)
{
	struct rb *rb;  /* pointer to newly created buffer */
	void      *buf; /* buffer to hold data in ring buffer */
	int       e;    /* error */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	rb = NULL;
	buf = NULL;
	e = -1;

	if (flags & rb_round_count)
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
 * Copy data from #buffer onto #rb ring buffer. This function does not
 * perform any checks, so so you must make sure #count is not bigger than
 * #buffer or current #rb count. It will handle memory wrapping.
 *
 * @param rb ring buffer object
 * @param buffer location where data from rb will be stored
 * @param count requested number of data from rb
 * @param peek read, but don't remove data from #rb
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
static long rb_copy_from(struct rb *rb, void *buffer, size_t count, int peek)
{
	size_t          cnte;     /* number of elements in rb until overlap */
	size_t          objsize;  /* size, in bytes, of single object in rb */
	unsigned char*  buf;      /* buffer treated as unsigned char type */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	objsize = RB_IS_DYNAMIC(rb) ? 1 : rb->object_size;
	cnte = rb_count_end(rb);
	buf = buffer;

	if (count > cnte) {
		/* Memory wraps, copy data in two turns */
		memcpy(buf, rb->buffer + rb->tail * objsize, objsize * cnte);
		memcpy(buf + cnte * objsize, rb->buffer, (count - cnte) * objsize);
		rb->tail = peek ? rb->tail : count - cnte;
	} else {
		memcpy(buf, rb->buffer + rb->tail * objsize, count * objsize);
		rb->tail += peek ? 0 : count;
		rb->tail &= rb->count - 1;
	}

	return count;
}

static inline size_t rb_dynamic_read_count_8(struct rb *rb, int peek)
{
	uint8_t cnt;
	rb_copy_from(rb, &cnt, sizeof(cnt), peek);
	return cnt;
}

static inline size_t rb_dynamic_read_count_16(struct rb *rb, int peek)
{
	uint16_t cnt;
	rb_copy_from(rb, &cnt, sizeof(cnt), peek);
	return cnt;
}

static inline size_t rb_dynamic_read_count_32(struct rb *rb, int peek)
{
	uint32_t cnt;
	rb_copy_from(rb, &cnt, sizeof(cnt), peek);
	return cnt;
}

#ifdef UINT64_MAX
static inline size_t rb_dynamic_read_count_64(struct rb *rb, int peek)
{
	uint64_t cnt;
	rb_copy_from(rb, &cnt, sizeof(cnt), peek);
	return cnt;
}
#endif

/** =========================================================================
 * @param rb ring buffer object
 * @param peek if set, data will not be removed from #rb on read
 *
 * @return length of next message on ring buffer
 * ========================================================================== */
static size_t rb_dynamic_read_count(struct rb *rb, int peek)
{
	int index;

	size_t (* const read[])(struct rb *rb, int peek) = {
		rb_dynamic_read_count_8,
		rb_dynamic_read_count_16,
		rb_dynamic_read_count_32,
#ifdef UINT64_MAX
		rb_dynamic_read_count_64,
#endif
	};

	index = rb->object_size;
	return read[index](rb, peek);
}

/** =========================================================================
 * Reads maximum of count elements from rb and stores them into buffer.
 *
 * Function will never block, and cannot guarantee writing count elements
 * into buffer. If there is not enough data in ring buffer, function will
 * read whatever is in the ring buffer and return with only elements read.
 *
 * Function also accepts flags.
 * - rb_peek: do normal read operation, but do not remove read data from
 *   ring buffer, calling recv() with this flag multiple times will yield
 *   same results (provided that no new data is copied to ring buffer)
 *
 * @param rb ring buffer object
 * @param buffer location where data from rb will be stored
 * @param count requested number of data from rb
 * @param flags read flags
 *
 * @return Number of bytes copied to #buffer
 * @return -1 when no data could be copied to #buffer (rb is empty)
 *
 * @exception EAGAIN ring buffer is empty, nothing copied to #buffer
 * @exception EMSGSIZE #rb is dynamic, and #count is bigger than it's
 *            described by #rb->object_size
 * @exception ENOBUFS #rb is dynamic and there is data on #rb, but #buffer
 *            is not big enough to hold whole message
 * ========================================================================== */
static long rb_recvs(struct rb *rb, void *buffer, size_t count, enum rb_flags flags)
{
	size_t  rbcount;     /* number of elements in rb */
	size_t  dyn_next_count; /* size of next dynamic message in buffer */
	size_t  nread;
	size_t  tail_save;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count == 0)
		return 0;

	if (rb_count(rb) == 0)
		return_errno(-1, EAGAIN);

	if (count > (size_t)LONG_MAX)
		/* function cannot read more than LONG_MAX count of elements,
		 * trim users count to acceptable value */
		count = LONG_MAX;

	if (!RB_IS_DYNAMIC(rb)) {
		rbcount = rb_count(rb);
		if (count > rbcount)
			/* Caller requested more data then is available, adjust count */
			count = rbcount;
		return rb_copy_from(rb, buffer, count, flags & rb_peek);
	}

	tail_save = rb->tail;
	dyn_next_count = rb_dynamic_read_count(rb, 0);
	if (count < dyn_next_count) {
		rb->tail = tail_save;
		return_errno(-1, ENOBUFS);
	}

	nread = rb_copy_from(rb, buffer, dyn_next_count, 0);
	if (flags & rb_peek)
		rb->tail = tail_save;
	return nread;
}

#if ENABLE_THREADS

/** =========================================================================
 * Check if we can safely read from the #rb buffer
 *
 * For not dynamic #rb, #count is ignored, and function returns 1 when there
 * is at least 1 element on the #rb
 *
 * For dynamic #rb, 1 will be returned only when next message on #rb can be
 * fully copied to buffer of size #count
 *
 * @param rb 
 * @param count 
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
static int rb_can_read(struct rb *rb, size_t count)
{
	if (!RB_IS_DYNAMIC(rb))
		return rb_count(rb);
	return rb_count(rb) && count >= rb_dynamic_read_count(rb, rb_peek);
}

/** =========================================================================
 * Wait for #rb to be readable.
 *
 * For non-dynamic #rb this will return OK when there is any data available.
 * For dynamic, function will return OK only when whole #count can be read. 
 *
 * Call this function with rb->lock in UNLOCK state. This mutex will be
 * locked on entry, and unlocked while waiting for more space on #rb. When
 * function returns 0 (OK), rb->lock will be in locked state. When there was
 * an error, it rb->lock will be in unlocked state.
 *
 * If #nread is greater than 0, we will quit immediately just as if #rb was
 * in non-blocking mode. Standard Unix read(2) can return less than
 * requested number of bytes if there was no more data to read. So we mimic
 * this behavior and if we've read anything, and there is no more data, we
 * will return with success.
 *
 * If either #rb or #flags show that operation is non-blocking, function
 * won't block, and will return -1/EAGAIN if #rb is not readable at the
 * moment.
 *
 * @param rb ring buffer object
 * @param count requested number of elements you'd like to read from buffer
 * @param nread number of bytes already read
 * @param flags operation flags
 *
 * @return 0 if #rb is readable, rb->lock will be in locked state
 * @return -1 #rb is NOT readable, rb->lock will be in unlocked state
 *
 * @exception EAGAIN non-blocking operation was requested
 * ========================================================================== */
static int rb_wait_for_data(struct rb *rb, size_t count,
	size_t nread, enum rb_flags flags)
{
	lock(rb->lock);
	while (rb_can_read(rb, count) == 0 && rb->force_exit == 0) {
		if (nread || !RB_IS_BLOCKING(rb, flags)) {
			unlock(rb->lock);
			return_errno(-1, EAGAIN);
		}

		rb_cond_wait(&rb->wait_data, &rb->lock);
	}

	return 0;
}


/** =========================================================================
 * Reads count data from #rb into #buffer.
 *
 * Function will block until any data is stored into #buffer,
 * unless non blocking #flag is set to 1.
 *
 * If caller passes more #count than there are bytes in #rb, function
 * will copy as many as it can and will return with value less than #count.
 *
 * If #rb is non blocking or #flag is rb_nonblock when there is no
 * data in buffer, function will return -1 and EAGAIN
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
	enum rb_flags flags)
{
	long             nread;    /* number of elements read */
	size_t           to_copy;  /* number of elements to copy from rb */
	unsigned char   *buf;      /* buffer treated as unsigned char type */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	nread = 0;
	buf = buffer;

	/* globally lock read mutex, we don't want to let multiple readers to
	 * read from single rb, this may lead to situation when T1 reads part of
	 * data, then T2 comes in reads some data, and then T1 comes back and
	 * reads more data, and now T1 read data that is not continuous. Very
	 * bad when reading full packets */

	lock(rb->rlock);
	trace("i/count: %zu, flags: %u", count, flags);
	while (count) {
		if (rb_wait_for_data(rb, count, nread, flags)) {
			unlock(rb->rlock);
			trace("i/ret, nread: %zu", nread);
			return nread ? nread : -1;
		}

		if (rb->force_exit) {
			/* ring buffer is going down operations on buffer are not allowed */
			unlock(rb->lock);
			unlock(rb->rlock);
			trace("i/force exit");
			return_errno(-1, ECANCELED);
		}

		/* read as much as we can from ring buffer */
		if (RB_IS_DYNAMIC(rb)) {
			nread = rb_recvs(rb, buffer, count, flags);
			count = 0;
		} else {
			to_copy = MIN(count, (size_t)rb_count(rb));

			rb_copy_from(rb, buf, to_copy, flags & rb_peek);

			buf += to_copy * rb->object_size;
			count -= to_copy;
			nread += to_copy;
		}

		/* Signal any threads that waits for space to put data in buffer */
		pthread_cond_signal(&rb->wait_room);
		unlock(rb->lock);
	}

	unlock(rb->rlock);
	trace("i/ret %zu", nread);
	return nread;
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
long rb_recv(struct rb *rb, void *buffer, size_t count, enum rb_flags flags)
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
	if ((rb->flags & rb_multithread) == 0)
		return rb_recvs(rb, buffer, count, flags);

	lock(rb->lock);
	if (rb->force_exit) {
		unlock(rb->lock);
		errno = ECANCELED;
		return -1;
	}
	unlock(rb->lock);

	if (flags & rb_peek) {
		/* when called is just peeking, we can simply call function for
		 * single thread, as it will not modify data, and will not cause
		 * deadlock */
		lock(rb->lock);
		count = rb_recvs(rb, buffer, count, flags);
		unlock(rb->lock);
		return count;
	}

	return rb_recvt(rb, buffer, count, flags);
#else
	return rb_recvs(rb, buffer, count, flags);
#endif
}

/** =========================================================================
 * Copy #buffer onto #rb ring buffer. This function does not perform any
 * checks, so you must make sure #count is not bigger than free space on
 * #rb buffer. It will handle memory wrapping.
 *
 * @param rb ring buffer where to copy data
 * @param buffer data to copy to ring buffer
 * @param count number of elements to copy to ring buffer
 *
 * @return number of bytes copied to ring buffer
 * ========================================================================== */
static size_t rb_copy_to(struct rb *rb, const void *buffer, size_t count)
{
	size_t                spce;     /* space left in rb until overlap */
	size_t                objsize;  /* size of a single element in rb */
	const unsigned char*  buf;      /* buffer treated as unsigned char */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	objsize = RB_IS_DYNAMIC(rb) ? 1 : rb->object_size;
	spce = rb_space_end(rb);
	buf = buffer;

	if (count > spce) {
		/* Memory wraps, copy data in two turns */
		memcpy(rb->buffer + rb->head * objsize, buf, spce * objsize);
		memcpy(rb->buffer, buf + spce * objsize, (count - spce) * objsize);
		rb->head = count - spce;
	} else {
		memcpy(rb->buffer + rb->head * objsize, buf, count * objsize);
		rb->head += count;
		rb->head &= rb->count - 1;
	}

	return count;
}

static inline void rb_dynamic_write_count_8(struct rb *rb, size_t count)
{
	uint8_t cnt = count;
	rb_copy_to(rb, &cnt, sizeof(cnt));
}

static inline void rb_dynamic_write_count_16(struct rb *rb, size_t count)
{
	uint16_t cnt = count;
	rb_copy_to(rb, &cnt, sizeof(cnt));
}

static inline void rb_dynamic_write_count_32(struct rb *rb, size_t count)
{
	uint32_t cnt = count;
	rb_copy_to(rb, &cnt, sizeof(cnt));
}

#ifdef UINT64_MAX
static inline void rb_dynamic_write_count_64(struct rb *rb, size_t count)
{
	uint64_t cnt = count;
	rb_copy_to(rb, &cnt, sizeof(cnt));
}
#endif

/** =========================================================================
 * Write #count into ring buffer. Maximum #count is determined by object_size
 * and is validated here. After validating size, and making sure there will
 * be no precision loss, count is casted to integer of size that is stored in
 * object_size.
 *
 * @param rb ring buffer object
 * @param count data to store onto ring buffer
 *
 * @return 0 when #count was written to buffer, or -1 on error
 *
 * @exception EMSGSIZE #count is too large
 * ========================================================================== */
static int rb_dynamic_write_count(struct rb *rb, size_t count)
{
	int index;

#ifdef UINT64_MAX
	const uint64_t max[] = { UINT8_MAX, UINT16_MAX, UINT32_MAX, UINT64_MAX };
#else
	const uint32_t max[] = { UINT8_MAX, UINT16_MAX, UINT32_MAX };
#endif
	void (* const write[])(struct rb *rb, size_t count) = {
		rb_dynamic_write_count_8,
		rb_dynamic_write_count_16,
		rb_dynamic_write_count_32,
#ifdef UINT64_MAX
		rb_dynamic_write_count_64,
#endif
	};

	index = rb->object_size;

	if (count > max[index])
		return_errno(-1, EMSGSIZE);

	write[index](rb, count);
	return 0;
}

#if ENABLE_THREADS
/** =========================================================================
 * Check if buffer of size #count can be written.
 *
 * For not dynamic #rb, #count is ignored, and function returns 1, when
 * there is at least 1 space free on #rb
 *
 * For dynamic #rb, 1 will be returned only when #count + metadata needed
 * to store message length in #rb can fit into #rb
 *
 * @param rb ring buffer object
 * @param count buffer size that we would like to put into #rb
 *
 * @return 0 when you cannot write to #rb
 * @return 1 when you can write to #rb
 * ========================================================================== */
static int rb_can_write(struct rb *rb, size_t count)
{
	if (!RB_IS_DYNAMIC(rb))
		return rb_space(rb);
	return count + rb_dynamic_len_size(rb) <= (size_t)rb_space(rb);
}

/** =========================================================================
 * Wait for #rb to be writable.
 *
 * For non-dynamic #rb this will return OK when there is any space available.
 * For dynamic, function will return OK only when whole #count can be written
 *
 * Call this function with rb->lock in UNLOCK state. This mutex will be
 * locked on entry, and unlocked while waiting for more space on #rb. When
 * function returns 0 (OK), rb->lock will be in locked state. When there was
 * an error, it rb->lock will be in unlocked state.
 *
 * If either #rb or #flags show that operation is non-blocking, function
 * won't block, and will return -1/EAGAIN if #rb is not writable at the
 * moment.
 *
 * @param rb ring buffer object
 * @param count requested number of elements you'd like to put on buffer
 * @param flags operation flags
 *
 * @return 0 if #rb is writable, rb->lock will be in locked state
 * @return -1 #rb is NOT writable, rb->lock will be in unlocked state
 *
 * @exception ENOMEM #rb is growable, but we failed to allocate more memory
 * @exception EAGAIN non-blocking operation was requested
 * ========================================================================== */
static int rb_wait_for_space(struct rb *rb, size_t count, enum rb_flags flags)
{
	lock(rb->lock);
	while (rb_can_write(rb, count) == 0 && rb->force_exit == 0) {
		/* no free space on the buffer, grow buffer if we are allowed */
		if (RB_IS_GROWABLE(rb)) {
			int ret;
			ret = rb_grow(rb);
			if (ret) {
				unlock(rb->lock);
				return_errno(-1, ENOMEM);
			} else
				continue;
		}

		if (!RB_IS_BLOCKING(rb, flags)) {
			unlock(rb->lock);
			return_errno(-1, EAGAIN);
		}

		/* wait for buffer to free some space */
		rb_cond_wait(&rb->wait_room, &rb->lock);
	}

	return 0;
}

/** =========================================================================
 * Writes #count data pointed by #buffer into #rb. Function will block
 * until there is space on #rb, unless non-blocking flag is set to 1.
 *
 * Function will block until all #count data has been written to the buffer.
 *
 * When non blocking flag is set to 1, and there is less space in #rb than
 * requested #count, function will copy as many elements as it can and will
 * return with number of elements written to #rb. If #rb is full, function
 * returns -1 and EAGAIN error.
 *
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
	enum rb_flags flags)
{
	long                  nwritten; /* number of elements written to rb */
	size_t                to_copy;  /* number of elements to copy to rb */
	const unsigned char  *buf;      /* buffer treated as unsigned char type */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	nwritten = 0;
	buf = buffer;
	trace("i/count: %zu, flags: %u", count, flags);
	lock(rb->wlock);

	while (count) {
		if (rb_wait_for_space(rb, count, flags)) {
			unlock(rb->wlock);
			trace("i/ret, nwritten: %zu", nwritten);
			return nwritten ? nwritten : -1;
		}

		if (rb->force_exit) {
			/* ring buffer is going down operations on buffer are not allowed */
			unlock(rb->lock);
			unlock(rb->wlock);
			trace("i/force exit");
			return_errno(-1, ECANCELED);
		}

		/* copy as much as we can to ring buffer */
		if (RB_IS_DYNAMIC(rb)) {
			/* in dynamic mode, we can only write all or nothing, and
			 * at this point we know there is enough space in #rb */
			if (rb_dynamic_write_count(rb, count)) {
				unlock(rb->lock);
				unlock(rb->wlock);
				return_errno(-1, EMSGSIZE);
			}
			rb_copy_to(rb, buffer, count);
			nwritten += count;
			count -= count;
		} else {
			to_copy = MIN(count, (size_t)rb_space(rb));

			rb_copy_to(rb, buf, to_copy);

			buf += to_copy * rb->object_size;
			count -= to_copy;
			nwritten += to_copy;
		}

		/* Signal any threads that waits for data to read */
		pthread_cond_signal(&rb->wait_data);
		unlock(rb->lock);
	}

	unlock(rb->wlock);
	trace("i/ret %zu", nwritten);
	return nwritten;
}
#endif

/** =========================================================================
 * Function writes maximum count of data into ring buffer from buffer
 *
 * If there is not enough space to store all data from buffer, function will
 * store as many as it can, and will return count of objects stored into
 * ring buffer. If buffer is full, function returns -1 and EAGAIN error.
 *
 * If #rb is configured with #rb_dynamic it will make sure that whole #buffer
 * is copied to #rb or else error is returned.
 *
 * @param rb ring buffer object
 * @param buffer location of data to put into rb
 * @param count number of elements to put on the rb
 * @param flags sending options
 *
 * @return On success function will return number of bytes copied to #buffer
 * @return On error -1 is returned
 *
 * @exception EAGAIN ring buffer is full, cannot copy anything to it
 * @exception EMSGSIZE #rb is dynamic, and #count is bigger than it's
 *            described by #rb->object_size
 * @exception ENOBUFS #rb is dynamic, but there is not enough space to
 *            copy whole #buffer onto #rb
 * ========================================================================== */
long rb_sends(struct rb *rb, const void *buffer, size_t count, enum rb_flags flags)
{
	size_t  rbspace;     /* space left in rb */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	(void)flags;

	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count == 0)
		return 0;

	if (!RB_IS_GROWABLE(rb) && rb_space(rb) == 0)
		return_errno(-1, EAGAIN);

	if (count > (size_t)LONG_MAX)
		/* function cannot read more than LONG_MAX count of elements, trim
		 * users count to acceptable value */
		count = LONG_MAX;

	while ((count + rb_dynamic_len_size(rb)) > (rbspace = rb_space(rb))) {
		/* Caller wants to store more data than there is space available */
		if (RB_IS_GROWABLE(rb)) {
			if (rb_grow(rb))
				return_errno(-1, ENOMEM);
		} else {
			if (RB_IS_DYNAMIC(rb))
				return_errno(-1, ENOBUFS);
			count = rbspace;
		}
	}

	if (!RB_IS_DYNAMIC(rb))
		return rb_copy_to(rb, buffer, count);

	if (rb_dynamic_write_count(rb, count))
		return -1;
	return rb_copy_to(rb, buffer, count);
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
long rb_send(struct rb *rb, const void *buffer, size_t count, enum rb_flags flags)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count > (size_t)LONG_MAX)
		/* function cannot read more than LONG_MAX count of elements,
		 * trim users count to acceptable value */
		count = LONG_MAX;

#if ENABLE_THREADS
	if ((rb->flags & rb_multithread) == 0)
		return rb_sends(rb, buffer, count, flags);

	lock(rb->lock);
	if (rb->force_exit) {
		unlock(rb->lock);
		errno = ECANCELED;
		return -1;
	}
	unlock(rb->lock);

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
	if (rb->flags & rb_multithread)
		lock(rb->lock);
#endif

	if (clear)
		memset(rb->buffer, 0x00, rb->count * rb->object_size);

	rb->head = 0;
	rb->tail = 0;

#if ENABLE_THREADS
	if (rb->flags & rb_multithread)
		unlock(rb->lock);
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
	VALID(EINVAL, rb->flags & rb_multithread);

	lock(rb->lock);
	rb->force_exit = 1;

	/* signal all conditional variables, #force_exit is set, so all
	 * threads should just exit theirs rb_write/read functions. */
	pthread_cond_broadcast(&rb->wait_data);
	pthread_cond_broadcast(&rb->wait_room);

	unlock(rb->lock);

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
	if ((rb->flags & rb_multithread) == 0)
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

/** =========================================================================
 * Peek at size of next message in the #rb. This only makes sense when #rb
 * is dynamic
 *
 * @param rb ring buffer object
 *
 * @return size of next message in the #rb
 * @return -1 on error
 *
 * @exception EINVAL #rb is NULL, or #rb is not #rb_dynamic
 * ========================================================================== */
long rb_peek_size(struct rb *rb)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, RB_IS_DYNAMIC(rb));

	if (rb_count(rb) == 0)
		return 0;
	return rb_dynamic_read_count(rb, rb_peek);
}
