local mps_luadict = require "mps_shdict"

local pagesize = 4096
mps_luadict.slab_sizes_init(pagesize)
local pagecount = 10
local pool_size = pagesize * pagecount
local dict = mps_luadict.open_or_create("/my_dict1", pool_size)
print(string.format("dict=%s, type=%s", dict, type(dict)))

local success, err, forcible = dict:set("foo", "value1")
print(string.format("shdict_set#1, success=%s, err=%s, forcible=%s", success, err, forcible))

local success, err, forcible = dict:set("bar", "value2")
print(string.format("shdict_set#2, success=%s, err=%s, forcible=%s", success, err, forcible))

local value, flags = dict:get("foo")
print(string.format("shdict_get#1, value=%s, flags=%s", value, flags))

local value, flags = dict:get("foo")
print(string.format("shdict_get#1-2, value=%s, flags=%s", value, flags))

local value, flags = dict:get("bar")
print(string.format("shdict_get#2, value=%s, flags=%s", value, flags))

print("before shdict_delete")
local success, err, forcible = dict:delete("foo")
print(string.format("shdict_delete, success=%s, err=%s, forcible=%s", success, err, forcible))

local value, flags = dict:get("foo")
print(string.format("shdict_get, value=%s, flags=%s", value, flags))
