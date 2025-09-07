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
#   include <pthread.h>
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
#   define trace(...) do { \
		pthread_mutex_lock(&trace_lock); \
		fprintf(stderr, "%ld [%s:%-5d%-20s%-6ld][rb h:%-6zu t:%-6zu c:%6zu,%6zu/%-6zu] ", \
			clock(), __FILE__, __LINE__, __func__, syscall(SYS_gettid), \
			trace_rb->head, trace_rb->tail, rb_space(trace_rb), rb_count(trace_rb), trace_rb->count); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
		pthread_mutex_unlock(&trace_lock); \
	} while (0)
#   define trace_init(RB) { trace_rb = RB; }
#else
#   define trace(...)
#   define trace_init(RB)
#endif

#include "rb.h"

/* ==========================================================================
 *               ░█▀▄░█▀▀░█▀▀░█░░░█▀█░█▀▄░█▀█░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *               ░█░█░█▀▀░█░░░█░░░█▀█░█▀▄░█▀█░░█░░░█░░█░█░█░█░▀▀█
 *               ░▀▀░░▀▀▀░▀▀▀░▀▀▀░▀░▀░▀░▀░▀░▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */
#define return_errno(R, E) do { errno = E; return R; } while (0)
#define trylock(L) (trace("try lock " #L), pthread_mutex_trylock(&L))
#define lock(L) do { pthread_mutex_lock(&L); trace("lock " #L); } while (0)
#define unlock(L) do { pthread_mutex_unlock(&L); trace("unlock " #L); } while (0)
#define RB_IS_GROWABLE(rb) ((rb->flags & rb_growable) && rb->count < rb->max_count)
#define RB_IS_ROUNDABLE(rb) (rb->flags & rb_round_count)
#define RB_IS_DYNAMIC(rb) (rb->flags & rb_dynamic)
#define RB_IS_BLOCKING(rb, flags) (!(rb->flags & rb_nonblock) && !(flags & rb_dontwait))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define VALID(e, x) if (!(x)) { errno = (e); return -1; }
#define VALIDGO(e, l, x) if (!(x)) { errno = (e); goto l; }

/* ==========================================================================
 *                     ░█▀▀░█░█░█▀█░█▀▀░▀█▀░▀█▀░█▀█░█▀█░█▀▀
 *                     ░█▀▀░█░█░█░█░█░░░░█░░░█░░█░█░█░█░▀▀█
 *                     ░▀░░░▀▀▀░▀░▀░▀▀▀░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
 * ========================================================================== */

/** =========================================================================
 * Perform shallow copy of ring buffer. This will not be a full fledged rb
 * object and both will share buffer.
 *
 * If ring buffer is dynamic, we have to perform 2 separate write/read
 * operations on rb. Since write/read are on separate semaphores and can
 * happen simultaneously, we cannot modify real #rb as we may get preempted
 * after first write, and then read thread will read #rb at inconsistent
 * state. We don't want to block read while doing write and vice/versa so
 * we create shallow copy of #rb, on which we will perform operation and
 * once things are done we will update real #rb object
 *
 * @param rb source ring buffer
 * @param dummy destination ring buffer.
 * ========================================================================== */
static void rb_make_shallow_copy(const struct rb *rb, struct rb *dummy)
{
	dummy->head = rb->head;
	dummy->tail = rb->tail;
	dummy->count = rb->count;
	dummy->buffer = rb->buffer;
	dummy->object_size = rb->object_size;
	dummy->flags = rb->flags;
}

/** =========================================================================
 * If #rb is blocking, lock mutex and wait for it until it really locks.
 * For non blocking operations function will only try to lock mutex, and
 * if it fails, it will exit with -1/EAGAIN error
 *
 * @param rb ring buffer object to lock
 * @param flags operation flags
 * @param mutex mutex to try and lock
 *
 * @return 0 #mutex has been locked
 * @return -1 #mutex has not been locked
 * ========================================================================== */
