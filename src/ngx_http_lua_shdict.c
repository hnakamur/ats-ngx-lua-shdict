
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_http_lua_shdict.h"
#if 0
#include "ngx_http_lua_util.h"
#endif
#include "ngx_http_lua_api.h"

static int ngx_http_lua_shdict_expire(ngx_http_lua_shdict_ctx_t *ctx,
    ngx_uint_t n);
static ngx_int_t ngx_http_lua_shdict_lookup(ngx_shm_zone_t *shm_zone,
    ngx_uint_t hash, u_char *kdata, size_t klen,
    ngx_http_lua_shdict_node_t **sdp);


#define NGX_HTTP_LUA_SHDICT_ADD         0x0001
#define NGX_HTTP_LUA_SHDICT_REPLACE     0x0002
#define NGX_HTTP_LUA_SHDICT_SAFE_STORE  0x0004


#define NGX_HTTP_LUA_SHDICT_LEFT        0x0001
#define NGX_HTTP_LUA_SHDICT_RIGHT       0x0002


enum {
    SHDICT_USERDATA_INDEX = 1,
};


enum {
    SHDICT_TNIL = 0,        /* same as LUA_TNIL */
    SHDICT_TBOOLEAN = 1,    /* same as LUA_TBOOLEAN */
    SHDICT_TNUMBER = 3,     /* same as LUA_TNUMBER */
    SHDICT_TSTRING = 4,     /* same as LUA_TSTRING */
    SHDICT_TLIST = 5,
};


static ngx_inline ngx_queue_t *
ngx_http_lua_shdict_get_list_head(ngx_http_lua_shdict_node_t *sd, size_t len)
{
    return (ngx_queue_t *) ngx_align_ptr(((u_char *) &sd->data + len),
                                         NGX_ALIGNMENT);
}


ngx_int_t
ngx_http_lua_shdict_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_lua_shdict_ctx_t  *octx = data;

    size_t                      len;
    ngx_http_lua_shdict_ctx_t  *ctx;

    dd("init zone");

    ctx = shm_zone->data;

    if (octx) {
        ctx->sh = octx->sh;
        ctx->shpool = octx->shpool;

        return NGX_OK;
    }

    ctx->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        ctx->sh = ctx->shpool->data;

        return NGX_OK;
    }

    ctx->sh = ngx_slab_alloc(ctx->shpool, sizeof(ngx_http_lua_shdict_shctx_t));
    if (ctx->sh == NULL) {
        return NGX_ERROR;
    }

    ctx->shpool->data = ctx->sh;

    ngx_rbtree_init(&ctx->sh->rbtree, &ctx->sh->sentinel,
                    ngx_http_lua_shdict_rbtree_insert_value);

    ngx_queue_init(&ctx->sh->lru_queue);

    len = sizeof(" in lua_shared_dict zone \"\"") + shm_zone->shm.name.len;

    ctx->shpool->log_ctx = ngx_slab_alloc(ctx->shpool, len);
    if (ctx->shpool->log_ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_sprintf(ctx->shpool->log_ctx, " in lua_shared_dict zone \"%V\"%Z",
                &shm_zone->shm.name);

    ctx->shpool->log_nomem = 0;

    return NGX_OK;
}


void
ngx_http_lua_shdict_rbtree_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t           **p;
    ngx_http_lua_shdict_node_t   *sdn, *sdnt;

    for ( ;; ) {

        if (node->key < temp->key) {

            p = &temp->left;

        } else if (node->key > temp->key) {

            p = &temp->right;

        } else { /* node->key == temp->key */

            sdn = (ngx_http_lua_shdict_node_t *) &node->color;
            sdnt = (ngx_http_lua_shdict_node_t *) &temp->color;

            p = ngx_memn2cmp(sdn->data, sdnt->data, sdn->key_len,
                             sdnt->key_len) < 0 ? &temp->left : &temp->right;
        }

        if (*p == sentinel) {
            break;
        }

        temp = *p;
    }

    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}


