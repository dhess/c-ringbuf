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
 *
 *
 * Some code was added by Ludwig Jaffe / levush and this code was donated
 * to the public domain at the time of writing it.
 * 
 * Merged in code by github user assp1r1n3 for define switch RINGBUG_NO_ASSERT
 * to kick out unwanted asserts at ease
 */

#include "ringbuf.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/*
 * The #include sys/uio.h is not really needed, but was 
 * added for some reason. So it is kept in the code but 
 * commented out.
*/
//#include <sys/uio.h>

#include <unistd.h>
#include <sys/param.h>


/*
* To remove assert() calls in production code by #define
* remove the // comment following this note
*/
//#define RINGBUF_NO_ASSERT

#ifndef RINGBUF_NO_ASSERT
#include <assert.h>
#endif /* !RINGBUF_NO_ASSERT */

/*
 * The code is written for clarity, not cleverness or performance, and
 * contains many assert()s to enforce invariant assumptions and catch
 * bugs. Feel free to optimize the code and to remove asserts for use
 * in your own projects, once you're comfortable that it functions as
 * intended.
 * Improvements have been added.
 */


/*
* Struct to manage the ringbuffer. One needs a memory range and 
* a struct variable of this struct to manage a ring buffer. 
* The data of the ring buffer is kept in the memory range poninted to
* by uint8_t *buf, while head and tail point to the rolling start and
* end of the ring buffer, while size knows how many elements are in
* the ring buffer
*/

struct ringbuf_t
{
    uint8_t *buf;
    uint8_t *head, *tail;
    size_t size;
};


/*
* @brief Allocate new memory for a ringbuffer structure and create it as a ringbuffer.
*
* This is to allocate a new memory area and produce a struct for management of it.
* This is intended for computers with dynamic memory management (malloc, free).
* @return: returns a ringbuffer_t struct in case of successfull malloc, if the malloc fails,
* it frees allocated memory and returns 0
* The ringbuffer is reset in order to initialize it to be empty. 
*/
ringbuf_t ringbufNew(size_t capacity)
{
    ringbuf_t rb = malloc(sizeof(struct ringbuf_t));
    if (rb) {

        /* One byte is used for detecting the full condition and to keep distance. */
        rb->size = capacity + 1;  //distance of one byte to keep distance from overrun
        rb->buf = malloc(rb->size);
        if (rb->buf)
            ringbufReset(rb);
        else {
            free(rb);
            return 0;
        }
    }
    return rb;
}

/*
* @brief Bind existing memory to a ringbuffer structure and make it a ringbuffer.
*
* This is to bind an existing memory area to a ringbuffer struct for management.
* This is intended for microcontrollers w/o malloc and with the need for well controlled
* memory management as the memory is assigned at linking time so we have enough or a linking error.
* One can expoit this to create a ringbuffer at existing memory buffers, for example dma memory
* areas. So dirty coding can use a dma finished callback and call this routine to bind a ring buffer
* to the transferred half of the dma memory area but beware of the buffer pointers and threads,
* know what you code.  
* @note: This bind assumes that the memory buffer is empty so the pointers are set up to show
* an empty ring buffer. If you do dirty hacks here, set the pointers for your self after binding
* the ringbuffer as you should know where the buffer ends and where useful data starts. Just
* bend ->head and -> tail accordingly. 
*/
ringbuf_t ringbufBind(ringbuf_t ringbuffer, uint8_t *bufferAddress) {
	size_t realcap=0;
	size_t usercap=0; //user cap is one byte smaller as I fear buffer overruns
	
	if (bufferAddress!=NULL) {
		ringbuffer->buf=bufferAddress;
		ringbuffer->head=bufferAddress;
		ringbuffer->tail=bufferAddress;
		
		//calculate size with the byte stolen for safety
		realcap=sizeof(bufferAddress);
		if (realcap) usercap=realcap-1;  // no negative
		else usercap=0; //not enough space
		ringbuffer->size=usercap;
	}
	else	{
		ringbuffer->buf=NULL;
		ringbuffer->head=NULL;
		ringbuffer->tail=NULL;
		ringbuffer->size=0;
	}
	return ringbuffer;
}	
		
		

size_t ringbufBufferSize(const struct ringbuf_t *rb)
{
    return rb->size;
}

void ringbufReset(ringbuf_t rb)
{
    rb->head = rb->tail = rb->buf;
}

void ringbufFree(ringbuf_t *rb)
{
    #ifndef RINGBUF_NO_ASSERT
    assert(rb && *rb);
    #endif /* !RINGBUF_NO_ASSERT */
    free((*rb)->buf);
    free(*rb);
    *rb = 0;
}

size_t ringbufCapacity(const struct ringbuf_t *rb)
{
    return ringbufBufferSize(rb) - 1;
}

