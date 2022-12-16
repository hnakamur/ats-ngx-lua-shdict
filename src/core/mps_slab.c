
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "mps_slab.h"


#define MPS_SLAB_PAGE_MASK   3
#define MPS_SLAB_PAGE        0
#define MPS_SLAB_BIG         1
#define MPS_SLAB_EXACT       2
#define MPS_SLAB_SMALL       3

#if (NGX_PTR_SIZE == 4)

#define MPS_SLAB_PAGE_FREE   0
#define MPS_SLAB_PAGE_BUSY   0xffffffff
#define MPS_SLAB_PAGE_START  0x80000000

#define MPS_SLAB_SHIFT_MASK  0x0000000f
#define MPS_SLAB_MAP_MASK    0xffff0000
#define MPS_SLAB_MAP_SHIFT   16

#define MPS_SLAB_BUSY        0xffffffff

#else /* (NGX_PTR_SIZE == 8) */

#define MPS_SLAB_PAGE_FREE   0
#define MPS_SLAB_PAGE_BUSY   0xffffffffffffffff
#define MPS_SLAB_PAGE_START  0x8000000000000000

#define MPS_SLAB_SHIFT_MASK  0x000000000000000f
#define MPS_SLAB_MAP_MASK    0xffffffff00000000
#define MPS_SLAB_MAP_SHIFT   32

#define MPS_SLAB_BUSY        0xffffffffffffffff

#endif


#define mps_slab_slots(pool)                                                  \
    (mps_slab_page_t *) ((u_char *) (pool) + sizeof(mps_slab_pool_t))

#define mps_slab_page_type(page)   ((page)->prev & MPS_SLAB_PAGE_MASK)

#define mps_slab_page_prev(page)                                              \
    (mps_slab_page_t *) ((page)->prev & ~MPS_SLAB_PAGE_MASK)

#define mps_slab_page_addr(pool, page)                                        \
    ((((page) - mps_pool_pages_ptr(pool)) << mps_pagesize_shift)              \
     + (uintptr_t) mps_pool_start_ptr(pool))


#if (NGX_DEBUG_MALLOC)

#define mps_slab_junk(p, size)     ngx_memset(p, 0xA5, size)

#elif (NGX_HAVE_DEBUG_MALLOC)

#define mps_slab_junk(p, size)                                                \
    if (ngx_debug_malloc)          ngx_memset(p, 0xA5, size)

#else

#define mps_slab_junk(p, size)

#endif

static mps_slab_page_t *mps_slab_alloc_pages(mps_slab_pool_t *pool,
    ngx_uint_t pages);
static void mps_slab_free_pages(mps_slab_pool_t *pool, mps_slab_page_t *page,
    ngx_uint_t pages);
static void mps_slab_error(mps_slab_pool_t *pool, ngx_uint_t level,
    char *text);


static ngx_uint_t  mps_pagesize;
static ngx_uint_t  mps_pagesize_shift;

static ngx_uint_t  mps_slab_max_size;
static ngx_uint_t  mps_slab_exact_size;
static ngx_uint_t  mps_slab_exact_shift;


void
mps_slab_sizes_init(ngx_uint_t pagesize)
{
    ngx_uint_t  n;

    mps_pagesize = pagesize;
    for (n = mps_pagesize; n >>= 1; mps_pagesize_shift++) { /* void */ }
    printf("mps_pagesize_shift=%lu\n", mps_pagesize_shift);

    mps_slab_max_size = mps_pagesize / 2;
    mps_slab_exact_size = mps_pagesize / (8 * sizeof(uintptr_t));
    for (n = mps_slab_exact_size; n >>= 1; mps_slab_exact_shift++) {
        /* void */
    }
    printf("mps_slab_exact_shift=%lu\n", mps_slab_exact_shift);
}

