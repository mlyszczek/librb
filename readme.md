[kursg-meta]: # (order: 1)

About
=====

This is library that provides fast, easy to use ring buffer. It's interface is
very similar to read/write interface from POSIX. It also provides optional
thread awarness and thread safety for concurrent access. If used without threads
there are 0 (zero) syscalls, everything is done in user's process without kernel
interfering and steeling precious cpu cycles. Altough librb provides
some more functions, it can be operated using 4 basic functions

  * [rb_new](http://librb.kurwinet.pl/manuals/man3/rb_clear.3.html) -
    creates new ring buffer
  * [rb_read](http://librb.kurwinet.pl/manuals/man3/rb_read.3.html) -
    reads from the ring buffer
  * [rb_write](http://librb.kurwinet.pl/manuals/man3/rb_write.3.html) -
     writes to the ring buffer
  * [rb_destroy](http://librb.kurwinet.pl/manuals/man3/rb_destroy.3.html) -
    destroys ring buffer

Additional functions are:

  * [rb_recv](http://librb.kurwinet.pl/manuals/man3/rb_recv.3.html) -
    reads from the ring buffer but also accepts flags
  * [rb_send](http://librb.kurwinet.pl/manuals/man3/rb_send.3.html) -
    writes to the ring buffer but also accepts flags
  * [rb_clear](http://librb.kurwinet.pl/manuals/man3/rb_clear.3.html) -
    removes all elements from the buffer
  * [rb_count](http://librb.kurwinet.pl/manuals/man3/rb_count.3.html) -
    returns number of elements in the buffer
  * [rb_space](http://librb.kurwinet.pl/manuals/man3/rb_space.3.html) -
    returns number of free space for number of elements
  * [rb_stop](http://librb.kurwinet.pl/manuals/man3/rb_stop.3.html) -
    forces all threads to exit *rb_write* and *rb_read* functions

Dependencies
============

Library is C89 complaint and will work under any POSIX environment that
implements pthreads and libc. If target system doesn't have posix, no worries.
in such case the only requirenment is C89 compiler and libc (no threads then
though. To build without threads, add *--disable-threads* to configure script.

License
=======

Library is licensed under BSD 2-clause license. See LICENSE file for details

Compiling and installing
========================

Project uses standard automake so to build you need to

~~~
$ ./autogen.sh
$ ./configure
$ make
# make install
~~~

**./autogen.sh** must be called only when **./configure** is not available -
like when cloning from git. If you downloaded **tar.gz** tarbal, this can be
ommited.

for tests run:

~~~
$ make check
~~~

Functions description
=====================

For detailed functions description please check
[man pages](http://librb.kurwinet.pl/manuals/man3.html)

Contact
=======

Michał Łyszczek <michal.lyszczek@bofc.pl>

Thanks to
=========

Myself for developing very simple unit test framework
[mtest](http://mtest.kurwinet.pl) this code uses
