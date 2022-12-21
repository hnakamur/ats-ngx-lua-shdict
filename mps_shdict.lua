---@diagnostic disable: lowercase-global
ngx = {
    config = {
        subsystem = 'http',
        ngx_lua_version = 10022,
    }
}

local ffi = require "ffi"
local S = ffi.load("mps_luadict")
local base = require "resty.core.base"
local sleep = require "sleep"

local ffi_new = ffi.new
local ffi_str = ffi.string
local C = ffi.C
local get_string_buf = base.get_string_buf
local get_string_buf_size = base.get_string_buf_size
local get_size_ptr = base.get_size_ptr

ffi.cdef[[
    typedef struct pthread_mutex_t {
        union {
            char __size[40];
            long int __align;
        };
    } pthread_mutex_t;

    typedef int       mps_err_t;
    typedef uintptr_t mps_ptroff_t;
    typedef int ngx_int_t;
    typedef unsigned int ngx_uint_t;
    typedef uint64_t size_t;
    typedef unsigned char u_char;

    typedef struct mps_slab_page_s  mps_slab_page_t;

    struct mps_slab_page_s {
        uintptr_t         slab;
        mps_ptroff_t      next;
        mps_ptroff_t      prev;
    };

    typedef unsigned long               ngx_atomic_uint_t;
    typedef volatile ngx_atomic_uint_t  ngx_atomic_t;

    typedef struct {
        ngx_atomic_t   lock;
        ngx_atomic_t   wait;
    } ngx_shmtx_sh_t;

    typedef struct {
        ngx_uint_t        total;
        ngx_uint_t        used;
    
        ngx_uint_t        reqs;
        ngx_uint_t        fails;
    } mps_slab_stat_t;
    
    typedef struct {
        pthread_mutex_t   mutex;
        mps_ptroff_t      data;
    
        size_t            min_size;
        size_t            min_shift;
    
        mps_ptroff_t      pages;
        mps_ptroff_t      last;
        mps_slab_page_t   free;
    
        mps_ptroff_t      stats;
        ngx_uint_t        pfree;
    
        mps_ptroff_t      start;
        mps_ptroff_t      end;
    
        u_char           *log_ctx;
        u_char            zero;
    
        unsigned          log_nomem:1;
    } mps_slab_pool_t;    

    void mps_slab_sizes_init(size_t pagesize);
    mps_slab_pool_t *mps_luadict_open_or_create(const char *shm_name, size_t shm_size);

    int mps_luadict_get(mps_slab_pool_t *pool, const unsigned char *key,
        size_t key_len, int *value_type, unsigned char **str_value_buf,
        size_t *str_value_len, double *num_value, int *user_flags,
        int get_stale, int *is_stale, char **errmsg);

    int mps_luadict_store(mps_slab_pool_t *pool, int op,
        const unsigned char *key, size_t key_len, int value_type,
        const unsigned char *str_value_buf, size_t str_value_len,
        double num_value, long exptime, int user_flags, char **errmsg,
        int *forcible);    
]]

local mps_luadict_get
local mps_luadict_store

mps_luadict_get = function(pool, key, key_len, value_type,
    str_value_buf, value_len,
    num_value, user_flags,
    get_stale, is_stale, errmsg)

    return S.mps_luadict_get(pool, key, key_len, value_type,
            str_value_buf, value_len,
            num_value, user_flags,
            get_stale, is_stale, errmsg)
end

mps_luadict_store = function(pool, op,
    key, key_len, value_type,
    str_value_buf, str_value_len,
    num_value, exptime, user_flags,
    errmsg, forcible)

    return S.mps_luadict_store(pool, op,
            key, key_len, value_type,
            str_value_buf, str_value_len,
            num_value, exptime, user_flags,
            errmsg, forcible)
end


if not pcall(function () return C.free end) then
    ffi.cdef[[
void free(void *ptr);
    ]]
end


local value_type = ffi_new("int[1]")
local user_flags = ffi_new("int[1]")
local num_value = ffi_new("double[1]")
local is_stale = ffi_new("int[1]")
local forcible = ffi_new("int[1]")
local str_value_buf = ffi_new("unsigned char *[1]")
local errmsg = base.get_errmsg_ptr()


local function check_pool(pool)
    return pool
end

