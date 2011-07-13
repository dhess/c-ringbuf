#ifndef INCLUDED_RINGBUF_H
#define INCLUDED_RINGBUF_H

/*
 * ringbuf.h - C ring buffer (FIFO) interface.
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

#include <stddef.h>
#include <sys/types.h>

#define RINGBUF_SIZE 4096
typedef struct ringbuf_t
{
    char buf[RINGBUF_SIZE];
    void *head, *tail;
} ringbuf_t;

/*
 * The size of the internal buffer, in bytes. One byte will always be
 * unused, to distinguish the "buffer full" state from the "buffer
 * empty" state.
 *
 * For future-proofness, use this function rather than the #define'd
 * RINGBUF_SIZE value; using the function makes it easier later to
 * support ring buffers with dynamic (and different) sizes.
 */
size_t
ringbuf_buffer_size(const ringbuf_t *rb);

/*
 * Initialize/reset a ring buffer.
 */
void
ringbuf_init(ringbuf_t *rb);

/*
 * The usable capacity of the ring buffer, in bytes.
 */
size_t
ringbuf_capacity(const ringbuf_t *rb);

/*
 * The number of free/available bytes in the ring buffer.
 */
size_t
ringbuf_bytes_free(const ringbuf_t *rb);

/*
 * The number of bytes currently being used in the ring buffer.
 */
size_t
ringbuf_bytes_used(const ringbuf_t *rb);

int
ringbuf_is_full(const ringbuf_t *rb);

int
ringbuf_is_empty(const ringbuf_t *rb);

const void *
ringbuf_tail(const ringbuf_t *rb);

const void *
ringbuf_head(const ringbuf_t *rb);

/*
 * Given a ring buffer rb and a pointer to a location within its
 * contiguous buffer, return the a pointer to the next logical
 * location in the ring buffer.
 *
 * If p does not point somewhere within the ring buffer's contiguous
 * buffer, the function returns 0.
 */
void *
ringbuf_nextp(ringbuf_t *rb, void *p);

/*
 * Locate the first occurrence of character c (converted to a char) in
 * ring buffer rb, beginning the search at offset bytes from the ring
 * buffer's tail pointer. The function returns the offset of the
 * character from the ring buffer's tail pointer, if found. If c does
 * not occur in the ring buffer, the function returns the number of
 * bytes used in the ring buffer.
 */
size_t
ringbuf_findchr(const ringbuf_t *rb, int c, size_t offset);
 
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
ringbuf_memcpy_into(ringbuf_t *dst, const void *src, size_t count);

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
ringbuf_read(int fd, ringbuf_t *rb, size_t count);

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
ringbuf_memcpy_from(void *dst, ringbuf_t *src, size_t count);

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
ringbuf_write(int fd, ringbuf_t *rb, size_t count);

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
ringbuf_copy(ringbuf_t *dst, ringbuf_t *src, size_t count);

#endif /* INCLUDED_RINGBUF_H */
