---@diagnostic disable: lowercase-global
ngx = {
    config = {
        subsystem = 'http',
        ngx_lua_version = 10022,
    }
}

local base = require "resty.core.base"
local ffi = require "ffi"
local S = ffi.load("ats_ngx_http_lua_shdict")

-- local get_size_ptr = base.get_size_ptr

ffi.cdef[[
    typedef int ngx_int_t;
    typedef unsigned int ngx_uint_t;
    typedef uint64_t size_t;
    typedef unsigned char u_char;

    typedef struct {
        size_t      len;
        u_char     *data;
    } ngx_str_t;
    
    typedef struct {
        u_char      *addr;
        size_t       size;
        ngx_str_t    name;
        void /* ngx_log_t */  *log;
        ngx_uint_t   exists;   /* unsigned  exists:1;  */
        int          fd;
    } ngx_shm_t;

    typedef struct ngx_shm_zone_s  ngx_shm_zone_t;

    typedef ngx_int_t (*ngx_shm_zone_init_pt) (ngx_shm_zone_t *zone, void *data);
    
    struct ngx_shm_zone_s {
        void                     *data;
        ngx_shm_t                 shm;
        ngx_shm_zone_init_pt      init;
        void                     *tag;
        void                     *sync;
        ngx_uint_t                noreuse;  /* unsigned  noreuse:1; */
    };
    
    typedef struct {
        void /* ngx_log_t */                  *log;
        void /* ngx_http_lua_main_conf_t */   *lmcf;
        void /* ngx_cycle_t */                *cycle;
        ngx_shm_zone_t               zone;
    } ngx_http_lua_shm_zone_ctx_t;

    ngx_int_t ngx_shm_alloc(ngx_shm_t *shm);

    typedef struct ngx_slab_page_s  ngx_slab_page_t;

    struct ngx_slab_page_s {
        uintptr_t         slab;
        ngx_slab_page_t  *next;
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
    } ngx_slab_stat_t;
    
    
    typedef struct {
        ngx_shmtx_sh_t    lock;
    
        size_t            min_size;
        size_t            min_shift;
    
        ngx_slab_page_t  *pages;
        ngx_slab_page_t  *last;
        ngx_slab_page_t   free;
    
        ngx_slab_stat_t  *stats;
        ngx_uint_t        pfree;
    
        u_char           *start;
        u_char           *end;
    
        ngx_shmtx_t       mutex;
    
        u_char           *log_ctx;
        u_char            zero;
    
        unsigned          log_nomem:1;
    
        void             *data;
        void             *addr;
    } ngx_slab_pool_t;    

    void ngx_set_pagesize(ngx_uint_t pagesize);
    void ngx_slab_sizes_init(void);
    void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);
    void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);
    void ngx_slab_free(ngx_slab_pool_t *pool, void *p);
]]

local ngx_http_lua_shm_zone_ctx_t = ffi.typeof("ngx_http_lua_shm_zone_ctx_t")

local ngx_shm_t = ffi.typeof("ngx_shm_t[1]")

local shm = ngx_shm_t()[0]

local name = "dict1"
local name_buf = base.get_string_buf(#name, true)
ffi.copy(name_buf, name, #name)
shm.name.data = name_buf
shm.name.len = ffi.new("uint64_t", #name)
shm.size = ffi.new("size_t", 1048576)
print(string.format("shm=%s", shm))

local rc = S.ngx_shm_alloc(shm)
print(string.format("rc=%d, shm->addr=%s", rc, shm.addr))
local key_val = ffi.string(shm.addr + 0x20c4, 6)
print(string.format("key_val=%s!", key_val))
local sp = ffi.cast("ngx_slab_pool_t *", shm.addr)
print(string.format("sp=%s", sp))
print(string.format("sp min_size=%d, min_shift=%d", sp.min_size, sp.min_shift))

S.ngx_set_pagesize(4096)
S.ngx_slab_sizes_init();
local buf = S.ngx_slab_alloc_locked(sp, ffi.new("size_t", 80))
print(string.format("buf=%s", buf))
-- local msg = "Hi, there"
-- ffi.copy(buf, msg, #msg)
-- S.ngx_slab_free(sp, buf)
