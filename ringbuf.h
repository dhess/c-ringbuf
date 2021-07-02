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
 * A byte-addressable ring buffer FIFO implementation.
 *
 * The ring buffer's head pointer points to the starting location
 * where data should be written when copying data *into* the buffer
 * (e.g., with ringbuf_read). The ring buffer's tail pointer points to
 * the starting location where data should be read when copying data
 * *from* the buffer (e.g., with ringbuf_write).
 */

#include <stddef.h>
#include <sys/types.h>

struct ringbuf_t;
typedef struct ringbuf_t* ringbuf;

#ifdef __cplusplus
    #define EXPORT_API extern "C" __attribute__((visibility("default")))
#else
    #define EXPORT_API __attribute__((visibility("default")))
#endif

/*
 * Create a new ring buffer with the given capacity (usable
 * bytes). Note that the actual internal buffer size may be one or
 * more bytes larger than the usable capacity, for bookkeeping.
 *
 * Returns the new ring buffer object, or 0 if there's not enough
 * memory to fulfill the request for the given capacity.
 */
EXPORT_API ringbuf ringbuf_new(size_t capacity);

/*
 * The size of the internal buffer, in bytes. One or more bytes may be
 * unusable in order to distinguish the "buffer full" state from the
 * "buffer empty" state.
 *
 * For the usable capacity of the ring buffer, use the
 * ringbuf_capacity function.
 */
EXPORT_API size_t ringbuf_buffer_size(const ringbuf rb);

/*
 * Deallocate a ring buffer, and, as a side effect, set the pointer to
 * 0.
 */
EXPORT_API void ringbuf_free(ringbuf* rb);

/*
 * Reset a ring buffer to its initial state (empty).
 */
EXPORT_API void ringbuf_reset(ringbuf rb);

/*
 * The usable capacity of the ring buffer, in bytes. Note that this
 * value may be less than the ring buffer's internal buffer size, as
 * returned by ringbuf_buffer_size.
 */
size_t
ringbuf_capacity(const ringbuf rb);

/*
 * The number of free/available bytes in the ring buffer. This value
 * is never larger than the ring buffer's usable capacity.
 */
size_t
ringbuf_bytes_free(const ringbuf rb);

/*
 * The number of bytes currently being used in the ring buffer. This
 * value is never larger than the ring buffer's usable capacity.
 */
EXPORT_API size_t ringbuf_bytes_used(const ringbuf rb);

EXPORT_API int ringbuf_is_full(const ringbuf rb);

EXPORT_API int ringbuf_is_empty(const ringbuf rb);

/*
 * Const access to the head and tail pointers of the ring buffer.
 */
EXPORT_API const void* ringbuf_tail(const ringbuf rb);

EXPORT_API const void* ringbuf_head(const ringbuf rb);

/*
 * Locate the first occurrence of character c (converted to an
 * unsigned char) in ring buffer rb, beginning the search at offset
 * bytes from the ring buffer's tail pointer. The function returns the
 * offset of the character from the ring buffer's tail pointer, if
 * found. If c does not occur in the ring buffer, the function returns
 * the number of bytes used in the ring buffer.
 *
 * Note that the offset parameter and the returned offset are logical
 * offsets from the tail pointer, not necessarily linear offsets.
 */
EXPORT_API size_t ringbuf_findchr(const ringbuf rb, int c, size_t offset);

/*
 * Beginning at ring buffer dst's head pointer, fill the ring buffer
 * with a repeating sequence of len bytes, each of value c (converted
 * to an unsigned char). len can be as large as you like, but the
 * function will never write more than ringbuf_buffer_size(dst) bytes
 * in a single invocation, since that size will cause all bytes in the
 * ring buffer to be written exactly once each.
 *
 * Note that if len is greater than the number of free bytes in dst,
 * the ring buffer will overflow. When an overflow occurs, the state
 * of the ring buffer is guaranteed to be consistent, including the
 * head and tail pointers; old data will simply be overwritten in FIFO
 * fashion, as needed. However, note that, if calling the function
 * results in an overflow, the value of the ring buffer's tail pointer
 * may be different than it was before the function was called.
 *
 * Returns the actual number of bytes written to dst: len, if
 * len < ringbuf_buffer_size(dst), else ringbuf_buffer_size(dst).
 */
EXPORT_API size_t ringbuf_memset(ringbuf dst, int c, size_t len);

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
EXPORT_API void* ringbuf_memcpy_into(ringbuf dst, const void *src, size_t count);

/*
 * This convenience function calls read(2) on the file descriptor fd,
 * using the ring buffer rb as the destination buffer for the read,
 * and returns the value returned by read(2). It will only call
 * read(2) once, and may return a short count.
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
EXPORT_API ssize_t ringbuf_read(int fd, ringbuf rb, size_t count);

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
EXPORT_API void * ringbuf_memcpy_from(void *dst, ringbuf src, size_t count);

/*
 * This convenience function calls write(2) on the file descriptor fd,
 * using the ring buffer rb as the source buffer for writing (starting
 * at the ring buffer's tail pointer), and returns the value returned
 * by write(2). It will only call write(2) once, and may return a
 * short count.
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
EXPORT_API ssize_t ringbuf_write(int fd, ringbuf rb, size_t count);

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
EXPORT_API void* ringbuf_copy(ringbuf dst, ringbuf src, size_t count);

#endif /* INCLUDED_RINGBUF_H */