local function shdict_store(pool, op, key, value, exptime, flags)
    print(string.format("shdict_store start, op=%s, key=%s, value=%s", op, key, value))
    pool = check_pool(pool)

    if not exptime then
        exptime = 0
    elseif exptime < 0 then
        error('bad "exptime" argument', 2)
    end

    if not flags then
        flags = 0
    end

    if key == nil then
        return nil, "nil key"
    end

    if type(key) ~= "string" then
        key = tostring(key)
    end

    local key_len = #key
    if key_len == 0 then
        return nil, "empty key"
    end
    if key_len > 65535 then
        return nil, "key too long"
    end

    local str_val_buf
    local str_val_len = 0
    local num_val = 0
    local valtyp_str = type(value)
    local valtyp

    -- print("value type: ", valtyp)
    -- print("exptime: ", exptime)

    if valtyp_str == "string" then
        valtyp = 4  -- LUA_TSTRING
        str_val_buf = value
        str_val_len = #value

    elseif valtyp_str == "number" then
        valtyp = 3  -- LUA_TNUMBER
        num_val = value

    elseif value == nil then
        valtyp = 0  -- LUA_TNIL

    elseif valtyp_str == "boolean" then
        valtyp = 1  -- LUA_TBOOLEAN
        num_val = value and 1 or 0

    else
        return nil, "bad value type"
    end

    local rc = mps_luadict_store(pool, op, key, key_len,
                                        valtyp, str_val_buf,
                                        str_val_len, num_val,
                                        exptime * 1000, flags, errmsg,
                                        forcible)

    -- print("rc == ", rc)

    if rc == 0 then  -- NGX_OK
        return true, nil, forcible[0] == 1
    end

    -- NGX_DECLINED or NGX_ERROR
    return false, ffi_str(errmsg[0]), forcible[0] == 1
end


local metatable = {}
metatable.__index = metatable

function metatable:get(key)
    pool = check_pool(self)

    if key == nil then
        return nil, "nil key"
    end

    if type(key) ~= "string" then
        key = tostring(key)
    end

    local key_len = #key
    if key_len == 0 then
        return nil, "empty key"
    end
    if key_len > 65535 then
        return nil, "key too long"
    end

    local size = get_string_buf_size()
    local buf = get_string_buf(size)
    str_value_buf[0] = buf
    local value_len = get_size_ptr()
    value_len[0] = size

    local rc = mps_luadict_get(pool, key, key_len, value_type,
                                      str_value_buf, value_len,
                                      num_value, user_flags, 0,
                                      is_stale, errmsg)
    if rc ~= 0 then
        if errmsg[0] ~= nil then
            return nil, ffi_str(errmsg[0])
        end

        error("failed to get the key")
    end

    local typ = value_type[0]

    if typ == 0 then -- LUA_TNIL
        return nil
    end

    local flags = tonumber(user_flags[0])

    local val

    if typ == 4 then -- LUA_TSTRING
        if str_value_buf[0] ~= buf then
            -- ngx.say("len: ", tonumber(value_len[0]))
            buf = str_value_buf[0]
            val = ffi_str(buf, value_len[0])
            C.free(buf)
        else
            val = ffi_str(buf, value_len[0])
        end

    elseif typ == 3 then -- LUA_TNUMBER
        val = tonumber(num_value[0])

    elseif typ == 1 then -- LUA_TBOOLEAN
        val = (tonumber(buf[0]) ~= 0)

    else
        error("unknown value type: " .. typ)
    end

    if flags ~= 0 then
        return val, flags
    end

    return val
end

function metatable:set(key, value, exptime, flags)
    return shdict_store(self, 0, key, value, exptime, flags)
end


function metatable:safe_set(key, value, exptime, flags)
    return shdict_store(self, 0x0004, key, value, exptime, flags)
end


function metatable:add(key, value, exptime, flags)
    return shdict_store(self, 0x0001, key, value, exptime, flags)
end


function metatable:safe_add(key, value, exptime, flags)
    return shdict_store(self, 0x0005, key, value, exptime, flags)
end


function metatable:replace(key, value, exptime, flags)
    return shdict_store(self, 0x0002, key, value, exptime, flags)
end


function metatable:delete(key)
    return self:set(key, nil)
end

ffi.metatype('mps_slab_pool_t', metatable)

return {
    slab_sizes_init = S.mps_slab_sizes_init,
    open_or_create = S.mps_luadict_open_or_create,
}
