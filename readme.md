About
=====

This is library that provides fast, easy to use ring buffer. It's interface is
very similar to read/write interface from POSIX. Library has only 4 functions:

  * rb_new - creates new ring buffer
  * rb_read - reads from the ring buffer
  * rb_write - writes to the ring buffer
  * rb_destroy - destroys ring buffer

Library is thread safe and thread aware. If there are not resources available
while reading or writting, caller thread gets locked and doesn't use any
resources until data is available. Ring buffer can also be configured to work
in non-blocking mode, so calls from read and write will return immediately
when there are not enough resources. There is only one malloc and free in new
and destory functions.

As this library is focused on speed, user can create buffer with only power of
two count (n^2). Thanks to this solution there is much less conditional jumps.
Altough user is limited to number of elements, single element can be any size.
Thanks to that, ring buffer can also be easily used with structs as an item.

Library is C89 complaint and will work under any POSIX environment that
implements pthreads and libc.

License
=======

Library is licensed under BSD 3-clause license. See LICENSE file for details

Compiling and installing
========================

Standard **make** and **make install** should be enough. If you want to
install to different directory, just use **INSTALL_DIR=/custom/path make
install**.

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
$ INSTALL_DIR=/usr/arm-none-eabi/usr make install
~~~

Function descriptions
=====================

rb_new
------

~~~.c
int rb_new(struct rb *rb, size_t count, size_t object_size, int non_blocking);
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
In short, object size should be a size of an object ring buffer will hold. You
should not write nor read objects of different size than created one, as this
will lead to undefinied behaviour or even crashes.

non_blocking defines whether **rb_read** and **rb_write** function should
block or not when there is not enough resources available.

rb_write and rb_read
--------------------

~~~.c
ssize_t rb_write(struct rb *rb, const void *buffer, size_t count);
ssize_t rb_read(struct rb *rb, void *buffer, size_t count);
~~~

Functions used read and write elements to the ring buffer. Function will
commit (count * rb->object_size) bytes. Functions can and will try to commit
as many bytes as possible in a single burst, so it is better to call these
function once with big count number than call it in a loop with count == 1.

When non_blocking flag is not set, every read and write will block until it
commits every element passed to the function. When read or write function
blocks, caller thread is put to sleep to it doesn't use any processor time,
until it is waked by opossite write or read call. Ie. when ring buffer is
full, and user calls **rb_write**, thread will sleep until another thread
calls **rb_read** and makes space for writing operation. When non_blocking
flag is set, and resources are not available, read or write will return
immediately with number of elements commited and EAGAIN errno. Just like with
sockets.

rb_destroy
----------

~~~.c
void rb_destroy(struct rb *rb);
~~~

Function frees resources allocated by **rb_new**. Also it forces **rb_write**
and **rb_read** return even in blocking mode, so users thread using them can
be joined.

Contact
=======

Michał Łyszczek <michal.lyszczek@bofc.pl>