/*
 * Return a pointer to one-past-the-end of the ring buffer's
 * contiguous buffer. You shouldn't normally need to use this function
 * unless you're writing a new ringbuf_* function.
 */
static const uint8_t *ringbufEnd(const struct ringbuf_t *rb)
{
    return rb->buf + ringbufBufferSize(rb);
}

size_t ringbufBytesFree(const struct ringbuf_t *rb)
{
    ssize_t s = rb->head - rb->tail;
    if (s >= 0)
        return ringbuf_capacity(rb) - s;
    else
        return -s - 1;
}

size_t
ringbufBytesUsed(const struct ringbuf_t *rb)
{
    return ringbufCapacity(rb) - ringbuf_bytes_free(rb);
}

int ringbufIsFull(const struct ringbuf_t *rb)
{
    return ringbufBytesFree(rb) == 0;
}

int ringbufIsEmpty(const struct ringbuf_t *rb)
{
    return ringbufBytesFree(rb) == ringbuf_capacity(rb);
}

const void *ringbufTail(const struct ringbuf_t *rb)
{
    return rb->tail;
}

const void *ringbufHead(const struct ringbuf_t *rb)
{
    return rb->head;
}

/*
 * Given a ring buffer rb and a pointer to a location within its
 * contiguous buffer, return the a pointer to the next logical
 * location in the ring buffer.
 */
static uint8_t *ringbufNextp(ringbuf_t rb, const uint8_t *p)
{
    /*
     * The assert guarantees the expression (++p - rb->buf) is
     * non-negative; therefore, the modulus operation is safe and
     * portable.
     */
    #ifndef RINGBUF_NO_ASSERT
    assert((p >= rb->buf) && (p < ringbuf_end(rb)));
    #endif /* !RINGBUF_NO_ASSERT */
    return rb->buf + ((++p - rb->buf) % ringbufBufferSize(rb));
}

size_t ringbufFindChr(const struct ringbuf_t *rb, int c, size_t offset)
{
    const uint8_t *bufend = ringbuf_end(rb);
    size_t bytes_used = ringbuf_bytes_used(rb);
    if (offset >= bytes_used)
        return bytes_used;

    const uint8_t *start = rb->buf +
        (((rb->tail - rb->buf) + offset) % ringbufBufferSize(rb));
    #ifndef RINGBUF_NO_ASSERT
    assert(bufend > start);
    #endif /* !RINGBUF_NO_ASSERT */
    size_t n = MIN(bufend - start, bytes_used - offset);
    const uint8_t *found = memchr(start, c, n);
    if (found)
        return offset + (found - start);
    else
        return ringbuf_findchr(rb, c, offset + n);
}

size_t ringbufMemset(ringbuf_t dst, int c, size_t len)
{
    const uint8_t *bufend = ringbuf_end(dst);
    size_t nwritten = 0;
    size_t count = MIN(len, ringbufBufferSize(dst));
    int overflow = count > ringbuf_bytes_free(dst);

    while (nwritten != count) {

        /* don't copy beyond the end of the buffer */
        #ifndef RINGBUF_NO_ASSERT
        assert(bufend > dst->head);
        #endif /* !RINGBUF_NO_ASSERT */
        size_t n = MIN(bufend - dst->head, count - nwritten);
        memset(dst->head, c, n);
        dst->head += n;
        nwritten += n;

        /* wrap? */
        if (dst->head == bufend)
            dst->head = dst->buf;
    }

    if (overflow) {
        dst->tail = ringbuf_nextp(dst, dst->head);
        #ifndef RINGBUF_NO_ASSERT
        assert(ringbuf_is_full(dst));
        #endif /* !RINGBUF_NO_ASSERT */
    }

    return nwritten;
}

void *ringbufMemcpyInto(ringbuf_t dst, const void *src, size_t count)
{
    const uint8_t *u8src = src;
    const uint8_t *bufend = ringbuf_end(dst);
    int overflow = count > ringbuf_bytes_free(dst);
    size_t nread = 0;

    while (nread != count) {
        /* don't copy beyond the end of the buffer */
        #ifndef RINGBUF_NO_ASSERT
        assert(bufend > dst->head);
        #endif /* !RINGBUF_NO_ASSERT */
        size_t n = MIN(bufend - dst->head, count - nread);
        memcpy(dst->head, u8src + nread, n);
        dst->head += n;
        nread += n;

        /* wrap? */
        if (dst->head == bufend)
            dst->head = dst->buf;
    }

    if (overflow) {
        dst->tail = ringbuf_nextp(dst, dst->head);
        #ifndef RINGBUF_NO_ASSERT
        assert(ringbuf_is_full(dst));
        #endif /* !RINGBUF_NO_ASSERT */
    }

    return dst->head;
}

