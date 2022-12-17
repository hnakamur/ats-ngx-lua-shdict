#ifndef _MPS_LUA_SHDICT_H_INCLUDED_
#define _MPS_LUA_SHDICT_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <lua.h>

#include "mps_slab.h"
#include "mps_queue.h"
#include "mps_rbtree.h"

typedef struct {
    uint8_t         type;

    union {
        int         b; /* boolean */
        lua_Number  n; /* number */
        ngx_str_t   s; /* string */
    } value;

} mps_lua_value_t;


typedef struct {
    u_char                       color;
    uint8_t                      value_type;
    u_short                      key_len;
    uint32_t                     value_len;
    uint64_t                     expires;
    mps_queue_t                  queue;
    uint32_t                     user_flags;
    u_char                       data[1];
} mps_luadict_node_t;


typedef struct {
    mps_queue_t                  queue;
    uint32_t                     value_len;
    uint8_t                      value_type;
    u_char                       data[1];
} mps_luadict_list_node_t;


typedef struct {
    mps_rbtree_t                  rbtree;
    mps_rbtree_node_t             sentinel;
    mps_queue_t                   lru_queue;
} mps_luadict_t;

void *
mps_luadict_open_or_create(const char *shm_name, size_t shm_size);

#define mps_luadict(pool)                                                     \
    ((mps_luadict_t *) mps_slab_to_ptr((pool), ((pool)->data)))

#endif /* _MPS_LUA_SHDICT_H_INCLUDED_ */
