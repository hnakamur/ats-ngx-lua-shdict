#include "mps_luadict.h"

static ngx_inline void
dd(const char *fmt, ...) {
}



#define NGX_HTTP_LUA_SHDICT_ADD         0x0001
#define NGX_HTTP_LUA_SHDICT_REPLACE     0x0002
#define NGX_HTTP_LUA_SHDICT_SAFE_STORE  0x0004


enum {
    SHDICT_TNIL = 0,        /* same as LUA_TNIL */
    SHDICT_TBOOLEAN = 1,    /* same as LUA_TBOOLEAN */
    SHDICT_TNUMBER = 3,     /* same as LUA_TNUMBER */
    SHDICT_TSTRING = 4,     /* same as LUA_TSTRING */
    SHDICT_TLIST = 5,
};


static ngx_inline mps_queue_t *
mps_luadict_get_list_head(mps_luadict_node_t *sd, size_t len)
{
    return (mps_queue_t *) ngx_align_ptr(((u_char *) &sd->data + len),
                                         NGX_ALIGNMENT);
}


static void
mps_luadict_rbtree_insert(mps_slab_pool_t *pool,
    mps_rbtree_node_t *root, mps_rbtree_node_t *node,
    mps_rbtree_node_t *sentinel)
{
    printf("mps_luadict_rbtree_insert start\n");
}

static void
mps_luadict_on_init(mps_slab_pool_t *pool)
{
    mps_luadict_t *dict;

    dict = mps_slab_alloc(pool, sizeof(mps_luadict_t));
    if (!dict) {
        fprintf(stderr, "mps_luadict_on_init: mps_slab_alloc failed\n");
        return;
    }

    pool->data = mps_offset(pool, dict);
    mps_rbtree_init(pool, &dict->rbtree, &dict->sentinel,
        mps_luadict_rbtree_insert);
    mps_queue_init(pool, &dict->lru_queue);
}

void *
mps_luadict_open_or_create(const char *shm_name, size_t shm_size)
{
    return mps_slab_open_or_create(shm_name, shm_size, mps_luadict_on_init);
}

static ngx_int_t
mps_luadict_lookup(mps_slab_pool_t *pl, ngx_uint_t hash,
    u_char *kdata, size_t klen, mps_luadict_node_t **sdp)
{
    mps_luadict_t       *dict;
    ngx_int_t            rc;
    ngx_time_t          *tp;
    uint64_t             now;
    int64_t              ms;
    mps_rbtree_node_t   *node, *sentinel;
    mps_luadict_node_t  *sd;

    dict = mps_luadict(pl);

    node = mps_rbtree_node(pl, dict->rbtree.root);
    sentinel = mps_rbtree_node(pl, dict->rbtree.sentinel);

    while (node != sentinel) {

        if (hash < node->key) {
            node = mps_rbtree_node(pl, node->left);
            continue;
        }

        if (hash > node->key) {
            node = mps_rbtree_node(pl, node->right);
            continue;
        }

        /* hash == node->key */

        sd = (mps_luadict_node_t *) &node->color;

        rc = ngx_memn2cmp(kdata, sd->data, klen, (size_t) sd->key_len);

        if (rc == 0) {
            mps_queue_remove(pl, &sd->queue);
            mps_queue_insert_head(pl, &dict->lru_queue, &sd->queue);

            *sdp = sd;

            dd("node expires: %lld", (long long) sd->expires);

            if (sd->expires != 0) {
                tp = ngx_timeofday();

                now = (uint64_t) tp->sec * 1000 + tp->msec;
                ms = sd->expires - now;

                dd("time to live: %lld", (long long) ms);

                if (ms < 0) {
                    dd("node already expired");
                    return NGX_DONE;
                }
            }

            return NGX_OK;
        }

        node = mps_rbtree_node(pl, (rc < 0) ? node->left : node->right);
    }

    *sdp = NULL;

    return NGX_DECLINED;
}


