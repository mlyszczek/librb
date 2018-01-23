[kursg-meta]: # (order: 1)

About
=====

This is library that provides fast, easy to use ring buffer. It's interface is
very similar to read/write interface from POSIX. It also provides optional
thread awarness and thread safety for concurrent access. If used without threads
there are 0 (zero) syscalls, everything is done in user's process without kernel
interfering and steeling precious cpu cycles. Altough librb provides
some more functions, it can be operated using 4 basic functions

  * [rb_new](http://librb.kurwinet.pl/manuals/rb_clear.3.html) -
    creates new ring buffer
  * [rb_read](http://librb.kurwinet.pl/manuals/rb_read.3.html) -
    reads from the ring buffer
  * [rb_write](http://librb.kurwinet.pl/manuals/rb_write.3.html) -
     writes to the ring buffer
  * [rb_destroy](http://librb.kurwinet.pl/manuals/rb_destroy.3.html) -
    destroys ring buffer

Additional functions are:

  * [rb_recv](http://librb.kurwinet.pl/manuals/rb_recv.3.html) -
    reads from the ring buffer but also accepts flags
  * [rb_send](http://librb.kurwinet.pl/manuals/rb_send.3.html) -
    writes to the ring buffer but also accepts flags
  * [rb_clear](http://librb.kurwinet.pl/manuals/rb_clear.3.html) -
    removes all elements from the buffer
  * [rb_count](http://librb.kurwinet.pl/manuals/rb_count.3.html) -
    returns number of elements in the buffer
  * [rb_space](http://librb.kurwinet.pl/manuals/rb_space.3.html) -
    returns number of free space for number of elements
  * [rb_stop](http://librb.kurwinet.pl/manuals/rb_stop.3.html) -
    forces all threads to exit **rb_write** and **rb_read** functions

Dependencies
============

Library is C89 complaint and will work under any POSIX environment that
implements pthreads and libc. If target system doesn't have posix, no worries.
in such case the only requirenment is C89 compiler and libc (no threads then
though. To build without threads, add **--disable-threads** to configure script.

Test results
============

machine tests
-------------

* aarch64-builder-linux-gnu ![test-result-svg][a64lg]
* armv5te926-builder-linux-gnueabihf ![test-result-svg][armv5]
* armv6j1136-builder-linux-gnueabihf ![test-result-svg][armv6]
* armv7a15-builder-linux-gnueabihf ![test-result-svg][armv7a15]
* armv7a9-builder-linux-gnueabihf ![test-result-svg][armv7a9]
* i686-builder-freebsd ![test-result-svg][x32fb]
* i686-builder-linux-gnu ![test-result-svg][x32lg]
* i686-builder-linux-musl ![test-result-svg][x32lm]
* i686-builder-linux-uclibc ![test-result-svg][x32lu]
* i686-builder-netbsd ![test-result-svg][x32nb]
* i686-builder-openbsd ![test-result-svg][x32ob]
* mips-builder-linux-gnu ![test-result-svg][m32lg]
* x86_64-builder-linux-gnu ![test-result-svg][x64lg]
* x86_64-builder-linux-musl ![test-result-svg][x64lm]
* x86_64-builder-linux-uclibc ![test-result-svg][x64lu]
* x86_64-builder-solaris ![test-result-svg][x64ss]

sanitizers
----------

* -fsanitize=address ![test-result-svg][fsan]
* -fsanitize=leak ![test-result-svg][fsleak]
* -fsanitize=thread ![test-result-svg][fsthread]
* -fsanitize=undefined ![test-result-svg][fsun]

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

See also
========

* [mtest](http://mtest.kurwinet.pl) unit test framework **librb** uses
* [git repository](http://git.kurwinet.pl/librb) to browde code online
* [continous integration](http://ci.librb.kurwinet.pl) for project

[a64lg]: http://ci.librb.kurwinet.pl/badges/aarch64-builder-linux-gnu-tests.svg
[armv5]: http://ci.librb.kurwinet.pl/badges/armv5te926-builder-linux-gnueabihf-tests.svg
[armv6]: http://ci.librb.kurwinet.pl/badges/armv6j1136-builder-linux-gnueabihf-tests.svg
[armv7a15]: http://ci.librb.kurwinet.pl/badges/armv7a15-builder-linux-gnueabihf-tests.svg
[armv7a9]: http://ci.librb.kurwinet.pl/badges/armv7a9-builder-linux-gnueabihf-tests.svg
[x32fb]: http://ci.librb.kurwinet.pl/badges/i686-builder-freebsd-tests.svg
[x32lg]: http://ci.librb.kurwinet.pl/badges/i686-builder-linux-gnu-tests.svg
[x32lm]: http://ci.librb.kurwinet.pl/badges/i686-builder-linux-musl-tests.svg
[x32lu]: http://ci.librb.kurwinet.pl/badges/i686-builder-linux-uclibc-tests.svg
[x32nb]: http://ci.librb.kurwinet.pl/badges/i686-builder-netbsd-tests.svg
[x32ob]: http://ci.librb.kurwinet.pl/badges/i686-builder-openbsd-tests.svg
[m32lg]: http://ci.librb.kurwinet.pl/badges/mips-builder-linux-gnu-tests.svg
[x64lg]: http://ci.librb.kurwinet.pl/badges/x86_64-builder-linux-gnu-tests.svg
[x64lm]: http://ci.librb.kurwinet.pl/badges/x86_64-builder-linux-musl-tests.svg
[x64lu]: http://ci.librb.kurwinet.pl/badges/x86_64-builder-linux-uclibc-tests.svg
[x64ss]: http://ci.librb.kurwinet.pl/badges/x86_64-builder-solaris-tests.svg

[fsan]: http://ci.librb.kurwinet.pl/badges/fsanitize-address.svg
[fsleak]: http://ci.librb.kurwinet.pl/badges/fsanitize-leak.svg
[fsthread]: http://ci.librb.kurwinet.pl/badges/fsanitize-thread.svg
[fsun]: http://ci.librb.kurwinet.pl/badges/fsanitize-undefined.svg
