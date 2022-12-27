
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _MPS_SLAB_H_INCLUDED_
#define _MPS_SLAB_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

extern ngx_uint_t mps_pagesize;

typedef int mps_err_t;
typedef uintptr_t mps_ptroff_t;

typedef struct mps_slab_page_s mps_slab_page_t;

struct mps_slab_page_s {
    uintptr_t slab;
    mps_ptroff_t next;
    mps_ptroff_t prev;
};

typedef struct {
    ngx_uint_t total;
    ngx_uint_t used;

    ngx_uint_t reqs;
    ngx_uint_t fails;
} mps_slab_stat_t;

typedef struct {
    pthread_mutex_t mutex;
    mps_ptroff_t data;

    size_t min_size;
    size_t min_shift;

    mps_ptroff_t pages;
    mps_ptroff_t last;
    mps_slab_page_t free;

    mps_ptroff_t stats;
    ngx_uint_t pfree;

    mps_ptroff_t start;
    mps_ptroff_t end;

    unsigned log_nomem : 1;
} mps_slab_pool_t;

#define mps_nulloff 0
#define mps_offset(pool, ptr) (mps_ptroff_t)((u_char *)(ptr) - (u_char *)(pool))

#define mps_nullptr(pool) ((void *)pool)
#define mps_ptr(pool, offset) ((u_char *)(pool) + (offset))

#define mps_slab_page(pool, off) ((mps_slab_page_t *)mps_ptr(pool, off))

typedef void (*mps_slab_on_init_pt)(mps_slab_pool_t *pool);

mps_slab_pool_t *mps_slab_open_or_create(const char *shm_name, size_t shm_size,
                                         mode_t mode,
                                         mps_slab_on_init_pt on_init);
void mps_slab_close(mps_slab_pool_t *pool, size_t shm_size);

void mps_slab_lock(mps_slab_pool_t *pool);
void mps_slab_unlock(mps_slab_pool_t *pool);
void *mps_slab_alloc(mps_slab_pool_t *pool, size_t size);
void *mps_slab_alloc_locked(mps_slab_pool_t *pool, size_t size);
void *mps_slab_calloc(mps_slab_pool_t *pool, size_t size);
void *mps_slab_calloc_locked(mps_slab_pool_t *pool, size_t size);
void mps_slab_free(mps_slab_pool_t *pool, void *p);
void mps_slab_free_locked(mps_slab_pool_t *pool, void *p);

#endif /* _MPS_SLAB_H_INCLUDED_ */