static ngx_int_t
ngx_http_lua_shdict_lookup(ngx_shm_zone_t *shm_zone, ngx_uint_t hash,
    u_char *kdata, size_t klen, ngx_http_lua_shdict_node_t **sdp)
{
    ngx_int_t                    rc;
    ngx_time_t                  *tp;
    uint64_t                     now;
    int64_t                      ms;
    ngx_rbtree_node_t           *node, *sentinel;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;

    ctx = shm_zone->data;

    node = ctx->sh->rbtree.root;
    sentinel = ctx->sh->rbtree.sentinel;

    while (node != sentinel) {

        if (hash < node->key) {
            node = node->left;
            continue;
        }

        if (hash > node->key) {
            node = node->right;
            continue;
        }

        /* hash == node->key */

        sd = (ngx_http_lua_shdict_node_t *) &node->color;

        rc = ngx_memn2cmp(kdata, sd->data, klen, (size_t) sd->key_len);

        if (rc == 0) {
            ngx_queue_remove(&sd->queue);
            ngx_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

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

        node = (rc < 0) ? node->left : node->right;
    }

    *sdp = NULL;

    return NGX_DECLINED;
}


static int
ngx_http_lua_shdict_expire(ngx_http_lua_shdict_ctx_t *ctx, ngx_uint_t n)
{
    ngx_time_t                      *tp;
    uint64_t                         now;
    ngx_queue_t                     *q, *list_queue, *lq;
    int64_t                          ms;
    ngx_rbtree_node_t               *node;
    ngx_http_lua_shdict_node_t      *sd;
    int                              freed = 0;
    ngx_http_lua_shdict_list_node_t *lnode;

    tp = ngx_timeofday();

    now = (uint64_t) tp->sec * 1000 + tp->msec;

    /*
     * n == 1 deletes one or two expired entries
     * n == 0 deletes oldest entry by force
     *        and one or two zero rate entries
     */

    while (n < 3) {

        if (ngx_queue_empty(&ctx->sh->lru_queue)) {
            return freed;
        }

        q = ngx_queue_last(&ctx->sh->lru_queue);

        sd = ngx_queue_data(q, ngx_http_lua_shdict_node_t, queue);

        if (n++ != 0) {

            if (sd->expires == 0) {
                return freed;
            }

            ms = sd->expires - now;
            if (ms > 0) {
                return freed;
            }
        }

        if (sd->value_type == SHDICT_TLIST) {
            list_queue = ngx_http_lua_shdict_get_list_head(sd, sd->key_len);

            for (lq = ngx_queue_head(list_queue);
                 lq != ngx_queue_sentinel(list_queue);
                 lq = ngx_queue_next(lq))
            {
                lnode = ngx_queue_data(lq, ngx_http_lua_shdict_list_node_t,
                                       queue);

                ngx_slab_free_locked(ctx->shpool, lnode);
            }
        }

        ngx_queue_remove(q);

        node = (ngx_rbtree_node_t *)
                   ((u_char *) sd - offsetof(ngx_rbtree_node_t, color));

        ngx_rbtree_delete(&ctx->sh->rbtree, node);

        ngx_slab_free_locked(ctx->shpool, node);

        freed++;
    }

    return freed;
}


ngx_int_t
ngx_http_lua_shared_dict_get(ngx_shm_zone_t *zone, u_char *key_data,
    size_t key_len, ngx_http_lua_value_t *value)
{
    u_char                      *data;
    size_t                       len;
    uint32_t                     hash;
    ngx_int_t                    rc;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;

    if (zone == NULL) {
        return NGX_ERROR;
    }

    hash = ngx_crc32_short(key_data, key_len);

    ctx = zone->data;

    ngx_shmtx_lock(&ctx->shpool->mutex);

    rc = ngx_http_lua_shdict_lookup(zone, hash, key_data, key_len, &sd);

    dd("shdict lookup returned %d", (int) rc);

    if (rc == NGX_DECLINED || rc == NGX_DONE) {
        ngx_shmtx_unlock(&ctx->shpool->mutex);

        return rc;
    }

    /* rc == NGX_OK */

    value->type = sd->value_type;

    dd("type: %d", (int) value->type);

    data = sd->data + sd->key_len;
    len = (size_t) sd->value_len;

    switch (value->type) {

    case SHDICT_TSTRING:

        if (value->value.s.data == NULL || value->value.s.len == 0) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "no string buffer "
                          "initialized");
            ngx_shmtx_unlock(&ctx->shpool->mutex);
            return NGX_ERROR;
        }

        if (len > value->value.s.len) {
            len = value->value.s.len;

        } else {
            value->value.s.len = len;
        }

        ngx_memcpy(value->value.s.data, data, len);
        break;

    case SHDICT_TNUMBER:

        if (len != sizeof(double)) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "bad lua number "
                          "value size found for key %*s: %lu", key_len,
                          key_data, (unsigned long) len);

            ngx_shmtx_unlock(&ctx->shpool->mutex);
            return NGX_ERROR;
        }

        ngx_memcpy(&value->value.b, data, len);
        break;

    case SHDICT_TBOOLEAN:

        if (len != sizeof(u_char)) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "bad lua boolean "
                          "value size found for key %*s: %lu", key_len,
                          key_data, (unsigned long) len);

            ngx_shmtx_unlock(&ctx->shpool->mutex);
            return NGX_ERROR;
        }

        value->value.b = *data;
        break;

    default:
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "bad lua value type "
                      "found for key %*s: %d", key_len, key_data,
                      (int) value->type);

        ngx_shmtx_unlock(&ctx->shpool->mutex);
        return NGX_ERROR;
    }

    ngx_shmtx_unlock(&ctx->shpool->mutex);
    return NGX_OK;
}


