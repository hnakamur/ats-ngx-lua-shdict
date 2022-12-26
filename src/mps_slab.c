
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "mps_slab.h"
#include "mps_log.h"

#define MPS_SLAB_PAGE_MASK 3
#define MPS_SLAB_PAGE 0
#define MPS_SLAB_BIG 1
#define MPS_SLAB_EXACT 2
#define MPS_SLAB_SMALL 3

#if (NGX_PTR_SIZE == 4)

#define MPS_SLAB_PAGE_FREE 0
#define MPS_SLAB_PAGE_BUSY 0xffffffff
#define MPS_SLAB_PAGE_START 0x80000000

#define MPS_SLAB_SHIFT_MASK 0x0000000f
#define MPS_SLAB_MAP_MASK 0xffff0000
#define MPS_SLAB_MAP_SHIFT 16

#define MPS_SLAB_BUSY 0xffffffff

#else /* (NGX_PTR_SIZE == 8) */

#define MPS_SLAB_PAGE_FREE 0
#define MPS_SLAB_PAGE_BUSY 0xffffffffffffffff
#define MPS_SLAB_PAGE_START 0x8000000000000000

#define MPS_SLAB_SHIFT_MASK 0x000000000000000f
#define MPS_SLAB_MAP_MASK 0xffffffff00000000
#define MPS_SLAB_MAP_SHIFT 32

#define MPS_SLAB_BUSY 0xffffffffffffffff

#endif

#define mps_pool_stats(pool)                                                   \
    ((mps_slab_stat_t *)(u_char *)(pool) + (pool)->stats)

#define mps_slab_slots(pool)                                                   \
    ((mps_slab_page_t *)((u_char *)(pool) + sizeof(mps_slab_pool_t)))

#define mps_slab_page_type(page) ((page)->prev & MPS_SLAB_PAGE_MASK)

#define mps_slab_page_prev(pool, page)                                         \
    mps_slab_page((pool), (page)->prev & ~MPS_SLAB_PAGE_MASK)

#define mps_slab_page_next(pool, page) mps_slab_page((pool), (page)->next)

#define mps_slab_page_addr(pool, page)                                         \
    (((mps_offset((pool), (page)) - (pool)->pages) << mps_pagesize_shift) +    \
     (uintptr_t)mps_ptr((pool), (pool)->start))

#if (NGX_DEBUG_MALLOC)

#define mps_slab_junk(p, size) ngx_memset(p, 0xA5, size)

#elif (NGX_HAVE_DEBUG_MALLOC)

#define mps_slab_junk(p, size)                                                 \
    if (ngx_debug_malloc)                                                      \
    ngx_memset(p, 0xA5, size)

#else

#define mps_slab_junk(p, size)

#endif

static mps_slab_page_t *mps_slab_alloc_pages(mps_slab_pool_t *pool,
                                             ngx_uint_t pages);
static void mps_slab_free_pages(mps_slab_pool_t *pool, mps_slab_page_t *page,
                                ngx_uint_t pages);

ngx_uint_t mps_pagesize;
static ngx_uint_t mps_pagesize_shift;

static ngx_uint_t mps_slab_max_size;
static ngx_uint_t mps_slab_exact_size;
static ngx_uint_t mps_slab_exact_shift;

static void mps_slab_sizes_init(ngx_uint_t pagesize)
{
    ngx_uint_t n;

    mps_pagesize = pagesize;
    for (n = mps_pagesize; n >>= 1; mps_pagesize_shift++) { /* void */
    }

    mps_slab_max_size = mps_pagesize / 2;
    mps_slab_exact_size = mps_pagesize / (8 * sizeof(uintptr_t));
    for (n = mps_slab_exact_size; n >>= 1; mps_slab_exact_shift++) {
        /* void */
    }

    TSDebug(MPS_LOG_TAG,
            "mps_slab_sizes_init pagesize=%u, shift=%u, max_sz=%u, "
            "exact_sz=%u, exact_shift=%u",
            pagesize, mps_pagesize_shift, mps_slab_max_size,
            mps_slab_exact_size, mps_slab_exact_shift);
}

