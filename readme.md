About
=====

This is library that provides fast, easy to use ring buffer. It's interface is
very similar to read/write interface from POSIX. Altough librb provides some
more functions, it can be operated using 4 basic functions

  * rb_new - creates new ring buffer
  * rb_read - reads from the ring buffer
  * rb_write - writes to the ring buffer
  * rb_destroy - destroys ring buffer

Additional functions are:

  * rb_recv - reads from the ring buffer but also accepts flags
  * rb_send - writes to the ring buffer but also accepts flags
  * rb_clear - removes all elements from the buffer
  * rb_count - returns number of elements in the buffer
  * rb_space - returns number of free space for number of elements

librb is thread safe and thread aware. If there are no resources available
while reading or writting, caller thread gets locked and doesn't use any
resources until data is available. Ring buffer can also be configured to work
in non-blocking mode, so calls from read and write will return immediately
when there are not enough resources. There is only one malloc and free in new
and destory functions. Thread awarness can be disabled via O_NONTHREAD flags
passed to rb_new. In such case all calls to rb function will be non blocking.
Library can also be compiled without thread support at all, then it can be
used even in very limited platforms.

As this library is focused on speed, user can create buffer with only power of
two count (n^2). Thanks to this solution there is much less conditional jumps.
Altough user is limited to number of elements that can be stored in buffer,
single element can be any size. Thanks to that, ring buffer can be used with raw
data (like from 8bit adc when object size is 1 byte) or bigger raw data (like
from 12bit adc when object size is 2 bytes) or even more sophisticated types
like structures with any number of paraneters when object size is set to
sizeof(struct).

Dependencies
============

Library is C89 complaint and will work under any POSIX environment that
implements pthreads and libc. If LIBRB_PTHREAD is disabled, then the only
requirenment is C89 compiler and libc.

License
=======

Library is licensed under BSD 2-clause license. See LICENSE file for details

Compiling and installing
========================

Standard **make** and **make install** should be enough. If you want to
install to different directory, just use **DESTDIR=/custom/path make
install**.

To compile with pthread support, just add THREAD=1 to make argument like
**make THREAD=1**

To compile for different architecture 3 env variables should be set

  * CC - cross compiler
  * INC - staging include directory
  * LINC - staging library directory

Example make might look like this

~~~.sh
$ CC=arm-none-eabi \
  INC=-I/usr/arm-none-eabi/usr/include \
  LINC=-L/usr/arm-none-eabi/usr/lib \
  make

$ DESTDIR=/usr/arm-none-eabi/usr make install
~~~

For tests simply run **make test** or **make test THREAD=1** to test with
threads.

Functions description
=====================

rb_new
------

~~~.c
int rb_new(struct rb *rb, size_t count, size_t object_size, int flags);
~~~

This function is used to create and initialize new ring buffer. Information
about created ring buffer is stored into memory pointed by rb. User is
responsible to create this structure in his application. This object will be
later used to identify ring buffer.

count and object_size describes buffer sizes. Library will allocate on callers
heap (count * object_size) bytes. Count must be a power of two number, as
library tries to avoid unecessary conditional jump to improve performance.
Object size determines how big is a single element. This value is constant for
lifetime of a ring buffer. Object size might be for example
**sizeof(uint8_t)** or **sizeof(uint32_t)** or even **sizeof(struct my_msg)**.
In short, object size should be a size of an object, ring buffer will hold. You
should not write nor read objects of different size than created one, as this
will lead to undefined behaviour or even crashes.

Function can accept following flags
- O_NONBLOCK - if set, all calls to library functions will return immediately
  even if buffer is full or empty. Proper functions will then return with
  errno set to EAGAIN and number of elements actually commited.
