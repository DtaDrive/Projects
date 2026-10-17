/*--------------------------------------------------------------------*/
/* heapmgr1.c                                                      */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "chunk.h" 

#define FALSE 0
#define TRUE  1

/* Minimum number of *payload* units to request on heap growth.
 * (The actual request adds 1 header unit on top.) */
enum { SYS_MIN_ALLOC_UNITS = 1024 };

/* Head of the free list */
static Header_T s_free_head = NULL;

/* Heap bounds: [s_heap_lo, s_heap_hi).
 * s_heap_hi moves forward whenever the heap grows. */
static void *s_heap_lo = NULL, *s_heap_hi = NULL;

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

    if (s_heap_lo == NULL) { fprintf(stderr, "Uninitialized heap start\n"); return FALSE; }
    if (s_heap_hi == NULL) { fprintf(stderr, "Uninitialized heap end\n");   return FALSE; }

    if (s_heap_lo == s_heap_hi) {
        if (s_free_head == NULL) return TRUE;
        fprintf(stderr, "Inconsistent empty heap\n");
        return FALSE;
    }

    /* Walk all physical blocks in address order. */
    for (w = (Header_T)s_heap_lo;
         w && (void *)w < s_heap_hi;
         w = chunk_get_adjacent_next(w, s_heap_lo, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;
    }

    /* Walk the free list; ensure nodes are free and not trivially coalescible. */
    for (w = s_free_head; w; w = chunk_get_next_free(w)) {
        Header_T n;

        if (chunk_get_status(w) != CHUNK_FREE) {
            fprintf(stderr, "Non-free chunk in the free list\n");
            return FALSE;
        }
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;

        n = chunk_get_adjacent_next(w, s_heap_lo, s_heap_hi);
        if (n != NULL && chunk_get_status(n) == CHUNK_FREE) {
            fprintf(stderr, "Uncoalesced adjacent free chunks\n");
            return FALSE;
        }
    }
    return TRUE;
}
#endif /* NDEBUG */

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
   
    return a;
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
    int alloc_span  = (int)(1 + need_units + 1);
    int remain_span = old_span - alloc_span;

    assert(remain_span >= 2);

    /* shrink existing free block (stays in free list) */
    chunk_set_span_units(c, remain_span);
    chunk_set_status(c, CHUNK_FREE);

    chunk_set_prev_free(chunk_get_footer(c), NULL);

    /* allocated block is AFTER it */
    Header_T alloc = chunk_get_adjacent_next(c, s_heap_lo, s_heap_hi);

    chunk_set_span_units(alloc, alloc_span);
    chunk_set_status(alloc, CHUNK_USED);

    chunk_set_next_free(alloc, NULL);
    chunk_set_prev_free(chunk_get_footer(alloc), NULL);

    return alloc;
}


/*--------------------------------------------------------------------*/
/* freelist_push_front
 *
 * Insert a block 'c' at the head of the free list (address-ordered).
 * If the new head is physically adjacent to the previous head,
 * coalesce them immediately to reduce fragmentation. */

static void
freelist_push_front(Header_T c)
{
    assert(chunk_get_span_units(c) >= 2);
    chunk_set_status(c, CHUNK_FREE);

    if (s_free_head == NULL) {
        s_free_head = c;
        chunk_set_next_free(c, NULL);
        chunk_set_prev_free(chunk_get_footer(c), NULL);

    }
    else {
        chunk_set_next_free(c, s_free_head);
        chunk_set_prev_free(chunk_get_footer(c), NULL);
        chunk_set_prev_free(chunk_get_footer(s_free_head), c);

        s_free_head = c;
    }
}
/*--------------------------------------------------------------------*/
/* freelist_insert_sorted
 *
 * Inserts blocks based on adresses. */
static void
freelist_insert_sorted(Header_T c)
{
    assert(chunk_get_span_units(c) >= 2);
     assert(chunk_get_next_free(c) == NULL);           
    assert(chunk_get_prev_free(chunk_get_footer(c)) == NULL); 
    chunk_set_status(c, CHUNK_FREE);

    /* Find first free block with higher address than c. */
    Header_T next = s_free_head;
    while (next != NULL && next < c)
        next = chunk_get_next_free(next);

    Header_T prev = NULL;
    if (next!= NULL)
        prev = chunk_get_prev_free(chunk_get_footer(next));

    chunk_set_next_free(c, next);
    chunk_set_prev_free(chunk_get_footer(c), prev);

    if (prev != NULL)
        chunk_set_next_free(prev, c);
    else
        s_free_head = c;

    if (next != NULL)
        chunk_set_prev_free(chunk_get_footer(next), c);
}