void
mps_slab_init(mps_slab_pool_t *pool, u_char *addr, size_t pool_size)
{
    u_char           *p;
    size_t            size;
    ngx_int_t         m;
    ngx_uint_t        i, n, pages;
    mps_slab_page_t  *slots, *page;

    pool->end = pool_size;
    pool->min_shift = 3;
    pool->addr = addr;

    pool->min_size = (size_t) 1 << pool->min_shift;

    slots = mps_slab_slots(pool);

    p = (u_char *) slots;
    size = mps_pool_end_ptr(pool) - p;

    mps_slab_junk(p, size);

    n = mps_pagesize_shift - pool->min_shift;

    for (i = 0; i < n; i++) {
        /* only "next" is used in list head */
        slots[i].slab = 0;
        mps_page_set_next_ptr(pool, &slots[i], &slots[i]);
        slots[i].prev = 0;
    }

    p += n * sizeof(mps_slab_page_t);

    pool->stats = p - addr;
    ngx_memzero(mps_pool_stats_ptr(pool), n * sizeof(mps_slab_stat_t));

    p += n * sizeof(mps_slab_stat_t);

    size -= n * (sizeof(mps_slab_page_t) + sizeof(mps_slab_stat_t));

    pages = (ngx_uint_t) (size / (mps_pagesize + sizeof(mps_slab_page_t)));

    pool->pages = p - addr;
    ngx_memzero(mps_pool_pages_ptr(pool), pages * sizeof(mps_slab_page_t));

    page = mps_pool_pages_ptr(pool);

    /* only "next" is used in list head */
    pool->free.slab = 0;
    mps_page_set_next_ptr(pool, &pool->free, page);
    pool->free.prev = 0;

    page->slab = pages;
    mps_page_set_next_ptr(pool, page, &pool->free);
    page->prev = (uintptr_t) &pool->free;

    pool->start = ngx_align_ptr(p + pages * sizeof(mps_slab_page_t),
                                mps_pagesize) - addr;

    m = pages - (mps_pool_end_ptr(pool) - mps_pool_start_ptr(pool)) / mps_pagesize;
    if (m > 0) {
        pages -= m;
        page->slab = pages;
    }

    pool->last = (u_char *)mps_pool_pages_ptr(pool) + pages - addr;
    pool->pfree = pages;

    pool->log_nomem = 1;
    pool->log_ctx = &pool->zero;
    pool->zero = '\0';
}