#if ENABLE_THREADS
static int rb_trylock(struct rb *rb, int flags, pthread_mutex_t *mutex)
{
	trace("try lock %s", mutex == &rb->wlock ? "write" : "read");
	if (RB_IS_BLOCKING(rb, flags))
		pthread_mutex_lock(mutex);
	else
		if (pthread_mutex_trylock(mutex))
			return_errno(-1, EAGAIN);
	return 0;
}
#endif

/** =========================================================================
 * Post to a semaphore, but do not exceed value of 1.
 *
 * It's normal for write operation to do sem_post() multiple times before
 * read thread does sem_wait(). If we increase semaphore to high values, and
 * then there are no new writes, read thread will be in a loop decrementing
 * semaphore for no gain. Same applies vice/versa.
 *
 * @param sem semaphore to post to
 * ========================================================================== */
#if ENABLE_THREADS
static void rb_sem_post(sem_t *sem)
{
	int value;
	sem_getvalue(sem, &value);
	if (value == 0)
		sem_post(sem);
}
#endif

/** =========================================================================
 * Calculates number of elements in ring buffer until the end of buffer
 * memory. If elements don't wrap, function acts like rb_count
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
 * without wrapping memory
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
	rb->max_count = SIZE_MAX;
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

	VALIDGO(errno, error_rsem, sem_init(&rb->read_sem, 0, 0) == 0);
	VALIDGO(errno, error_wsem, sem_init(&rb->write_sem, 0, 1) == 0);
	VALIDGO(e, error_lock,  (e = pthread_mutex_init(&rb->lock, NULL)) == 0);
	VALIDGO(e, error_rlock, (e = pthread_mutex_init(&rb->rlock, NULL)) == 0);
	VALIDGO(e, error_wlock, (e = pthread_mutex_init(&rb->wlock, NULL)) == 0);

	return 0;

error_wlock:
	pthread_mutex_destroy(&rb->rlock);
error_rlock:
	pthread_mutex_destroy(&rb->lock);
error_lock:
	sem_close(&rb->write_sem);
	errno = e;
error_wsem:
	sem_close(&rb->read_sem);
error_rsem:
	return -1;
#endif
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
 * Increase #rb->tail pointer by #count
 * ========================================================================== */
