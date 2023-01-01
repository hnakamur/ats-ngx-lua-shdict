# ats-ngx-lua-shdict

This project is open source but closed development.
I don't accept issues or pull requests.
Feel free to fork it and modify it by yourself.

I wrote a blog post about this at [Apache Traffic Serverとnginxで使えるLuaJIT用shared dictを作ってみた](https://hnakamur.github.io/blog/2023/01/01/ats-ngx-lua-shdict/).

## Credits

This library based on the following source codes. Thanks!

* [nginx/src/core](https://github.com/nginx/nginx/tree/release-1.23.3/src/core)
* [lua-nginx-module/ngx_http_lua_shdict.c](https://github.com/openresty/lua-nginx-module/blob/f488965b89238e0bba11c13fdb9b11b4a0f20d67/src/ngx_http_lua_shdict.c), [lua-resty-core/shdict.lua](https://github.com/openresty/lua-resty-core/blob/d2179dbcb3d6d77127462cadd40cca103d89a52a/lib/resty/core/shdict.lua)