ssize_t ringbufRead(int fd, ringbuf_t rb, size_t count)
{
    const uint8_t *bufend = ringbuf_end(rb);
    size_t nfree = ringbuf_bytes_free(rb);

    /* don't write beyond the end of the buffer */
    #ifndef RINGBUF_NO_ASSERT
    assert(bufend > rb->head);
    #endif /* !RINGBUF_NO_ASSERT */
    count = MIN(bufend - rb->head, count);
    ssize_t n = read(fd, rb->head, count);
    if (n > 0) {
        #ifndef RINGBUF_NO_ASSERT
        assert(rb->head + n <= bufend);
        #endif /* !RINGBUF_NO_ASSERT */
        rb->head += n;

        /* wrap? */
        if (rb->head == bufend)
            rb->head = rb->buf;

        /* fix up the tail pointer if an overflow occurred */
        if (n > nfree) {
            rb->tail = ringbuf_nextp(rb, rb->head);
            #ifndef RINGBUF_NO_ASSERT
            assert(ringbuf_is_full(rb));
            #endif /* !RINGBUF_NO_ASSERT */
        }
    }

    return n;
}

void *ringbufMemcpyFrom(void *dst, ringbuf_t src, size_t count)
{
    size_t bytes_used = ringbuf_bytes_used(src);
    if (count > bytes_used)
        return 0;

    uint8_t *u8dst = dst;
    const uint8_t *bufend = ringbuf_end(src);
    size_t nwritten = 0;
    while (nwritten != count) {
        #ifndef RINGBUF_NO_ASSERT
        assert(bufend > src->tail);
        #endif /* !RINGBUF_NO_ASSERT */
        size_t n = MIN(bufend - src->tail, count - nwritten);
        memcpy(u8dst + nwritten, src->tail, n);
        src->tail += n;
        nwritten += n;

        /* wrap ? */
        if (src->tail == bufend)
            src->tail = src->buf;
    }
    #ifndef RINGBUF_NO_ASSERT
    assert(count + ringbuf_bytes_used(src) == bytes_used);
    #endif /* !RINGBUF_NO_ASSERT */
    return src->tail;
}

ssize_t ringbufWrite(int fd, ringbuf_t rb, size_t count)
{
    size_t bytes_used = ringbuf_bytes_used(rb);
    if (count > bytes_used)
        return 0;

    const uint8_t *bufend = ringbuf_end(rb);
    #ifndef RINGBUF_NO_ASSERT
    assert(bufend > rb->head);
    #endif /* !RINGBUF_NO_ASSERT */
    count = MIN(bufend - rb->tail, count);
    ssize_t n = write(fd, rb->tail, count);
    if (n > 0) {
        #ifndef RINGBUF_NO_ASSERT
        assert(rb->tail + n <= bufend);
        #endif /* !RINGBUF_NO_ASSERT */
        rb->tail += n;

        /* wrap? */
        if (rb->tail == bufend)
            rb->tail = rb->buf;
        #ifndef RINGBUF_NO_ASSERT
        assert(n + ringbuf_bytes_used(rb) == bytes_used);
        #endif /* !RINGBUF_NO_ASSERT */
    }

    return n;
}

