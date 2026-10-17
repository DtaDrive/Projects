
#include <stdio.h>
#ifndef _CHUNK_BASE_H_
#define _CHUNK_BASE_H_

#pragma once

#include <stdbool.h>
#include <unistd.h>


typedef struct Header *Header_T;
typedef struct Footer *Footer_T;
/* Status flags */
enum {
    CHUNK_FREE = 0,
    CHUNK_USED = 1,
};

/* Chunk unit size (bytes). This equals sizeof(struct Chunk) in this baseline. */
enum {
    CHUNK_UNIT = 16,
};

/* ----------------------- Getters / Setters ------------------------ */

/* chunk_get_status / chunk_set_status:
 * Get/Set the status of a block (CHUNK_FREE or CHUNK_USED). */
int   chunk_get_status(Header_T c);
void  chunk_set_status(Header_T c, int status);

/* chunk_get_span_units / chunk_set_span_units:
 * Get/Set the total number of units of the block, including its header & footer.
 * (span = 1 header unit + payload units + 1 footer unit) */
int chunk_get_span_units(Header_T h);
void  chunk_set_span_units(Header_T h, int span_units);

/* chunk_get_next_free / chunk_set_next_free:
 * Get/Set the next pointer in the singly-linked free list. */
Header_T chunk_get_next_free(Header_T c);
void    chunk_set_next_free(Header_T c, Header_T next);

Header_T chunk_get_prev_free(Footer_T c);
void    chunk_set_prev_free(Footer_T c, Header_T prev);

Footer_T chunk_get_footer(Header_T h);
Header_T chunk_get_header(Footer_T f);


/* chunk_get_adjacent:
 * Return the physically next adjacent block's header (if any) by walking
 * 'span' units forward from 'c'. Returns NULL if 'c' is the last block.
 * 'start' and 'end' are the inclusive start and exclusive end addresses
 * of the heap region. */
Header_T chunk_get_adjacent_next(Header_T c, void *start, void *end);
Header_T chunk_get_adjacent_prev(Header_T c, void *start, void *end);

/* Debug-only sanity check (compiled only if NDEBUG is not defined). */
#ifndef NDEBUG

/* chunk_is_valid:
 * Return 1 iff 'c' lies within [start, end) and has a positive span. */
int   chunk_is_valid(Header_T c, void *start, void *end);

#endif

#endif /* _CHUNK_BASE_H_ */