int
mps_luadict_get(void *pool, u_char *key,
    size_t key_len, int *value_type, u_char **str_value_buf,
    size_t *str_value_len, double *num_value, int *user_flags,
    int get_stale, int *is_stale, char **err)
{
    mps_slab_pool_t     *pl;
    uint32_t             hash;
    ngx_int_t            rc;
    mps_luadict_node_t  *sd;
    ngx_str_t            value;

    pl = (mps_slab_pool_t *) pool;

    hash = ngx_crc32_short(key, key_len);

    mps_slab_lock(pl);

    rc = mps_luadict_lookup(pl, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED || (rc == NGX_DONE && !get_stale)) {
        mps_slab_unlock(pl);
        *value_type = LUA_TNIL;
        return NGX_OK;
    }

    *value_type = sd->value_type;

    dd("data: %p", sd->data);
    dd("key len: %d", (int) sd->key_len);

    value.data = sd->data + sd->key_len;
    value.len = (size_t) sd->value_len;

    if (*str_value_len < (size_t) value.len) {
        if (*value_type == SHDICT_TBOOLEAN) {
            mps_slab_unlock(pl);
            return NGX_ERROR;
        }

        if (*value_type == SHDICT_TSTRING) {
            *str_value_buf = malloc(value.len);
            if (*str_value_buf == NULL) {
                mps_slab_unlock(pl);
                return NGX_ERROR;
            }
        }
    }

    switch (*value_type) {

    case SHDICT_TSTRING:
        *str_value_len = value.len;
        ngx_memcpy(*str_value_buf, value.data, value.len);
        break;

    case SHDICT_TNUMBER:

        if (value.len != sizeof(double)) {
            mps_slab_unlock(pl);
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                          "bad lua number value size found for key %*s "
                          "in shared_dict %V: %z", key_len, key,
                          &name, value.len);
            return NGX_ERROR;
        }

        *str_value_len = value.len;
        ngx_memcpy(num_value, value.data, sizeof(double));
        break;

    case SHDICT_TBOOLEAN:

        if (value.len != sizeof(u_char)) {
            mps_slab_unlock(pl);
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                          "bad lua boolean value size found for key %*s "
                          "in shared_dict %V: %z", key_len, key, &name,
                          value.len);
            return NGX_ERROR;
        }

        ngx_memcpy(*str_value_buf, value.data, value.len);
        break;

    case SHDICT_TLIST:

        mps_slab_unlock(pl);

        *err = "value is a list";
        return NGX_ERROR;

    default:

        mps_slab_unlock(pl);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "bad value type found for key %*s in "
                      "shared_dict %V: %d", key_len, key, &name,
                      *value_type);
        return NGX_ERROR;
    }

    *user_flags = sd->user_flags;
    dd("user flags: %d", *user_flags);

    mps_slab_unlock(pl);

    if (get_stale) {

        /* always return value, flags, stale */

        *is_stale = (rc == NGX_DONE);
        return NGX_OK;
    }

    return NGX_OK;
}