static void rb_increase_tail(struct rb *rb, size_t count)
{
	size_t          cnte;     /* number of elements in rb until wrap */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	cnte = rb_count_end(rb);

	if (count > cnte)
		rb->tail = count - cnte;
	else
		rb->tail += count;

	rb->tail &= rb->count - 1;
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
	size_t          cnte;     /* number of elements in rb until wrap */
	size_t          objsize;  /* size, in bytes, of single object in rb */
	unsigned char*  buf;      /* buffer treated as unsigned char type */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	objsize = RB_IS_DYNAMIC(rb) ? 1 : rb->object_size;
	cnte = rb_count_end(rb);
	buf = buffer;

	if (count > cnte) {
		/* Memory wraps, copy data in two turns */
		memcpy(buf, rb->buffer + rb->tail * objsize, cnte * objsize);
		memcpy(buf + cnte * objsize, rb->buffer, (count - cnte) * objsize);
	} else
		memcpy(buf, rb->buffer + rb->tail * objsize, count * objsize);

	if (!peek)
		rb_increase_tail(rb, count);

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
 * @param rb ring buffer object
 * @param buffer location where data from rb will be stored
 * @param count requested number of data from rb
 * @param flags read flags
 *
 * @return Number of bytes copied to #buffer
 * @return -1 when no data could be copied to #buffer (rb is empty)
 *
 * @exception EAGAIN ring buffer is empty, nothing copied to #buffer
 * @exception ENOBUFS #rb is dynamic and there is data on #rb, but #buffer
 *            is not big enough to hold whole message
 * ========================================================================== */
static long rb_recvs(struct rb *rb, void *buffer, size_t count, enum rb_flags flags)
{
	size_t  rbcount;     /* number of elements in rb */
	size_t  dyn_next_count; /* size of next dynamic message in buffer */
	size_t  nread;
	struct rb dummy_rb;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (rb_count(rb) == 0)
		return_errno(-1, EAGAIN);

	if (count > (size_t)LONG_MAX)
		count = LONG_MAX;

	if (!RB_IS_DYNAMIC(rb)) {
		rbcount = rb_count(rb);
		if (count > rbcount)
			/* Caller requested more data than is available, adjust count */
			count = rbcount;
		return rb_copy_from(rb, buffer, count, flags & rb_peek);
	}

	rb_make_shallow_copy(rb, &dummy_rb);
	dyn_next_count = rb_dynamic_read_count(&dummy_rb, 0);
	if (count < dyn_next_count)
		return_errno(-1, ENOBUFS);

	nread = rb_copy_from(&dummy_rb, buffer, dyn_next_count, 0);
	if (!(flags & rb_peek))
		/* update real rb object, if we are not peeking */
		rb->tail = dummy_rb.tail;
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
	long ret = rb_count(rb);
	size_t next_count = 0;
	if (ret)
		next_count = rb_dynamic_read_count(rb, rb_peek);
	trace("can read: h:%zu t:%zu c:%ld nc: %zu", rb->head, rb->tail, ret, next_count);
	return rb_count(rb) && count >= next_count;
}

/** =========================================================================
 * Wait for #rb to be readable.
 *
 * For non-dynamic #rb this will return OK when there is any data available.
 * For dynamic, function will return OK only when whole #count can be read. 
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
 * @return  0 #rb is readable
 * @return -1 #rb is NOT readable
 *
 * @exception EAGAIN non-blocking operation was requested
 * ========================================================================== */
static int rb_wait_for_data(struct rb *rb, size_t count,
	size_t nread, enum rb_flags flags)
{
	while (rb_can_read(rb, count) == 0 && rb->force_exit == 0) {
		if (nread || !RB_IS_BLOCKING(rb, flags))
			return_errno(-1, EAGAIN);

		trace("sem wait read");
		sem_wait(&rb->read_sem);
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
	int              e;        /* errno cache */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	nread = 0;
	buf = buffer;

	if (rb_trylock(rb, flags, &rb->rlock))
		return -1;
	trace("count: %zu, flags: %u", count, flags);

	while (count) {
		if (rb_wait_for_data(rb, count, nread, flags)) {
			unlock(rb->rlock);
			trace("ret, nread: %zu", nread);
			return nread ? nread : -1;
		}

		if ((e = rb->force_exit)) {
			/* ring buffer is going down operations on buffer are not allowed */
			unlock(rb->rlock);
			trace("force exit %d nread %ld", e, nread);
			errno = e;
			return nread ? nread : -1;
		}

		/* read as much as we can from ring buffer */
		if (RB_IS_DYNAMIC(rb)) {
			nread = rb_recvs(rb, buffer, count, flags);
			count = 0;
		} else {
			size_t rbcount = rb_count(rb);
			to_copy = MIN(count, rbcount);

			rb_copy_from(rb, buf, to_copy, flags & rb_peek);

			buf += to_copy * rb->object_size;
			count -= to_copy;
			nread += to_copy;
		}

		/* Signal any threads that wait for space to put data in buffer */
		trace("sem post write");
		rb_sem_post(&rb->write_sem);
	}

	unlock(rb->rlock);
	trace("ret nread %zu", nread);
	return nread;
}
#endif  /* ENABLE_THREADS */

/** =========================================================================
 * Reads maximum #count elements from rb and stores them into buffer.
 *
 * If there is not enough data in ring buffer, function will read whatever
 * is in the ring buffer and return with only elements read.
 *
 * If multi-threading is enabled, and operation is blocking, function will
 * block until at least 1 element has been read.
 *
 * If #rb is growable and write thread wants to grow #rb, it's possible
 * for #rb_read() to return early with -1/EAGAIN.
 *
 * @param rb ring buffer object
 * @param buffer location where data from rb will be stored
 * @param count requested number of data from rb
 *
 * @return Number of bytes copied to #buffer
 * @return -1 when no data could be copied to #buffer (rb is empty)
 *
 * @exception EAGAIN ring buffer is empty, nothing copied to #buffer
 * @exception EINVAL invalid parameter passed
 * @exception ENOBUFS #rb is #rb_dynamic and there is data on #rb, but
 *            #buffer is not big enough to hold whole message
 * ========================================================================== */
long rb_read(struct rb *rb, void *buffer, size_t count)
{
	return rb_recv(rb, buffer, count, 0);
}

/** =========================================================================
 * Same as rb_read but also accepts #flags
 *
 * - rb_peek: do normal read operation, but do not remove read data from
 *   ring buffer, calling rb_recv() with this flag multiple times will yield
 *   same results (provided that no new data is copied to ring buffer).
 *   Peeking is always non-blocking operation regardless of other settings,
 *   if it cannot immediately read data it will return -1/EAGAIN
 * - rb_dontwait: don't ever block a call, return with -1/EAGAIN if there is
 *   no data to read
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
 * @exception ENOBUFS #rb is #rb_dynamic and there is data on #rb, but
 *            #buffer is not big enough to hold whole message
 * ========================================================================== */
long rb_recv(struct rb *rb, void *buffer, size_t count, enum rb_flags flags)
{
	int e;
	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count == 0)
		return 0;

	if (count > (size_t)LONG_MAX)
		count = LONG_MAX;

#if ENABLE_THREADS
	if ((rb->flags & rb_multithread) == 0)
		return rb_recvs(rb, buffer, count, flags);

	if ((e = rb->force_exit))
		return_errno(-1, e);

	if (flags & rb_peek) {
		/* when call is just peeking, we can simply call function for
		 * single thread, as it will not modify data, and will not cause
		 * deadlock */
		trace("try lock peeking read");
		if (pthread_mutex_trylock(&rb->rlock))
			return_errno(-1, EAGAIN);
		count = rb_recvs(rb, buffer, count, flags);
		unlock(rb->rlock);
		return count;
	}

	return rb_recvt(rb, buffer, count, flags);
#else
	return rb_recvs(rb, buffer, count, flags);
#endif
}

/** =========================================================================
 * Increase #rb->head pointer by #count
 * ========================================================================== */
static void rb_increase_head(struct rb *rb, size_t count)
{
	size_t spce; /* space left in rb until wrap */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	spce = rb_space_end(rb);

	if (count > spce)
		rb->head = count - spce;
	else
		rb->head += count;

	rb->head &= rb->count - 1;
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
	size_t                spce;     /* space left in rb until wrap */
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
	} else
		memcpy(rb->buffer + rb->head * objsize, buf, count * objsize);

	rb_increase_head(rb, count);
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

	if (count >= max[index])
		return_errno(-1, EMSGSIZE);

	write[index](rb, count);
	return 0;
}


