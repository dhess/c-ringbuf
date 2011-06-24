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
 * You should have received a copy of the CC0 Public Domain Dedication
 * along with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "ringbuf.h"

#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/param.h>
#include <assert.h>

size_t
ringbuf_buffer_size(const ringbuf_t *rb)
{
    return _RINGBUF_SIZE;
}

void
ringbuf_init(ringbuf_t *rb)
{
    rb->head = rb->tail = rb->buf;
}

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

size_t
ringbuf_bytes_free(const ringbuf_t *rb)
{
    if (rb->head >= rb->tail)
        return ringbuf_capacity(rb) - (rb->head - rb->tail);
    else
        return rb->tail - rb->head - 1;
}

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
 * Same as ringbuf_nextp, minus the range check; used by internal
 * functions where the check is unnecessary.
 */
static void *
_ringbuf_nextp(ringbuf_t *rb, void *p)
{
    return rb->buf +
        ((++p - (const void *) rb->buf) % ringbuf_buffer_size(rb));
}

void *
ringbuf_nextp(ringbuf_t *rb, void *p)
{
    if (p < (void *) rb->buf || p >= ringbuf_end(rb))
        return 0;
    else
        return _ringbuf_nextp(rb, p);
}

size_t
ringbuf_findchr(const ringbuf_t *rb, int c, size_t offset)
{
    const void *bufend = ringbuf_end(rb);
    size_t bytes_used = ringbuf_bytes_used(rb);
    if (offset >= bytes_used)
        return bytes_used;

    const void *start = rb->buf +
        (((rb->tail -
           (const void *) rb->buf) + offset) % ringbuf_buffer_size(rb));
    assert(bufend > start);
    size_t n = MIN(bufend - start, bytes_used - offset);
    const void *found = memchr(start, c, n);
    if (found)
        return offset + found - start;
    else
        return ringbuf_findchr(rb, c, offset + n);
}

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
        dst->tail = _ringbuf_nextp(dst, dst->head);
        assert(ringbuf_is_full(dst));
    }

    return dst->head;
}

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
            rb->tail = _ringbuf_nextp(rb, rb->head);
            assert(ringbuf_is_full(rb));
        }
    }

    return n;
}

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
        dst->tail = _ringbuf_nextp(dst, dst->head);
        assert(ringbuf_is_full(dst));
    }

    return dst->head;
}
