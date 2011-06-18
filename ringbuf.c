/*
 * ringbuf.c - C ring buffer (FIFO) implementation.
 *
 * Written in 2011 by Drew Hess <dhess-src@bothan.net>.
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to
 * the public domain worldwide. This software is distributed without
 * any warranty.
 *
 * For the full statement of the dedication, see the Creative Commons
 * CC0 Public Domain Dedication at
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

/*
 * This file includes a main() function so that the file can be
 * compiled into an executable, in order to run some simple test cases
 * on the included ring buffer functions. If you want to use this ring
 * buffer implementation in your own project, make sure you cut or
 * comment out the code where indicated below.
 */

/*
 * A naive byte-addressable ring buffer FIFO implementation.
 *
 * The ring buffer's head pointer points to the starting location
 * where data should be written when copying data *into* the buffer
 * (e.g., with ringbuf_read). The ring buffer's tail pointer points to
 * the starting location where data should be read when copying data
 * *from* the buffer (e.g., with ringbuf_write).
 *
 * The ring buffer is full when the tail pointer is head + 1 (modulo
 * the buffer size).
 *
 * The code is written somewhat pedantically and contains many
 * assert()s to ensure that it's readable and correct. Feel free to
 * optimize the code for use in your own projects once you're
 * comfortable that it functions as intended.
 */

#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/param.h>
#include <assert.h>

/*
 * The size of the internal buffer, in bytes. One byte wil always be
 * unused, to distinguish the buffer-full state from the buffer-empty
 * state.
 */
#define MAX_BUF 4096

typedef struct ringbuf_t
{
    char buf[MAX_BUF];
    void *head, *tail;
} ringbuf_t;

/*
 * Initialize/reset a ring buffer.
 */
void
ringbuf_init(ringbuf_t *rb)
{
    rb->head = rb->tail = rb->buf;
}

/*
 * The usable capacity of the ring buffer, in bytes.
 */
size_t
ringbuf_capacity(const ringbuf_t *rb)
{
    return sizeof(rb->buf) - 1;
}

/*
 * Return a pointer to one-past-the-end of the ring buffer's
 * contiguous buffer. You shouldn't normally need to use this function
 * unless you're writing a new ringbuf_* function.
 */
static const void *
ringbuf_end(const ringbuf_t *rb)
{
    return rb->buf + sizeof(rb->buf);
}

/*
 * The number of free/available bytes in the ring buffer.
 */
size_t
ringbuf_bytes_free(const ringbuf_t *rb)
{
    if (rb->head >= rb->tail)
        return ringbuf_capacity(rb) - (rb->head - rb->tail);
    else
        return rb->tail - rb->head - 1;
}

/*
 * The number of bytes currently being used in the ring buffer.
 */
size_t
ringbuf_bytes_used(const ringbuf_t *rb)
{
    return ringbuf_capacity(rb) - ringbuf_bytes_free(rb);
}

int
ringbuf_is_full(const ringbuf_t *rb)
{
    return ringbuf_bytes_free(rb) == 0;
}

int
ringbuf_is_empty(const ringbuf_t *rb)
{
    return ringbuf_bytes_free(rb) == ringbuf_capacity(rb);
}

const void *
ringbuf_tail(const ringbuf_t *rb)
{
    return rb->tail;
}

const void *
ringbuf_head(const ringbuf_t *rb)
{
    return rb->head;
}

/*
 * This function is used by other ringbuf_* functions to fix up (make
 * consistent) the ring buffer's tail pointer after it has overflowed,
 * due to copying into the buffer more bytes than were free prior to
 * the copy.
 *
 * Set the overflowed ring buffer's tail pointer to the value returned
 * by this function.
 */
static void *
ringbuf_smash_tail(ringbuf_t *rb)
{
    const void *bufend = ringbuf_end(rb);
    if (rb->head + 1 == bufend)
        return rb->buf;
    else
        return rb->head + 1;
}

/*
 * Copy n bytes from a contiguous memory area src into the ring buffer
 * dst. Returns the ring buffer's new head pointer.
 *
 * It is possible to copy more data from src than is available in the
 * buffer; i.e., it's possible to overflow the ring buffer using this
 * function. When an overflow occurs, the state of the ring buffer is
 * guaranteed to be consistent, including the head and tail pointers;
 * old data will simply be overwritten in FIFO fashion, as
 * needed. However, note that, if calling the function results in an
 * overflow, the value of the ring buffer's tail pointer may be
 * different than it was before the function was called.
 */
void *
ringbuf_memcpy_into(ringbuf_t *dst, const void *src, size_t count)
{
    const void *bufend = ringbuf_end(dst);
    int overflow = count > ringbuf_bytes_free(dst);
    size_t nread = 0;

    while (nread != count) {
        /* don't copy beyond the end of the buffer */
        assert(bufend > dst->head);
        size_t n = MIN(bufend - dst->head, count - nread);
        memcpy(dst->head, src + nread, n);
        dst->head += n;
        nread += n;

        /* wrap? */
        if (dst->head == bufend)
            dst->head = dst->buf;
    }

    if (overflow) {
        dst->tail = ringbuf_smash_tail(dst);
        assert(ringbuf_is_full(dst));
    }

    return dst->head;
}

/*
 * This convenience function calls read(2) on the file descriptor fd,
 * using the ring buffer rb as the destination buffer for the read,
 * and returns the value returned by read(2). It will only call
 * read(2) once, and may return a short count, either because read(2)
 * returned a short count, or because the ring buffer must wrap around
 * in order to read more data from the file descriptor; handling the
 * latter condition is the primary reason this function exists.
 *
 * It is possible to read more data from the file descriptor than is
 * available in the buffer; i.e., it's possible to overflow the ring
 * buffer using this function. When an overflow occurs, the state of
 * the ring buffer is guaranteed to be consistent, including the head
 * and tail pointers: old data will simply be overwritten in FIFO
 * fashion, as needed. However, note that, if calling the function
 * results in an overflow, the value of the ring buffer's tail pointer
 * may be different than it was before the function was called.
 */
ssize_t
ringbuf_read(int fd, ringbuf_t *rb, size_t count)
{
    const void *bufend = ringbuf_end(rb);
    size_t nfree = ringbuf_bytes_free(rb);

    /* don't write beyond the end of the buffer */
    assert(bufend > rb->head);
    count = MIN(bufend - rb->head, count);
    ssize_t n = read(fd, rb->head, count);
    if (n > 0) {
        assert(rb->head + n <= bufend);
        rb->head += n;

        /* wrap? */
        if (rb->head == bufend)
            rb->head = rb->buf;

        /* fix up the tail pointer if an overflow occurred */
        if (n > nfree) {
            rb->tail = ringbuf_smash_tail(rb);
            assert(ringbuf_is_full(rb));
        }
    }

    return n;
}

