---@diagnostic disable: lowercase-global
ngx = {
    config = {
        subsystem = 'http',
        ngx_lua_version = 10022,
    }
}

-- local shdict_c_api = require "ats_ngx_http_lua_shdict"
-- print(string.format("shdict_c_api=%s", shdict_c_api))
-- shdict_c_api.open_multi{"foo", "bar"}

local ffi = require 'ffi'
local C = ffi.C

local shdict = ffi.load("ats_ngx_http_lua_shdict")

ffi.cdef[[
    int ngx_http_lua_ffi_shdict_get(void *zone, const unsigned char *key,
        size_t key_len, int *value_type, unsigned char **str_value_buf,
        size_t *str_value_len, double *num_value, int *user_flags,
        int get_stale, int *is_stale, char **errmsg);

    int ngx_http_lua_ffi_shdict_flush_all(void *zone);
]]

local ngx_lua_ffi_shdict_get = shdict.ngx_http_lua_ffi_shdict_get

-- function(zone, key, key_len, value_type,
--     str_value_buf, value_len,
--     num_value, user_flags,
--     get_stale, is_stale, errmsg)

--     return C.ngx_http_lua_ffi_shdict_get(zone, key, key_len, value_type,
--            str_value_buf, value_len,
--            num_value, user_flags,
--            get_stale, is_stale, errmsg)
-- end

local ngx_lua_ffi_shdict_flush_all
ngx_lua_ffi_shdict_flush_all = shdict.ngx_http_lua_ffi_shdict_flush_all

return ngx_lua_ffi_shdict_flush_all

