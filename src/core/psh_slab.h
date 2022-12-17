
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _PSH_SLAB_H_INCLUDED_
#define _PSH_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef uintptr_t psh_ptroff_t;

typedef struct psh_slab_page_s  psh_slab_page_t;

struct psh_slab_page_s {
    uintptr_t         slab;
    psh_ptroff_t      next;
    psh_ptroff_t      prev;
};

typedef struct {
    ngx_uint_t        total;
    ngx_uint_t        used;

    ngx_uint_t        reqs;
    ngx_uint_t        fails;
} psh_slab_stat_t;


typedef struct {
    ngx_shmtx_sh_t    lock;

    size_t            min_size;
    size_t            min_shift;

    psh_ptroff_t      pages;
    psh_ptroff_t      last;
    psh_slab_page_t   free;

    psh_ptroff_t      stats;
    ngx_uint_t        pfree;

    psh_ptroff_t      start;
    psh_ptroff_t      end;

    ngx_shmtx_t       mutex;

    u_char           *log_ctx;
    u_char            zero;

    unsigned          log_nomem:1;

    void             *data;
    void             *addr;
} psh_slab_pool_t;

void psh_slab_sizes_init(ngx_uint_t pagesize);
void psh_slab_init(psh_slab_pool_t *pool, u_char *addr, size_t size);
void *psh_slab_alloc(psh_slab_pool_t *pool, size_t size);
void *psh_slab_alloc_locked(psh_slab_pool_t *pool, size_t size);
void *psh_slab_calloc(psh_slab_pool_t *pool, size_t size);
void *psh_slab_calloc_locked(psh_slab_pool_t *pool, size_t size);
void psh_slab_free(psh_slab_pool_t *pool, void *p);
void psh_slab_free_locked(psh_slab_pool_t *pool, void *p);


#endif /* _PSH_SLAB_H_INCLUDED_ */
