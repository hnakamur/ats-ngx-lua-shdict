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

-- local success, err, forcible = my_dict1:set("key1", "value1")
-- print(string.format("success=%s, err=%s, forcible=%s", success, err, forcible))

-- local value, flags = my_dict1:get("key1")
-- print(string.format("value=%s, flags=%s", value, flags))

-- double-ended queue1

print("queue1 -------------------------")

local n, err
n, err = my_dict1:rpush("queue1", "val1")
print(string.format("rpush#1, n=%s, err=%s", n, err))

print("queue1 rpush#2 -------------------------")
n, err = my_dict1:rpush("queue1", "val2")
print(string.format("rpush#2, n=%s, err=%s", n, err))

print("queue1 lpush#2 -------------------------")
n, err = my_dict1:lpush("queue1", "val3")
print(string.format("lpush#1, n=%s, err=%s", n, err))

print("queue1 llen after lpush#2 -------------------------")
n, err = my_dict1:llen("queue1")
print(string.format("llen#1 n=%s, err=%s", n, err))

local val
print("queue1 lpop#1 -------------------------")
val, err = my_dict1:lpop("queue1")
print(string.format("lpop#1 val=%s, err=%s", val, err))
print("queue1 llen after lpop#1 -------------------------")
n, err = my_dict1:llen("queue1")
print(string.format("llen after lpop#1 n=%s, err=%s", n, err))

print("queue1 rpop#1 -------------------------")
val, err = my_dict1:rpop("queue1")
print(string.format("rpop#1 val=%s, err=%s", val, err))
print("queue1 llen after rpop#1 -------------------------")
n, err = my_dict1:llen("queue1")
print(string.format("llen after rpop#1 n=%s, err=%s", n, err))

print("queue1 lpop#2 -------------------------")
val, err = my_dict1:lpop("queue1")
print(string.format("lpop#2 val=%s, err=%s", val, err))
print("queue1 llen after lpop#2 -------------------------")
n, err = my_dict1:llen("queue1")
print(string.format("llen after lpop#2 n=%s, err=%s", n, err))