static pthread_once_t mps_slab_initialized = PTHREAD_ONCE_INIT;

static void mps_slab_init_once()
{
    mps_slab_sizes_init(getpagesize());
}

static mps_err_t mps_slab_init_mutex(mps_slab_pool_t *pool)
{
    pthread_mutexattr_t attr;
    int rc;

    rc = pthread_mutexattr_init(&attr);
    if (rc != 0) {
        return rc;
    }

    rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (rc != 0) {
        return rc;
    }

    rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
        return rc;
    }

    rc = pthread_mutex_init(&pool->mutex, &attr);
    if (rc != 0) {
        return rc;
    }

    rc = pthread_mutexattr_destroy(&attr);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void mps_slab_init(mps_slab_pool_t *pool, u_char *addr, size_t pool_size)
{
    u_char *p, *start;
    size_t size;
    ngx_int_t m;
    ngx_uint_t i, n, pages;
    mps_slab_page_t *slots, *page, *last;
    mps_err_t err;

    err = mps_slab_init_mutex(pool);
    if (err != 0) {
        TSEmergency("mps_slab_init_mutex failed, err=%d", err);
    }

    pool->end = pool_size;
    pool->min_shift = 3;

    pool->min_size = (size_t)1 << pool->min_shift;

    slots = mps_slab_slots(pool);

    p = (u_char *)slots;
    size = pool->end - mps_offset(pool, p);

    mps_slab_junk(p, size);

    n = mps_pagesize_shift - pool->min_shift;

    for (i = 0; i < n; i++) {
        /* only "next" is used in list head */
        slots[i].slab = 0;
        slots[i].next = mps_offset(pool, &slots[i]);
        slots[i].prev = 0;
    }

    p += n * sizeof(mps_slab_page_t);

    pool->stats = mps_offset(pool, p);
    ngx_memzero(mps_pool_stats(pool), n * sizeof(mps_slab_stat_t));

    p += n * sizeof(mps_slab_stat_t);

    size -= n * (sizeof(mps_slab_page_t) + sizeof(mps_slab_stat_t));

    pages = (ngx_uint_t)(size / (mps_pagesize + sizeof(mps_slab_page_t)));

    pool->pages = mps_offset(pool, p);
    TSDebug(MPS_LOG_TAG, "mps_slab_init pool->pages=%" PRId64, pool->pages);
    ngx_memzero(p, pages * sizeof(mps_slab_page_t));

    page = (mps_slab_page_t *)p;

    /* only "next" is used in list head */
    pool->free.slab = 0;
    pool->free.next = mps_offset(pool, page);
    pool->free.prev = 0;

    page->slab = pages;
    page->next = mps_offset(pool, &pool->free);
    page->prev = mps_offset(pool, &pool->free);

    start = ngx_align_ptr(p + pages * sizeof(mps_slab_page_t), mps_pagesize);
    pool->start = mps_offset(pool, start);

    m = pages - (pool->end - pool->start) / mps_pagesize;
    if (m > 0) {
        pages -= m;
        page->slab = pages;
    }

    last = mps_slab_page(pool, pool->pages) + pages;
    TSDebug(MPS_LOG_TAG,
            "mps_slab_init last=%p, pool=%p, pages_ptr=%p, pool->pages=%ld, "
            "pages=%d, end=%p",
            last, pool, mps_slab_page(pool, pool->pages), pool->pages, pages,
            mps_slab_page(pool, pool->end));
    TSDebug(MPS_LOG_TAG, "sizeof(mps_slab_page_t)=%" PRId64,
            sizeof(mps_slab_page_t));
    pool->last = mps_offset(pool, last);
    pool->pfree = pages;
}

