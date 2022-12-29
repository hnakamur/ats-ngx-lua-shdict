
local ffi = require "ffi"
local C = ffi.C
local S = ffi.load("mps_ngx_shdict")

local str_buf_size = 4096
local str_buf
local size_ptr
local c_buf_type = ffi.typeof("char[?]")

local function get_string_buf(size, must_alloc)
    if size > str_buf_size or must_alloc then
        return ffi.new(c_buf_type, size)
    end

    if not str_buf then
        str_buf = ffi.new(c_buf_type, str_buf_size)
    end

    return str_buf
end

local function get_string_buf_size()
    return str_buf_size
end

local function get_size_ptr()
    if not size_ptr then
        size_ptr = ffi.new("size_t[1]")
    end

    return size_ptr
end

-- local FFI_OK = 0
-- local FFI_ERROR = -1
-- local FFI_AGAIN = -2
-- local FFI_BUSY = -3
-- local FFI_DONE = -4
local FFI_DECLINED = -5

ffi.cdef[[
    void free(void *ptr);

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
    typedef unsigned int mode_t;

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

    typedef struct {
        size_t      len;
        u_char     *data;
    } ngx_str_t;
        
    typedef struct {
        mps_slab_pool_t  *pool;
        ngx_str_t         name;
    } mps_shdict_t;

    mps_shdict_t *mps_shdict_open_or_create(const char *dict_name,
        size_t shm_size, size_t min_shift, mode_t mode);

    void mps_shdict_close(mps_shdict_t *dict);

    int mps_shdict_store(mps_shdict_t *dict, int op, const u_char *key,
        size_t key_len, int value_type,
        const u_char *str_value_buf, size_t str_value_len,
        double num_value, long exptime, int user_flags,
        char **errmsg, int *forcible);
        
    int mps_shdict_get(mps_shdict_t *dict, const u_char *key,
        size_t key_len, int *value_type, u_char **str_value_buf,
        size_t *str_value_len, double *num_value, int *user_flags,
        int get_stale, int *is_stale, char **errmsg);

    int mps_shdict_incr(mps_shdict_t *dict, const u_char *key,
        size_t key_len, double *value, char **err, int has_init, double init,
        long init_ttl, int *forcible);

    int mps_shdict_flush_all(mps_shdict_t *dict);

    long mps_shdict_get_ttl(mps_shdict_t *dict, const u_char *key,
        size_t key_len);

    int mps_shdict_set_expire(mps_shdict_t *dict, const u_char *key,
        size_t key_len, long exptime);

    size_t mps_shdict_capacity(mps_shdict_t *dict);

    size_t mps_shdict_free_space(mps_shdict_t *dict);
]]

local value_type = ffi.new("int[1]")
local user_flags = ffi.new("int[1]")
local num_value = ffi.new("double[1]")
local is_stale = ffi.new("int[1]")
local forcible = ffi.new("int[1]")
local str_value_buf = ffi.new("unsigned char *[1]")
local errmsg = ffi.new("char *[1]")

local function shdict_store(dict, op, key, value, exptime, flags)
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

    local rc = S.mps_shdict_store(dict, op, key, key_len,
                                  valtyp, str_val_buf,
                                  str_val_len, num_val,
                                  exptime * 1000, flags, errmsg,
                                  forcible)

    -- print("rc == ", rc)

    if rc == 0 then  -- NGX_OK
        return true, nil, forcible[0] == 1
    end

    -- NGX_DECLINED or NGX_ERROR
    return false, ffi.string(errmsg[0]), forcible[0] == 1
end

local metatable = {}
metatable.__index = metatable

function metatable:get(key)
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

    local rc = S.mps_shdict_get(self, key, key_len, value_type,
                                str_value_buf, value_len,
                                num_value, user_flags, 0,
                                is_stale, errmsg)
    if rc ~= 0 then
        if errmsg[0] ~= nil then
            return nil, ffi.string(errmsg[0])
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
            val = ffi.string(buf, value_len[0])
            C.free(buf)
        else
            val = ffi.string(buf, value_len[0])
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

function metatable:close()
    S.mps_shdict_close(self)
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

function metatable:incr(key, value, init, init_ttl)
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

    if type(value) ~= "number" then
        value = tonumber(value)
    end
    num_value[0] = value

    if init then
        local typ = type(init)
        if typ ~= "number" then
            init = tonumber(init)

            if not init then
                error("bad init arg: number expected, got " .. typ, 2)
            end
        end
    end

    if init_ttl ~= nil then
        local typ = type(init_ttl)
        if typ ~= "number" then
            init_ttl = tonumber(init_ttl)

            if not init_ttl then
                error("bad init_ttl arg: number expected, got " .. typ, 2)
            end
        end

        if init_ttl < 0 then
            error('bad "init_ttl" argument', 2)
        end

        if not init then
            error('must provide "init" when providing "init_ttl"', 2)
        end

    else
        init_ttl = 0
    end

    local rc = S.mps_shdict_incr(self, key, key_len, num_value,
                                 errmsg, init and 1 or 0,
                                 init or 0, init_ttl * 1000,
                                 forcible)
    if rc ~= 0 then  -- ~= NGX_OK
        return nil, ffi.string(errmsg[0])
    end

    if not init then
        return tonumber(num_value[0])
    end

    return tonumber(num_value[0]), nil, forcible[0] == 1
end

function metatable:flush_all()
    S.mps_shdict_flush_all(self)
end

function metatable:ttl(key)
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

    local rc = S.mps_shdict_get_ttl(self, key, key_len)

    if rc == FFI_DECLINED then
        return nil, "not found"
    end

    return tonumber(rc) / 1000
end

function metatable:expire(key, exptime)
    if not exptime then
        error('bad "exptime" argument', 2)
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

    local rc = S.mps_shdict_set_expire(self, key, key_len, exptime * 1000)

    if rc == FFI_DECLINED then
        return nil, "not found"
    end

    -- NGINX_OK/FFI_OK

    return true
end

function metatable:capacity()
    return tonumber(S.mps_shdict_capacity(self))
end

function metatable:free_space()
    return tonumber(S.mps_shdict_free_space(self))
end

ffi.metatype('mps_shdict_t', metatable)

local MPS_SLAB_DEFAULT_MIN_SHIFT = 3

local function open_or_create(dict_name, shm_size, mode)
    return S.mps_shdict_open_or_create(dict_name, shm_size,
        MPS_SLAB_DEFAULT_MIN_SHIFT, mode)
end

return {
    open_or_create = open_or_create,
    S_IRUSR = 0x100,
    S_IWUSR = 0x080,
    S_IRGRP = 0x020,
    S_IWGRP = 0x010,
    S_IROTH = 0x004,
    S_IWOTH = 0x002,
}