ngx_shm_zone_t *
ngx_http_lua_ffi_shdict_udata_to_zone(void *zone_udata)
{
    if (zone_udata == NULL) {
        return NULL;
    }

    return *(ngx_shm_zone_t **) zone_udata;
}


int
ngx_http_lua_ffi_shdict_store(ngx_shm_zone_t *zone, int op, u_char *key,
    size_t key_len, int value_type, u_char *str_value_buf,
    size_t str_value_len, double num_value, long exptime, int user_flags,
    char **errmsg, int *forcible)
{
    int                          i, n;
    u_char                       c, *p;
    uint32_t                     hash;
    ngx_int_t                    rc;
    ngx_time_t                  *tp;
    ngx_queue_t                 *queue, *q;
    ngx_rbtree_node_t           *node;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;

    dd("exptime: %ld", exptime);

    ctx = zone->data;

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

    ngx_shmtx_lock(&ctx->shpool->mutex);

#if 1
    ngx_http_lua_shdict_expire(ctx, 1);
#endif

    rc = ngx_http_lua_shdict_lookup(zone, hash, key, key_len, &sd);

    dd("lookup returns %d", (int) rc);

    if (op & NGX_HTTP_LUA_SHDICT_REPLACE) {

        if (rc == NGX_DECLINED || rc == NGX_DONE) {
            ngx_shmtx_unlock(&ctx->shpool->mutex);
            *errmsg = "not found";
            return NGX_DECLINED;
        }

        /* rc == NGX_OK */

        goto replace;
    }

    if (op & NGX_HTTP_LUA_SHDICT_ADD) {

        if (rc == NGX_OK) {
            ngx_shmtx_unlock(&ctx->shpool->mutex);
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

            ngx_queue_remove(&sd->queue);
            ngx_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

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

            ngx_shmtx_unlock(&ctx->shpool->mutex);

            return NGX_OK;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                       "lua shared dict set: found old entry but value size "
                       "NOT matched, removing it first");

remove:

        if (sd->value_type == SHDICT_TLIST) {
            queue = ngx_http_lua_shdict_get_list_head(sd, key_len);

            for (q = ngx_queue_head(queue);
                 q != ngx_queue_sentinel(queue);
                 q = ngx_queue_next(q))
            {
                p = (u_char *) ngx_queue_data(q,
                                              ngx_http_lua_shdict_list_node_t,
                                              queue);

                ngx_slab_free_locked(ctx->shpool, p);
            }
        }

        ngx_queue_remove(&sd->queue);

        node = (ngx_rbtree_node_t *)
                   ((u_char *) sd - offsetof(ngx_rbtree_node_t, color));

        ngx_rbtree_delete(&ctx->sh->rbtree, node);

        ngx_slab_free_locked(ctx->shpool, node);

    }

insert:

    /* rc == NGX_DECLINED or value size unmatch */

    if (str_value_buf == NULL) {
        ngx_shmtx_unlock(&ctx->shpool->mutex);
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "lua shared dict set: creating a new entry");

    n = offsetof(ngx_rbtree_node_t, color)
        + offsetof(ngx_http_lua_shdict_node_t, data)
        + key_len
        + str_value_len;

    node = ngx_slab_alloc_locked(ctx->shpool, n);

    if (node == NULL) {

        if (op & NGX_HTTP_LUA_SHDICT_SAFE_STORE) {
            ngx_shmtx_unlock(&ctx->shpool->mutex);

            *errmsg = "no memory";
            return NGX_ERROR;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                       "lua shared dict set: overriding non-expired items "
                       "due to memory shortage for entry \"%*s\"", key_len,
                       key);

        for (i = 0; i < 30; i++) {
            if (ngx_http_lua_shdict_expire(ctx, 0) == 0) {
                break;
            }

            *forcible = 1;

            node = ngx_slab_alloc_locked(ctx->shpool, n);
            if (node != NULL) {
                goto allocated;
            }
        }

        ngx_shmtx_unlock(&ctx->shpool->mutex);

        *errmsg = "no memory";
        return NGX_ERROR;
    }

allocated:

    sd = (ngx_http_lua_shdict_node_t *) &node->color;

    node->key = hash;
    sd->key_len = (u_short) key_len;

    if (exptime > 0) {
        tp = ngx_timeofday();
        sd->expires = (uint64_t) tp->sec * 1000 + tp->msec
                      + (uint64_t) exptime;

    } else {
        sd->expires = 0;
    }

    sd->user_flags = user_flags;
    sd->value_len = (uint32_t) str_value_len;
    dd("setting value type to %d", value_type);
    sd->value_type = (uint8_t) value_type;

    p = ngx_copy(sd->data, key, key_len);
    ngx_memcpy(p, str_value_buf, str_value_len);

    ngx_rbtree_insert(&ctx->sh->rbtree, node);
    ngx_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);
    ngx_shmtx_unlock(&ctx->shpool->mutex);

    return NGX_OK;
}