void *ringbufCopy(ringbuf_t dst, ringbuf_t src, size_t count)
{
    size_t src_bytes_used = ringbuf_bytes_used(src);
    if (count > src_bytes_used)
        return 0;
    int overflow = count > ringbuf_bytes_free(dst);

    const uint8_t *src_bufend = ringbuf_end(src);
    const uint8_t *dst_bufend = ringbuf_end(dst);
    size_t ncopied = 0;
    while (ncopied != count) {
        #ifndef RINGBUF_NO_ASSERT
        assert(src_bufend > src->tail);
        #endif /* !RINGBUF_NO_ASSERT */
        size_t nsrc = MIN(src_bufend - src->tail, count - ncopied);
        #ifndef RINGBUF_NO_ASSERT
        assert(dst_bufend > dst->head);
        #endif /* !RINGBUF_NO_ASSERT */
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
    #ifndef RINGBUF_NO_ASSERT
    assert(count + ringbuf_bytes_used(src) == src_bytes_used);
    #endif /* !RINGBUF_NO_ASSERT */
    if (overflow) {
        dst->tail = ringbuf_nextp(dst, dst->head);
        #ifndef RINGBUF_NO_ASSERT
        assert(ringbuf_is_full(dst));
        #endif /* !RINGBUF_NO_ASSERT */
    }

    return dst->head;
}

size_t min(size_t a, size_t b) {
    if(a<b)
        return a;
    else
        return b;
}

//additions taken from fork of zipper97412/c-ringbuf
/*poke some data from buffer src starting from offset into ringbuffer*/
int ringbufMemwrite(ringbuf_t rb, size_t* src, size_t offset, size_t len) 
{
    src = &(src[offset]);
    size_t nbToWrite = min(ringbuf_bytes_free(rb), len);
    ringbuf_memcpy_into(rb, (const void*)src, nbToWrite);
    return nbToWrite;
}

//additions taken from fork of zipper97412/c-ringbuf
/*peek some data from ringbuffer into buffer dst starting at offset.
 *Use this to combine strings in a destination buffer.
 */
int ringbufMemread(ringbuf_t rb, size_t* dst, size_t offset, size_t len) 
{
    dst = &(dst[offset]);
    size_t nbToRead = min(ringbuf_bytes_used(rb), len);
    ringbuf_memcpy_from((void*)dst, rb, nbToRead);
    return nbToRead;
}


/*
 *  D M A
 *  to be done ...
 */

/*
 * Read data from peripheral to ringbuffer using DMA
 * How to fill the ringbuffer using DMA:
 * In order to fill the ringbuffer using a DMA it is wise to use small increments on the DMA-controller
 * so the CPU gets some time to breathe and the foreground task that serves the other end of the ringbuffer
 * can do its part.
 *
 * The following strategy can be used:
 * Lets say the DMA controller is set-up to transfer 16Byte at a time from an UART to the ringbuffer.
 * We have a call back for DMA-Transfer-Half-Complete and DMA-Transfer-Complete.
 * 
 * The strategy is to let the DMA climb the memory area underlying the ringbuffer if this area is not 
 * occupied by valid data to be read of the ring buffer.
 * At the call back DMA-complete and DMA-half-complete we set the write-pointer which is head of the ringbuffer
 * to the new end of valid data. Then we look for the tail pointer if it points to a memory patch that will
 * be overwritten by the next DMA transfer. If the tail ponter points out of our way, we initiate the next
 * DMA transfer with the next memory patch, which climed the buffer.
 * As we should use dubble buffering with DMA we prepare the patch+1 while we wait for patch. 
 *

 * +---------------------------------------------------+ -> callback DMA complete -> set Head (rb->head) to here
 * !  3rd DMA BLOCK: Top Half                          !
 * !                                                   !
 * !                                                   !
 * !                                                   !
 * +- - - - - - - - - - - - - - - - - - - - - - - - - -+ -> callback DMA-Half complete -> set Head (rb->head) to here
 * !  3rd DMA BLOCK: Bottom Half                       !
 * !                                                   !    and enable DMA only if rb->tail does not point to the memory area to be 
 * !                                                   !    filled by the next DMA
 * !                                                   !
 * +---------------------------------------------------+ -> callback DMA complete -> set Head (rb->head) to here
 * !  2nd DMA BLOCK: Top Half                          !
 * !                                                   !
 * !                                                   !
 * !                                                   !
 * +- - - - - - - - - - - - - - - - - - - - - - - - - -+ -> callback DMA-Half complete -> set Head (rb->head) to here
 * !  2nd DMA BLOCK: Bottom Half                       !    and prepare DMA (set up DMA-controller for new addresses) for 2nd Block
 * !                                                   !    and enable DMA only if rb->tail does not point to the memory area to be 
 * !                                                   !    filled by the next DMA
 * !                                                   !
 * +---------------------------------------------------+ -> callback DMA complete -> set Head (rb->head) to here
 * !  1st DMA BLOCK: Top Half                          !
 * !                                                   !
 * !                                                   !
 * !                                                   !
 * +- - - - - - - - - - - - - - - - - - - - - - - - - -+ -> callback DMA-Half complete -> set Head (rb->head) to here
 * !  1st DMA BLOCK: Bottom Half                       !    and prepare DMA (set up DMA-controller for new addresses) for 2nd Block
 * !                                                   !    and enable DMA only if rb->tail does not point to the memory area to be 
 * !                                                   !    filled by the next DMA
 * !                                                   !
 * +---------------------------------------------------+<---Head (rb->head)
 * !  old data that was transfered using DMA           !
 * !                                                   !<---Tail (rb->tail)  ^ 
 * !                                                   !                     !
 * !                                                   !
 * +- - - - - - - - - - - - - - - - - - - - - - - - - -+ 
 * !                                                   !
 * !                                                   !
 * !                                                   !
 * !                                                   !
 * +---------------------------------------------------+
 *
 *
 *
 *
 */
void ringbufDMArx(ringbuf_t rb) 
{
	
}


/*
 * Send data from ringbuffer to peripheral using DMA
 */
void ringbufDMAtx(ringbuf_t rb) 
{
	
}



