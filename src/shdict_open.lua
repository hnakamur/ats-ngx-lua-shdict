---@diagnostic disable: lowercase-global
ngx = {
    config = {
        subsystem = 'http',
        ngx_lua_version = 10022,
    }
}

local shdict_c_api = require "ats_ngx_http_lua_shdict"
print(string.format("shdict_c_api=%s", shdict_c_api))
shdict_c_api.open_multi{"foo", "bar"}

ngx.shared = {}

require "resty.core.shdict"