int
mps_luadict_store(void *pool, int op, u_char *key,
    size_t key_len, int value_type, u_char *str_value_buf,
    size_t str_value_len, double num_value, long exptime, int user_flags,
    char **errmsg, int *forcible)
{
    mps_slab_pool_t     *pl;
    mps_luadict_t       *dict;
    int                  n;
    uint32_t             hash;
    ngx_int_t            rc;
    ngx_time_t          *tp;
    mps_queue_t         *queue, *q;
    mps_rbtree_node_t   *node;
    mps_luadict_node_t  *sd;
    u_char               c, *p;

    pl = (mps_slab_pool_t *) pool;
    dict = mps_luadict(pl);

    *forcible = 0;

    hash = ngx_crc32_short(key, key_len);

    switch (value_type) {

    case SHDICT_TSTRING:
        /* do nothing */
        break;

    case SHDICT_TNUMBER:
        dd("num value: %lf", num_value);
        str_value_buf = (u_char *) &num_value;
        str_value_len = sizeof(double);
        break;

    case SHDICT_TBOOLEAN:
        c = num_value ? 1 : 0;
        str_value_buf = &c;
        str_value_len = sizeof(u_char);
        break;

    case LUA_TNIL:
        if (op & (NGX_HTTP_LUA_SHDICT_ADD|NGX_HTTP_LUA_SHDICT_REPLACE)) {
            *errmsg = "attempt to add or replace nil values";
            return NGX_ERROR;
        }

        str_value_buf = NULL;
        str_value_len = 0;
        break;

    default:
        *errmsg = "unsupported value type";
        return NGX_ERROR;
    }

    mps_slab_lock(pl);

#if 0
    mps_luadict_expire(ctx, 1);
#endif

    rc = mps_luadict_lookup(pl, hash, key, key_len, &sd);
    dd("lookup returns %d", (int) rc);

    if (op & NGX_HTTP_LUA_SHDICT_REPLACE) {

        if (rc == NGX_DECLINED || rc == NGX_DONE) {
            mps_slab_unlock(pl);
            *errmsg = "not found";
            return NGX_DECLINED;
        }

        /* rc == NGX_OK */

        goto replace;
    }

    if (op & NGX_HTTP_LUA_SHDICT_ADD) {

        if (rc == NGX_OK) {
            mps_slab_unlock(pl);
            *errmsg = "exists";
            return NGX_DECLINED;
        }

        if (rc == NGX_DONE) {
            /* exists but expired */

            dd("go to replace");
            goto replace;
        }

        /* rc == NGX_DECLINED */

        dd("go to insert");
        goto insert;
    }

    if (rc == NGX_OK || rc == NGX_DONE) {

        if (value_type == LUA_TNIL) {
            goto remove;
        }

replace:

        if (str_value_buf
            && str_value_len == (size_t) sd->value_len
            && sd->value_type != SHDICT_TLIST)
        {

            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                           "lua shared dict set: found old entry and value "
                           "size matched, reusing it");

            mps_queue_remove(pl, &sd->queue);
            mps_queue_insert_head(pl, &dict->lru_queue, &sd->queue);

            if (exptime > 0) {
                tp = ngx_timeofday();
                sd->expires = (uint64_t) tp->sec * 1000 + tp->msec
                              + (uint64_t) exptime;

            } else {
                sd->expires = 0;
            }

            sd->user_flags = user_flags;

            dd("setting value type to %d", value_type);

            sd->value_type = (uint8_t) value_type;

            ngx_memcpy(sd->data + key_len, str_value_buf, str_value_len);

            mps_slab_unlock(pl);

            return NGX_OK;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                       "lua shared dict set: found old entry but value size "
                       "NOT matched, removing it first");

remove:

        if (sd->value_type == SHDICT_TLIST) {
            queue = mps_luadict_get_list_head(sd, key_len);

            for (q = mps_queue_head(pl, queue);
                 q != mps_queue_sentinel(pl, queue);
                 q = mps_queue_next(pl, q))
            {
                p = (u_char *) mps_queue_data(q,
                                              mps_luadict_list_node_t,
                                              queue);

                mps_slab_free_locked(pl, p);
            }
        }

        mps_queue_remove(pl, &sd->queue);

        node = (mps_rbtree_node_t *)
                   ((u_char *) sd - offsetof(mps_rbtree_node_t, color));

        mps_rbtree_delete(pl, &dict->rbtree, node);

        mps_slab_free_locked(pl, node);

    }

insert:

    /* rc == NGX_DECLINED or value size unmatch */

    if (str_value_buf == NULL) {
        mps_slab_unlock(pl);
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "lua shared dict set: creating a new entry");

    n = offsetof(mps_rbtree_node_t, color)
        + offsetof(mps_luadict_node_t, data)
        + key_len
        + str_value_len;

    node = mps_slab_alloc_locked(pl, n);

    if (node == NULL) {
        mps_slab_unlock(pl);
        return NGX_ERROR;
    }

    sd = (mps_luadict_node_t *) &node->color;

    node->key = hash;
    sd->key_len = (u_short) key_len;

    sd->user_flags = user_flags;
    sd->value_len = (uint32_t) str_value_len;
    sd->value_type = (uint8_t) value_type;

    p = ngx_copy(sd->data, key, key_len);
    ngx_memcpy(p, str_value_buf, str_value_len);

    mps_rbtree_insert(pl, &dict->rbtree, node);
    mps_queue_insert_head(pl, &dict->lru_queue, &sd->queue);
    mps_slab_unlock(pl);

    return NGX_OK;
}
