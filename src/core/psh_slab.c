
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "psh_slab.h"


#define PSH_SLAB_PAGE_MASK   3
#define PSH_SLAB_PAGE        0
#define PSH_SLAB_BIG         1
#define PSH_SLAB_EXACT       2
#define PSH_SLAB_SMALL       3

#if (NGX_PTR_SIZE == 4)

#define PSH_SLAB_PAGE_FREE   0
#define PSH_SLAB_PAGE_BUSY   0xffffffff
#define PSH_SLAB_PAGE_START  0x80000000

#define PSH_SLAB_SHIFT_MASK  0x0000000f
#define PSH_SLAB_MAP_MASK    0xffff0000
#define PSH_SLAB_MAP_SHIFT   16

#define PSH_SLAB_BUSY        0xffffffff

#else /* (NGX_PTR_SIZE == 8) */

#define PSH_SLAB_PAGE_FREE   0
#define PSH_SLAB_PAGE_BUSY   0xffffffffffffffff
#define PSH_SLAB_PAGE_START  0x8000000000000000

#define PSH_SLAB_SHIFT_MASK  0x000000000000000f
#define PSH_SLAB_MAP_MASK    0xffffffff00000000
#define PSH_SLAB_MAP_SHIFT   32

#define PSH_SLAB_BUSY        0xffffffffffffffff

#endif

#define psh_slab_to_off(pool, ptr)                                            \
    (psh_ptroff_t) ((ptr) ? (u_char *) (ptr) - (u_char *) (pool) : 0)

#define psh_slab_to_ptr(pool, off)   ((off) ? (u_char *) (pool) + (off) : NULL)
#define psh_slab_page(pool, off)                                              \
    ((psh_slab_page_t *) psh_slab_to_ptr(pool, off))                                       

#define psh_pool_stats(pool)                                                  \
    ((psh_slab_stat_t *) (u_char *) (pool) + (pool)->stats)

#define psh_slab_slots(pool)                                                  \
    ((psh_slab_page_t *) ((u_char *) (pool) + sizeof(psh_slab_pool_t)))

#define psh_slab_page_type(page)   ((page)->prev & PSH_SLAB_PAGE_MASK)

#define psh_slab_page_prev(pool, page)                                        \
    psh_slab_page(pool, (page)->prev & ~PSH_SLAB_PAGE_MASK)

#define psh_slab_page_next(pool, page)  psh_slab_page(pool, (page)->next)

#define psh_slab_page_addr(pool, page)                                        \
    (((psh_slab_to_off(pool, page) - pool->pages) << psh_pagesize_shift)      \
     + (uintptr_t) psh_slab_to_ptr(pool, pool->start))


#if (NGX_DEBUG_MALLOC)

#define psh_slab_junk(p, size)     ngx_memset(p, 0xA5, size)

#elif (NGX_HAVE_DEBUG_MALLOC)

#define psh_slab_junk(p, size)                                                \
    if (ngx_debug_malloc)          ngx_memset(p, 0xA5, size)

#else

#define psh_slab_junk(p, size)

#endif

static psh_slab_page_t *psh_slab_alloc_pages(psh_slab_pool_t *pool,
    ngx_uint_t pages);
static void psh_slab_free_pages(psh_slab_pool_t *pool, psh_slab_page_t *page,
    ngx_uint_t pages);
static void psh_slab_error(psh_slab_pool_t *pool, ngx_uint_t level,
    char *text);


static ngx_uint_t  psh_pagesize;
static ngx_uint_t  psh_pagesize_shift;

static ngx_uint_t  psh_slab_max_size;
static ngx_uint_t  psh_slab_exact_size;
static ngx_uint_t  psh_slab_exact_shift;


