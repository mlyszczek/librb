.. include:: in/ref-list.in

========
Overview
========

NAME
----
**rb_overview** - quick overview of librb ring buffer library

SYNOPSIS
--------
**librb** is a ring buffer FIFO queue with POSIX-like API. **librb**
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

**librb** - library that provides fast, easy to use ring buffer implementation. 
Its interface is very similar to read/write functions from POSIX.
Basic usage can be done with only 4 functions:

.. list-table::
    :header-rows: 1

    * - function
      - description
    * - |rb_new|
      - create new ring buffer, allocate all needed buffers
    * - |rb_write|
      - write arbitrary number of elements into ring buffer
    * - |rb_read|
      - read arbitrary number of elements from ring buffer
    * - |rb_destroy|
      - destroy ring buffer once you are done with it

Flags for |rb_new| or |rb_init|:

.. list-table::
    :header-rows: 1

    * - function
      - description
    * - |rb_nonblock|
      - Create a non-blocking ring buffer (for multi-thread buffer only)
    * - |rb_multithread|
      - Create thread aware ring buffer
    * - |rb_dynamic|
      - Create dynamic buffer, where you can put object of any size on the
        buffer
    * - |rb_growable|
      - Automatically increase size of a buffer, if you use all buffer
    * - |rb_round_count|
      - Automatically round count passed during creation to next power of two
        value

Claim/commit API, useful when you want to pass internal ring buffer's buffer
directly to read(2)/write(2) functions, or work directly on buffer for any
reason.

.. list-table::
    :header-rows: 1

    * - function
      - description
    * - |rb_read_claim|
      - claim buffer for reading
    * - |rb_read_commit|
      - commit data to ring buffer and release buffer
    * - |rb_read_commit_claim|
      - commit data then immediately claim another buffer without unlocking
    * - |rb_write_claim|
      - claim buffer for writing
    * - |rb_write_commit|
      - commit data to ring buffer and release buffer
    * - |rb_write_commit_claim|
      - commit data then immediately claim another buffer without unlocking

Ring buffer control related functions:

.. list-table::
    :header-rows: 1

    * - function
      - description
    * - |rb_clear|
      - quickly drop all data from ring buffer
    * - |rb_discard|
      - quickly discard number of elements from buffer
    * - |rb_count|
      - check how many elements are currently in buffer
    * - |rb_space|
      - check how much space left is there on buffer
    * - |rb_stop|
      - for multi-thread, wake all blocked threads and tell them to finish
        operation
    * - |rb_peek_size|
      - check size of next frame that buffer will return, only when buffer is
        dynamic
    * - |rb_set_hard_max_count|
      - set how much ring buffer can grow when buffer is growable

Additional functions are provided if classics are not enough and more
fine-grained control is needed:

.. list-table::
    :header-rows: 1

    * - function
      - description
    * - |rb_init|
      - initialize stack allocated ring buffer, must bring your own buffer
    * - |rb_cleanup|
      - cleanup ring buffer
    * - |rb_send|
      - same as |rb_write| but accepts flags for altering behavior for one call
    * - |rb_recv|
      - same as |rb_read| but accepts flags for altering behavior for one call
    * - |rb_recv_claim|
      - claim buffer for reading
    * - |rb_recv_commit|
      - commit data to ring buffer and release buffer
    * - |rb_recv_commit_claim|
      - commit data then immediately claim another buffer without unlocking
    * - |rb_send_claim|
      - claim buffer for writing
    * - |rb_send_commit|
      - commit data to ring buffer and release buffer
    * - |rb_send_commit_claim|
      - commit data then immediately claim another buffer without unlocking


DESCRIPTION
-----------
**librb** works with "elements" or "objects" not bytes. During ring buffer
initialization you specify how big a single object is. That object may be
an integer, but it also can be a struct. If you really want to work on bytes
object size may just as well be equal to 1. After **librb** is initialized,
you can only write or read objects with that specific size. It's best to just
pick one type and just stick to it for the whole life of the buffer.

You can enable multi-thread mode by passing **rb_multithread** flag during
buffer creation. In this mode, read and write functions will block caller
if there is no data or space available on buffer. **librb** utilizes double
locking, one for write and read, so you can read and write simultaneously from
a single ring buffer. If you don't want to have your thread blocked during
operation, you can either create buffer with **rb_nonblock** flag (which will
result in all calls to be not blocking), or pass **rb_dontwait** flag to either
|rb_send| or |rb_recv| function, for single, non-blocking operation.

EXAMPLE
-------
.. literalinclude:: ../examples/simple-read-write.c
   :language: c