/** =========================================================================
 * Perform dynamic write operation.
 *
 * This operations consists of 2 separate write operations. First it writes
 * size of frame onto buffer, and next it writes frame itself.
 *
 * @param rb ring buffer object
 * @param buffer data to put onto #rb
 * @param count number of bytes to put onto #rb
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
static int rb_dynamic_write(struct rb *rb, const void *buffer, size_t count)
{
	int written_len;

	/* we can't yet write anything to rb->head, or else read thread will
	 * start acting on that partial write. That means functions that
	 * operate on #rb like rb_space_end() will return us invalid value. */
	struct rb dummy_rb;
	rb_make_shallow_copy(rb, &dummy_rb);

	if ((written_len = rb_dynamic_write_count(&dummy_rb, count)) == -1)
		return -1;

	rb_copy_to(&dummy_rb, buffer, count);
	/* now that all data is on buffer, we can safely move head pointer */
	rb->head = dummy_rb.head;

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
 * If either #rb or #flags show that operation is non-blocking, function
 * won't block, and will return -1/EAGAIN if #rb is not writable at the
 * moment.
 *
 * @param rb ring buffer object
 * @param count requested number of elements you'd like to put on buffer
 * @param flags operation flags
 *
 * @return  0 #rb is writable
 * @return -1 #rb is NOT writable
 *
 * @exception ENOMEM #rb is growable, but we failed to allocate more memory
 * @exception EAGAIN non-blocking operation was requested
 * ========================================================================== */