void *
mps_slab_alloc(mps_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = mps_slab_alloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


void *
mps_slab_alloc_locked(mps_slab_pool_t *pool, size_t size)
{
    size_t            s;
    uintptr_t         p, m, mask, *bitmap;
    ngx_uint_t        i, n, slot, shift, map;
    mps_slab_page_t  *page, *prev, *next, *slots;

    printf("mps_slab_alloc_locked start, pool=%p, size=%lu, mps_slab_max_size=%lu\n", pool, size, mps_slab_max_size);
    if (size > mps_slab_max_size) {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                       "slab alloc: %uz", size);

        page = mps_slab_alloc_pages(pool, (size >> mps_pagesize_shift)
                                          + ((size % mps_pagesize) ? 1 : 0));
        if (page) {
            p = mps_slab_page_addr(pool, page);

        } else {
            p = 0;
        }

        goto done;
    }

    printf("mps_slab_alloc_locked pool->min_size=%lu, min_shift=%lu\n", pool->min_size, pool->min_shift);
    if (size > pool->min_size) {
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) { /* void */ }
        slot = shift - pool->min_shift;

    } else {
        shift = pool->min_shift;
        slot = 0;
    }

    printf("mps_slab_alloc_locked slot=%lu, shift=%lu, stats=%p\n", slot, shift, mps_pool_stats_ptr(pool));
    mps_pool_stats_ptr(pool)[slot].reqs++;
    printf("mps_slab_alloc_locked slot=%lu, reqs=%lu\n", slot, mps_pool_stats_ptr(pool)[slot].reqs);

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %uz slot: %ui", size, slot);

    slots = mps_slab_slots(pool);
    page = mps_page_next_ptr(pool, &slots[slot]);

    if (mps_page_next_ptr(pool, page) != page) {

        if (shift < mps_slab_exact_shift) {

            bitmap = (uintptr_t *) mps_slab_page_addr(pool, page);

            map = (mps_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (n = 0; n < map; n++) {

                if (bitmap[n] != MPS_SLAB_BUSY) {

                    for (m = 1, i = 0; m; m <<= 1, i++) {
                        if (bitmap[n] & m) {
                            continue;
                        }

                        bitmap[n] |= m;

                        i = (n * 8 * sizeof(uintptr_t) + i) << shift;

                        p = (uintptr_t) bitmap + i;

                        mps_pool_stats_ptr(pool)[slot].used++;

                        if (bitmap[n] == MPS_SLAB_BUSY) {
                            for (n = n + 1; n < map; n++) {
                                if (bitmap[n] != MPS_SLAB_BUSY) {
                                    goto done;
                                }
                            }

                            prev = mps_slab_page_prev(page);
                            next = mps_page_next_ptr(pool, page);
                            mps_page_set_next_ptr(pool, prev, next);
                            next->prev = page->prev;

                            mps_page_set_next_ptr(pool, page, NULL);
                            page->prev = MPS_SLAB_SMALL;
                        }

                        goto done;
                    }
                }
            }

        } else if (shift == mps_slab_exact_shift) {

            for (m = 1, i = 0; m; m <<= 1, i++) {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if (page->slab == MPS_SLAB_BUSY) {
                    prev = mps_slab_page_prev(page);
                    next = mps_page_next_ptr(pool, page);
                    mps_page_set_next_ptr(pool, prev, next);
                    next->prev = page->prev;

                    mps_page_set_next_ptr(pool, page, NULL);
                    page->prev = MPS_SLAB_EXACT;
                }

                p = mps_slab_page_addr(pool, page) + (i << shift);

                mps_pool_stats_ptr(pool)[slot].used++;

                goto done;
            }

        } else { /* shift > mps_slab_exact_shift */

            mask = ((uintptr_t) 1 << (mps_pagesize >> shift)) - 1;
            mask <<= MPS_SLAB_MAP_SHIFT;

            for (m = (uintptr_t) 1 << MPS_SLAB_MAP_SHIFT, i = 0;
                 m & mask;
                 m <<= 1, i++)
            {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if ((page->slab & MPS_SLAB_MAP_MASK) == mask) {
                    prev = mps_slab_page_prev(page);
                    next = mps_page_next_ptr(pool, page);
                    mps_page_set_next_ptr(pool, prev, next);
                    next->prev = page->prev;

                    mps_page_set_next_ptr(pool, page, NULL);
                    page->prev = MPS_SLAB_BIG;
                }

                p = mps_slab_page_addr(pool, page) + (i << shift);

                mps_pool_stats_ptr(pool)[slot].used++;

                goto done;
            }
        }

        mps_slab_error(pool, NGX_LOG_ALERT, "mps_slab_alloc(): page is busy");
        ngx_debug_point();
    }

    page = mps_slab_alloc_pages(pool, 1);
    printf("mps_slab_alloc_locked after mps_slab_alloc_pages, page=%p\n", (void *) page);

    if (page) {
        if (shift < mps_slab_exact_shift) {
            bitmap = (uintptr_t *) mps_slab_page_addr(pool, page);

            n = (mps_pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            /* "n" elements for bitmap, plus one requested */

            for (i = 0; i < (n + 1) / (8 * sizeof(uintptr_t)); i++) {
                bitmap[i] = MPS_SLAB_BUSY;
            }

            m = ((uintptr_t) 1 << ((n + 1) % (8 * sizeof(uintptr_t)))) - 1;
            bitmap[i] = m;

            map = (mps_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                bitmap[i] = 0;
            }

            page->slab = shift;
            mps_page_set_next_ptr(pool, page, &slots[slot]);
            page->prev = (uintptr_t) &slots[slot] | MPS_SLAB_SMALL;

            mps_page_set_next_ptr(pool, &slots[slot], page);

            mps_pool_stats_ptr(pool)[slot].total += (mps_pagesize >> shift) - n;

            p = mps_slab_page_addr(pool, page) + (n << shift);

            mps_pool_stats_ptr(pool)[slot].used++;

            goto done;

        } else if (shift == mps_slab_exact_shift) {

            page->slab = 1;
            mps_page_set_next_ptr(pool, page, &slots[slot]);
            page->prev = (uintptr_t) &slots[slot] | MPS_SLAB_EXACT;

            mps_page_set_next_ptr(pool, &slots[slot], page);

            mps_pool_stats_ptr(pool)[slot].total += 8 * sizeof(uintptr_t);

            p = mps_slab_page_addr(pool, page);

            mps_pool_stats_ptr(pool)[slot].used++;

            goto done;

        } else { /* shift > mps_slab_exact_shift */

            page->slab = ((uintptr_t) 1 << MPS_SLAB_MAP_SHIFT) | shift;
            mps_page_set_next_ptr(pool, page, &slots[slot]);
            page->prev = (uintptr_t) &slots[slot] | MPS_SLAB_BIG;

            mps_page_set_next_ptr(pool, &slots[slot], page);

            mps_pool_stats_ptr(pool)[slot].total += mps_pagesize >> shift;

            p = mps_slab_page_addr(pool, page);

            mps_pool_stats_ptr(pool)[slot].used++;

            goto done;
        }
    }

    p = 0;

    mps_pool_stats_ptr(pool)[slot].fails++;

done:
    printf("mps_slab_alloc_locked done, alloc=%p\n", (void *) p);

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %p", (void *) p);

    return (void *) p;
}


void *
mps_slab_calloc(mps_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = mps_slab_calloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


void *
mps_slab_calloc_locked(mps_slab_pool_t *pool, size_t size)
{
    void  *p;

    p = mps_slab_alloc_locked(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


void
mps_slab_free(mps_slab_pool_t *pool, void *p)
{
    ngx_shmtx_lock(&pool->mutex);

    mps_slab_free_locked(pool, p);

    ngx_shmtx_unlock(&pool->mutex);
}


void
mps_slab_free_locked(mps_slab_pool_t *pool, void *p)
{
    size_t            size;
    uintptr_t         slab, m, *bitmap;
    ngx_uint_t        i, n, type, slot, shift, map;
    mps_slab_page_t  *slots, *page, *next;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab free: %p", p);

    if ((u_char *) p < mps_pool_start_ptr(pool) || (u_char *) p > mps_pool_end_ptr(pool)) {
        mps_slab_error(pool, NGX_LOG_ALERT, "mps_slab_free(): outside of pool");
        goto fail;
    }

    n = ((u_char *) p - mps_pool_start_ptr(pool)) >> mps_pagesize_shift;
    page = &mps_pool_pages_ptr(pool)[n];
    slab = page->slab;
    type = mps_slab_page_type(page);

    switch (type) {

    case MPS_SLAB_SMALL:

        shift = slab & MPS_SLAB_SHIFT_MASK;
        size = (size_t) 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        n = ((uintptr_t) p & (mps_pagesize - 1)) >> shift;
        m = (uintptr_t) 1 << (n % (8 * sizeof(uintptr_t)));
        n /= 8 * sizeof(uintptr_t);
        bitmap = (uintptr_t *)
                             ((uintptr_t) p & ~((uintptr_t) mps_pagesize - 1));

        if (bitmap[n] & m) {
            slot = shift - pool->min_shift;

            if (mps_page_next_ptr(pool, page) == NULL) {
                slots = mps_slab_slots(pool);

                next = mps_page_next_ptr(pool, &slots[slot]);
                mps_page_set_next_ptr(pool, page, next);
                mps_page_set_next_ptr(pool, &slots[slot], page);

                page->prev = (uintptr_t) &slots[slot] | MPS_SLAB_SMALL;
                next->prev = (uintptr_t) page | MPS_SLAB_SMALL;
            }

            bitmap[n] &= ~m;

            n = (mps_pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            i = n / (8 * sizeof(uintptr_t));
            m = ((uintptr_t) 1 << (n % (8 * sizeof(uintptr_t)))) - 1;

            if (bitmap[i] & ~m) {
                goto done;
            }

            map = (mps_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                if (bitmap[i]) {
                    goto done;
                }
            }

            mps_slab_free_pages(pool, page, 1);

            mps_pool_stats_ptr(pool)[slot].total -= (mps_pagesize >> shift) - n;

            goto done;
        }

        goto chunk_already_free;

    case MPS_SLAB_EXACT:

        m = (uintptr_t) 1 <<
                (((uintptr_t) p & (mps_pagesize - 1)) >> mps_slab_exact_shift);
        size = mps_slab_exact_size;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        if (slab & m) {
            slot = mps_slab_exact_shift - pool->min_shift;

            if (slab == MPS_SLAB_BUSY) {
                slots = mps_slab_slots(pool);

                next = mps_page_next_ptr(pool, &slots[slot]);
                mps_page_set_next_ptr(pool, page, next);
                mps_page_set_next_ptr(pool, &slots[slot], page);

                page->prev = (uintptr_t) &slots[slot] | MPS_SLAB_EXACT;
                next->prev = (uintptr_t) page | MPS_SLAB_EXACT;
            }

            page->slab &= ~m;

            if (page->slab) {
                goto done;
            }

            mps_slab_free_pages(pool, page, 1);

            mps_pool_stats_ptr(pool)[slot].total -= 8 * sizeof(uintptr_t);

            goto done;
        }

        goto chunk_already_free;

    case MPS_SLAB_BIG:

        shift = slab & MPS_SLAB_SHIFT_MASK;
        size = (size_t) 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        m = (uintptr_t) 1 << ((((uintptr_t) p & (mps_pagesize - 1)) >> shift)
                              + MPS_SLAB_MAP_SHIFT);

        if (slab & m) {
            slot = shift - pool->min_shift;

            if (mps_page_next_ptr(pool, page) == NULL) {
                slots = mps_slab_slots(pool);

                next = mps_page_next_ptr(pool, &slots[slot]);
                mps_page_set_next_ptr(pool, page, next);
                mps_page_set_next_ptr(pool, &slots[slot], page);

                page->prev = (uintptr_t) &slots[slot] | MPS_SLAB_BIG;
                next->prev = (uintptr_t) page | MPS_SLAB_BIG;
            }

            page->slab &= ~m;

            if (page->slab & MPS_SLAB_MAP_MASK) {
                goto done;
            }

            mps_slab_free_pages(pool, page, 1);

            mps_pool_stats_ptr(pool)[slot].total -= mps_pagesize >> shift;

            goto done;
        }

        goto chunk_already_free;

    case MPS_SLAB_PAGE:

        if ((uintptr_t) p & (mps_pagesize - 1)) {
            goto wrong_chunk;
        }

        if (!(slab & MPS_SLAB_PAGE_START)) {
            mps_slab_error(pool, NGX_LOG_ALERT,
                           "mps_slab_free(): page is already free");
            goto fail;
        }

        if (slab == MPS_SLAB_PAGE_BUSY) {
            mps_slab_error(pool, NGX_LOG_ALERT,
                           "mps_slab_free(): pointer to wrong page");
            goto fail;
        }

        size = slab & ~MPS_SLAB_PAGE_START;

        mps_slab_free_pages(pool, page, size);

        mps_slab_junk(p, size << mps_pagesize_shift);

        return;
    }

    /* not reached */

    return;

done:

    mps_pool_stats_ptr(pool)[slot].used--;

    mps_slab_junk(p, size);

    return;

wrong_chunk:

    mps_slab_error(pool, NGX_LOG_ALERT,
                   "mps_slab_free(): pointer to wrong chunk");

    goto fail;

chunk_already_free:

    mps_slab_error(pool, NGX_LOG_ALERT,
                   "mps_slab_free(): chunk is already free");

fail:

    return;
}


static mps_slab_page_t *
mps_slab_alloc_pages(mps_slab_pool_t *pool, ngx_uint_t pages)
{
    mps_slab_page_t  *page, *p, *next;

    for (page = mps_page_next_ptr(pool, &pool->free); page != &pool->free;
         page = mps_page_next_ptr(pool, page))
    {
        if (page->slab >= pages) {

            if (page->slab > pages) {
                page[page->slab - 1].prev = (uintptr_t) &page[pages];

                page[pages].slab = page->slab - pages;
                next = mps_page_next_ptr(pool, page);
                mps_page_set_next_ptr(pool, &page[pages], next);
                page[pages].prev = page->prev;

                p = (mps_slab_page_t *) page->prev;
                mps_page_set_next_ptr(pool, p, &page[pages]);
                next->prev = (uintptr_t) &page[pages];

            } else {
                p = (mps_slab_page_t *) page->prev;
                next = mps_page_next_ptr(pool, page);
                mps_page_set_next_ptr(pool, p, next);
                next->prev = page->prev;
            }

            page->slab = pages | MPS_SLAB_PAGE_START;
            mps_page_set_next_ptr(pool, page, NULL);
            page->prev = MPS_SLAB_PAGE;

            pool->pfree -= pages;

            if (--pages == 0) {
                return page;
            }

            for (p = page + 1; pages; pages--) {
                p->slab = MPS_SLAB_PAGE_BUSY;
                mps_page_set_next_ptr(pool, p, NULL);
                p->prev = MPS_SLAB_PAGE;
                p++;
            }

            return page;
        }
    }

    if (pool->log_nomem) {
        mps_slab_error(pool, NGX_LOG_CRIT,
                       "mps_slab_alloc() failed: no memory");
    }

    return NULL;
}


static void
mps_slab_free_pages(mps_slab_pool_t *pool, mps_slab_page_t *page,
    ngx_uint_t pages)
{
    mps_slab_page_t  *prev, *join, *next;

    pool->pfree += pages;

    page->slab = pages--;

    if (pages) {
        ngx_memzero(&page[1], pages * sizeof(mps_slab_page_t));
    }

    if (mps_page_next_ptr(pool, page)) {
        prev = mps_slab_page_prev(page);
        next = mps_page_next_ptr(pool, page);
        mps_page_set_next_ptr(pool, prev, next);
        next->prev = page->prev;
    }

    join = page + page->slab;

    if (join < mps_pool_last_ptr(pool)) {

        if (mps_slab_page_type(join) == MPS_SLAB_PAGE) {

            if (mps_page_next_ptr(pool, join) != NULL) {
                pages += join->slab;
                page->slab += join->slab;

                prev = mps_slab_page_prev(join);
                next = mps_page_next_ptr(pool, join);
                mps_page_set_next_ptr(pool, prev, next);
                next->prev = join->prev;

                join->slab = MPS_SLAB_PAGE_FREE;
                mps_page_set_next_ptr(pool, join, NULL);
                join->prev = MPS_SLAB_PAGE;
            }
        }
    }

    if (page > mps_pool_pages_ptr(pool)) {
        join = page - 1;

        if (mps_slab_page_type(join) == MPS_SLAB_PAGE) {

            if (join->slab == MPS_SLAB_PAGE_FREE) {
                join = mps_slab_page_prev(join);
            }

            if (mps_page_next_ptr(pool, join) != NULL) {
                pages += join->slab;
                join->slab += page->slab;

                prev = mps_slab_page_prev(join);
                next = mps_page_next_ptr(pool, join);
                mps_page_set_next_ptr(pool, prev, next);
                next->prev = join->prev;

                page->slab = MPS_SLAB_PAGE_FREE;
                mps_page_set_next_ptr(pool, page, NULL);
                page->prev = MPS_SLAB_PAGE;

                page = join;
            }
        }
    }

    if (pages) {
        page[pages].prev = (uintptr_t) page;
    }

    page->prev = (uintptr_t) &pool->free;
    next = mps_page_next_ptr(pool, &pool->free);
    mps_page_set_next_ptr(pool, page, next);

    next->prev = (uintptr_t) page;

    mps_page_set_next_ptr(pool, &pool->free, page);
}


static void
mps_slab_error(mps_slab_pool_t *pool, ngx_uint_t level, char *text)
{
    ngx_log_error(level, ngx_cycle->log, 0, "%s%s", text, pool->log_ctx);
}