/*
 * Copy n bytes from the ring buffer src, starting from its tail
 * pointer, into a contiguous memory area dst. Returns the value of
 * src's tail pointer after the copy is finished.
 *
 * Note that this copy is destructive with respect to the ring buffer:
 * the n bytes copied from the ring buffer are no longer available in
 * the ring buffer after the copy is complete, and the ring buffer
 * will have n more free bytes than it did before the function was
 * called.
 *
 * This function will *not* allow the ring buffer to underflow. If
 * count is greater than the number of bytes used in the ring buffer,
 * no bytes are copied, and the function will return 0.
 */
void *
ringbuf_memcpy_from(void *dst, ringbuf_t *src, size_t count)
{
    size_t bytes_used = ringbuf_bytes_used(src);
    if (count > bytes_used)
        return 0;

    const void *bufend = ringbuf_end(src);
    size_t nwritten = 0;
    while (nwritten != count) {
        assert(bufend > src->tail);
        size_t n = MIN(bufend - src->tail, count - nwritten);
        memcpy(dst + nwritten, src->tail, n);
        src->tail += n;
        nwritten += n;

        /* wrap ? */
        if (src->tail == bufend)
            src->tail = src->buf;
    }

    assert(count + ringbuf_bytes_used(src) == bytes_used);
    return src->tail;
}

/*
 * This convenience function calls write(2) on the file descriptor fd,
 * using the ring buffer rb as the source buffer for writing (starting
 * at the ring buffer's tail pointer), and returns the value returned
 * by write(2). It will only call write(2) once, and may return a
 * short count, either because write(2) returned a short count, or
 * because the ring buffer must wrap around in order to write more
 * data to the file descriptor; handling the latter condition is the
 * primary reason this function exists.
 *
 * Note that this copy is destructive with respect to the ring buffer:
 * any bytes written from the ring buffer to the file descriptor are
 * no longer available in the ring buffer after the copy is complete,
 * and the ring buffer will have N more free bytes than it did before
 * the function was called, where N is the value returned by the
 * function (unless N is < 0, in which case an error occurred and no
 * bytes were written).
 *
 * This function will *not* allow the ring buffer to underflow. If
 * count is greater than the number of bytes used in the ring buffer,
 * no bytes are written to the file descriptor, and the function will
 * return 0.
 */
ssize_t
ringbuf_write(int fd, ringbuf_t *rb, size_t count)
{
    size_t bytes_used = ringbuf_bytes_used(rb);
    if (count > bytes_used)
        return 0;

    const void *bufend = ringbuf_end(rb);
    assert(bufend > rb->head);
    count = MIN(bufend - rb->tail, count);
    ssize_t n = write(fd, rb->tail, count);
    if (n > 0) {
        assert(rb->tail + n <= bufend);
        rb->tail += n;

        /* wrap? */
        if (rb->tail == bufend)
            rb->tail = rb->buf;

        assert(n + ringbuf_bytes_used(rb) == bytes_used);
    }

    return n;
}

/*
 * Copy count bytes from ring buffer src, starting from its tail
 * pointer, into ring buffer dst. Returns dst's new head pointer after
 * the copy is finished.
 *
 * Note that this copy is destructive with respect to the ring buffer
 * src: any bytes copied from src into dst are no longer available in
 * src after the copy is complete, and src will have 'count' more free
 * bytes than it did before the function was called.
 *
 * It is possible to copy more data from src than is available in dst;
 * i.e., it's possible to overflow dst using this function. When an
 * overflow occurs, the state of dst is guaranteed to be consistent,
 * including the head and tail pointers; old data will simply be
 * overwritten in FIFO fashion, as needed. However, note that, if
 * calling the function results in an overflow, the value dst's tail
 * pointer may be different than it was before the function was
 * called.
 *
 * It is *not* possible to underflow src; if count is greater than the
 * number of bytes used in src, no bytes are copied, and the function
 * returns 0.
 */
void *
ringbuf_copy(ringbuf_t *dst, ringbuf_t *src, size_t count)
{
    size_t src_bytes_used = ringbuf_bytes_used(src);
    if (count > src_bytes_used)
        return 0;
    int overflow = count > ringbuf_bytes_free(dst);

    const void *src_bufend = ringbuf_end(src);
    const void *dst_bufend = ringbuf_end(dst);
    size_t ncopied = 0;
    while (ncopied != count) {
        assert(src_bufend > src->tail);
        size_t nsrc = MIN(src_bufend - src->tail, count - ncopied);
        assert(dst_bufend > dst->head);
        size_t n = MIN(dst_bufend - dst->head, nsrc);
        memcpy(dst->head, src->tail, n);
        src->tail += n;
        dst->head += n;
        ncopied += n;

        /* wrap ? */
        if (src->tail == src_bufend)
            src->tail = src->buf;
        if (dst->head == dst_bufend)
            dst->head = dst->buf;
    }

    assert(count + ringbuf_bytes_used(src) == src_bytes_used);
    
    if (overflow) {
        dst->tail = ringbuf_smash_tail(dst);
        assert(ringbuf_is_full(dst));
    }

    return dst->head;
}

/**** Cut here. ****/

/*
 * A set of simple self-tests. To use the ring buffer functions in
 * your own project, cut or comment out the lines that follow.
 *
 * Otherwise, to compile this file into a test program, do this:
 *
 * cc -o ringbuf ringbuf.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

/*
 * Fill a buffer with a test pattern.
 */
void *
fill_buffer(void *buf, size_t buf_size, const char *test_pattern)
{
    size_t pattern_size = strlen(test_pattern);
    size_t nblocks = buf_size / pattern_size;
    void *p = buf;
    size_t n;
    for (n = 0; n != nblocks; ++n, p += pattern_size)
        memcpy(p, (const void *) test_pattern, pattern_size);
    memcpy(p, (const void *) test_pattern, buf_size % pattern_size);
    
    return buf;
}

int rdfd = -1;
int wrfd = -1;
char rd_template[] = "/tmp/tmpXXXXXXringbuf";
char wr_template[] = "/tmp/tmpXXXXXXringbuf-wr";