int
ngx_http_lua_ffi_shdict_get(ngx_shm_zone_t *zone, u_char *key,
    size_t key_len, int *value_type, u_char **str_value_buf,
    size_t *str_value_len, double *num_value, int *user_flags,
    int get_stale, int *is_stale, char **err)
{
#if (NGX_DEBUG)
    ngx_str_t                    name;
#endif /* NGX_DEBUG */
    uint32_t                     hash;
    ngx_int_t                    rc;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;
    ngx_str_t                    value;

    *err = NULL;

    ctx = zone->data;
#if (NGX_DEBUG)
    name = ctx->name;
#endif /* NGX_DEBUG */

    hash = ngx_crc32_short(key, key_len);

#if (NGX_DEBUG)
    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "fetching key \"%*s\" in shared dict \"%V\"", key_len,
                   key, &name);
#endif /* NGX_DEBUG */

    ngx_shmtx_lock(&ctx->shpool->mutex);

#if 1
    if (!get_stale) {
        ngx_http_lua_shdict_expire(ctx, 1);
    }
#endif

    rc = ngx_http_lua_shdict_lookup(zone, hash, key, key_len, &sd);

    dd("shdict lookup returns %d", (int) rc);

    if (rc == NGX_DECLINED || (rc == NGX_DONE && !get_stale)) {
        ngx_shmtx_unlock(&ctx->shpool->mutex);
        *value_type = LUA_TNIL;
        return NGX_OK;
    }

    /* rc == NGX_OK || (rc == NGX_DONE && get_stale) */

    *value_type = sd->value_type;

    dd("data: %p", sd->data);
    dd("key len: %d", (int) sd->key_len);

    value.data = sd->data + sd->key_len;
    value.len = (size_t) sd->value_len;

    if (*str_value_len < (size_t) value.len) {
        if (*value_type == SHDICT_TBOOLEAN) {
            ngx_shmtx_unlock(&ctx->shpool->mutex);
            return NGX_ERROR;
        }

        if (*value_type == SHDICT_TSTRING) {
            *str_value_buf = malloc(value.len);
            if (*str_value_buf == NULL) {
                ngx_shmtx_unlock(&ctx->shpool->mutex);
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
            ngx_shmtx_unlock(&ctx->shpool->mutex);
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
            ngx_shmtx_unlock(&ctx->shpool->mutex);
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                          "bad lua boolean value size found for key %*s "
                          "in shared_dict %V: %z", key_len, key, &name,
                          value.len);
            return NGX_ERROR;
        }

        ngx_memcpy(*str_value_buf, value.data, value.len);
        break;

    case SHDICT_TLIST:

        ngx_shmtx_unlock(&ctx->shpool->mutex);

        *err = "value is a list";
        return NGX_ERROR;

    default:

        ngx_shmtx_unlock(&ctx->shpool->mutex);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "bad value type found for key %*s in "
                      "shared_dict %V: %d", key_len, key, &name,
                      *value_type);
        return NGX_ERROR;
    }

    *user_flags = sd->user_flags;
    dd("user flags: %d", *user_flags);

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    if (get_stale) {

        /* always return value, flags, stale */

        *is_stale = (rc == NGX_DONE);
        return NGX_OK;
    }

    return NGX_OK;
}


