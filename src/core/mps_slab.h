
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _MPS_SLAB_H_INCLUDED_
#define _MPS_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef uintptr_t mps_ptroff_t;

typedef struct mps_slab_page_s  mps_slab_page_t;

struct mps_slab_page_s {
    uintptr_t         slab;
    mps_ptroff_t      next;
    mps_ptroff_t      prev;
};

typedef struct {
    ngx_uint_t        total;
    ngx_uint_t        used;

    ngx_uint_t        reqs;
    ngx_uint_t        fails;
} mps_slab_stat_t;


typedef struct {
    ngx_shmtx_sh_t    lock;

    size_t            min_size;
    size_t            min_shift;

    mps_ptroff_t      pages;
    mps_ptroff_t      last;
    mps_slab_page_t   free;

    mps_ptroff_t      stats;
    ngx_uint_t        pfree;

    mps_ptroff_t      start;
    mps_ptroff_t      end;

    pthread_mutex_t   mutex;

    u_char           *log_ctx;
    u_char            zero;

    unsigned          log_nomem:1;

    void             *data;
    void             *addr;
} mps_slab_pool_t;

void mps_slab_sizes_init(ngx_uint_t pagesize);
void mps_slab_init(mps_slab_pool_t *pool, u_char *addr, size_t size);
void *mps_slab_alloc(mps_slab_pool_t *pool, size_t size);
void *mps_slab_alloc_locked(mps_slab_pool_t *pool, size_t size);
void *mps_slab_calloc(mps_slab_pool_t *pool, size_t size);
void *mps_slab_calloc_locked(mps_slab_pool_t *pool, size_t size);
void mps_slab_free(mps_slab_pool_t *pool, void *p);
void mps_slab_free_locked(mps_slab_pool_t *pool, void *p);


#endif /* _MPS_SLAB_H_INCLUDED_ */