static int rb_wait_for_space(struct rb *rb, size_t count, enum rb_flags flags)
{
	while (rb_can_write(rb, count) == 0 && rb->force_exit == 0) {
		/* no free space on the buffer, grow buffer if we are allowed */
		if (RB_IS_GROWABLE(rb)) {
			int ret;

			rb->force_exit = EAGAIN;
			while (pthread_mutex_trylock(&rb->rlock))
				rb_sem_post(&rb->read_sem);
			ret = rb_grow(rb);
			rb->force_exit = 0;
			unlock(rb->rlock);

			if (ret)
				return_errno(-1, ENOMEM);
			else
				continue;
		}

		if (!RB_IS_BLOCKING(rb, flags))
			return_errno(-1, EAGAIN);

		/* wait for buffer to free some space */
		trace("sem wait write");
		sem_wait(&rb->write_sem);
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
 * @exception ENOMEM #rb is growable, but we failed to allocate more memory
 * @exception EMSGSIZE #rb is dynamic and #count is too large. Increase
 *            object_size in #rb creation.
 * @exception ECANCELED rb_stop() has been called, and there was no data
 *            written to rb
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
	trace("count: %zu, flags: %u", count, flags);

	if (rb_trylock(rb, flags, &rb->wlock))
		return -1;

	while (count) {
		if (rb_wait_for_space(rb, count, flags)) {
			unlock(rb->wlock);
			trace("ret, nwritten: %zu", nwritten);
			return nwritten ? nwritten : -1;
		}

		if (rb->force_exit) {
			/* ring buffer is going down operations on buffer are not allowed */
			unlock(rb->wlock);
			trace("force exit");
			errno = ECANCELED;
			return nwritten ? nwritten : -1;
		}

		/* copy as much as we can to ring buffer */
		if (RB_IS_DYNAMIC(rb)) {
			/* in dynamic mode, we can only write all or nothing, and
			 * at this point we know there is enough space in #rb */
			if (rb_dynamic_write(rb, buffer, count)) {
				unlock(rb->wlock);
				return_errno(-1, EMSGSIZE);
			}
			nwritten += count;
			count -= count;
		} else {
			size_t rbspace = rb_space(rb);
			to_copy = MIN(count, rbspace);

			rb_copy_to(rb, buf, to_copy);

			buf += to_copy * rb->object_size;
			count -= to_copy;
			nwritten += to_copy;
		}

		/* Signal any threads that waits for data to read */
		trace("sem post read");
		rb_sem_post(&rb->read_sem);
	}

	unlock(rb->wlock);
	trace("ret nwritten %zu", nwritten);
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
static long rb_sends(struct rb *rb, const void *buffer, size_t count)
{
	size_t  rbspace;     /* space left in rb */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

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
 * Same as #rb_write but also accepts flags.
 *
 * - rb_dontwait when flag is passed, function will never block execution
 *   thread
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
 * @exception ENOMEM #rb is growable, but we failed to allocate more memory
 * @exception EMSGSIZE #rb is dynamic and #count is too large. Increase
 *            object_size in #rb creation.
 * @exception ECANCELED rb_stop() has been called, and there was no data
 *            written to rb, returned only when #rb it multi-threaded
 * ========================================================================== */
long rb_send(struct rb *rb, const void *buffer, size_t count, enum rb_flags flags)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, buffer);
	VALID(EINVAL, rb->buffer);

	if (count == 0)
		return 0;

	if (count > (size_t)LONG_MAX)
		count = LONG_MAX;

#if ENABLE_THREADS
	if ((rb->flags & rb_multithread) == 0)
		return rb_sends(rb, buffer, count);

	if (rb->force_exit == ECANCELED)
		return_errno(-1, ECANCELED);

	return rb_sendt(rb, buffer, count, flags);
#else
	return rb_sends(rb, buffer, count);
#endif
}

/** =========================================================================
 * Function writes maximum count of data into ring buffer from buffer
 *
 * If there is not enough space to store all data from buffer, function will
 * store as many as it can, and will return count of objects stored into
 * ring buffer. If buffer is full, function returns -1 and EAGAIN error.
 *
 * If #rb is blocking, function will block until all #count data is written,
 * or there is an error.
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
 * @exception ENOMEM #rb is growable, but we failed to allocate more memory
 * @exception EMSGSIZE #rb is dynamic and #count is too large. Increase
 *            object_size in #rb creation.
 * @exception ECANCELED rb_stop() has been called, and there was no data
 *            written to rb, returned only when #rb it multi-threaded
 * ========================================================================== */
long rb_write(struct rb *rb, const void *buffer, size_t count)
{
    return rb_send(rb, buffer, count, 0);
}

/** =========================================================================
 * Claims ring buffer for writing.
 *
 * In return you will get pointer to a #buffer. You can directly write to it
 * starting from offset 0. #count will tell you how big #buffer is. Do not
 * even think about writing more elements than #count. If you are close to
 * memory wrap, #count may be very small, fill the #buffer and next call will
 * give you more memory.
 *
 * #object_size defines size of a single object that is held on ring buffer.
 * You should write to a buffer in increments of #object_size bytes.
 *
 * You can specify #flags as with #rb_send()
 *
 * If multi-thread #rb is used, function will block like #rb_send until
 * space is available on #rb. After function finishes, you will be the owner
 * of #rb->wlock mutex, so no one will interfere with your writing.
 *
 * @note that #count does not describe #buffer size in bytes, but in "elements".
 *       #buffer size in bytes is #count * #object_size
 * @note remember to call #rb_write_commit() once you are done
 *
 * @param rb ring buffer object
 * @param buffer location where data should be copied to
 * @param count max number of elements that can be copied to #buffer
 * @param object_size size of single object #rb holds (in bytes)
 * @param flags operation flags
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
long rb_write_claim(struct rb *rb, void **buffer, size_t *count,
	size_t *object_size, enum rb_flags flags)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);
	VALID(EINVAL, buffer);
	VALID(EINVAL, count);
	VALID(EINVAL, object_size);

#if ENABLE_THREADS
	if (rb->flags & rb_multithread) {
		if (rb_trylock(rb, flags, &rb->wlock))
			return -1;
		if (rb_wait_for_space(rb, 1, flags))
			return -1;
	}
#endif

	trace("write claimed");
	*count = rb_space_end(rb);
	*object_size = RB_IS_DYNAMIC(rb) ? 1 : rb->object_size;
	*buffer = rb->buffer + rb->head * *object_size;

	return 0;
}

/** =========================================================================
 * Commits data written to #rb in #rb_write_claim buffer. You just have to
 * specify number of elements actually written to #rb in claim call.
 *
 * In multi-thread environment, this will release write mutex and threads
 * blocked in read will be woken up. It's ok to pass 0 as #count.
 *
 * @param rb ring object to commit to
 * @param count number of elements written to ring buffer after claiming it
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
long rb_write_commit(struct rb *rb, size_t count)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

	rb_increase_head(rb, count);

#if ENABLE_THREADS
	if (rb->flags & rb_multithread) {
		if (count)
			rb_sem_post(&rb->read_sem);
		unlock(rb->wlock);
	}
#endif

	trace("write committed");
	return 0;
}

/** =========================================================================
 * Claims ring buffer for reading.
 *
 * In return you will get pointer to a #buffer. You can directly read from it
 * starting at offset 0. #count will tell you how big #buffer is. Do not
 * even think about reading more elements than #count. If you are close to
 * memory wrap, #count may be very small, fully read the #buffer and next
 * call will give you more memory to read.
 *
 * #object_size defines size of a single object that is held on ring buffer.
 * You should read from a #buffer in increments of #object_size bytes.
 *
 * You can specify #flags as with #rb_recv()
 *
 * If multi-thread #rb is used, function will block like #rb_read until
 * data is available on #rb. After function finishes, you will be the owner
 * of #rb->rlock mutex, so no one will interfere with your reading.
 *
 * @note that #count does not describe #buffer size in bytes, but in "elements".
 *       #buffer size in bytes is #count * #object_size
 * @note remember to call #rb_read_commit() once you are done
 *
 * @param rb ring buffer object
 * @param buffer location from where data can be read
 * @param count max number of elements that can be read from #buffer
 * @param object_size size of single object #rb holds (in bytes)
 * @param flags operation flags
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
long rb_read_claim(struct rb *rb, void **buffer, size_t *count,
	size_t *object_size, enum rb_flags flags)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);
	VALID(EINVAL, buffer);
	VALID(EINVAL, count);
	VALID(EINVAL, object_size);

#if ENABLE_THREADS
	if (rb->flags & rb_multithread) {
		if (rb_trylock(rb, flags, &rb->rlock))
			return -1;
		if (rb_wait_for_data(rb, 1, 0, flags))
			return -1;
	}
#endif

	trace("read claimed");
	*count = rb_count_end(rb);
	*object_size = RB_IS_DYNAMIC(rb) ? 1 : rb->object_size;
	*buffer = rb->buffer + rb->tail * *object_size;

	return 0;
}

/** =========================================================================
 * Commits data read from #rb in #rb_read_claim. You just have to
 * specify number of elements actually read from #rb in claim call.
 *
 * In multi-thread environment, this will release read mutex and threads
 * blocked in write will be woken up. It's ok to pass 0 as #count.
 *
 * @param rb ring object to commit to
 * @param count number of elements read from ring buffer after claiming it
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
long rb_read_commit(struct rb *rb, size_t count)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

	rb_increase_tail(rb, count);

#if ENABLE_THREADS
	if (rb->flags & rb_multithread) {
		if (count)
			rb_sem_post(&rb->write_sem);
		unlock(rb->rlock);
	}
#endif

	trace("read committed");
	return 0;
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
		lock(rb->rlock);
#endif

	if (clear)
		memset(rb->buffer, 0x00, rb->count * rb->object_size);

	rb->head = 0;
	rb->tail = 0;

#if ENABLE_THREADS
	if (rb->flags & rb_multithread)
		unlock(rb->rlock);
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

	rb->force_exit = ECANCELED;

	/* signal all conditional variables, #force_exit is set, so all
	 * threads should just exit theirs rb_write/read functions. */
	rb_sem_post(&rb->read_sem);
	rb_sem_post(&rb->write_sem);

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

	sem_close(&rb->write_sem);
	sem_close(&rb->read_sem);
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
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);

#if ENABLE_THREADS
	if (rb->flags & rb_multithread)
		if (pthread_mutex_trylock(&rb->rlock))
			return_errno(-1, EAGAIN);
#endif

	if (count > (rbcount = rb_count(rb)))
		count = rbcount;

	rb_increase_tail(rb, count);

#if ENABLE_THREADS
	if (rb->flags & rb_multithread)
		unlock(rb->rlock);
#endif

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

/** =========================================================================
 * Set hard limit for ring buffer count, when #rb is #rb_growable
 *
 * @param rb ring buffer object
 * @param count hard limit for count when growing buffer
 *
 * @return 0 on success, otherwise -1 is returned
 * ========================================================================== */
int rb_set_hard_max_count(struct rb *rb, size_t count)
{
	VALID(EINVAL, rb);
	VALID(EINVAL, rb->buffer);
	VALID(EINVAL, rb_is_power_of_two(count));

	if (rb->flags & rb_dynamic)
		if (rb_is_uint_size(rb->object_size) == 0)
			return_errno(-1, EINVAL);

	rb->max_count = count;
	return 0;
}
