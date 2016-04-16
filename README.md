# WHAT
  ringbuf is a simple ring buffer implementation in C.

  It includes support for read(2) and write(2) operations on ring
  buffers, memcpy's into and out of ring buffers, setting the buffer
  contents to a constant value, and copies between ring buffers. It
  also supports searching for single characters, for use with
  line-oriented or character-delimited network protocols.

  It should be fairly straightforward to extend ringbuf to support
  other C library operations that operate on buffers, e.g., recv(2).

# WHY
  I implemented ringbuf because I needed a simple, dependency-free
  ring buffer type for use with network services written in C.

# INSTALLING
  ringbuf is not a library as such, so it doesn't need to be
  installed. Just copy the ringbuf.[ch] source files into your
  project. (Also see LICENSE below.)

  ringbuf has no dependencies beyond an ISO C90 standard library.

  Note that ringbuf.c contains several assert() statements. These are
  intended for use with the test harness (see below), and should
  probably be removed from production code by defining `RINGBUF_NO_ASSERT`, once you're confident that
  ringbuf works as intended.

  This distribution includes source for a test program executable
  (ringbuf-test.c), which runs extensive unit tests on the ringbuf
  implementation. On most platforms (other than Windows, which is not
  supported), you should be able to type 'make' to run the unit
  tests. Note that the Makefile uses the clang C compiler by default,
  but also has support for gcc -- just edit the Makefile so that it
  uses gcc instead of clang.

  The Makefile also includes targets for gcov coverage testing and
  valgrind memory testing, assuming you have those tools installed on
  your system.

# LICENSE
  ringbuf has no license; it is dedicated to the public domain. See
  the file COPYING, included in this distribution, for the specifics.

# CONTACT
  Drew Hess <dhess-src@bothan.net>

  http://drewhess.com/
