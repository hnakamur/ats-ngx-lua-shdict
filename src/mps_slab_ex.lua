local ffi = require "ffi"
local S = ffi.load("mps_slab")

ffi.cdef[[
    typedef int ngx_int_t;
    typedef unsigned int ngx_uint_t;
    typedef uint64_t size_t;
    typedef unsigned char u_char;

    typedef struct mps_slab_page_s  mps_slab_page_t;

    struct mps_slab_page_s {
        uintptr_t         slab;
        mps_slab_page_t  *next;
        uintptr_t         prev;
    };
    

    typedef union
    {
      char __size[32]; /* __SIZEOF_SEM_T for __WORDSIZE == 64 */
      long int __align;
    } sem_t; 

    typedef unsigned long               ngx_atomic_uint_t;
    typedef volatile ngx_atomic_uint_t  ngx_atomic_t;

    typedef struct {
        ngx_atomic_t   lock;
        ngx_atomic_t   wait;
    } ngx_shmtx_sh_t;
    
    
    typedef struct {
        ngx_atomic_t  *lock;
        ngx_atomic_t  *wait;
        ngx_uint_t     semaphore;
        sem_t          sem;
        ngx_uint_t     spin;
    } ngx_shmtx_t;
        

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
    
        mps_slab_page_t  *pages;
        mps_slab_page_t  *last;
        mps_slab_page_t   free;
    
        mps_slab_stat_t  *stats;
        ngx_uint_t        pfree;
    
        u_char           *start;
        u_char           *end;
    
        ngx_shmtx_t       mutex;
    
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
    void mps_slab_free(mps_slab_pool_t *pool, void *p);
    void mps_slab_free_locked(mps_slab_pool_t *pool, void *p);
]]

local pagesize = 4096
S.mps_slab_sizes_init(pagesize)
local pagecount = 10
local pool_size = pagesize * pagecount
local pool_buf = ffi.new("u_char[?]", pool_size)
print(string.format("pool_buf=%s", pool_buf))

local pool = ffi.cast("mps_slab_pool_t *", pool_buf)
S.mps_slab_init(pool, pool_buf, pool_size)

local p = S.mps_slab_alloc_locked(pool, 80)
print(string.format("p=%s", p))
local q = S.mps_slab_alloc_locked(pool, 128)
print(string.format("q=%s", q))

S.mps_slab_free_locked(pool, q)
S.mps_slab_free_locked(pool, p)
