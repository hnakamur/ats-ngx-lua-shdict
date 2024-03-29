#ifndef _MPS_SHDICT_H_INCLUDED_
#define _MPS_SHDICT_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <lua.h>

#include "mps_slab.h"
#include "mps_queue.h"
#include "mps_rbtree.h"

typedef struct {
    uint8_t type;

    union {
        int b;        /* boolean */
        lua_Number n; /* number */
        ngx_str_t s;  /* string */
    } value;

} mps_lua_value_t;

typedef struct {
    u_char color;
    uint8_t value_type;
    u_short key_len;
    uint32_t value_len;
    uint64_t expires;
    mps_queue_t queue;
    uint32_t user_flags;
    u_char data[1];
} mps_shdict_node_t;

typedef struct {
    mps_queue_t queue;
    uint32_t value_len;
    uint8_t value_type;
    u_char data[1];
} mps_shdict_list_node_t;

typedef struct {
    mps_rbtree_t rbtree;
    mps_rbtree_node_t sentinel;
    mps_queue_t lru_queue;
} mps_shdict_tree_t;

typedef struct {
    mps_slab_pool_t *pool;
    ngx_str_t name;
    size_t size;
} mps_shdict_t;

/* value type */
enum {
    MPS_SHDICT_TNIL = 0,     /* same as LUA_TNIL */
    MPS_SHDICT_TBOOLEAN = 1, /* same as LUA_TBOOLEAN */
    MPS_SHDICT_TNUMBER = 3,  /* same as LUA_TNUMBER */
    MPS_SHDICT_TSTRING = 4,  /* same as LUA_TSTRING */
    MPS_SHDICT_TLIST = 5,
};

mps_shdict_t *mps_shdict_open_or_create(const char *pathname, size_t shm_size,
                                        size_t min_shift, mode_t mode);
void mps_shdict_close(mps_shdict_t *dict);

/* Unconditionally set the value. */
int mps_shdict_set(mps_shdict_t *dict, const u_char *key, size_t key_len,
                   int value_type, const u_char *str_value_buf,
                   size_t str_value_len, double num_value, long exptime,
                   int user_flags, char **errmsg, int *forcible);

/* Set the value, but never overrides an unexpired value. */
int mps_shdict_safe_set(mps_shdict_t *dict, const u_char *key, size_t key_len,
                        int value_type, const u_char *str_value_buf,
                        size_t str_value_len, double num_value, long exptime,
                        int user_flags, char **errmsg, int *forcible);

/* Set the value only when the key does not exist. This overrides an expired
 * value. */
int mps_shdict_add(mps_shdict_t *dict, const u_char *key, size_t key_len,
                   int value_type, const u_char *str_value_buf,
                   size_t str_value_len, double num_value, long exptime,
                   int user_flags, char **errmsg, int *forcible);

/* Set the value only when the key does not exist. This does not override an
 * expired value. */
int mps_shdict_safe_add(mps_shdict_t *dict, const u_char *key, size_t key_len,
                        int value_type, const u_char *str_value_buf,
                        size_t str_value_len, double num_value, long exptime,
                        int user_flags, char **errmsg, int *forcible);

/* Replace the value only when the key exists. */
int mps_shdict_replace(mps_shdict_t *dict, const u_char *key, size_t key_len,
                       int value_type, const u_char *str_value_buf,
                       size_t str_value_len, double num_value, long exptime,
                       int user_flags, char **errmsg, int *forcible);

/* Unconditionally delete the value. Deleting a non existing key does not cause
 * an error. */
int mps_shdict_delete(mps_shdict_t *dict, const u_char *key, size_t key_len);

int mps_shdict_get(mps_shdict_t *dict, const u_char *key, size_t key_len,
                   int *value_type, u_char **str_value_buf,
                   size_t *str_value_len, double *num_value, int *user_flags,
                   int get_stale, int *is_stale, char **err);

int mps_shdict_incr(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    double *value, char **err, int has_init, double init,
                    long init_ttl, int *forcible);

int mps_shdict_flush_all(mps_shdict_t *dict);

long mps_shdict_get_ttl(mps_shdict_t *dict, const u_char *key, size_t key_len);

int mps_shdict_set_expire(mps_shdict_t *dict, const u_char *key, size_t key_len,
                          long exptime);

int mps_shdict_lpush(mps_shdict_t *dict, const u_char *key, size_t key_len,
                     int value_type, const u_char *str_value_buf,
                     size_t str_value_len, double num_value, char **errmsg);

int mps_shdict_rpush(mps_shdict_t *dict, const u_char *key, size_t key_len,
                     int value_type, const u_char *str_value_buf,
                     size_t str_value_len, double num_value, char **errmsg);

int mps_shdict_lpop(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    int *value_type, u_char **str_value_buf,
                    size_t *str_value_len, double *num_value, char **errmsg);

int mps_shdict_rpop(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    int *value_type, u_char **str_value_buf,
                    size_t *str_value_len, double *num_value, char **errmsg);

int mps_shdict_llen(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    char **errmsg);

size_t mps_shdict_capacity(mps_shdict_t *dict);

size_t mps_shdict_free_space(mps_shdict_t *dict);

#define mps_shdict_tree(pool)                                                  \
    ((mps_shdict_tree_t *)mps_ptr((pool), ((pool)->data)))

#endif /* _MPS_SHDICT_H_INCLUDED_ */
