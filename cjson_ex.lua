local cjson = require "cjson"
print(string.format('cjson=%s', cjson))

local json_text = '[ true, { "foo": "bar" } ]'
local value = cjson.decode(json_text)
local json_text2 = cjson.encode(value)
print(string.format("json_text2=%s", json_text2)

