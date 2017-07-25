About
=====

This is library that provides fast, easy to use ring buffer. It's interface is
very similar to read/write interface from POSIX. It also provices optional
thread awarness and thread safety for concurrent access. If used without threads
there are 0 (zero) syscalls, everything is done in user's process without kernel
interfering and steeling precious cpu cycles. Altough librb provides
some more functions, it can be operated using 4 basic functions

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

Dependencies
============

Library is C89 complaint and will work under any POSIX environment that
implements pthreads and libc. If target system doesn't have posix, no worries.
in such case the only requirenment is C89 compiler and libc (no threads then
though). This is automatically detected.

License
=======

Library is licensed under BSD 2-clause license. See LICENSE file for details

Compiling and installing
========================

Project uses standard automake so to build you need to

autoreconf -i
./configure
make
make install

for tests run:

make check

Functions description
=====================

For function description please check man pages.

Contact
=======

Michał Łyszczek <michal.lyszczek@bofc.pl>

Thanks to
=========

Myself for developing very simple unit test framework
(https://github.com/mlyszczek/mtest) this code uses