void
psh_slab_sizes_init(ngx_uint_t pagesize)
{
    ngx_uint_t  n;

    psh_pagesize = pagesize;
    for (n = psh_pagesize; n >>= 1; psh_pagesize_shift++) { /* void */ }
    printf("psh_pagesize_shift=%lu\n", psh_pagesize_shift);

    psh_slab_max_size = psh_pagesize / 2;
    psh_slab_exact_size = psh_pagesize / (8 * sizeof(uintptr_t));
    for (n = psh_slab_exact_size; n >>= 1; psh_slab_exact_shift++) {
        /* void */
    }
    printf("psh_slab_exact_shift=%lu\n", psh_slab_exact_shift);
}

void
psh_slab_init(psh_slab_pool_t *pool, u_char *addr, size_t pool_size)
{
    u_char           *p, *start;
    size_t            size;
    ngx_int_t         m;
    ngx_uint_t        i, n, pages;
    psh_slab_page_t  *slots, *page, *last;

    pool->end = pool_size;
    pool->min_shift = 3;
    pool->addr = addr;

    pool->min_size = (size_t) 1 << pool->min_shift;

    slots = psh_slab_slots(pool);

    p = (u_char *) slots;
    size = pool->end - psh_slab_to_off(pool, p);

    psh_slab_junk(p, size);

    n = psh_pagesize_shift - pool->min_shift;

    for (i = 0; i < n; i++) {
        /* only "next" is used in list head */
        slots[i].slab = 0;
        slots[i].next = psh_slab_to_off(pool, &slots[i]);
        slots[i].prev = 0;
    }

    p += n * sizeof(psh_slab_page_t);

    pool->stats = psh_slab_to_off(pool, p);
    ngx_memzero(psh_pool_stats(pool), n * sizeof(psh_slab_stat_t));

    p += n * sizeof(psh_slab_stat_t);

    size -= n * (sizeof(psh_slab_page_t) + sizeof(psh_slab_stat_t));

    pages = (ngx_uint_t) (size / (psh_pagesize + sizeof(psh_slab_page_t)));

    pool->pages = psh_slab_to_off(pool, p);
    ngx_memzero(p, pages * sizeof(psh_slab_page_t));

    page = (psh_slab_page_t *) p;

    /* only "next" is used in list head */
    pool->free.slab = 0;
    pool->free.next = psh_slab_to_off(pool, page);
    pool->free.prev = 0;

    page->slab = pages;
    page->next = psh_slab_to_off(pool, &pool->free);
    page->prev = psh_slab_to_off(pool, &pool->free);

    start = ngx_align_ptr(p + pages * sizeof(psh_slab_page_t), psh_pagesize);
    pool->start = psh_slab_to_off(pool, start);

    m = pages - (pool->end - pool->start) / psh_pagesize;
    if (m > 0) {
        pages -= m;
        page->slab = pages;
    }

    last = psh_slab_page(pool, pages) + pages;
    pool->last = psh_slab_to_off(pool, last);
    pool->pfree = pages;

    pool->log_nomem = 1;
    pool->log_ctx = &pool->zero;
    pool->zero = '\0';
}


