local mps_shdict = require "mps_shdict"
local bit = require "bit"

local pagesize = 4096
local pagecount = 10
local pool_size = pagesize * pagecount

local mode = bit.bor(mps_shdict.S_IRUSR, mps_shdict.S_IWUSR,
                     mps_shdict.S_IRGRP, mps_shdict.S_IWGRP,
                     mps_shdict.S_IROTH, mps_shdict.S_IWOTH)
local dict = mps_shdict.open_or_create("/my_dict1", pool_size, mode)
print(string.format("dict=%s, type=%s", dict, type(dict)))

local success, err, forcible = dict:set("foo", "value1")
print(string.format("shdict_set#1, success=%s, err=%s, forcible=%s", success, err, forcible))

success, err, forcible = dict:set("bar", "value2")
print(string.format("shdict_set#2, success=%s, err=%s, forcible=%s", success, err, forcible))

local value, flags = dict:get("foo")
print(string.format("shdict_get#1, value=%s, flags=%s", value, flags))

value, flags = dict:get("foo")
print(string.format("shdict_get#1-2, value=%s, flags=%s", value, flags))

value, flags = dict:get("bar")
print(string.format("shdict_get#2, value=%s, flags=%s", value, flags))

print("before shdict_delete")
success, err, forcible = dict:delete("foo")
print(string.format("shdict_delete, success=%s, err=%s, forcible=%s", success, err, forcible))

value, flags = dict:get("foo")
print(string.format("shdict_get, value=%s, flags=%s", value, flags))

success, err, forcible = dict:set("count", 0)
print(string.format("set(\"count\", 0), success=%s, err=%s, forcible=%s", success, err, forcible))

value, err, forcible = dict:incr("count", 1)
print(string.format("incr(\"count\", 1), value=%d, err=%s, forcible=%s", value, err, forcible))

value, err, forcible = dict:incr("count", 2)
print(string.format("incr(\"count\", 2), value=%d, err=%s, forcible=%s", value, err, forcible))