static mps_err_t mps_slab_create(mps_slab_pool_t **pool, const char *shm_name,
                                 size_t shm_size, mode_t mode,
                                 mps_slab_on_init_pt on_init)
{
    int fd;
    void *addr;
    mps_err_t err = 0;

    fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR);
    if (fd == -1) {
        return errno;
    }

    if (ftruncate(fd, shm_size) == -1) {
        err = errno;
        goto close;
    }

    addr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        err = errno;
        goto close;
    }
    *pool = (mps_slab_pool_t *)addr;

    mps_slab_init(*pool, (u_char *)addr, shm_size);
    if (on_init) {
        on_init(*pool);
    }

    if (fchmod(fd, mode) == -1) {
        err = errno;
    }

    if (err && munmap(addr, shm_size) == -1) {
        TSError("mps_slab_create: munmap: err=%s\n", strerror(errno));
    }

close:
    if (close(fd) == -1) {
        if (err == 0) {
            err = errno;
        }
    }
    return err;
}

static mps_err_t mps_slab_open(mps_slab_pool_t **pool, const char *shm_name,
                               size_t shm_size, mode_t mode)
{
    mps_err_t err;
    int fd;
    void *addr;

    fd = shm_open(shm_name, O_RDWR, mode);
    if (fd == -1) {
        return errno;
    }

    addr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        return errno;
    }
    *pool = (mps_slab_pool_t *)addr;

    if (close(fd) == -1) {
        err = errno;
        if (munmap(addr, shm_size) == -1) {
            TSError("mps_slab_open: munmap: err=%s", strerror(errno));
        }
        return err;
    }

    return 0;
}

mps_slab_pool_t *mps_slab_open_or_create(const char *shm_name, size_t shm_size,
                                         mode_t mode,
                                         mps_slab_on_init_pt on_init)
{
    mps_err_t err = 0;
    mps_slab_pool_t *pool;
    struct timespec sleep_time;
    int rc;

    rc = pthread_once(&mps_slab_initialized, mps_slab_init_once);
    if (rc != 0) {
        TSError("mps_slab_open_or_create init sizes err=%s", strerror(rc));
        return NULL;
    }

    err = mps_slab_open(&pool, shm_name, shm_size, mode);
    if (err) {
        if (err != ENOENT && err != EACCES) {
            TSError("mps_slab_open_or_create: mps_slab_open#1: err=%s",
                    strerror(err));
            return NULL;
        }

        err = mps_slab_create(&pool, shm_name, shm_size, mode, on_init);
        if (err) {
            if (err != EEXIST) {
                TSError("mps_slab_open_or_create: mps_slab_create: err=%s",
                        strerror(err));
                return NULL;
            }

            sleep_time.tv_sec = 0;
            sleep_time.tv_nsec = 10 * 1000 * 1000; // 10ms
            if (nanosleep(&sleep_time, NULL) == -1) {
                TSWarning("mps_slab_open_or_create: nanosleep: err=%s",
                          strerror(errno));
                return NULL;
            }

            err = mps_slab_open(&pool, shm_name, shm_size, mode);
            if (err) {
                TSError("mps_slab_open_or_create: mps_slab_open#2: err=%s",
                        strerror(err));
                return NULL;

            } else {
                TSStatus("mps_slab_open_or_create second open ok name=%s",
                         shm_name);
            }

        } else {
            TSStatus("mps_slab_open_or_create create ok name=%s", shm_name);
        }

    } else {
        TSStatus("mps_slab_open_or_create first open ok name=%s", shm_name);
    }
    return pool;
}

void mps_slab_close(mps_slab_pool_t *pool, size_t shm_size)
{
    if (munmap(pool, shm_size) == -1) {
        TSError("mps_slab_close: munmap: err=%s", strerror(errno));
    }
}

void mps_slab_lock(mps_slab_pool_t *pool)
{
    pthread_mutex_lock(&pool->mutex);
}

void mps_slab_unlock(mps_slab_pool_t *pool)
{
    pthread_mutex_unlock(&pool->mutex);
}

