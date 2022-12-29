local shdict = require "mps_stderr_shdict"
local bit = require "bit"

local dict_name = "my_dict1"
local pagesize = 4096
local pagecount = 10
local pool_size = pagesize * pagecount
local mode = bit.bor(shdict.S_IRUSR, shdict.S_IWUSR,
                     shdict.S_IRGRP, shdict.S_IWGRP,
                     shdict.S_IROTH, shdict.S_IWOTH)
local my_dict1 = shdict.open_or_create(dict_name, pool_size, mode)

local success, err, forcible = my_dict1:set("key1", "value1")
print(string.format("success=%s, err=%s, forcible=%s", success, err, forcible))

local value, flags = my_dict1:get("key1")
print(string.format("value=%s, flags=%s", value, flags))