void *
psh_slab_alloc(psh_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = psh_slab_alloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


void *
psh_slab_alloc_locked(psh_slab_pool_t *pool, size_t size)
{
    size_t            s;
    uintptr_t         p, m, mask, *bitmap;
    ngx_uint_t        i, n, slot, shift, map;
    psh_slab_page_t  *page, *prev, *next, *slots;

    printf("psh_slab_alloc_locked start, pool=%p, size=%lu, psh_slab_max_size=%lu\n", pool, size, psh_slab_max_size);
    if (size > psh_slab_max_size) {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                       "slab alloc: %uz", size);

        page = psh_slab_alloc_pages(pool, (size >> psh_pagesize_shift)
                                          + ((size % psh_pagesize) ? 1 : 0));
        if (page) {
            p = psh_slab_page_addr(pool, page);

        } else {
            p = 0;
        }

        goto done;
    }

    printf("psh_slab_alloc_locked pool->min_size=%lu, min_shift=%lu\n", pool->min_size, pool->min_shift);
    if (size > pool->min_size) {
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) { /* void */ }
        slot = shift - pool->min_shift;

    } else {
        shift = pool->min_shift;
        slot = 0;
    }

    printf("psh_slab_alloc_locked slot=%lu, shift=%lu, stats=%p\n", slot, shift, psh_pool_stats(pool));
    psh_pool_stats(pool)[slot].reqs++;
    printf("psh_slab_alloc_locked slot=%lu, reqs=%lu\n", slot, psh_pool_stats(pool)[slot].reqs);

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %uz slot: %ui", size, slot);

    slots = psh_slab_slots(pool);
    page = psh_slab_page_next(pool, &slots[slot]);

    if (psh_slab_page_next(pool, page) != page) {

        if (shift < psh_slab_exact_shift) {

            bitmap = (uintptr_t *) psh_slab_page_addr(pool, page);

            map = (psh_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (n = 0; n < map; n++) {

                if (bitmap[n] != PSH_SLAB_BUSY) {

                    for (m = 1, i = 0; m; m <<= 1, i++) {
                        if (bitmap[n] & m) {
                            continue;
                        }

                        bitmap[n] |= m;

                        i = (n * 8 * sizeof(uintptr_t) + i) << shift;

                        p = (uintptr_t) bitmap + i;

                        psh_pool_stats(pool)[slot].used++;

                        if (bitmap[n] == PSH_SLAB_BUSY) {
                            for (n = n + 1; n < map; n++) {
                                if (bitmap[n] != PSH_SLAB_BUSY) {
                                    goto done;
                                }
                            }

                            prev = psh_slab_page_prev(pool, page);
                            prev->next = page->next;
                            next = psh_slab_page_next(pool, page);
                            next->prev = page->prev;

                            page->next = 0;
                            page->prev = PSH_SLAB_SMALL;
                        }

                        goto done;
                    }
                }
            }

        } else if (shift == psh_slab_exact_shift) {

            for (m = 1, i = 0; m; m <<= 1, i++) {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if (page->slab == PSH_SLAB_BUSY) {
                    prev = psh_slab_page_prev(pool, page);
                    prev->next = page->next;
                    next = psh_slab_page_next(pool, page);
                    next->prev = page->prev;

                    page->next = 0;
                    page->prev = PSH_SLAB_EXACT;
                }

                p = psh_slab_page_addr(pool, page) + (i << shift);

                psh_pool_stats(pool)[slot].used++;

                goto done;
            }

        } else { /* shift > psh_slab_exact_shift */

            mask = ((uintptr_t) 1 << (psh_pagesize >> shift)) - 1;
            mask <<= PSH_SLAB_MAP_SHIFT;

            for (m = (uintptr_t) 1 << PSH_SLAB_MAP_SHIFT, i = 0;
                 m & mask;
                 m <<= 1, i++)
            {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if ((page->slab & PSH_SLAB_MAP_MASK) == mask) {
                    prev = psh_slab_page_prev(pool, page);
                    prev->next = page->next;
                    next = psh_slab_page_next(pool, page);
                    next->prev = page->prev;

                    page->next = 0;
                    page->prev = PSH_SLAB_BIG;
                }

                p = psh_slab_page_addr(pool, page) + (i << shift);

                psh_pool_stats(pool)[slot].used++;

                goto done;
            }
        }

        psh_slab_error(pool, NGX_LOG_ALERT, "psh_slab_alloc(): page is busy");
        ngx_debug_point();
    }

    page = psh_slab_alloc_pages(pool, 1);
    printf("psh_slab_alloc_locked after psh_slab_alloc_pages, page=%p\n", (void *) page);

    if (page) {
        if (shift < psh_slab_exact_shift) {
            bitmap = (uintptr_t *) psh_slab_page_addr(pool, page);

            n = (psh_pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            /* "n" elements for bitmap, plus one requested */

            for (i = 0; i < (n + 1) / (8 * sizeof(uintptr_t)); i++) {
                bitmap[i] = PSH_SLAB_BUSY;
            }

            m = ((uintptr_t) 1 << ((n + 1) % (8 * sizeof(uintptr_t)))) - 1;
            bitmap[i] = m;

            map = (psh_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                bitmap[i] = 0;
            }

            page->slab = shift;
            page->next = psh_slab_to_off(pool, &slots[slot]);
            page->prev = psh_slab_to_off(pool, &slots[slot]) | PSH_SLAB_SMALL;

            slots[slot].next = psh_slab_to_off(pool, page);

            psh_pool_stats(pool)[slot].total += (psh_pagesize >> shift) - n;

            p = psh_slab_page_addr(pool, page) + (n << shift);

            psh_pool_stats(pool)[slot].used++;

            goto done;

        } else if (shift == psh_slab_exact_shift) {

            page->slab = 1;
            page->next = psh_slab_to_off(pool, &slots[slot]);
            page->prev = psh_slab_to_off(pool, &slots[slot]) | PSH_SLAB_EXACT;

            slots[slot].next = psh_slab_to_off(pool, page);

            psh_pool_stats(pool)[slot].total += 8 * sizeof(uintptr_t);

            p = psh_slab_page_addr(pool, page);

            psh_pool_stats(pool)[slot].used++;

            goto done;

        } else { /* shift > psh_slab_exact_shift */

            page->slab = ((uintptr_t) 1 << PSH_SLAB_MAP_SHIFT) | shift;
            page->next = psh_slab_to_off(pool, &slots[slot]);
            page->prev = psh_slab_to_off(pool, &slots[slot]) | PSH_SLAB_BIG;

            slots[slot].next = psh_slab_to_off(pool, page);

            psh_pool_stats(pool)[slot].total += psh_pagesize >> shift;

            p = psh_slab_page_addr(pool, page);

            psh_pool_stats(pool)[slot].used++;

            goto done;
        }
    }

    p = 0;

    psh_pool_stats(pool)[slot].fails++;

done:
    printf("psh_slab_alloc_locked done, alloc=%p\n", (void *) p);

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %p", (void *) p);

    return (void *) p;
}


void *
psh_slab_calloc(psh_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = psh_slab_calloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


void *
psh_slab_calloc_locked(psh_slab_pool_t *pool, size_t size)
{
    void  *p;

    p = psh_slab_alloc_locked(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


void
psh_slab_free(psh_slab_pool_t *pool, void *p)
{
    ngx_shmtx_lock(&pool->mutex);

    psh_slab_free_locked(pool, p);

    ngx_shmtx_unlock(&pool->mutex);
}


void
psh_slab_free_locked(psh_slab_pool_t *pool, void *p)
{
    size_t            size;
    uintptr_t         slab, m, *bitmap;
    ngx_uint_t        i, n, type, slot, shift, map;
    psh_slab_page_t  *slots, *page, *next;
    psh_ptroff_t      p_off;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab free: %p", p);

    p_off = psh_slab_to_off(pool, p);
    if (p_off < pool->start || p_off > pool->end) {
        psh_slab_error(pool, NGX_LOG_ALERT, "psh_slab_free(): outside of pool");
        goto fail;
    }

    n = (p_off - pool->start) >> psh_pagesize_shift;
    page = &psh_slab_page(pool, pool->pages)[n];
    slab = page->slab;
    type = psh_slab_page_type(page);

    switch (type) {

    case PSH_SLAB_SMALL:

        shift = slab & PSH_SLAB_SHIFT_MASK;
        size = (size_t) 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        n = ((uintptr_t) p & (psh_pagesize - 1)) >> shift;
        m = (uintptr_t) 1 << (n % (8 * sizeof(uintptr_t)));
        n /= 8 * sizeof(uintptr_t);
        bitmap = (uintptr_t *)
                             ((uintptr_t) p & ~((uintptr_t) psh_pagesize - 1));

        if (bitmap[n] & m) {
            slot = shift - pool->min_shift;

            if (page->next == 0) {
                slots = psh_slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = psh_slab_to_off(pool, page);

                page->prev = psh_slab_to_off(pool, &slots[slot])
                             | PSH_SLAB_SMALL;
                next = psh_slab_page_next(pool, page);
                next->prev = psh_slab_to_off(pool, page) | PSH_SLAB_SMALL;
            }

            bitmap[n] &= ~m;

            n = (psh_pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            i = n / (8 * sizeof(uintptr_t));
            m = ((uintptr_t) 1 << (n % (8 * sizeof(uintptr_t)))) - 1;

            if (bitmap[i] & ~m) {
                goto done;
            }

            map = (psh_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                if (bitmap[i]) {
                    goto done;
                }
            }

            psh_slab_free_pages(pool, page, 1);

            psh_pool_stats(pool)[slot].total -= (psh_pagesize >> shift) - n;

            goto done;
        }

        goto chunk_already_free;

    case PSH_SLAB_EXACT:

        m = (uintptr_t) 1 <<
                (((uintptr_t) p & (psh_pagesize - 1)) >> psh_slab_exact_shift);
        size = psh_slab_exact_size;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        if (slab & m) {
            slot = psh_slab_exact_shift - pool->min_shift;

            if (slab == PSH_SLAB_BUSY) {
                slots = psh_slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = psh_slab_to_off(pool, page);

                page->prev = psh_slab_to_off(pool, &slots[slot])
                             | PSH_SLAB_EXACT;
                next = psh_slab_page_next(pool, page);
                next->prev = psh_slab_to_off(pool, page) | PSH_SLAB_EXACT;
            }

            page->slab &= ~m;

            if (page->slab) {
                goto done;
            }

            psh_slab_free_pages(pool, page, 1);

            psh_pool_stats(pool)[slot].total -= 8 * sizeof(uintptr_t);

            goto done;
        }

        goto chunk_already_free;

    case PSH_SLAB_BIG:

        shift = slab & PSH_SLAB_SHIFT_MASK;
        size = (size_t) 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        m = (uintptr_t) 1 << ((((uintptr_t) p & (psh_pagesize - 1)) >> shift)
                              + PSH_SLAB_MAP_SHIFT);

        if (slab & m) {
            slot = shift - pool->min_shift;

            if (page->next == 0) {
                slots = psh_slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = psh_slab_to_off(pool, page);

                page->prev = psh_slab_to_off(pool, &slots[slot]) | PSH_SLAB_BIG;
                next = psh_slab_page_next(pool, page);
                next->prev = psh_slab_to_off(pool, page) | PSH_SLAB_BIG;
            }

            page->slab &= ~m;

            if (page->slab & PSH_SLAB_MAP_MASK) {
                goto done;
            }

            psh_slab_free_pages(pool, page, 1);

            psh_pool_stats(pool)[slot].total -= psh_pagesize >> shift;

            goto done;
        }

        goto chunk_already_free;

    case PSH_SLAB_PAGE:

        if ((uintptr_t) p & (psh_pagesize - 1)) {
            goto wrong_chunk;
        }

        if (!(slab & PSH_SLAB_PAGE_START)) {
            psh_slab_error(pool, NGX_LOG_ALERT,
                           "psh_slab_free(): page is already free");
            goto fail;
        }

        if (slab == PSH_SLAB_PAGE_BUSY) {
            psh_slab_error(pool, NGX_LOG_ALERT,
                           "psh_slab_free(): pointer to wrong page");
            goto fail;
        }

        size = slab & ~PSH_SLAB_PAGE_START;

        psh_slab_free_pages(pool, page, size);

        psh_slab_junk(p, size << psh_pagesize_shift);

        return;
    }

    /* not reached */

    return;

done:

    psh_pool_stats(pool)[slot].used--;

    psh_slab_junk(p, size);

    return;

wrong_chunk:

    psh_slab_error(pool, NGX_LOG_ALERT,
                   "psh_slab_free(): pointer to wrong chunk");

    goto fail;

chunk_already_free:

    psh_slab_error(pool, NGX_LOG_ALERT,
                   "psh_slab_free(): chunk is already free");

fail:

    return;
}


static psh_slab_page_t *
psh_slab_alloc_pages(psh_slab_pool_t *pool, ngx_uint_t pages)
{
    psh_slab_page_t  *page, *p, *next;

    for (page = psh_slab_page_next(pool, &pool->free); page != &pool->free;
         page = psh_slab_page_next(pool, page))
    {
        if (page->slab >= pages) {

            if (page->slab > pages) {
                page[page->slab - 1].prev = (uintptr_t) &page[pages];

                page[pages].slab = page->slab - pages;
                page[pages].next = page->next;
                page[pages].prev = page->prev;

                p = psh_slab_page(pool, page->prev);
                p->next = psh_slab_to_off(pool, &page[pages]);
                next = psh_slab_page_next(pool, page);
                next->prev = (uintptr_t) &page[pages];

            } else {
                p = psh_slab_page(pool, page->prev);
                p->next = page->next;
                next = psh_slab_page_next(pool, page);
                next->prev = page->prev;
            }

            page->slab = pages | PSH_SLAB_PAGE_START;
            page->next = 0;
            page->prev = PSH_SLAB_PAGE;

            pool->pfree -= pages;

            if (--pages == 0) {
                return page;
            }

            for (p = page + 1; pages; pages--) {
                p->slab = PSH_SLAB_PAGE_BUSY;
                p->next = 0;
                p->prev = PSH_SLAB_PAGE;
                p++;
            }

            return page;
        }
    }

    if (pool->log_nomem) {
        psh_slab_error(pool, NGX_LOG_CRIT,
                       "psh_slab_alloc() failed: no memory");
    }

    return NULL;
}


static void
psh_slab_free_pages(psh_slab_pool_t *pool, psh_slab_page_t *page,
    ngx_uint_t pages)
{
    psh_slab_page_t  *prev, *join, *next;

    pool->pfree += pages;

    page->slab = pages--;

    if (pages) {
        ngx_memzero(&page[1], pages * sizeof(psh_slab_page_t));
    }

    if (page->next) {
        prev = psh_slab_page_prev(pool, page);
        prev->next = page->next;
        next = psh_slab_page_next(pool, page);
        next->prev = page->prev;
    }

    join = page + page->slab;

    if (psh_slab_to_off(pool, join) < pool->last) {

        if (psh_slab_page_type(join) == PSH_SLAB_PAGE) {

            if (join->next != 0) {
                pages += join->slab;
                page->slab += join->slab;

                prev = psh_slab_page_prev(pool, join);
                prev->next = join->next;
                next = psh_slab_page_next(pool, join);
                next->prev = join->prev;

                join->slab = PSH_SLAB_PAGE_FREE;
                join->next = 0;
                join->prev = PSH_SLAB_PAGE;
            }
        }
    }

    if (psh_slab_to_off(pool, page) > pool->pages) {
        join = page - 1;

        if (psh_slab_page_type(join) == PSH_SLAB_PAGE) {

            if (join->slab == PSH_SLAB_PAGE_FREE) {
                join = psh_slab_page_prev(pool, join);
            }

            if (join->next != 0) {
                pages += join->slab;
                join->slab += page->slab;

                prev = psh_slab_page_prev(pool, join);
                prev->next = join->next;
                next = psh_slab_page_next(pool, join);
                next->prev = join->prev;

                page->slab = PSH_SLAB_PAGE_FREE;
                page->next = 0;
                page->prev = PSH_SLAB_PAGE;

                page = join;
            }
        }
    }

    if (pages) {
        page[pages].prev = psh_slab_to_off(pool, page);
    }

    page->prev = psh_slab_to_off(pool, &pool->free);
    page->next = pool->free.next;

    next = psh_slab_page_next(pool, page);
    next->prev = psh_slab_to_off(pool, page);

    pool->free.next = psh_slab_to_off(pool, page);
}


static void
psh_slab_error(psh_slab_pool_t *pool, ngx_uint_t level, char *text)
{
    ngx_log_error(level, ngx_cycle->log, 0, "%s%s", text, pool->log_ctx);
}