int
ngx_http_lua_ffi_shdict_incr(ngx_shm_zone_t *zone, u_char *key,
    size_t key_len, double *value, char **err, int has_init, double init,
    long init_ttl, int *forcible)
{
    int                          i, n;
    uint32_t                     hash;
    ngx_int_t                    rc;
    ngx_time_t                  *tp = NULL;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;
    double                       num;
    ngx_rbtree_node_t           *node;
    u_char                      *p;
    ngx_queue_t                 *queue, *q;

    if (init_ttl > 0) {
        tp = ngx_timeofday();
    }

    ctx = zone->data;

    *forcible = 0;

    hash = ngx_crc32_short(key, key_len);

    dd("looking up key %.*s in shared dict %.*s", (int) key_len, key,
       (int) ctx->name.len, ctx->name.data);

    ngx_shmtx_lock(&ctx->shpool->mutex);
#if 1
    ngx_http_lua_shdict_expire(ctx, 1);
#endif
    rc = ngx_http_lua_shdict_lookup(zone, hash, key, key_len, &sd);

    dd("shdict lookup returned %d", (int) rc);

    if (rc == NGX_DECLINED || rc == NGX_DONE) {
        if (!has_init) {
            ngx_shmtx_unlock(&ctx->shpool->mutex);
            *err = "not found";
            return NGX_ERROR;
        }

        /* add value */
        num = *value + init;

        if (rc == NGX_DONE) {

            /* found an expired item */

            if ((size_t) sd->value_len == sizeof(double)
                && sd->value_type != SHDICT_TLIST)
            {
                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                               "lua shared dict incr: found old entry and "
                               "value size matched, reusing it");

                ngx_queue_remove(&sd->queue);
                ngx_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

                dd("go to setvalue");
                goto setvalue;
            }

            dd("go to remove");
            goto remove;
        }

        dd("go to insert");
        goto insert;
    }

    /* rc == NGX_OK */

    if (sd->value_type != SHDICT_TNUMBER || sd->value_len != sizeof(double)) {
        ngx_shmtx_unlock(&ctx->shpool->mutex);
        *err = "not a number";
        return NGX_ERROR;
    }

    ngx_queue_remove(&sd->queue);
    ngx_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

    dd("setting value type to %d", (int) sd->value_type);

    p = sd->data + key_len;

    ngx_memcpy(&num, p, sizeof(double));
    num += *value;

    ngx_memcpy(p, (double *) &num, sizeof(double));

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    *value = num;
    return NGX_OK;

