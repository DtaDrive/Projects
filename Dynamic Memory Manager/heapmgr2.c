#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "chunk.h" 

#define FALSE 0
#define TRUE  1

/* Minimum number of *payload* units to request on heap growth.
 * (The actual request adds 1 header unit on top.) */
enum { SYS_MIN_ALLOC_UNITS = 1024 };

/* Heap bounds: [s_heap_lo, s_heap_hi).
 * s_heap_hi moves forward whenever the heap grows. */
static void *s_heap_lo = NULL, *s_heap_hi = NULL;

#define BIN_COUNT 16

/* Bins instead of single free list */
static Header_T s_bins[BIN_COUNT];


static int get_bin_index(size_t span)
{
    int index = 0;
    span >>= 1;
    while (span && index < BIN_COUNT - 1) {
        span >>= 1;
        index++;
    }
    return index;
}

/*--------------------------------------------------------------------*/
/* check_heap_validity
 *
 * Lightweight integrity checks for the entire heap and the free list.
 * This is a *basic* sanity check. Passing it does not prove correctness
 * of all invariants, but it helps catch common structural bugs.
 */
#ifndef NDEBUG
static int
check_heap_validity(void)
{
    Header_T w;

    if (s_heap_lo == NULL) {fprintf(stderr, "Uninitialized heap start\n"); return FALSE;}
    if (s_heap_hi == NULL) { fprintf(stderr, "Uninitialized heap end\n"); return FALSE;}

    if (s_heap_lo == s_heap_hi) {
        for (int i = 0; i < BIN_COUNT; i++) {
            if (s_bins[i] != NULL) {
                fprintf(stderr, "Non-empty bin in empty heap\n");
                return FALSE;
            }
        }
        return TRUE;
    }

    for (w = (Header_T)s_heap_lo;
         w && (void *)w < s_heap_hi;
         w = chunk_get_adjacent_next(w, s_heap_lo, s_heap_hi)) {

        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi))
            return FALSE;
    }

    for (int i = 0; i < BIN_COUNT; i++) {

        for (w = s_bins[i]; w != NULL; w = chunk_get_next_free(w)) {

            if (chunk_get_status(w) != CHUNK_FREE) {
                fprintf(stderr, "Non-free chunk in bin\n");
                return FALSE;
            }

            if (!chunk_is_valid(w, s_heap_lo, s_heap_hi))
                return FALSE;

            // prev pointer consistency
            Header_T prev =
                chunk_get_prev_free(chunk_get_footer(w));

            if (prev != NULL &&
                chunk_get_next_free(prev) != w) {
                fprintf(stderr, "Broken link in bin\n");
                return FALSE;
            }

            // adjacency check
            Header_T n = chunk_get_adjacent_next(w, s_heap_lo, s_heap_hi);

            if (n != NULL && chunk_get_status(n) == CHUNK_FREE) {
                fprintf(stderr, "Uncoalesced free chunks\n");
                return FALSE;
            }
        }
    }

    return TRUE;
}
#endif

/*--------------------------------------------------------------------*/
/* bytes_to_payload_units
 *
 * Convert a byte count to the number of *payload* units, rounding up
 * to the nearest multiple of CHUNK_UNIT. The result does not include
 * the header unit. */
static size_t
bytes_to_payload_units(size_t bytes)
{
    return (bytes + (CHUNK_UNIT - 1)) / CHUNK_UNIT;
}

/*--------------------------------------------------------------------*/
/* header_from_payload
 *
 * Map a client data pointer back to its block header pointer by
 * stepping one header unit backward. */