- O_NONTHREAD - this can be set only when library has been compiled with
  LIBRB_PTHREAD otherwise, this flag is ignored. If set, rb functions will not
  be using pthread. This should be used only when you are sure that rb object
  will be accessed at most by 1 thread at a time. If more than 1 thread will
  try access rb object when this flag is set, such call will result in
  undefined behaviour. When O_NONTHREAD is set, O_NONBLOCK should also be set
  so all function calls are non-blocking. If LIBRB_PTHREAD is not set, funcions
  behave as (O_NONTHREAD | O_NONBLOCK) flag was set.

Return values:

Upon success 0 is returned. When error occured all previously allocated
resources are freed, -1 is returned and errno is set.

EINVAL *rb* is NULL
EINVAL count is not a power of two number (like 4, 16, 64 etc.)
EINVAL O_NONTHREAD was set but O_NONBLOCK was not
ENOMEM could not allocate memory for given count and object_size

function can also return errors from pthread_mutex_init or pthread_cond_init

rb_destroy
----------

~~~.c
void rb_destroy(struct rb *rb);
~~~

Function frees resources allocated by **rb_new**. If threading is enabled also
forces **rb_write** and **rb_read** return even in blocking mode, so users
thread using them can be joined. After calling this, *rb* that was freed,
cannot be used anymore without calling *rb_new* first. It's ok to pass same
object more than once to this function - but don't try to pass uninitialized
*rb* object (like created on stack) or this will probably trigger segmentation
fault.

Return values:

0 on success, -1 on error.

EINVAL *rb* object is NULL

read and write operations on ring buffer
----------------------------------------

~~~.c
long rb_write(struct rb *rb, const void *buffer, size_t count);
long rb_send(struct rb *rb, const void *buffer, size_t count, int flags);
long rb_read(struct rb *rb, void *buffer, size_t count);
long rb_recv(struct rb *rb, void *buffer, size_t count, int flags);
~~~

For write and send: function copy at most *count* _elements_ to ring buffer
*rb* starting from memory pointed by *buffer*. rb_send also accepts flags.

For read and recv: function copy at most *count* _elements_ from ring buffer
*rb* to memory pointer by *buffer*. rb_recv also accepts flags.

Function will commit (count * rb->object_size) bytes. Functions can and will
try to commit as many bytes as possible in a single burst, so it is better to
call these function once with big *count* number than call it in a loop with
count == 1.

By defaulti (if buffer wasn't created with O_NONBLOCK flag), every read and
write will block until it commits every element passed to the function. When
read or write function blocks, caller thread is put to sleep, so it doesn't
use any processor time, until it is waked by opossite write or read call.
Ie. when ring buffer is full, and user calls **rb_write**, thread will sleep
until another thread calls **rb_read** and makes space for writing operation.

Flags:

- MSG_DONTWAIT - when resources are not available, read or write will return
  immediately with number of elements commited and EAGAIN errno. Flag only
  affects a single call

*rb_recv* also supports

- MSG_PEEK - reads from buffer as normal, but doesn't remove data from buffer.
  So consecutive calls to this function will return same data (provided nobody
  calld rb_recv without this flag). Flag works only when threading is disabled

Return values:

On success function will return number of bytes commited (read from or wrote
to buffer). Returned value can be less than *count*, then it means buffer is
either full or empty and not all elements could be placed in buffer. This can
occur only when O_NONBLOCK or MSG_DONTWAIT is set. If buffer operates in
blocking mode, it should always return *count* (if error didn't occur). When
error occurs -1 is returned and errno is set accordingly:

EINVAL  any of the passed pointers is NULL
EINVAL  *rb* was not initialized or was freed earlier
EAGAIN  ring buffer operates in non blocking mode and resources are temporary
unavailable

rb_clear
--------

Simply clears *rb* from all data. Note, if security is concern, that data is
not actually cleared from memory (it is not zeroed) but only internal pointers
are resetted.

rb_count and rb_space
---------------------

Returns respectively, number of elements in buffer and number of elements that
can be yet put on buffer without prior read.

Contact
=======

Michał Łyszczek <michal.lyszczek@bofc.pl>

Thanks to
=========

Code beautified using GreatCode (https://sourceforge.net/projects/gcgreatcode)
