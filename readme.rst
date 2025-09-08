=====
About
=====

Quick Intro
-----------

**librb** is a ring buffer (or circular buffer) based FIFO queue with API
very similar to POSIX read/write. In the simplest form it looks like this:

.. code-block:: c

   #include <rb.h>

   int main(void) {
       struct rb *rb;
       int wrbuf[4] = { 0, 1, 2, 3 };
       int rdbuf[4] = { 0 };

       /* create new ring buffer that can hold maximum 7 integers */
       rb = rb_new(8, sizeof(int), 0);

       /* write then read back data from buffer */
       rb_write(rb, wrbuf, rb_array_size(wrbuf));
       rb_read(rb, rdbuf, rb_array_size(rdbuf));

       /* destroy ring buffer */
       rb_destroy(rb);
       return 0;
   }

About
-----
Again, **librb** is a ring buffer FIFO queue with POSIX-like API. **librb**
is very versatile library with multiple features to make your life simpler.

* classic ring buffer, you don't have to put elements one by one on a queue,
  can put multiple elements on queue with 1 call
* full multi-thread awareness, with thread blocking, and read/write working
  simultaneously, non-blocking operations are supported
* automatically grow buffer when reaching max buffer, with configurable hard
  limit on max growable size
* malloc() less mode for embedded
* claim/commit API allows you to get ring buffers internal buffer to fill it
  in custom way, or even passing it to POSIX read/write function
* dynamic mode, that allows you to put elements with arbitrary sizes
* everything is written in one header and c-source file, easy to copy and use
  in your project

Which features are available are defined by flags you use. Some features can
be disable during compile time to save on code size.

Dependencies
------------
Library is written using **C99** standard. If you enable thread awareness you
will have to have **C11** compatible compiler, since it uses *std atomics* to
make multi-thread implementations simpler and faster. If you want multi-thread
support, your platform must have **pthread** API for threads. There are no
other dependencies.

Robustness
----------
Whole library with all features enables is less than 500 lines of code or 300
with multi-thread support disabled - without any hacky syntax to hide LOC ;)
Code is heavily tested with plethora of different buffer sizes and amount of
consumers and producers::

    # total tests.......:   68874
    # passed tests......:   68874
    # failed tests......:       0
    # total checks......: 3163321
    # passed checks.....: 3163321
    # failed checks.....:       0

Code has been also tested with thread and address sanitizers plus valgrind
for good measure.

Documentation
-------------
All manual pages are available at project's homepage at https://librb.bofc.pl .
It's best to start with an overview page that references to all functions
and describes how library works. Overview is available at address
https://librb.bofc.pl/rb_overview.7.html

When you install **librb** all manual pages are installed with it. You can
start by reading **rb_overview(7)**.

Examples
--------
Examples are in **examples** directory. They show how library can be used in
common situations. They can be viewed online at my git server
https://git.bofc.pl/librb/tree/examples or at github at
https://github.com/mlyszczek/librb/tree/master/examples.

Current examples are

simple-read-write.c
    Shows the most basic use of ring buffer in single thread environment.
    That is buffer creation, writing, reading and cleaning up after job is done.

prod-cons-threads.c
    Classic producer consumer problem. Thanks to internal thread awareness
    solving this problem is trivial. Example shows how to use ring buffer
    in with multiple threads. It will also present how growable buffer can
    be created with hard limit, so it doesn't grow too large.

posix-network-parser.c
    Example creates simple TCP server. Data is read to ring buffer, and then
    it's being processed in another thread. This again shows usage of buffer
    in multi-thread environment, but also shows how you can pass internal
    ring buffer's buffer directly to POSIX **read(2)**, to put data on buffer
    without performing intermediate read. It shows to to use claim/commit API
    of the library. With that API you can work directly on buffer's memory to
    perform custom work.

event-loop.c
    Simple event loop. There are 3 separate threads that put events on a queue
    (keyboard, network and posix signal events). Those events are then later
    processed in main thread. Nothing really new here, but I needed event loop
    in my own project and that's the reason I created this library.

Compiling and installing
------------------------

Compiling library
^^^^^^^^^^^^^^^^^
On POSIX systems it's just as easy as doing::

    $ ./autogen.sh
    $ ./configure
    $ make
    # make install

This will also install manual pages for quick reference.

Running tests
^^^^^^^^^^^^^
To run tests simply run::

    $ make check

Compiling and running examples
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
To compile examples run::

    $ make examples

Example binaries will generate in ``./examples`` directory.

Integrating library with own software
-------------------------------------
Library has been written as single file for it to be easy to integrate with
any project.

#. Copy **rb.c** and **rb.h.in** to your project.
#. Rename **rb.h.in** to **rb.h**
#. In newly renamed **rb.h** replace all `@.*@` strings with either 1 or 0
   depending on which features you want to compile in or not.
#. Add **rb.c** to your build system and you can start using **librb**

Contact
-------
Current author and maintainer: Michał Łyszczek <michal.lyszczek@bofc.pl>.
You can report bugs directly to my email or via
`github issues page <https://github.com/mlyszczek/librb/issues>`_.

License
-------
Library is licensed under BSD 2-clause license.

See also
---------
* `mtest <https://mtest.bofc.pl>`_ unit test framework **librb** uses
* `git repository <http://git.bofc.pl/librb>`_ to browse code online
* `github <https://github.com/mlyszczek/librb>`_ if you prefer to cgit
* `polarhome <http://www.polarhome.com>`_ now dead, but it let me have fun
  testing **librb** on exotic Unices like AIX and HP-UX. You will be missed.
