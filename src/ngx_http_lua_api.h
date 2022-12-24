
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef _NGX_HTTP_LUA_API_H_INCLUDED_
#define _NGX_HTTP_LUA_API_H_INCLUDED_


#include <ngx_core.h>

#include <lua.h>
#include <stdint.h>


/* Public API for other Nginx modules */


typedef struct {
    uint8_t         type;

    union {
        int         b; /* boolean */
        lua_Number  n; /* number */
        ngx_str_t   s; /* string */
    } value;

} ngx_http_lua_value_t;


#endif /* _NGX_HTTP_LUA_API_H_INCLUDED_ */

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