remove:

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "lua shared dict incr: found old entry but value size "
                   "NOT matched, removing it first");

    if (sd->value_type == SHDICT_TLIST) {
        queue = ngx_http_lua_shdict_get_list_head(sd, key_len);

        for (q = ngx_queue_head(queue);
             q != ngx_queue_sentinel(queue);
             q = ngx_queue_next(q))
        {
            p = (u_char *) ngx_queue_data(q, ngx_http_lua_shdict_list_node_t,
                                          queue);

            ngx_slab_free_locked(ctx->shpool, p);
        }
    }

    ngx_queue_remove(&sd->queue);

    node = (ngx_rbtree_node_t *)
               ((u_char *) sd - offsetof(ngx_rbtree_node_t, color));

    ngx_rbtree_delete(&ctx->sh->rbtree, node);

    ngx_slab_free_locked(ctx->shpool, node);

insert:

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "lua shared dict incr: creating a new entry");

    n = offsetof(ngx_rbtree_node_t, color)
        + offsetof(ngx_http_lua_shdict_node_t, data)
        + key_len
        + sizeof(double);

    node = ngx_slab_alloc_locked(ctx->shpool, n);

    if (node == NULL) {

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                       "lua shared dict incr: overriding non-expired items "
                       "due to memory shortage for entry \"%*s\"", key_len,
                       key);

        for (i = 0; i < 30; i++) {
            if (ngx_http_lua_shdict_expire(ctx, 0) == 0) {
                break;
            }

            *forcible = 1;

            node = ngx_slab_alloc_locked(ctx->shpool, n);
            if (node != NULL) {
                goto allocated;
            }
        }

        ngx_shmtx_unlock(&ctx->shpool->mutex);

        *err = "no memory";
        return NGX_ERROR;
    }

allocated:

    sd = (ngx_http_lua_shdict_node_t *) &node->color;

    node->key = hash;

    sd->key_len = (u_short) key_len;

    sd->value_len = (uint32_t) sizeof(double);

    ngx_rbtree_insert(&ctx->sh->rbtree, node);

    ngx_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

setvalue:

    sd->user_flags = 0;

    if (init_ttl > 0) {
        sd->expires = (uint64_t) tp->sec * 1000 + tp->msec
                      + (uint64_t) init_ttl;

    } else {
        sd->expires = 0;
    }

    dd("setting value type to %d", LUA_TNUMBER);

    sd->value_type = (uint8_t) LUA_TNUMBER;

    p = ngx_copy(sd->data, key, key_len);
    ngx_memcpy(p, (double *) &num, sizeof(double));

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    *value = num;
    return NGX_OK;
}


