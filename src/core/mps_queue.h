
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef _MPS_QUEUE_H_INCLUDED_
#define _MPS_QUEUE_H_INCLUDED_


typedef struct mps_queue_s  mps_queue_t;

struct mps_queue_s {
    mps_queue_t  *prev;
    mps_queue_t  *next;
};


#define mps_queue_init(q)                                                     \
    (q)->prev = q;                                                            \
    (q)->next = q


#define mps_queue_empty(h)                                                    \
    (h == (h)->prev)


#define mps_queue_insert_head(h, x)                                           \
    (x)->next = (h)->next;                                                    \
    (x)->next->prev = x;                                                      \
    (x)->prev = h;                                                            \
    (h)->next = x


#define mps_queue_insert_after   mps_queue_insert_head


#define mps_queue_insert_tail(h, x)                                           \
    (x)->prev = (h)->prev;                                                    \
    (x)->prev->next = x;                                                      \
    (x)->next = h;                                                            \
    (h)->prev = x


#define mps_queue_head(h)                                                     \
    (h)->next


#define mps_queue_last(h)                                                     \
    (h)->prev


#define mps_queue_sentinel(h)                                                 \
    (h)


#define mps_queue_next(q)                                                     \
    (q)->next


#define mps_queue_prev(q)                                                     \
    (q)->prev


#if (NGX_DEBUG)

#define mps_queue_remove(x)                                                   \
    (x)->next->prev = (x)->prev;                                              \
    (x)->prev->next = (x)->next;                                              \
    (x)->prev = NULL;                                                         \
    (x)->next = NULL

#else

#define mps_queue_remove(x)                                                   \
    (x)->next->prev = (x)->prev;                                              \
    (x)->prev->next = (x)->next

#endif


#define mps_queue_split(h, q, n)                                              \
    (n)->prev = (h)->prev;                                                    \
    (n)->prev->next = n;                                                      \
    (n)->next = q;                                                            \
    (h)->prev = (q)->prev;                                                    \
    (h)->prev->next = h;                                                      \
    (q)->prev = n;


#define mps_queue_add(h, n)                                                   \
    (h)->prev->next = (n)->next;                                              \
    (n)->next->prev = (h)->prev;                                              \
    (h)->prev = (n)->prev;                                                    \
    (h)->prev->next = h;


#define mps_queue_data(q, type, link)                                         \
    (type *) ((u_char *) q - offsetof(type, link))


mps_queue_t *mps_queue_middle(mps_queue_t *queue);
void mps_queue_sort(mps_queue_t *queue,
    ngx_int_t (*cmp)(const mps_queue_t *, const mps_queue_t *));


#endif /* _MPS_QUEUE_H_INCLUDED_ */
