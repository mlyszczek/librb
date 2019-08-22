[kursg-meta]: # (order: 1)

About
=====

This is library that provides fast, easy to use ring buffer. It's interface is
very similar to read/write interface from POSIX. It also provides optional
thread awarness and thread safety for concurrent access. If used without threads
there are 0 (zero) syscalls, everything is done in user's process without kernel
interfering and steeling precious cpu cycles. Altough librb provides
some more functions, it can be operated using 4 basic functions

  * [rb_new](https://librb.bofc.pl/manuals/rb_clear.3.html) -
    creates new ring buffer
  * [rb_read](https://librb.bofc.pl/manuals/rb_read.3.html) -
    reads from the ring buffer
  * [rb_write](https://librb.bofc.pl/manuals/rb_write.3.html) -
     writes to the ring buffer
  * [rb_destroy](https://librb.bofc.pl/manuals/rb_destroy.3.html) -
    destroys ring buffer

Additional functions are:

  * [rb_init](https://librb.bofc.pl/manuals/rb_init.3.html) -
    initializes new ring buffer but does not use dynamic allocation
  * [rb_cleanup](https://librb.bofc.pl/manuals/rb_cleanup.3.html) -
    cleans up whatever has been initialized with rb_init
  * [rb_recv](https://librb.bofc.pl/manuals/rb_recv.3.html) -
    reads from the ring buffer but also accepts flags
  * [rb_send](https://librb.bofc.pl/manuals/rb_send.3.html) -
    writes to the ring buffer but also accepts flags
  * [rb_clear](https://librb.bofc.pl/manuals/rb_clear.3.html) -
    removes all elements from the buffer
  * [rb_count](https://librb.bofc.pl/manuals/rb_count.3.html) -
    returns number of elements in the buffer
  * [rb_space](https://librb.bofc.pl/manuals/rb_space.3.html) -
    returns number of free space for number of elements
  * [rb_stop](https://librb.bofc.pl/manuals/rb_stop.3.html) -
    forces all threads to exit **rb_write** and **rb_read** functions
  * [rb_discard](https://librb.bofc.pl/manuals/rb_discard.3.html) -
    allows to quickly discard part of buffers data
  * [rb_header_size](https://librb.bofc.pl/manuals/rb_header_size.3.html) -
    get size of internal struct with buffer information
  * [rb_version](https://librb.bofc.pl/manuals/rb_version.3.html) -
    get version of library that is used

Extra functions enabled on POSIX compliant systems. These functions are provided
for convenience, but can also be used to limit copying operations from socket
to **rb**:

  * [rb_posix_read](https://librb.bofc.pl/manuals/rb_posix_read.3.html) -
    reads from the ring buffer directly to provided socket
  * [rb_posix_write](https://librb.bofc.pl/manuals/rb_posix_write.3.html) -
    writes to the ring buffer directly from socket
  * [rb_posix_recv](https://librb.bofc.pl/manuals/rb_posix_recv.3.html) -
    reads from the ring buffer directly to provided socket but also accepts
    flags
  * [rb_posix_send](https://librb.bofc.pl/manuals/rb_posix_send.3.html) -
    writes to the ring buffer directly from socket but also accepts flags

Downloads
=========

Released binary packages and source tarballs can be downloaded from
[https://librb.bofc.pl/downloads.html ](https://librb.bofc.pl/downloads.html)

Dependencies
============

Library is **C89** complaint and will work under any **POSIX** environment that
implements pthreads and libc. If target system doesn't have **POSIX**, no
worries.  in such case the only requirenment is **C89** compiler and libc (no
threads then though. To build without threads, add **--disable-threads** to
configure script.

Test results
============

Note: tests are done on **master** branch, on release tags, all tests *always*
pass.

operating system tests
----------------------

* parisc-polarhome-hpux-11.11 ![test-result-svg][prhpux]
* power4-polarhome-aix-7.1 ![test-result-svg][p4aix]
* i686-bofc-freebsd-11.1 ![test-result-svg][x32fb]
* i686-bofc-netbsd-8.0 ![test-result-svg][x32nb]
* i686-bofc-openbsd-6.2 ![test-result-svg][x32ob]
* x86_64-bofc-solaris-11.3 ![test-result-svg][x64ss]
* i686-bofc-linux-gnu-4.9 ![test-result-svg][x32lg]
* i686-bofc-linux-musl-4.9 ![test-result-svg][x32lm]
* i686-bofc-linux-uclibc-4.9 ![test-result-svg][x32lu]
* x86_64-bofc-linux-gnu-4.9 ![test-result-svg][x64lg]
* x86_64-bofc-linux-musl-4.9 ![test-result-svg][x64lm]
* x86_64-bofc-linux-uclibc-4.9 ![test-result-svg][x64lu]
* x86_64-bofc-debian-9 ![test-result-svg][x64debian9]
* x86_64-bofc-centos-7 ![test-result-svg][x64centos7]
* x86_64-bofc-fedora-28 ![test-result-svg][x64fedora28]
* x86_64-bofc-opensuse-15 ![test-result-svg][x64suse15]
* x86_64-bofc-rhel-7 ![test-result-svg][x64rhel7]
* x86_64-bofc-slackware-14.2 ![test-result-svg][x64slackware142]
* x86_64-bofc-ubuntu-18.04 ![test-result-svg][x64ubuntu1804]

machine tests
-------------

* aarch64-bofc-linux-gnu ![test-result-svg][a64lg]
* armv5te926-bofc-linux-gnueabihf ![test-result-svg][armv5]
* armv6j1136-bofc-linux-gnueabihf ![test-result-svg][armv6]
* armv7a15-bofc-linux-gnueabihf ![test-result-svg][armv7a15]
* armv7a9-bofc-linux-gnueabihf ![test-result-svg][armv7a9]
* mips-bofc-linux-gnu ![test-result-svg][m32lg]

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

to build examples, simply run, compiled examples will be shows in ./examples
directory

~~~
$ make examples
~~~

Examples
========

Example codes can be found here:
[https://git.bofc.pl/librb/tree/examples ][examples]

Build time options
==================

Some fatures can be disabled to save some code size or when particular feature
is not available on target system. All options are passed to configure script
in common way **./configure --enable-_feature_**. Run **./configure --help** to
see help on that matter. For every **--enable** option it is also valid to use
**--disable-_feature_**.  Enabling option here does not mean it will be hard
enabled in runtime, this will just give you an option to enable these settings
later in runtime.

--enable-threads (default: enable)
----------------------------------

When set, you will be able to create **rb** object with **O_MULTITHREAD** flag,
which will enable multi-thread safety for that **rb** object. This needs your
system to support **pthreads**.

--enable-posix-calls (default: enable)
--------------------------------------

When enabled, you will be able to read/write data directly from/into **posix**
file descriptor (which may be a file on disk, serial device or network socket).

--enable-trace (default: disable)
---------------------------------

When enabled, library will print tons of logs, use this only when debugging
this library. When disabled, **rb** will be totally silent.

Functions description
=====================

For detailed functions description please check
[man pages](https://librb.bofc.pl/manuals/man3.html)

Contact
=======

Michał Łyszczek <michal.lyszczek@bofc.pl>

See also
========

* [mtest](https://mtest.bofc.pl) unit test framework **librb** uses
* [git repository](https://git.bofc.pl/librb) to browse code online
* [continous integration](https://ci.librb.bofc.pl) for project
* [polarhome](https://www.polarhome.com) nearly free shell accounts for virtually
  any unix there is.

[a64lg]: https://ci.librb.bofc.pl/badges/aarch64-builder-linux-gnu-tests.svg
[armv5]: https://ci.librb.bofc.pl/badges/armv5te926-builder-linux-gnueabihf-tests.svg
[armv6]: https://ci.librb.bofc.pl/badges/armv6j1136-builder-linux-gnueabihf-tests.svg
[armv7a15]: https://ci.librb.bofc.pl/badges/armv7a15-builder-linux-gnueabihf-tests.svg
[armv7a9]: https://ci.librb.bofc.pl/badges/armv7a9-builder-linux-gnueabihf-tests.svg
[x32fb]: https://ci.librb.bofc.pl/badges/i686-builder-freebsd-tests.svg
[x32lg]: https://ci.librb.bofc.pl/badges/i686-builder-linux-gnu-tests.svg
[x32lm]: https://ci.librb.bofc.pl/badges/i686-builder-linux-musl-tests.svg
[x32lu]: https://ci.librb.bofc.pl/badges/i686-builder-linux-uclibc-tests.svg
[x32nb]: https://ci.librb.bofc.pl/badges/i686-builder-netbsd-tests.svg
[x32ob]: https://ci.librb.bofc.pl/badges/i686-builder-openbsd-tests.svg
[m32lg]: https://ci.librb.bofc.pl/badges/mips-builder-linux-gnu-tests.svg
[x64lg]: https://ci.librb.bofc.pl/badges/x86_64-builder-linux-gnu-tests.svg
[x64lm]: https://ci.librb.bofc.pl/badges/x86_64-builder-linux-musl-tests.svg
[x64lu]: https://ci.librb.bofc.pl/badges/x86_64-builder-linux-uclibc-tests.svg
[x64ss]: https://ci.librb.bofc.pl/badges/x86_64-builder-solaris-tests.svg
[prhpux]: https://ci.librb.bofc.pl/badges/parisc-polarhome-hpux-tests.svg
[p4aix]: https://ci.librb.bofc.pl/badges/power4-polarhome-aix-tests.svg
[x64debian9]: https://ci.librb.bofc.pl/badges/x86_64-debian-9-tests.svg
[x64centos7]: https://ci.librb.bofc.pl/badges/x86_64-centos-7-tests.svg
[x64fedora28]: https://ci.librb.bofc.pl/badges/x86_64-fedora-28-tests.svg
[x64suse15]: https://ci.librb.bofc.pl/badges/x86_64-opensuse-15-tests.svg
[x64rhel7]: https://ci.librb.bofc.pl/badges/x86_64-rhel-7-tests.svg
[x64slackware142]: https://ci.librb.bofc.pl/badges/x86_64-slackware-142-tests.svg
[x64ubuntu1804]: https://ci.librb.bofc.pl/badges/x86_64-ubuntu-1804-tests.svg

[fsan]: https://ci.librb.bofc.pl/badges/fsanitize-address.svg
[fsleak]: https://ci.librb.bofc.pl/badges/fsanitize-leak.svg
[fsthread]: https://ci.librb.bofc.pl/badges/fsanitize-thread.svg
[fsun]: https://ci.librb.bofc.pl/badges/fsanitize-undefined.svg

[examples]: https://git.bofc.pl/librb/tree/examples
