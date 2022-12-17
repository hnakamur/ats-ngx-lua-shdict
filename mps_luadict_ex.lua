local ffi = require "ffi"
local S = ffi.load("mps_luadict")
local sleep = require "sleep"

ffi.cdef[[
    typedef uint64_t size_t;

    void mps_slab_sizes_init(size_t pagesize);
    void *mps_luadict_open_or_create(const char *shm_name, size_t shm_size);    
]]

local pagesize = 4096
S.mps_slab_sizes_init(pagesize)
local pagecount = 10
local pool_size = pagesize * pagecount
local pool = S.mps_luadict_open_or_create("/my_dict1", pool_size)
print(string.format("pool=%s", pool))