static Header_T
header_from_payload(void *p)
{
    return (Header_T)((char *)p - CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* heap_bootstrap
 *
 * Initialize heap bounds using sbrk(0). Must be called exactly once
 * before any allocation occurs. Exits the process on fatal failure. */
static void
heap_bootstrap(void)
{
    s_heap_lo = s_heap_hi = sbrk(0);
    if (s_heap_lo == (void *)-1) {
        fprintf(stderr, "sbrk(0) failed\n");
        exit(-1);
    }

    for (int i = 0; i < BIN_COUNT; i++)
        s_bins[i] = NULL;
}

/*--------------------------------------------------------------------*/
/* coalesce_two
 *
 * Given two *adjacent* free blocks a and b (a < b), merge them into a.
 * The new span of 'a' becomes span(a) + span(b). The free-list link of
 * 'a' is updated to skip 'b'. Returns the merged block 'a'. */
static Header_T
coalesce_two(Header_T a, Header_T b)
{
    chunk_set_span_units(a, chunk_get_span_units(a) + chunk_get_span_units(b));
    chunk_set_next_free(b, NULL);
    chunk_set_prev_free(chunk_get_footer(b), NULL);
    return a;
}

/* ------------------------------------------------------------ 
    Pushes a header into the bin 
*/
static void bin_push(Header_T c)
{
    size_t span = chunk_get_span_units(c);
    int bin = get_bin_index(span);

    Header_T head = s_bins[bin];

    chunk_set_next_free(c, head);
    chunk_set_prev_free(chunk_get_footer(c), NULL);

    if (head != NULL) {
        chunk_set_prev_free(chunk_get_footer(head), c);
    }

    s_bins[bin] = c;
}

/* ------------------------------------------------------------ */
static void bin_detach(Header_T c)
{
    size_t span = chunk_get_span_units(c);
    int bin = get_bin_index(span);

    Header_T prev = chunk_get_prev_free(chunk_get_footer(c));
    Header_T next = chunk_get_next_free(c);

    if (prev != NULL)
        chunk_set_next_free(prev, next);
    else
        s_bins[bin] = next;

    if (next != NULL)
        chunk_set_prev_free(chunk_get_footer(next), prev);

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(chunk_get_footer(c), NULL);
}

/*--------------------------------------------------------------------*/
/* split_for_alloc
 *
 * Split a free block 'c' into:
 *   - a smaller *leading* free block of span 'remain_span'
 *   - a *trailing* allocated block 'alloc' of span 'alloc_span'
*/
static Header_T
split_for_alloc(Header_T c, size_t need_units)
{
    int old_span    = chunk_get_span_units(c);
    int alloc_span  = 1 + (int)need_units + 1;
    int remain_span = old_span - alloc_span;

    assert(remain_span >= 2);

    /* IMPORTANT: ensure correct state */
    chunk_set_status(c, CHUNK_FREE);
    chunk_set_span_units(c, remain_span);

    Header_T alloc =
        (Header_T)((char *)c + (size_t)remain_span * CHUNK_UNIT);

    chunk_set_span_units(alloc, alloc_span);
    chunk_set_status(alloc, CHUNK_USED);

    chunk_set_next_free(alloc, NULL);
    chunk_set_prev_free(chunk_get_footer(alloc), NULL);

    bin_push(c);

    return alloc;
}


/*--------------------------------------------------------------------*/
/* sys_grow_and_link
 *
 * Request more memory from the system via sbrk() and link the new
 * block into the free list (coalescing when possible).
 */
static Header_T
sys_grow_and_link(size_t need_units)
{
    Header_T c;
    size_t grow_data = (need_units < SYS_MIN_ALLOC_UNITS) ? SYS_MIN_ALLOC_UNITS : need_units;
    size_t grow_span = 1 + grow_data + 1;  /* header + payload units + footer */

    c = (Header_T)sbrk(grow_span * CHUNK_UNIT);
    if (c == (Header_T)-1)
        return NULL;

    s_heap_hi = sbrk(0);

    chunk_set_span_units(c, (int)grow_span);
    chunk_set_status(c, CHUNK_FREE);

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(chunk_get_footer(c), NULL);

    Header_T prev_phys = NULL;
    if ((void *)c > s_heap_lo) {
        prev_phys = chunk_get_adjacent_prev(c, s_heap_lo, s_heap_hi);
    }

    if (prev_phys && chunk_get_status(prev_phys) == CHUNK_FREE) {
        bin_detach(prev_phys);
        c = coalesce_two(prev_phys, c);
    }

    bin_push(c);

    assert(check_heap_validity());
    return c;
}


/*--------------------------------------------------------------------*/
/* heapmgr_malloc
 *
 * Allocate a block capable of holding 'size' bytes. Zero bytes returns
 * NULL. The allocated region is *uninitialized*. Strategy:
*/
void *
heapmgr_malloc(size_t size)
{
   static int booted = FALSE;
    size_t need_units;

    if (size == 0)
        return NULL;

    if (!booted) {
        heap_bootstrap();
        booted = TRUE;
    }

    assert(check_heap_validity());

    need_units = bytes_to_payload_units(size);
    size_t alloc_span = 1 + need_units + 1;

    int start_bin = get_bin_index(alloc_span);

    
    for (int i = start_bin; i < BIN_COUNT; i++) {
        Header_T cur = s_bins[i];

        while (cur != NULL) {
            Header_T next = chunk_get_next_free(cur);
            size_t span = chunk_get_span_units(cur);

            if (span >= alloc_span) {
                
                Header_T prev = chunk_get_prev_free(chunk_get_footer(cur));

                if (prev != NULL)
                    chunk_set_next_free(prev, next);
                else
                    s_bins[i] = next;

                if (next != NULL)
                    chunk_set_prev_free(chunk_get_footer(next), prev);

                chunk_set_next_free(cur, NULL);
                chunk_set_prev_free(chunk_get_footer(cur), NULL);

                
                if (span >= alloc_span + 2) {
                    cur = split_for_alloc(cur, need_units);
                } else {
                    chunk_set_status(cur, CHUNK_USED);
                }

                assert(check_heap_validity());
                return (char *)cur + CHUNK_UNIT;
            }

            cur = next;
        }
    }

    // Grow heap
    Header_T c = sys_grow_and_link(need_units);
    if (c == NULL)
        return NULL;

    assert(check_heap_validity());

    start_bin = get_bin_index(alloc_span);

    for (int i = start_bin; i < BIN_COUNT; i++) {
        Header_T cur = s_bins[i];

        while (cur != NULL) {
            Header_T next = chunk_get_next_free(cur);
            size_t span = chunk_get_span_units(cur);

            if (span >= alloc_span) {
                Header_T prev = chunk_get_prev_free(chunk_get_footer(cur));

                if (prev != NULL)
                    chunk_set_next_free(prev, next);
                else
                    s_bins[i] = next;

                if (next != NULL)
                    chunk_set_prev_free(chunk_get_footer(next), prev);

                chunk_set_next_free(cur, NULL);
                chunk_set_prev_free(chunk_get_footer(cur), NULL);

                if (span >= alloc_span + 2)
                    cur = split_for_alloc(cur, need_units);
                else
                    chunk_set_status(cur, CHUNK_USED);

                assert(check_heap_validity());
                return (char *)cur + CHUNK_UNIT;
            }
            cur = next;
        }
    }

    return NULL;
    
}

/*--------------------------------------------------------------------*/
/* heapmgr_free
 *
 * Free a previously allocated block pointed to by 'p'. If 'p' is NULL,
 * do nothing. The pointer must have been returned by heapmgr_malloc().
 */
void heapmgr_free(void *p)
{
    if (p == NULL)
        return;

    assert(check_heap_validity());

    Header_T c = header_from_payload(p);

    // sanity check
    assert(c >= (Header_T)s_heap_lo && (void *)c < s_heap_hi);
    assert(chunk_get_status(c) == CHUNK_USED);

    chunk_set_status(c, CHUNK_FREE);

    Header_T prev = chunk_get_adjacent_prev(c, s_heap_lo, s_heap_hi);
    if (prev != NULL && chunk_get_status(prev) == CHUNK_FREE) {
        bin_detach(prev);
        c = coalesce_two(prev, c);
    }

    Header_T next = chunk_get_adjacent_next(c, s_heap_lo, s_heap_hi);
    if (next != NULL && chunk_get_status(next) == CHUNK_FREE) {
        bin_detach(next);
        c = coalesce_two(c, next);
    }

    bin_push(c);

    assert(check_heap_validity());

}