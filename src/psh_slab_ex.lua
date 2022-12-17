local ffi = require "ffi"
local S = ffi.load("psh_slab")

ffi.cdef[[
    typedef int ngx_int_t;
    typedef unsigned int ngx_uint_t;
    typedef uint64_t size_t;
    typedef unsigned char u_char;

    typedef struct psh_slab_page_s  psh_slab_page_t;

    struct psh_slab_page_s {
        uintptr_t         slab;
        psh_slab_page_t  *next;
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
    } psh_slab_stat_t;
    
    
    typedef struct {
        ngx_shmtx_sh_t    lock;
    
        size_t            min_size;
        size_t            min_shift;
    
        psh_slab_page_t  *pages;
        psh_slab_page_t  *last;
        psh_slab_page_t   free;
    
        psh_slab_stat_t  *stats;
        ngx_uint_t        pfree;
    
        u_char           *start;
        u_char           *end;
    
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
    void psh_slab_free(psh_slab_pool_t *pool, void *p);
    void psh_slab_free_locked(psh_slab_pool_t *pool, void *p);
]]

local pagesize = 4096
S.psh_slab_sizes_init(pagesize)
local pagecount = 10
local pool_size = pagesize * pagecount
local pool_buf = ffi.new("u_char[?]", pool_size)
print(string.format("pool_buf=%s", pool_buf))

local pool = ffi.cast("psh_slab_pool_t *", pool_buf)
S.psh_slab_init(pool, pool_buf, pool_size)

local p = S.psh_slab_alloc_locked(pool, 80)
print(string.format("p=%s", p))
local q = S.psh_slab_alloc_locked(pool, 128)
print(string.format("q=%s", q))

S.psh_slab_free_locked(pool, q)
S.psh_slab_free_locked(pool, p)