void *mps_slab_alloc(mps_slab_pool_t *pool, size_t size)
{
    void *p;

    mps_slab_lock(pool);

    p = mps_slab_alloc_locked(pool, size);

    mps_slab_unlock(pool);

    return p;
}

void *mps_slab_alloc_locked(mps_slab_pool_t *pool, size_t size)
{
    size_t s;
    uintptr_t p, m, mask, *bitmap;
    ngx_uint_t i, n, slot, shift, map;
    mps_slab_page_t *page, *slots;

    if (size > mps_slab_max_size) {

        TSDebug(MPS_LOG_TAG, "mps_slab_alloc_locked: pool=%p, alloc: size=%lu",
                pool, size);

        page = mps_slab_alloc_pages(pool, (size >> mps_pagesize_shift) +
                                              ((size % mps_pagesize) ? 1 : 0));
        if (page) {
            p = mps_slab_page_addr(pool, page);

        } else {
            p = 0;
        }

        goto done;
    }

    if (size > pool->min_size) {
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) {
            /* void */
        }
        slot = shift - pool->min_shift;

    } else {
        shift = pool->min_shift;
        slot = 0;
    }

    mps_pool_stats(pool)[slot].reqs++;

    TSDebug(MPS_LOG_TAG,
            "mps_slab_alloc_locked: pool=%p, alloc: size=%lu, slot=%lu", pool,
            size, slot);

    slots = mps_slab_slots(pool);
    page = mps_slab_page_next(pool, &slots[slot]);

    if (mps_slab_page_next(pool, page) != page) {

        if (shift < mps_slab_exact_shift) {

            bitmap = (uintptr_t *)mps_slab_page_addr(pool, page);

            map = (mps_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (n = 0; n < map; n++) {

                if (bitmap[n] != MPS_SLAB_BUSY) {

                    for (m = 1, i = 0; m; m <<= 1, i++) {
                        if (bitmap[n] & m) {
                            continue;
                        }

                        bitmap[n] |= m;

                        i = (n * 8 * sizeof(uintptr_t) + i) << shift;

                        p = (uintptr_t)bitmap + i;

                        mps_pool_stats(pool)[slot].used++;

                        if (bitmap[n] == MPS_SLAB_BUSY) {
                            for (n = n + 1; n < map; n++) {
                                if (bitmap[n] != MPS_SLAB_BUSY) {
                                    goto done;
                                }
                            }

                            mps_slab_page_prev(pool, page)->next = page->next;
                            mps_slab_page_next(pool, page)->prev = page->prev;

                            page->next = mps_nulloff;
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
                    mps_slab_page_prev(pool, page)->next = page->next;
                    mps_slab_page_next(pool, page)->prev = page->prev;

                    page->next = mps_nulloff;
                    page->prev = MPS_SLAB_EXACT;
                }

                p = mps_slab_page_addr(pool, page) + (i << shift);

                mps_pool_stats(pool)[slot].used++;

                goto done;
            }

        } else { /* shift > mps_slab_exact_shift */

            mask = ((uintptr_t)1 << (mps_pagesize >> shift)) - 1;
            mask <<= MPS_SLAB_MAP_SHIFT;

            for (m = (uintptr_t)1 << MPS_SLAB_MAP_SHIFT, i = 0; m & mask;
                 m <<= 1, i++) {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if ((page->slab & MPS_SLAB_MAP_MASK) == mask) {
                    mps_slab_page_prev(pool, page)->next = page->next;
                    mps_slab_page_next(pool, page)->prev = page->prev;

                    page->next = mps_nulloff;
                    page->prev = MPS_SLAB_BIG;
                }

                p = mps_slab_page_addr(pool, page) + (i << shift);

                mps_pool_stats(pool)[slot].used++;

                goto done;
            }
        }

        TSAlert("mps_slab_alloc_locked: page is busy: pool=%p", pool);
        ngx_debug_point();
    }

    page = mps_slab_alloc_pages(pool, 1);

    if (page) {
        if (shift < mps_slab_exact_shift) {
            bitmap = (uintptr_t *)mps_slab_page_addr(pool, page);

            n = (mps_pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            /* "n" elements for bitmap, plus one requested */

            for (i = 0; i < (n + 1) / (8 * sizeof(uintptr_t)); i++) {
                bitmap[i] = MPS_SLAB_BUSY;
            }

            m = ((uintptr_t)1 << ((n + 1) % (8 * sizeof(uintptr_t)))) - 1;
            bitmap[i] = m;

            map = (mps_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                bitmap[i] = 0;
            }

            page->slab = shift;
            page->next = mps_offset(pool, &slots[slot]);
            page->prev = mps_offset(pool, &slots[slot]) | MPS_SLAB_SMALL;

            slots[slot].next = mps_offset(pool, page);

            mps_pool_stats(pool)[slot].total += (mps_pagesize >> shift) - n;

            p = mps_slab_page_addr(pool, page) + (n << shift);

            mps_pool_stats(pool)[slot].used++;

            goto done;

        } else if (shift == mps_slab_exact_shift) {

            page->slab = 1;
            page->next = mps_offset(pool, &slots[slot]);
            page->prev = mps_offset(pool, &slots[slot]) | MPS_SLAB_EXACT;

            slots[slot].next = mps_offset(pool, page);

            mps_pool_stats(pool)[slot].total += 8 * sizeof(uintptr_t);

            p = mps_slab_page_addr(pool, page);

            mps_pool_stats(pool)[slot].used++;

            goto done;

        } else { /* shift > mps_slab_exact_shift */

            page->slab = ((uintptr_t)1 << MPS_SLAB_MAP_SHIFT) | shift;
            page->next = mps_offset(pool, &slots[slot]);
            page->prev = mps_offset(pool, &slots[slot]) | MPS_SLAB_BIG;

            slots[slot].next = mps_offset(pool, page);

            mps_pool_stats(pool)[slot].total += mps_pagesize >> shift;

            p = mps_slab_page_addr(pool, page);

            mps_pool_stats(pool)[slot].used++;

            goto done;
        }
    }

    p = 0;

    mps_pool_stats(pool)[slot].fails++;

done:
    TSDebug(MPS_LOG_TAG, "mps_slab_alloc_locked, return %p", (void *)p);

    return (void *)p;
}

void *mps_slab_calloc(mps_slab_pool_t *pool, size_t size)
{
    void *p;

    mps_slab_lock(pool);

    p = mps_slab_calloc_locked(pool, size);

    mps_slab_unlock(pool);

    return p;
}

void *mps_slab_calloc_locked(mps_slab_pool_t *pool, size_t size)
{
    void *p;

    p = mps_slab_alloc_locked(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}

void mps_slab_free(mps_slab_pool_t *pool, void *p)
{
    mps_slab_lock(pool);

    mps_slab_free_locked(pool, p);

    mps_slab_unlock(pool);
}

void mps_slab_free_locked(mps_slab_pool_t *pool, void *p)
{
    size_t size;
    uintptr_t slab, m, *bitmap;
    ngx_uint_t i, n, type, slot, shift, map;
    mps_slab_page_t *slots, *page, *next;
    mps_ptroff_t p_off;

#if 0
    TSDebug(MPS_LOG_TAG, "mps_slab_free_locked: pool=%p", pool);
#endif

    p_off = mps_offset(pool, p);
    if (p_off < pool->start || p_off > pool->end) {
        TSAlert("mps_slab_free_locked: pool=%p: outside of pool", pool);
        goto fail;
    }

    n = (p_off - pool->start) >> mps_pagesize_shift;
    page = &mps_slab_page(pool, pool->pages)[n];
    slab = page->slab;
    type = mps_slab_page_type(page);

    switch (type) {

    case MPS_SLAB_SMALL:

        shift = slab & MPS_SLAB_SHIFT_MASK;
        size = (size_t)1 << shift;

        if ((uintptr_t)p & (size - 1)) {
            goto wrong_chunk;
        }

        n = ((uintptr_t)p & (mps_pagesize - 1)) >> shift;
        m = (uintptr_t)1 << (n % (8 * sizeof(uintptr_t)));
        n /= 8 * sizeof(uintptr_t);
        bitmap = (uintptr_t *)((uintptr_t)p & ~((uintptr_t)mps_pagesize - 1));

        if (bitmap[n] & m) {
            slot = shift - pool->min_shift;

            if (page->next == 0) {
                slots = mps_slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = mps_offset(pool, page);

                page->prev = mps_offset(pool, &slots[slot]) | MPS_SLAB_SMALL;
                next = mps_slab_page_next(pool, page);
                next->prev = mps_offset(pool, page) | MPS_SLAB_SMALL;
            }

            bitmap[n] &= ~m;

            n = (mps_pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            i = n / (8 * sizeof(uintptr_t));
            m = ((uintptr_t)1 << (n % (8 * sizeof(uintptr_t)))) - 1;

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

            mps_pool_stats(pool)[slot].total -= (mps_pagesize >> shift) - n;

            goto done;
        }

        goto chunk_already_free;

    case MPS_SLAB_EXACT:

        m = (uintptr_t)1 << (((uintptr_t)p & (mps_pagesize - 1)) >>
                             mps_slab_exact_shift);
        size = mps_slab_exact_size;

        if ((uintptr_t)p & (size - 1)) {
            goto wrong_chunk;
        }

        if (slab & m) {
            slot = mps_slab_exact_shift - pool->min_shift;

            if (slab == MPS_SLAB_BUSY) {
                slots = mps_slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = mps_offset(pool, page);

                page->prev = mps_offset(pool, &slots[slot]) | MPS_SLAB_EXACT;
                next = mps_slab_page_next(pool, page);
                next->prev = mps_offset(pool, page) | MPS_SLAB_EXACT;
            }

            page->slab &= ~m;

            if (page->slab) {
                goto done;
            }

            mps_slab_free_pages(pool, page, 1);

            mps_pool_stats(pool)[slot].total -= 8 * sizeof(uintptr_t);

            goto done;
        }

        goto chunk_already_free;

    case MPS_SLAB_BIG:

        shift = slab & MPS_SLAB_SHIFT_MASK;
        size = (size_t)1 << shift;

        if ((uintptr_t)p & (size - 1)) {
            goto wrong_chunk;
        }

        m = (uintptr_t)1 << ((((uintptr_t)p & (mps_pagesize - 1)) >> shift) +
                             MPS_SLAB_MAP_SHIFT);

        if (slab & m) {
            slot = shift - pool->min_shift;

            if (page->next == 0) {
                slots = mps_slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = mps_offset(pool, page);

                page->prev = mps_offset(pool, &slots[slot]) | MPS_SLAB_BIG;
                next = mps_slab_page_next(pool, page);
                next->prev = mps_offset(pool, page) | MPS_SLAB_BIG;
            }

            page->slab &= ~m;

            if (page->slab & MPS_SLAB_MAP_MASK) {
                goto done;
            }

            mps_slab_free_pages(pool, page, 1);

            mps_pool_stats(pool)[slot].total -= mps_pagesize >> shift;

            goto done;
        }

        goto chunk_already_free;

    case MPS_SLAB_PAGE:

        if ((uintptr_t)p & (mps_pagesize - 1)) {
            goto wrong_chunk;
        }

        if (!(slab & MPS_SLAB_PAGE_START)) {
            TSAlert("mps_slab_free_locked: pool=%p: page is already free",
                    pool);
            goto fail;
        }

        if (slab == MPS_SLAB_PAGE_BUSY) {
            TSAlert("mps_slab_free_locked: pool=%p: pointer to wrong page",
                    pool);
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

    mps_pool_stats(pool)[slot].used--;

    mps_slab_junk(p, size);

    return;

wrong_chunk:

    TSAlert("mps_slab_free_locked: pool=%p: pointer to wrong chunk", pool);

    goto fail;

chunk_already_free:

    TSAlert("mps_slab_free_locked: pool=%p: chunk is already free", pool);

fail:

    return;
}

static mps_slab_page_t *mps_slab_alloc_pages(mps_slab_pool_t *pool,
                                             ngx_uint_t pages)
{
    mps_slab_page_t *page, *p, *next;

    for (page = mps_slab_page_next(pool, &pool->free); page != &pool->free;
         page = mps_slab_page_next(pool, page)) {
        if (page->slab >= pages) {

            if (page->slab > pages) {
                page[page->slab - 1].prev = (uintptr_t)&page[pages];

                page[pages].slab = page->slab - pages;
                page[pages].next = page->next;
                page[pages].prev = page->prev;

                p = mps_slab_page(pool, page->prev);
                p->next = mps_offset(pool, &page[pages]);
                next = mps_slab_page_next(pool, page);
                next->prev = mps_offset(pool, &page[pages]);

            } else {
                p = mps_slab_page(pool, page->prev);
                p->next = page->next;
                next = mps_slab_page_next(pool, page);
                next->prev = page->prev;
            }

            page->slab = pages | MPS_SLAB_PAGE_START;
            page->next = mps_nulloff;
            page->prev = MPS_SLAB_PAGE;

            pool->pfree -= pages;

            if (--pages == 0) {
                return page;
            }

            for (p = page + 1; pages; pages--) {
                p->slab = MPS_SLAB_PAGE_BUSY;
                p->next = mps_nulloff;
                p->prev = MPS_SLAB_PAGE;
                p++;
            }

            return page;
        }
    }

    TSFatal("mps_slab_free_locked: pool=%p: no memory", pool);

    return NULL;
}

static void mps_slab_free_pages(mps_slab_pool_t *pool, mps_slab_page_t *page,
                                ngx_uint_t pages)
{
    mps_slab_page_t *prev, *join, *next;

    pool->pfree += pages;

    page->slab = pages--;

    if (pages) {
        ngx_memzero(&page[1], pages * sizeof(mps_slab_page_t));
    }

    if (page->next) {
        prev = mps_slab_page_prev(pool, page);
        prev->next = page->next;
        next = mps_slab_page_next(pool, page);
        next->prev = page->prev;
    }

    join = page + page->slab;

    if (mps_offset(pool, join) < pool->last) {

        if (mps_slab_page_type(join) == MPS_SLAB_PAGE) {

            if (join->next != mps_nulloff) {
                pages += join->slab;
                page->slab += join->slab;

                prev = mps_slab_page_prev(pool, join);
                prev->next = join->next;
                next = mps_slab_page_next(pool, join);
                next->prev = join->prev;

                join->slab = MPS_SLAB_PAGE_FREE;
                join->next = mps_nulloff;
                join->prev = MPS_SLAB_PAGE;
            }
        }
    }

    if (mps_offset(pool, page) > pool->pages) {
        join = page - 1;

        if (mps_slab_page_type(join) == MPS_SLAB_PAGE) {

            if (join->slab == MPS_SLAB_PAGE_FREE) {
                join = mps_slab_page_prev(pool, join);
            }

            if (join->next != mps_nulloff) {
                pages += join->slab;
                join->slab += page->slab;

                prev = mps_slab_page_prev(pool, join);
                prev->next = join->next;
                next = mps_slab_page_next(pool, join);
                next->prev = join->prev;

                page->slab = MPS_SLAB_PAGE_FREE;
                page->next = mps_nulloff;
                page->prev = MPS_SLAB_PAGE;

                page = join;
            }
        }
    }

    if (pages) {
        page[pages].prev = mps_offset(pool, page);
    }

    page->prev = mps_offset(pool, &pool->free);
    page->next = pool->free.next;

    next = mps_slab_page_next(pool, page);
    next->prev = mps_offset(pool, page);

    pool->free.next = mps_offset(pool, page);
}
