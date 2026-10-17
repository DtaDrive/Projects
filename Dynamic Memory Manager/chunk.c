#include <stdio.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include "chunk.h"

/* Internal header and footer layout
 * - status: CHUNK_FREE or CHUNK_USED
 * - span:   total units, including the header itself
 * - next:   next-free pointer for the doubly-linked free list
 * - prev:   prev-free pointer for the doubly-linked free list
 */
struct Header {
    int     status;
    int     span;
    Header_T next;
};

struct Footer
{
    int span;
    int padding;
    Header_T prev;
};




/* ----------------------- Getters / Setters ------------------------ */
Footer_T chunk_get_footer(Header_T h)             { return (Footer_T)((char *)h + h->span * CHUNK_UNIT - sizeof(struct Footer));}

Header_T chunk_get_header(Footer_T f)             { return (Header_T)((char *) f + sizeof(struct Footer) - f->span * CHUNK_UNIT);}

int   chunk_get_status(Header_T c)                 { return c->status; }
void  chunk_set_status(Header_T c, int status)     { c->status = status; }

int chunk_get_span_units(Header_T h) {return h->span;}
void  chunk_set_span_units(Header_T h, int span_u) { h->span = span_u; chunk_get_footer(h)->span = span_u; }

Header_T chunk_get_next_free(Header_T c)            { return c->next; }
void    chunk_set_next_free(Header_T c, Header_T n) { c->next = n; }

Header_T chunk_get_prev_free(Footer_T c)            { return c->prev; }
void    chunk_set_prev_free(Footer_T c, Header_T n) { c->prev = n; }

/* chunk_get_adjacent_next:
 * Compute the next physical block header by jumping 'span' units
 * (1 header + payload + 1 footer) forward from 'c'. If that address falls at or
 * beyond 'end', return NULL to indicate there is no next block. */
Header_T
chunk_get_adjacent_next(Header_T c, void *start, void *end)
{
    Header_T n;

    assert((void *)c >= start);

    n = c + c->span;    

    if ((void *)n >= end)
        return NULL;

    return n;
}

Header_T
chunk_get_adjacent_prev(Header_T c, void *start, void *end)
{
    if ((void *)c <= start)
        return NULL;

    /* Get footer of previous block */
    Footer_T f = (Footer_T)((char *)c - sizeof(struct Footer));

    /* Compute previous header */
    Header_T p = (Header_T)((char *)c - f->span * CHUNK_UNIT);

    if ((void *)p < start)
        return NULL;

    return p;
}


#ifndef NDEBUG
/* chunk_is_valid:
 * Minimal per-block validity checks used by the heap validator:
 *  - c must lie within [start, end)
 *  - span must be positive (non-zero) */
int
chunk_is_valid(Header_T c, void *start, void *end)
{
    assert(c     != NULL);
    assert(start != NULL);
    assert(end   != NULL);

    if (c < (Header_T)start) { fprintf(stderr, "Bad heap start\n"); return 0; }
    if (c >= (Header_T)end)  { fprintf(stderr, "Bad heap end\n");   return 0; }
    if (c->span <= 0)       { fprintf(stderr, "Non-positive span\n"); return 0; }
    return 1;
}
#endif