void
cleanup()
{
    if (rdfd != -1) {
        close(rdfd);
        unlink(rd_template);
    }
    if (wrfd != -1) {
        close(wrfd);
        unlink(wr_template);
    }
}

void
sigabort(int unused)
{
    cleanup();
    exit(1);
}

int
main(int argc, char **argv)
{
    if (atexit(cleanup) == -1) {
        fprintf(stderr, "Can't install atexit handler, exiting.\n");
        exit(98);
    }
    
    /*
     * catch SIGABRT when asserts fail for proper test file
     * cleanup.
     */
    struct sigaction sa, osa;
    sa.sa_handler = sigabort;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGABRT, &sa, &osa) == -1) {
        fprintf(stderr, "Can't install SIGABRT handler, exiting.\n");
        exit(99);
    }

    ringbuf_t *rb1 = (ringbuf_t *) malloc(sizeof(ringbuf_t));

    /* Initial conditions */
    fprintf(stderr, "Test 1... ");
    ringbuf_init(rb1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    fprintf(stderr, "pass.\n");

    /*
     * Make sure strlen(test_pattern) is not a multiple of MAX_BUF - 1
     */
    const char test_pattern[] = "abcdefghijk";
    void *buf = malloc(MAX_BUF * 2);
    fill_buffer(buf, MAX_BUF * 2, test_pattern);

    /* ringbuf_memcpy_into with zero count */
    fprintf(stderr, "Test 2... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 0) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into a few bytes of data */
    fprintf(stderr, "Test 3... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, strlen(test_pattern)) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - strlen(test_pattern));
    assert(ringbuf_bytes_used(rb1) == strlen(test_pattern));
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(test_pattern, ringbuf_tail(rb1), strlen(test_pattern)) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into full capacity */
    fprintf(stderr, "Test 4... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(ringbuf_tail(rb1), buf, MAX_BUF - 1) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");
    
    /* ringbuf_memcpy_into, twice */
    fprintf(stderr, "Test 5... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, strlen(test_pattern)) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + strlen(test_pattern), strlen(test_pattern) - 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - (2 * strlen(test_pattern) - 1));
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(buf, ringbuf_tail(rb1), 2 * strlen(test_pattern) - 1) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into, twice (to full capacity) */
    fprintf(stderr, "Test 6... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 2) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + MAX_BUF - 2, 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(buf, ringbuf_tail(rb1), MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into, overflow by 1 byte */
    fprintf(stderr, "Test 7... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    /* head should point to the beginning of the buffer */
    assert(ringbuf_head(rb1) == rb1->buf);
    /* tail should have bumped forward by 1 byte */
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(strncmp(ringbuf_tail(rb1), buf + 1, MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into, twice (overflow by 1 byte on 2nd copy) */
    fprintf(stderr, "Test 8... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 1) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + MAX_BUF - 1, 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    /* head should point to the beginning of the buffer */
    assert(ringbuf_head(rb1) == rb1->buf);
    /* tail should have bumped forward by 1 byte */
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(strncmp(ringbuf_tail(rb1), buf + 1, MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into, overflow by 2 bytes (will wrap) */
    fprintf(stderr, "Test 9... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF + 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(ringbuf_tail(rb1) == rb1->buf + 2);
    assert(strncmp(ringbuf_tail(rb1), buf + 2, MAX_BUF - 2) == 0);
    assert(strncmp(rb1->buf, buf + MAX_BUF, 1) == 0);
    fprintf(stderr, "pass.\n");

    rdfd = mkstemps(rd_template, strlen("ringbuf"));
    assert(rdfd != -1);
    assert(write(rdfd, buf, MAX_BUF * 2) == MAX_BUF * 2);
    
    /* ringbuf_read with zero count */
    fprintf(stderr, "Test 10... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 0) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(*(char *)ringbuf_head(rb1) == '\1');
    assert(lseek(rdfd, 0, SEEK_CUR) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_read a few bytes of data */
    fprintf(stderr, "Test 11... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, strlen(test_pattern)) == strlen(test_pattern));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - strlen(test_pattern));
    assert(ringbuf_bytes_used(rb1) == strlen(test_pattern));
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(test_pattern, ringbuf_tail(rb1), strlen(test_pattern)) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_read full capacity */
    fprintf(stderr, "Test 12... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(ringbuf_tail(rb1), buf, MAX_BUF - 1) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");
    
    /* ringbuf_read, twice */
    fprintf(stderr, "Test 13... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, strlen(test_pattern)) == strlen(test_pattern));
    assert(ringbuf_read(rdfd, rb1, strlen(test_pattern) - 1) == strlen(test_pattern) - 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - (2 * strlen(test_pattern) - 1));
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(buf, ringbuf_tail(rb1), 2 * strlen(test_pattern) - 1) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_read, twice (to full capacity) */
    fprintf(stderr, "Test 14... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 2) == MAX_BUF - 2);
    assert(ringbuf_read(rdfd, rb1, 1) == 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(buf, ringbuf_tail(rb1), MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_read, overflow by 1 byte */
    fprintf(stderr, "Test 15... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, MAX_BUF) == MAX_BUF);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    /* head should point to the beginning of the buffer */
    assert(ringbuf_head(rb1) == rb1->buf);
    /* tail should have bumped forward by 1 byte */
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(strncmp(ringbuf_tail(rb1), buf + 1, MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_read, twice (overflow by 1 byte on 2nd copy) */
    fprintf(stderr, "Test 16... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 1) == MAX_BUF - 1);
    assert(ringbuf_read(rdfd, rb1, 1) == 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    /* head should point to the beginning of the buffer */
    assert(ringbuf_head(rb1) == rb1->buf);
    /* tail should have bumped forward by 1 byte */
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(strncmp(ringbuf_tail(rb1), buf + 1, MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_read, try to overflow by 2 bytes; will return a short count */
    fprintf(stderr, "Test 17... ");
    ringbuf_init(rb1);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, MAX_BUF + 1) == MAX_BUF); /* short count */
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    /* head should point to the beginning of the buffer */
    assert(ringbuf_head(rb1) == rb1->buf);
    /* tail should have bumped forward by 1 byte */
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(strncmp(ringbuf_tail(rb1), buf + 1, MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    void *dst = malloc(MAX_BUF * 2);
    
    /* ringbuf_memcpy_from with zero count, empty ring buffer */
    fprintf(stderr, "Test 18... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    assert(ringbuf_memcpy_from(dst, rb1, 0) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(strncmp(dst, buf, MAX_BUF * 2) == 0);
    fprintf(stderr, "pass.\n");

    const char test_pattern2[] = "0123456789A";
    void *buf2 = malloc(MAX_BUF * 2);
    fill_buffer(buf2, MAX_BUF * 2, test_pattern2);

    /* ringbuf_memcpy_from with zero count, non-empty ring buffer */
    fprintf(stderr, "Test 19... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, test_pattern2, strlen(test_pattern2));
    assert(ringbuf_memcpy_from(dst, rb1, 0) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - strlen(test_pattern2));
    assert(ringbuf_bytes_used(rb1) == strlen(test_pattern2));
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(strncmp(dst, buf, MAX_BUF * 2) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_from a few bytes of data */
    fprintf(stderr, "Test 20... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, test_pattern2, strlen(test_pattern2));
    assert(ringbuf_memcpy_from(dst, rb1, 3) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - (strlen(test_pattern2) - 3));
    assert(ringbuf_bytes_used(rb1) == strlen(test_pattern2) - 3);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 3);
    assert(ringbuf_head(rb1) == ringbuf_tail(rb1) + (strlen(test_pattern2) - 3));
    assert(strncmp(dst, test_pattern2, 3) == 0);
    assert(strncmp(dst + 3, buf + 3, MAX_BUF * 2 - 3) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_from full capacity */
    fprintf(stderr, "Test 21... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, MAX_BUF - 1);
    assert(ringbuf_memcpy_from(dst, rb1, MAX_BUF - 1) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    assert(strncmp(dst, buf2, MAX_BUF - 1) == 0);
    assert(strncmp(dst + MAX_BUF - 1, buf + MAX_BUF - 1, MAX_BUF + 1) == 0);
    fprintf(stderr, "pass.\n");
    
    /* ringbuf_memcpy_from, twice */
    fprintf(stderr, "Test 22... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, 13);
    assert(ringbuf_memcpy_from(dst, rb1, 9) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_from(dst + 9, rb1, 4) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 13);
    assert(strncmp(dst, buf2, 13) == 0);
    assert(strncmp(dst + 13, buf + 13, MAX_BUF * 2 - 13) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_from, twice (full capacity) */
    fprintf(stderr, "Test 23... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, MAX_BUF - 1);
    assert(ringbuf_memcpy_from(dst, rb1, MAX_BUF - 2) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_from(dst + MAX_BUF - 2, rb1, 1) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 1);
    assert(strncmp(dst, buf2, MAX_BUF - 1) == 0);
    assert(strncmp(dst + MAX_BUF - 1, buf + MAX_BUF - 1, MAX_BUF * 2 - (MAX_BUF -1)) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_from, attempt to underflow */
    fprintf(stderr, "Test 24... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, 15);
    assert(ringbuf_memcpy_from(dst, rb1, 16) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 15);
    assert(ringbuf_bytes_used(rb1) == 15);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 15);
    assert(strncmp(dst, buf, MAX_BUF * 2) == 0);
    fprintf(stderr, "pass.\n");
    
    /* ringbuf_memcpy_from, attempt to underflow on 2nd call */
    fprintf(stderr, "Test 25... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, 15);
    assert(ringbuf_memcpy_from(dst, rb1, 14) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_from(dst + 14, rb1, 2) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 1);
    assert(ringbuf_bytes_used(rb1) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 14);
    assert(ringbuf_head(rb1) == rb1->buf + 15);
    assert(strncmp(dst, buf2, 14) == 0);
    assert(strncmp(dst + 14, buf + 14, MAX_BUF * 2 - 14) == 0);
    fprintf(stderr, "pass.\n");
    
    wrfd = mkstemps(wr_template, strlen("ringbuf-wr"));
    assert(wrfd != -1);

    /* ringbuf_write with zero count, empty ring buffer */
    fprintf(stderr, "Test 26... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(ringbuf_write(wrfd, rb1, 0) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    /* should return 0 (EOF) */
    assert(read(wrfd, dst, 10) == 0);
                
    //assert(strncmp(dst, buf, MAX_BUF * 2) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_write with zero count, non-empty ring buffer */
    fprintf(stderr, "Test 27... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    ringbuf_memcpy_into(rb1, test_pattern2, strlen(test_pattern2));
    assert(ringbuf_write(wrfd, rb1, 0) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - strlen(test_pattern2));
    assert(ringbuf_bytes_used(rb1) == strlen(test_pattern2));
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    /* should return 0 (EOF) */
    assert(read(wrfd, dst, 10) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_write a few bytes of data */
    fprintf(stderr, "Test 28... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, test_pattern2, strlen(test_pattern2));
    assert(ringbuf_write(wrfd, rb1, 3) == 3);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - (strlen(test_pattern2) - 3));
    assert(ringbuf_bytes_used(rb1) == strlen(test_pattern2) - 3);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 3);
    assert(ringbuf_head(rb1) == ringbuf_tail(rb1) + (strlen(test_pattern2) - 3));
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 4) == 3);
    assert(read(wrfd, dst + 3, 1) == 0);
    assert(strncmp(dst, test_pattern2, 3) == 0);
    assert(strncmp(dst + 3, buf + 3, MAX_BUF * 2 - 3) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_write full capacity */
    fprintf(stderr, "Test 29... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, MAX_BUF - 1);
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, MAX_BUF) == MAX_BUF - 1);
    assert(read(wrfd, dst + MAX_BUF - 1, 1) == 0);
    assert(strncmp(dst, buf2, MAX_BUF - 1) == 0);
    assert(strncmp(dst + MAX_BUF - 1, buf + MAX_BUF - 1, MAX_BUF + 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_write, twice */
    fprintf(stderr, "Test 30... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, 13);
    assert(ringbuf_write(wrfd, rb1, 9) == 9);
    assert(ringbuf_write(wrfd, rb1, 4) == 4);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 13);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 14) == 13);
    assert(read(wrfd, dst + 13, 1) == 0);
    assert(strncmp(dst, buf2, 13) == 0);
    assert(strncmp(dst + 13, buf + 13, MAX_BUF * 2 - 13) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_write, twice (full capacity) */
    fprintf(stderr, "Test 31... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, MAX_BUF - 1);
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 2) == MAX_BUF - 2);
    assert(ringbuf_write(wrfd, rb1, 1) == 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 1);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, MAX_BUF - 1) == MAX_BUF - 1);
    assert(read(wrfd, dst + MAX_BUF - 1, 1) == 0);
    assert(strncmp(dst, buf2, MAX_BUF - 1) == 0);
    assert(strncmp(dst + MAX_BUF - 1, buf + MAX_BUF - 1, MAX_BUF * 2 - (MAX_BUF -1)) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_write, attempt to underflow */
    fprintf(stderr, "Test 32... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, 15);
    assert(ringbuf_write(wrfd, rb1, 16) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 15);
    assert(ringbuf_bytes_used(rb1) == 15);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 15);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, MAX_BUF * 2) == 0);
    fprintf(stderr, "pass.\n");
    
    /* ringbuf_write, attempt to underflow on 2nd call */
    fprintf(stderr, "Test 33... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    fill_buffer(dst, MAX_BUF * 2, test_pattern);
    ringbuf_memcpy_into(rb1, buf2, 15);
    assert(ringbuf_write(wrfd, rb1, 14) == 14);
    assert(ringbuf_write(wrfd, rb1, 2) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 1);
    assert(ringbuf_bytes_used(rb1) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 14);
    assert(ringbuf_head(rb1) == rb1->buf + 15);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 15) == 14);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf2, 1) == 0);
    assert(strncmp(dst + 14, buf + 14, MAX_BUF * 2 - 14) == 0);
    fprintf(stderr, "pass.\n");
    
    /* ringbuf_read followed by ringbuf_write */
    fprintf(stderr, "Test 34... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 11) == 11);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, 11) == 0);
    assert(strncmp(dst + 11, buf2 + 11, MAX_BUF * 2 - 11) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_read followed by partial ringbuf_write */
    fprintf(stderr, "Test 35... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 7) == 7);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 4);
    assert(ringbuf_bytes_used(rb1) == 4);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 11) == 7);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, 7) == 0);
    assert(strncmp(dst + 7, buf2 + 7, MAX_BUF * 2 - 7) == 0);
    assert(ringbuf_tail(rb1) == rb1->buf + 7);
    assert(ringbuf_head(rb1) == rb1->buf + 11);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /*
     * ringbuf_read, ringbuf_write, then ringbuf_read to just before
     * the end of contiguous buffer, but don't wrap
     */
    fprintf(stderr, "Test 36... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11 - 1) == MAX_BUF - 11 - 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 11);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 11 - 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 11);
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 11) == 11);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, 11) == 0);
    assert(strncmp(dst + 11, buf2 + 11, MAX_BUF * 2 - 11) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * ringbuf_read, ringbuf_write, then ringbuf_read to the end of
     * the contiguous buffer, which should cause the head pointer to
     * wrap.
     */
    fprintf(stderr, "Test 37... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11) == MAX_BUF - 11);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 10);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 11);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 11);
    assert(ringbuf_head(rb1) == rb1->buf);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, 11) == 11);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, 11) == 0);
    assert(strncmp(dst + 11, buf2 + 11, MAX_BUF * 2 - 11) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except the 2nd ringbuf_read attempts to
     * read 1 beyond the end of the contiguous buffer. Because
     * ringbuf_read only calls read(2) at most once, it should return
     * a short count (short by one byte), since wrapping around and
     * continuing to fill the ring buffer would require 2 read(2)
     * calls. For good measure, follow it up with a ringbuf_write that
     * causes the tail pointer to stop just short of wrapping, which
     * tests behavior when the ring buffer's head pointer is less than
     * its tail pointer.
     */
    fprintf(stderr, "Test 38... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    /* should return a short count! */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11 + 1) == MAX_BUF - 11);
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 12) == MAX_BUF - 12);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 2);
    assert(ringbuf_bytes_used(rb1) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 1);
    assert(ringbuf_head(rb1) == rb1->buf);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, MAX_BUF - 1) == MAX_BUF - 1);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, MAX_BUF - 1) == 0);
    assert(strncmp(dst + MAX_BUF - 1, buf2 + MAX_BUF - 1, MAX_BUF * 2 - (MAX_BUF - 1)) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except when the 2nd ringbuf_read returns
     * a short count, do another.
     */
    fprintf(stderr, "Test 39... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    /* should return a short count! */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11 + 1) == MAX_BUF - 11);
    assert(ringbuf_read(rdfd, rb1, 1) == 1);
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 12) == MAX_BUF - 12);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 3);
    assert(ringbuf_bytes_used(rb1) == 2);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 1);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, MAX_BUF - 1) == MAX_BUF - 1);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, MAX_BUF - 1) == 0);
    assert(strncmp(dst + MAX_BUF - 1, buf2 + MAX_BUF - 1, MAX_BUF * 2 - (MAX_BUF - 1)) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except the 2nd ringbuf_write causes the
     * tail pointer to wrap (just).
     */
    fprintf(stderr, "Test 40... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    /* should return a short count! */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11 + 1) == MAX_BUF - 11);
    assert(ringbuf_read(rdfd, rb1, 1) == 1);
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 11) == MAX_BUF - 11);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 2);
    assert(ringbuf_bytes_used(rb1) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, MAX_BUF) == MAX_BUF);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, MAX_BUF) == 0);
    assert(strncmp(dst + MAX_BUF, buf2 + MAX_BUF, MAX_BUF) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except the 2nd ringbuf_write returns a
     * short count.
     */
    fprintf(stderr, "Test 41... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    /* should return a short count! */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11 + 1) == MAX_BUF - 11);
    assert(ringbuf_read(rdfd, rb1, 1) == 1);
    /* should return a short count! */
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 10) == MAX_BUF - 11);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 2);
    assert(ringbuf_bytes_used(rb1) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, MAX_BUF) == MAX_BUF);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, MAX_BUF) == 0);
    assert(strncmp(dst + MAX_BUF, buf2 + MAX_BUF, MAX_BUF) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except do a 3rd ringbuf_write after the
     * 2nd returns the short count.
     */
    fprintf(stderr, "Test 42... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    /* should return a short count! */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11 + 1) == MAX_BUF - 11);
    assert(ringbuf_read(rdfd, rb1, 1) == 1);
    /* should return a short count! */
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 10) == MAX_BUF - 11);
    assert(ringbuf_write(wrfd, rb1, 1) == 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(read(wrfd, dst, MAX_BUF + 1) == MAX_BUF + 1);
    assert(read(wrfd, dst, 1) == 0);
    assert(strncmp(dst, buf, MAX_BUF + 1) == 0);
    assert(strncmp(dst + MAX_BUF + 1, buf2 + MAX_BUF + 1, MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into followed by ringbuf_memcpy_from */
    fprintf(stderr, "Test 43... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(strncmp(dst, buf, 11) == 0);
    assert(strncmp(dst + 11, buf2 + 11, MAX_BUF * 2 - 11) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_memcpy_into followed by partial ringbuf_memcpy_from */
    fprintf(stderr, "Test 44... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 7) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 4);
    assert(ringbuf_bytes_used(rb1) == 4);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(strncmp(dst, buf, 7) == 0);
    assert(strncmp(dst + 7, buf2 + 7, MAX_BUF * 2 - 7) == 0);
    assert(ringbuf_tail(rb1) == rb1->buf + 7);
    assert(ringbuf_head(rb1) == rb1->buf + 11);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /*
     * ringbuf_memcpy_into, ringbuf_memcpy_from, then
     * ringbuf_memcpy_into to just before the end of contiguous
     * buffer, but don't wrap
     */
    fprintf(stderr, "Test 45... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + 11, MAX_BUF - 11 - 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 11);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 11 - 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 11);
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    assert(strncmp(dst, buf, 11) == 0);
    assert(strncmp(dst + 11, buf2 + 11, MAX_BUF * 2 - 11) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * ringbuf_memcpy_into, ringbuf_memcpy_from, then
     * ringbuf_memcpy_into to the end of the contiguous buffer, which
     * should cause the head pointer to wrap.
     */
    fprintf(stderr, "Test 46... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + 11, MAX_BUF - 11) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 10);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 11);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 11);
    assert(ringbuf_head(rb1) == rb1->buf);
    assert(strncmp(dst, buf, 11) == 0);
    assert(strncmp(dst + 11, buf2 + 11, MAX_BUF * 2 - 11) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except the 2nd ringbuf_memcpy_into reads
     * 1 beyond the end of the contiguous buffer, which causes it to
     * wrap and do a 2nd memcpy from the start of the contiguous
     * buffer. For good measure, follow it up with a ringbuf_write
     * that causes the tail pointer to stop just short of wrapping,
     * which tests behavior when the ring buffer's head pointer is
     * less than its tail pointer.
     */
    fprintf(stderr, "Test 47... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + 11, MAX_BUF - 11 + 1) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst + 11, rb1, MAX_BUF - 12) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 3);
    assert(ringbuf_bytes_used(rb1) == 2);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 1);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(strncmp(dst, buf, MAX_BUF - 1) == 0);
    assert(strncmp(dst + MAX_BUF - 1, buf2 + MAX_BUF - 1, MAX_BUF * 2 - (MAX_BUF - 1)) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except the 2nd ringbuf_write causes the
     * tail pointer to wrap (just).
     */
    fprintf(stderr, "Test 48... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + 11, MAX_BUF - 11 + 1) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst + 11, rb1, MAX_BUF - 11) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 2);
    assert(ringbuf_bytes_used(rb1) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(strncmp(dst, buf, MAX_BUF) == 0);
    assert(strncmp(dst + MAX_BUF, buf2 + MAX_BUF, MAX_BUF) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Same as previous test, except the 2nd ringbuf_write performs 2
     * memcpy's, the 2nd of which starts from the beginning of the
     * contiguous buffer after the wrap.
     */
    fprintf(stderr, "Test 49... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_into(rb1, buf + 11, MAX_BUF - 11 + 1) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst + 11, rb1, MAX_BUF - 11 + 1) == ringbuf_tail(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(strncmp(dst, buf, MAX_BUF + 1) == 0);
    assert(strncmp(dst + MAX_BUF + 1, buf2 + MAX_BUF + 1, MAX_BUF - 1) == 0);
    fprintf(stderr, "pass.\n");

    /*
     * Overflow with ringbuf_read when tail pointer is > head
     * pointer. Should bump tail pointer to head + 1.
     */
    fprintf(stderr, "Test 50... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    /* wrap head */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11) == MAX_BUF - 11);
    /* overflow */
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 1);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + 11);
    assert(ringbuf_tail(rb1) == rb1->buf + 12);
    fprintf(stderr, "pass.\n");

    /*
     * Overflow with ringbuf_read when tail pointer is > head pointer,
     * and tail pointer is at the end of the contiguous buffer. Should
     * wrap tail pointer to beginning of contiguous buffer.
     */
    fprintf(stderr, "Test 51... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    assert(ftruncate(wrfd, 0) == 0);
    assert(lseek(wrfd, 0, SEEK_SET) == 0);
    assert(lseek(rdfd, 0, SEEK_SET) == 0);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_read(rdfd, rb1, 11) == 11);
    assert(ringbuf_write(wrfd, rb1, 11) == 11);
    /* wrap head */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 11) == MAX_BUF - 11);
    /* write until tail points to end of contiguous buffer */
    assert(ringbuf_write(wrfd, rb1, MAX_BUF - 12) == MAX_BUF - 12);
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 1);
    /* overflow */
    assert(ringbuf_read(rdfd, rb1, MAX_BUF - 1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 1);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    assert(ringbuf_tail(rb1) == rb1->buf);
    fprintf(stderr, "pass.\n");

    /*
     * Overflow with ringbuf_memcpy_into when tail pointer is > head
     * pointer. Should bump tail pointer to head + 1.
     */
    fprintf(stderr, "Test 52... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    /* wrap head */
    assert(ringbuf_memcpy_into(rb1, buf + 11, MAX_BUF - 11) == ringbuf_head(rb1));
    /* overflow */
    assert(ringbuf_memcpy_into(rb1, buf + MAX_BUF, 11) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 1);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + 11);
    assert(ringbuf_tail(rb1) == rb1->buf + 12);
    fprintf(stderr, "pass.\n");

    /*
     * Overflow with ringbuf_memcpy_into when tail pointer is > head
     * pointer, and tail pointer is at the end of the contiguous
     * buffer. Should wrap tail pointer to beginning of contiguous
     * buffer.
     */
    fprintf(stderr, "Test 52... ");
    ringbuf_init(rb1);
    memset(rb1->buf, 1, MAX_BUF);
    fill_buffer(dst, MAX_BUF * 2, test_pattern2);
    memset(rb1->buf, 1, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 11) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_from(dst, rb1, 11) == ringbuf_tail(rb1));
    /* wrap head */
    assert(ringbuf_memcpy_into(rb1, buf + 11, MAX_BUF - 11) == ringbuf_head(rb1));
    /* copy from until tail points to end of contiguous buffer */
    assert(ringbuf_memcpy_from(dst + 11, rb1, MAX_BUF - 12) == ringbuf_tail(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 1);
    /* overflow */
    assert(ringbuf_memcpy_into(rb1, buf + MAX_BUF, MAX_BUF - 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 1);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    assert(ringbuf_tail(rb1) == rb1->buf);
    fprintf(stderr, "pass.\n");

    ringbuf_t *rb2 = (ringbuf_t *) malloc(sizeof(ringbuf_t));

    /* ringbuf_copy with zero count, empty buffers */
    fprintf(stderr, "Test 53... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_copy(rb1, rb2, 0) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2));
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_tail(rb2) == ringbuf_head(rb2));
    assert(ringbuf_head(rb1) == rb1->buf);
    assert(ringbuf_head(rb2) == rb2->buf);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy with zero count, empty src */
    fprintf(stderr, "Test 54... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 2) == ringbuf_head(rb1));
    assert(ringbuf_copy(rb1, rb2, 0) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 2);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2));
    assert(ringbuf_bytes_used(rb1) == 2);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_tail(rb2) == ringbuf_head(rb2));
    assert(ringbuf_head(rb1) == rb1->buf + 2);
    assert(ringbuf_head(rb2) == rb2->buf);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy with zero count, empty dst */
    fprintf(stderr, "Test 55... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb2, buf, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 0) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1));
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2) - 2);
    assert(ringbuf_bytes_used(rb1) == 0);
    assert(ringbuf_bytes_used(rb2) == 2);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(ringbuf_is_empty(rb1));
    assert(!ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == ringbuf_head(rb1));
    assert(ringbuf_tail(rb2) == rb2->buf);
    assert(ringbuf_head(rb1) == rb1->buf);
    assert(ringbuf_head(rb2) == rb2->buf + 2);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy with zero count */
    fprintf(stderr, "Test 56... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 2) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_into(rb2, buf2, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 0) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 2);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2) - 2);
    assert(ringbuf_bytes_used(rb1) == 2);
    assert(ringbuf_bytes_used(rb2) == 2);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(!ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_tail(rb2) == rb2->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 2);
    assert(ringbuf_head(rb2) == rb2->buf + 2);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy full contents of rb2 into rb1 (initially empty) */
    fprintf(stderr, "Test 57... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb2, buf2, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 2) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 2);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2));
    assert(ringbuf_bytes_used(rb1) == 2);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_tail(rb2) == rb2->buf + 2);
    assert(ringbuf_head(rb1) == rb1->buf + 2);
    assert(ringbuf_head(rb2) == rb2->buf + 2);
    assert(strncmp(rb1->tail, buf2, 2) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy full contents of rb2 into rb1 (latter initially
     * has 3 bytes) */
    fprintf(stderr, "Test 58... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 3) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_into(rb2, buf2, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 2) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 5);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2));
    assert(ringbuf_bytes_used(rb1) == 5);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_tail(rb2) == rb2->buf + 2);
    assert(ringbuf_head(rb1) == rb1->buf + 5);
    assert(ringbuf_head(rb2) == rb2->buf + 2);
    assert(strncmp(rb1->tail, buf, 3) == 0);
    assert(strncmp(rb1->tail + 3, buf2, 2) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy, wrap head of dst */
    fprintf(stderr, "Test 59... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 1) == ringbuf_head(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    /* make sure rb1 doesn't overflow on later ringbuf_copy */
    assert(ringbuf_memcpy_from(dst, rb1, 1) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_into(rb2, buf2, 1) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 1) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2));
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf + 1);
    assert(ringbuf_tail(rb2) == rb2->buf + 1);
    assert(ringbuf_head(rb1) == rb1->buf);
    assert(ringbuf_head(rb2) == rb2->buf + 1);
    assert(strncmp(rb1->tail, buf + 1, MAX_BUF - 2) == 0);
    assert(strncmp(rb1->tail + MAX_BUF - 2, buf2, 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy, wrap head of dst and continue copying into start
     * of contiguous buffer */
    fprintf(stderr, "Test 60... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 1) == ringbuf_head(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    /* make sure rb1 doesn't overflow on later ringbuf_copy */
    assert(ringbuf_memcpy_from(dst, rb1, 2) == ringbuf_tail(rb1));
    assert(ringbuf_memcpy_into(rb2, buf2, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 2) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2));
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf + 2);
    assert(ringbuf_tail(rb2) == rb2->buf + 2);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(ringbuf_head(rb2) == rb2->buf + 2);
    assert(strncmp(rb1->tail, buf + 2, MAX_BUF - 3) == 0);
    /* last position in contiguous buffer */
    assert(strncmp(rb1->tail + MAX_BUF - 3, buf2, 1) == 0);
    /* start of contiguous buffer (from copy wrap) */
    assert(strncmp(rb1->buf, buf2 + 1, 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy, wrap tail of src */
    fprintf(stderr, "Test 61... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb2, buf2, MAX_BUF - 1) == ringbuf_head(rb2));
    assert(ringbuf_head(rb2) == rb2->buf + MAX_BUF - 1);
    assert(ringbuf_memcpy_from(dst, rb2, MAX_BUF - 3) == ringbuf_tail(rb2));
    assert(ringbuf_tail(rb2) == rb2->buf + MAX_BUF - 3);
    assert(ringbuf_memcpy_into(rb2, buf2 + MAX_BUF - 1, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 3) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 1 - 3);
    assert(ringbuf_bytes_free(rb2) == MAX_BUF - 1 - 1);
    assert(ringbuf_bytes_used(rb1) == 3);
    assert(ringbuf_bytes_used(rb2) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(!ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_tail(rb2) == rb2->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 3);
    assert(ringbuf_head(rb2) == rb2->buf + 1);
    assert(strncmp(rb1->tail, buf2 + MAX_BUF - 3, 3) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy, wrap tail of src and continue copying from start
     * of contiguous buffer */
    fprintf(stderr, "Test 62... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb2, buf2, MAX_BUF - 1) == ringbuf_head(rb2));
    assert(ringbuf_head(rb2) == rb2->buf + MAX_BUF - 1);
    assert(ringbuf_memcpy_from(dst, rb2, MAX_BUF - 3) == ringbuf_tail(rb2));
    assert(ringbuf_tail(rb2) == rb2->buf + MAX_BUF - 3);
    assert(ringbuf_memcpy_into(rb2, buf2 + MAX_BUF - 1, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 4) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 1 - 4);
    assert(ringbuf_bytes_free(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_used(rb1) == 4);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_tail(rb2) == rb2->buf + 1);
    assert(ringbuf_head(rb1) == rb1->buf + 4);
    assert(ringbuf_head(rb2) == rb2->buf + 1);
    assert(strncmp(rb1->tail, buf2 + MAX_BUF - 3, 4) == 0);
    assert(*(char *)ringbuf_head(rb1) == '\1');
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy, wrap tail of src and head of dst simultaneously,
     * then continue copying from start of contiguous buffer */
    fprintf(stderr, "Test 63... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb2, buf2, MAX_BUF - 1) == ringbuf_head(rb2));
    assert(ringbuf_head(rb2) == rb2->buf + MAX_BUF - 1);
    assert(ringbuf_memcpy_from(dst, rb2, MAX_BUF - 3) == ringbuf_tail(rb2));
    assert(ringbuf_tail(rb2) == rb2->buf + MAX_BUF - 3);
    assert(ringbuf_memcpy_into(rb2, buf2 + MAX_BUF - 1, 2) == ringbuf_head(rb2));
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 3) == ringbuf_head(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 3);
    assert(ringbuf_memcpy_from(dst, rb1, MAX_BUF - 3) == ringbuf_tail(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 3);
    assert(ringbuf_copy(rb1, rb2, 4) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 1 - 4);
    assert(ringbuf_bytes_free(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_used(rb1) == 4);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 3);
    assert(ringbuf_tail(rb2) == rb2->buf + 1);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(ringbuf_head(rb2) == rb2->buf + 1);
    assert(strncmp(rb1->tail, buf2 + MAX_BUF - 3, 3) == 0);
    assert(strncmp(rb1->buf, buf2 + MAX_BUF, 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy, force 3 separate memcpy's: up to end of src, then
     * up to end of dst, then copy remaining bytes. */
    fprintf(stderr, "Test 64... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb2, buf2, MAX_BUF - 1) == ringbuf_head(rb2));
    assert(ringbuf_head(rb2) == rb2->buf + MAX_BUF - 1);
    assert(ringbuf_memcpy_from(dst, rb2, MAX_BUF - 2) == ringbuf_tail(rb2));
    assert(ringbuf_tail(rb2) == rb2->buf + MAX_BUF - 2);
    assert(ringbuf_memcpy_into(rb2, buf2 + MAX_BUF - 1, 5) == ringbuf_head(rb2));
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 3) == ringbuf_head(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 3);
    assert(ringbuf_memcpy_from(dst, rb1, MAX_BUF - 4) == ringbuf_tail(rb1));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 4);
    assert(ringbuf_copy(rb1, rb2, 5) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == MAX_BUF - 1 - 6);
    assert(ringbuf_bytes_free(rb2) == MAX_BUF - 1 - 1);
    assert(ringbuf_bytes_used(rb1) == 6);
    assert(ringbuf_bytes_used(rb2) == 1);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(!ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf + MAX_BUF - 4);
    assert(ringbuf_tail(rb2) == rb2->buf + 3);
    assert(ringbuf_head(rb1) == rb1->buf + 2);
    assert(ringbuf_head(rb2) == rb2->buf + 4);
    /* one byte from buf */
    assert(strncmp(rb1->tail, buf + MAX_BUF - 4, 1) == 0);
    /* 5 bytes from buf2, 3 at end of contiguous buffer and 2 after the wrap */
    assert(strncmp(rb1->tail + 1, buf2 + MAX_BUF - 2, 3) == 0);
    assert(strncmp(rb1->buf, buf2 + MAX_BUF - 2 + 3, 2) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy overflow */
    fprintf(stderr, "Test 65... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, MAX_BUF - 1) == ringbuf_head(rb1));
    assert(ringbuf_head(rb1) == rb1->buf + MAX_BUF - 1);
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_memcpy_into(rb2, buf2, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 2) == ringbuf_head(rb1));
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == 0);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2));
    assert(ringbuf_bytes_used(rb1) == MAX_BUF - 1);
    assert(ringbuf_bytes_used(rb2) == 0);
    assert(ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf + 2);
    assert(ringbuf_tail(rb2) == rb2->buf + 2);
    assert(ringbuf_head(rb1) == rb1->buf + 1);
    assert(ringbuf_head(rb2) == rb2->buf + 2);
    assert(strncmp(rb1->tail, buf + 2, MAX_BUF - 1 - 2) == 0);
    assert(strncmp(rb1->tail + MAX_BUF - 1 - 2, buf2, 1) == 0);
    assert(strncmp(rb1->buf, buf2 + 1, 1) == 0);
    fprintf(stderr, "pass.\n");

    /* ringbuf_copy attempted underflow */
    fprintf(stderr, "Test 66... ");
    ringbuf_init(rb1);
    ringbuf_init(rb2);
    memset(rb1->buf, 1, MAX_BUF);
    memset(rb2->buf, 2, MAX_BUF);
    assert(ringbuf_memcpy_into(rb1, buf, 2) == ringbuf_head(rb1));
    assert(ringbuf_memcpy_into(rb2, buf2, 2) == ringbuf_head(rb2));
    assert(ringbuf_copy(rb1, rb2, 3) == 0);
    assert(ringbuf_capacity(rb1) == MAX_BUF - 1);
    assert(ringbuf_capacity(rb2) == MAX_BUF - 1);
    assert(ringbuf_bytes_free(rb1) == ringbuf_capacity(rb1) - 2);
    assert(ringbuf_bytes_free(rb2) == ringbuf_capacity(rb2) - 2);
    assert(ringbuf_bytes_used(rb1) == 2);
    assert(ringbuf_bytes_used(rb2) == 2);
    assert(!ringbuf_is_full(rb1));
    assert(!ringbuf_is_full(rb2));
    assert(!ringbuf_is_empty(rb1));
    assert(!ringbuf_is_empty(rb2));
    assert(ringbuf_tail(rb1) == rb1->buf);
    assert(ringbuf_tail(rb2) == rb2->buf);
    assert(ringbuf_head(rb1) == rb1->buf + 2);
    assert(ringbuf_head(rb2) == rb2->buf + 2);
    fprintf(stderr, "pass.\n");

    free((void *) rb1);
    free((void *) rb2);
    free(buf);
    free(buf2);
    free(dst);
    return 0;
}