/*--------------------------------------------------------------------*/
/* freelist_detach
 *
 * Remove block 'c' from the free list. If 'c' is the head, 'prev'
 * must be NULL; otherwise 'prev' must precede 'c' in the list.
 * The block is marked as CHUNK_USED afterwards. */
static void
freelist_detach(Header_T c)
{
    assert(chunk_get_status(c) == CHUNK_FREE);

    Header_T prev = chunk_get_prev_free(chunk_get_footer(c));
    Header_T next = chunk_get_next_free(c);

    if(prev!=NULL){
        chunk_set_next_free(prev, next);
    } else{
        s_free_head = next;
    }
    if(next!=NULL){
        chunk_set_prev_free(chunk_get_footer(next), prev);
    }

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(chunk_get_footer(c), NULL);
    chunk_set_status(c, CHUNK_USED);
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
    chunk_set_next_free(c, NULL);
    chunk_set_status(c, CHUNK_USED); 

    Header_T prev_phys = NULL;

    if ((void *)c > s_heap_lo) {
        prev_phys = chunk_get_adjacent_prev(c, s_heap_lo, s_heap_hi);
    }

    if (prev_phys != NULL && chunk_get_status(prev_phys) == CHUNK_FREE) {
        freelist_detach(prev_phys);
        c = coalesce_two(prev_phys, c);

        chunk_set_next_free(c, NULL);
        chunk_set_prev_free(chunk_get_footer(c), NULL);
    }

    freelist_insert_sorted(c);
    
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

    if (!booted) { heap_bootstrap(); booted = TRUE; }

    assert(check_heap_validity());

    need_units = bytes_to_payload_units(size);
  
    Header_T cur = s_free_head;
    size_t alloc_span = 1 + need_units + 1;
    size_t span;

    while (cur != NULL) {
        span = (size_t)chunk_get_span_units(cur);
        if (span >= alloc_span) {
            if (span >= alloc_span + 2)
                cur = split_for_alloc(cur, need_units);
            else
                freelist_detach(cur);

            assert(check_heap_validity());
            return (void *)((char *)cur + CHUNK_UNIT);
        }
        cur = chunk_get_next_free(cur);
    }

    /* Need to grow the heap. */
    cur = sys_grow_and_link(need_units);
    if (cur == NULL) {
        assert(check_heap_validity());
        return NULL;
    }

    size_t total_span = chunk_get_span_units(cur);
    alloc_span = 1 + need_units + 1;

    if (total_span >= alloc_span + 2) {
        cur = split_for_alloc(cur, need_units);
    } else {
        freelist_detach(cur);
    }    

    assert(check_heap_validity());
    return (void *)((char *)cur + CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* heapmgr_free
 *
 * Free a previously allocated block pointed to by 'p'. If 'p' is NULL,
 * do nothing. The pointer must have been returned by heapmgr_malloc().
 */
void
heapmgr_free(void *p)
{
    if (p == NULL){
        return;
    }   

    assert(check_heap_validity());

    Header_T c = header_from_payload(p);

    chunk_set_status(c, CHUNK_FREE);

    Header_T prev = chunk_get_adjacent_prev(c, s_heap_lo, s_heap_hi);
    if (prev != NULL && chunk_get_status(prev) == CHUNK_FREE) {
        freelist_detach(prev);
        c = coalesce_two(prev, c);
    }

    Header_T next = chunk_get_adjacent_next(c, s_heap_lo, s_heap_hi);
    if (next != NULL && chunk_get_status(next) == CHUNK_FREE) {
        freelist_detach(next);
        c = coalesce_two(c, next);          
    }

    if (prev != NULL || next != NULL) {
        freelist_insert_sorted(c);
    } else {
        freelist_push_front(c);
    }

    assert(check_heap_validity());
}
