
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _MPS_QUEUE_H_INCLUDED_
#define _MPS_QUEUE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include "mps_slab.h"

typedef struct mps_queue_s  mps_queue_t;

struct mps_queue_s {
    mps_ptroff_t  prev;
    mps_ptroff_t  next;
};

#define mps_queue(pool, offset)   ((mps_queue_t *) mps_ptr(pool, offset))

#define mps_queue_init(pool, q)                                               \
    do {                                                                      \
        (q)->prev = mps_offset((pool), (q));                                  \
        (q)->next = mps_offset((pool), (q));                                  \
    } while (0)


#define mps_queue_empty(pool, h)  ((h) == mps_queue_prev(pool, h))


#define mps_queue_insert_head(pool, h, x)                                     \
    do {                                                                      \
        mps_queue_t *x_next;                                                  \
        (x)->next = (h)->next;                                                \
        x_next = mps_queue((pool), (x)->next);                                \
        x_next->prev = mps_offset((pool), (x));                               \
        (x)->prev = mps_offset((pool), (h));                                  \
        (h)->next = mps_offset((pool), (x));                                  \
    } while (0)


#define mps_queue_insert_after   mps_queue_insert_head


#define mps_queue_insert_tail(pool, h, x)                                     \
    do {                                                                      \
        mps_queue_t *x_prev;                                                  \
        (x)->prev = (h)->prev;                                                \
        x_prev = mps_queue((pool), (x)->prev);                                \
        x_prev->next = mps_offset((pool), (x));                               \
        (x)->next = mps_offset((pool), (h));                                  \
        (h)->prev = mps_offset((pool), (x));                                  \
    } while (0)


#define mps_queue_head(pool, h)                                               \
    mps_queue((pool), (h)->next)


#define mps_queue_last(pool, h)                                               \
    mps_queue((pool), (h)->prev)


#define mps_queue_sentinel(pool, h)                                           \
    (h)


#define mps_queue_next(pool, q)                                               \
    mps_queue((pool), (q)->next)


#define mps_queue_prev(pool, q)                                               \
    mps_queue((pool), (q)->prev)


#if (NGX_DEBUG)

#define mps_queue_remove(pool, x)                                             \
    do {                                                                      \
        mps_queue_t *x_next, *x_prev;                                         \
        x_next = mps_queue((pool), (x)->next);                                \
        x_next->prev = (x)->prev;                                             \
        x_prev = mps_queue((pool), (x)->prev);                                \
        x_prev->next = (x)->next;                                             \
        (x)->prev = mps_nulloff;                                              \
        (x)->next = mps_nulloff;                                              \
    } while (0)

#else

#define mps_queue_remove(pool, x)                                             \
    do {                                                                      \
        mps_queue_t *x_next, *x_prev;                                         \
        x_next = mps_queue((pool), (x)->next);                                \
        x_next->prev = (x)->prev;                                             \
        x_prev = mps_queue((pool), (x)->prev);                                \
        x_prev->next = (x)->next;                                             \
    } while (0)

#endif


#define mps_queue_split(pool, h, q, n)                                        \
    do {                                                                      \
        mps_queue_t *n_prev, *h_prev;                                         \
        (n)->prev = (h)->prev;                                                \
        n_prev = mps_queue((pool), (n)->prev);                                \
        n_prev->next = mps_offset((pool), (n));                               \
        (n)->next = mps_offset((pool), (q));                                  \
        (h)->prev = (q)->prev;                                                \
        h_prev = mps_queue((pool), (h)->prev);                                \
        h_prev->next = mps_offset((pool), (h));                               \
        (q)->prev = mps_offset((pool), (n));                                  \
    } while (0)


#define mps_queue_add(pool, h, n)                                             \
    do {                                                                      \
        mps_queue_t *h_prev, *n_next;                                         \
        h_prev = mps_queue((pool), (h)->prev);                                \
        h_prev->next = (n)->next;                                             \
        n_next = mps_queue((pool), (n)->next);                                \
        n_next->prev = (h)->prev;                                             \
        (h)->prev = (n)->prev;                                                \
        h_prev = mps_queue((pool), (h)->prev);                                \
        h_prev->next = mps_offset((pool), (h));                               \
    } while (0)


#define mps_queue_data(q, type, link)                                         \
    (type *) ((u_char *) q - offsetof(type, link))


#endif /* _MPS_QUEUE_H_INCLUDED_ */