int
ngx_http_lua_ffi_shdict_flush_all(ngx_shm_zone_t *zone)
{
    ngx_queue_t                 *q;
    ngx_http_lua_shdict_node_t  *sd;
    ngx_http_lua_shdict_ctx_t   *ctx;

    ctx = zone->data;

    ngx_shmtx_lock(&ctx->shpool->mutex);

    for (q = ngx_queue_head(&ctx->sh->lru_queue);
         q != ngx_queue_sentinel(&ctx->sh->lru_queue);
         q = ngx_queue_next(q))
    {
        sd = ngx_queue_data(q, ngx_http_lua_shdict_node_t, queue);
        sd->expires = 1;
    }

    ngx_http_lua_shdict_expire(ctx, 0);

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_shdict_peek(ngx_shm_zone_t *shm_zone, ngx_uint_t hash,
    u_char *kdata, size_t klen, ngx_http_lua_shdict_node_t **sdp)
{
    ngx_int_t                    rc;
    ngx_rbtree_node_t           *node, *sentinel;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;

    ctx = shm_zone->data;

    node = ctx->sh->rbtree.root;
    sentinel = ctx->sh->rbtree.sentinel;

    while (node != sentinel) {

        if (hash < node->key) {
            node = node->left;
            continue;
        }

        if (hash > node->key) {
            node = node->right;
            continue;
        }

        /* hash == node->key */

        sd = (ngx_http_lua_shdict_node_t *) &node->color;

        rc = ngx_memn2cmp(kdata, sd->data, klen, (size_t) sd->key_len);

        if (rc == 0) {
            *sdp = sd;

            return NGX_OK;
        }

        node = (rc < 0) ? node->left : node->right;
    }

    *sdp = NULL;

    return NGX_DECLINED;
}


long
ngx_http_lua_ffi_shdict_get_ttl(ngx_shm_zone_t *zone, u_char *key,
    size_t key_len)
{
    uint32_t                     hash;
    uint64_t                     now;
    uint64_t                     expires;
    ngx_int_t                    rc;
    ngx_time_t                  *tp;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;

    ctx = zone->data;
    hash = ngx_crc32_short(key, key_len);

    ngx_shmtx_lock(&ctx->shpool->mutex);

    rc = ngx_http_lua_shdict_peek(zone, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED) {
        ngx_shmtx_unlock(&ctx->shpool->mutex);

        return NGX_DECLINED;
    }

    /* rc == NGX_OK */

    expires = sd->expires;

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    if (expires == 0) {
        return 0;
    }

    tp = ngx_timeofday();
    now = (uint64_t) tp->sec * 1000 + tp->msec;

    return expires - now;
}


int
ngx_http_lua_ffi_shdict_set_expire(ngx_shm_zone_t *zone, u_char *key,
    size_t key_len, long exptime)
{
    uint32_t                     hash;
    ngx_int_t                    rc;
    ngx_time_t                  *tp = NULL;
    ngx_http_lua_shdict_ctx_t   *ctx;
    ngx_http_lua_shdict_node_t  *sd;

    if (exptime > 0) {
        tp = ngx_timeofday();
    }

    ctx = zone->data;
    hash = ngx_crc32_short(key, key_len);

    ngx_shmtx_lock(&ctx->shpool->mutex);

    rc = ngx_http_lua_shdict_peek(zone, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED) {
        ngx_shmtx_unlock(&ctx->shpool->mutex);

        return NGX_DECLINED;
    }

    /* rc == NGX_OK */

    if (exptime > 0) {
        sd->expires = (uint64_t) tp->sec * 1000 + tp->msec
                      + (uint64_t) exptime;

    } else {
        sd->expires = 0;
    }

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    return NGX_OK;
}


size_t
ngx_http_lua_ffi_shdict_capacity(ngx_shm_zone_t *zone)
{
    return zone->shm.size;
}


#if (nginx_version >= 1011007)
size_t
ngx_http_lua_ffi_shdict_free_space(ngx_shm_zone_t *zone)
{
    size_t                       bytes;
    ngx_http_lua_shdict_ctx_t   *ctx;

    ctx = zone->data;

    ngx_shmtx_lock(&ctx->shpool->mutex);
    bytes = ctx->shpool->pfree * ngx_pagesize;
    ngx_shmtx_unlock(&ctx->shpool->mutex);

    return bytes;
}
#endif


#if (NGX_DARWIN)
int
ngx_http_lua_ffi_shdict_get_macos(ngx_http_lua_shdict_get_params_t *p)
{
    return ngx_http_lua_ffi_shdict_get(p->zone, p->key, p->key_len,
                                       p->value_type, p->str_value_buf,
                                       p->str_value_len, p->num_value,
                                       p->user_flags, p->get_stale,
                                       p->is_stale, p->errmsg);
}


int
ngx_http_lua_ffi_shdict_store_macos(ngx_http_lua_shdict_store_params_t *p)
{
    return ngx_http_lua_ffi_shdict_store(p->zone, p->op, p->key, p->key_len,
                                         p->value_type, p->str_value_buf,
                                         p->str_value_len, p->num_value,
                                         p->exptime, p->user_flags,
                                         p->errmsg, p->forcible);
}


int
ngx_http_lua_ffi_shdict_incr_macos(ngx_http_lua_shdict_incr_params_t *p)
{
    return ngx_http_lua_ffi_shdict_incr(p->zone, p->key, p->key_len,
                                        p->num_value, p->errmsg,
                                        p->has_init, p->init, p->init_ttl,
                                        p->forcible);
}
#endif


/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
