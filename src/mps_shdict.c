#include "mps_shdict.h"
#include "mps_log.h"

#       define dd(...) fprintf(stderr, "lua *** %s: ", __func__);            \
            fprintf(stderr, __VA_ARGS__);                                    \
            fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__)

static int mps_shdict_expire(mps_slab_pool_t *pool, mps_shdict_t *dict,
    ngx_uint_t n);


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


static inline uint64_t
msec_from_timespec(const struct timespec *ts)
{
    return (uint64_t) ts->tv_sec * 1000 + (uint64_t) ts->tv_nsec / 1000000;
}


static inline uint64_t
mps_clock_time_ms()
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        exit(1);
    }

    return msec_from_timespec(&ts);
}


static ngx_inline mps_queue_t *
mps_shdict_get_list_head(mps_shdict_node_t *sd, size_t len)
{
    return (mps_queue_t *) ngx_align_ptr(((u_char *) &sd->data + len),
                                         NGX_ALIGNMENT);
}


void
mps_shdict_rbtree_insert_value(mps_slab_pool_t *pool,
    mps_rbtree_node_t *temp, mps_rbtree_node_t *node,
    mps_rbtree_node_t *sentinel)
{
    mps_ptroff_t        *p, s;
    mps_shdict_node_t   *sdn, *sdnt;

    printf("mps_shdict_rbtree_insert_value start\n");
    s = mps_offset(pool, sentinel);

    for ( ;; ) {

        if (node->key < temp->key) {

            p = &temp->left;

        } else if (node->key > temp->key) {

            p = &temp->right;

        } else { /* node->key == temp->key */

            sdn = (mps_shdict_node_t *) &node->color;
            sdnt = (mps_shdict_node_t *) &temp->color;

            p = ngx_memn2cmp(sdn->data, sdnt->data, sdn->key_len,
                             sdnt->key_len) < 0 ? &temp->left : &temp->right;
        }

        if (*p == s) {
            break;
        }

        temp = mps_rbtree_node(pool, *p);
    }

    *p = mps_offset(pool, node);
    node->parent = mps_offset(pool, temp);
    node->left = s;
    node->right = s;
    ngx_rbt_red(node);
}

static void
mps_shdict_on_init(mps_slab_pool_t *pool)
{
    mps_shdict_t *dict;

    TSNote("mps_shdict_on_init start");
    dict = mps_slab_alloc(pool, sizeof(mps_shdict_t));
    if (!dict) {
        fprintf(stderr, "mps_shdict_on_init: mps_slab_alloc failed\n");
        return;
    }

    pool->data = mps_offset(pool, dict);
    mps_rbtree_init(pool, &dict->rbtree, &dict->sentinel,
        MPS_RBTREE_INSERT_TYPE_ID_LUADICT);
    mps_queue_init(pool, &dict->lru_queue);
    TSNote("mps_shdict_on_init exit");
}

mps_slab_pool_t *
mps_shdict_open_or_create(const char *shm_name, size_t shm_size, mode_t mode)
{
    return mps_slab_open_or_create(shm_name, shm_size, mode,
        mps_shdict_on_init);
}

static ngx_int_t
mps_shdict_lookup(mps_slab_pool_t *pool, ngx_uint_t hash,
    const u_char *kdata, size_t klen, mps_shdict_node_t **sdp)
{
    mps_shdict_t       *dict;
    ngx_int_t           rc;
    uint64_t            now;
    int64_t             ms;
    mps_rbtree_node_t  *node, *sentinel;
    mps_shdict_node_t  *sd;

    dict = mps_shdict(pool);

    node = mps_rbtree_node(pool, dict->rbtree.root);
    sentinel = mps_rbtree_node(pool, dict->rbtree.sentinel);

    while (node != sentinel) {

        if (hash < node->key) {
            node = mps_rbtree_node(pool, node->left);
            continue;
        }

        if (hash > node->key) {
            node = mps_rbtree_node(pool, node->right);
            continue;
        }

        /* hash == node->key */

        sd = (mps_shdict_node_t *) &node->color;

        rc = ngx_memn2cmp(kdata, sd->data, klen, (size_t) sd->key_len);

        if (rc == 0) {
            mps_queue_remove(pool, &sd->queue);
            mps_queue_insert_head(pool, &dict->lru_queue, &sd->queue);

            *sdp = sd;

            dd("node expires: %lld", (long long) sd->expires);

            if (sd->expires != 0) {
                now = mps_clock_time_ms();
                ms = sd->expires - now;

                dd("time to live: %lld", (long long) ms);

                if (ms < 0) {
                    dd("node already expired");
                    return NGX_DONE;
                }
            }

            return NGX_OK;
        }

        node = mps_rbtree_node(pool, (rc < 0) ? node->left : node->right);
    }

    *sdp = NULL;

    return NGX_DECLINED;
}


static int
mps_shdict_expire(mps_slab_pool_t *pool, mps_shdict_t *dict, ngx_uint_t n)
{
    uint64_t                 now;
    mps_queue_t             *q, *list_queue, *lq;
    int64_t                  ms;
    mps_rbtree_node_t       *node;
    mps_shdict_node_t       *sd;
    int                      freed = 0;
    mps_shdict_list_node_t  *lnode;

    now = mps_clock_time_ms();

    /*
     * n == 1 deletes one or two expired entries
     * n == 0 deletes oldest entry by force
     *        and one or two zero rate entries
     */

    while (n < 3) {

        if (mps_queue_empty(pool, &dict->lru_queue)) {
            return freed;
        }

        q = mps_queue_last(pool, &dict->lru_queue);

        sd = mps_queue_data(q, mps_shdict_node_t, queue);

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
            list_queue = mps_shdict_get_list_head(sd, sd->key_len);

            for (lq = mps_queue_head(pool, list_queue);
                 lq != mps_queue_sentinel(pool, list_queue);
                 lq = mps_queue_next(pool, lq))
            {
                lnode = mps_queue_data(lq, mps_shdict_list_node_t,
                                       queue);

                mps_slab_free_locked(pool, lnode);
            }
        }

        mps_queue_remove(pool, q);

        node = (mps_rbtree_node_t *)
                          ((u_char *) sd - offsetof(mps_rbtree_node_t, color));

        mps_rbtree_delete(pool, &dict->rbtree, node);

        mps_slab_free_locked(pool, node);

        freed++;
    }

    return freed;
}


int
mps_shdict_store(mps_slab_pool_t *pool, int op, const u_char *key,
    size_t key_len, int value_type, const u_char *str_value_buf,
    size_t str_value_len, double num_value, long exptime, int user_flags,
    char **errmsg, int *forcible)
{
    mps_shdict_t       *dict;
    int                 n;
    uint32_t            hash;
    ngx_int_t           rc;
    mps_queue_t        *queue, *q;
    mps_rbtree_node_t  *node;
    mps_shdict_node_t  *sd;
    u_char              c, *p;

    dict = mps_shdict(pool);

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

    mps_slab_lock(pool);

#if 1
    mps_shdict_expire(pool, dict, 1);
#endif

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);
    dd("lookup returns %d", (int) rc);

    if (op & NGX_HTTP_LUA_SHDICT_REPLACE) {

        if (rc == NGX_DECLINED || rc == NGX_DONE) {
            mps_slab_unlock(pool);
            *errmsg = "not found";
            return NGX_DECLINED;
        }

        /* rc == NGX_OK */

        goto replace;
    }

    if (op & NGX_HTTP_LUA_SHDICT_ADD) {

        if (rc == NGX_OK) {
            mps_slab_unlock(pool);
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

#if 0
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                           "lua shared dict set: found old entry and value "
                           "size matched, reusing it");
#endif

            mps_queue_remove(pool, &sd->queue);
            mps_queue_insert_head(pool, &dict->lru_queue, &sd->queue);

            if (exptime > 0) {
                sd->expires = mps_clock_time_ms() + (uint64_t) exptime;

            } else {
                sd->expires = 0;
            }

            sd->user_flags = user_flags;

            dd("setting value type to %d", value_type);

            sd->value_type = (uint8_t) value_type;

            ngx_memcpy(sd->data + key_len, str_value_buf, str_value_len);

            mps_slab_unlock(pool);

            return NGX_OK;
        }

#if 0
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                       "lua shared dict set: found old entry but value size "
                       "NOT matched, removing it first");
#endif

remove:

        if (sd->value_type == SHDICT_TLIST) {
            queue = mps_shdict_get_list_head(sd, key_len);

            for (q = mps_queue_head(pool, queue);
                 q != mps_queue_sentinel(pool, queue);
                 q = mps_queue_next(pool, q))
            {
                p = (u_char *) mps_queue_data(q,
                                              mps_shdict_list_node_t,
                                              queue);

                mps_slab_free_locked(pool, p);
            }
        }

        mps_queue_remove(pool, &sd->queue);

        node = (mps_rbtree_node_t *)
                   ((u_char *) sd - offsetof(mps_rbtree_node_t, color));

        mps_rbtree_delete(pool, &dict->rbtree, node);

        mps_slab_free_locked(pool, node);

    }

insert:

    /* rc == NGX_DECLINED or value size unmatch */

    if (str_value_buf == NULL) {
        mps_slab_unlock(pool);
        return NGX_OK;
    }

#if 0
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "lua shared dict set: creating a new entry");
#endif

    n = offsetof(mps_rbtree_node_t, color)
        + offsetof(mps_shdict_node_t, data)
        + key_len
        + str_value_len;

    node = mps_slab_alloc_locked(pool, n);

    if (node == NULL) {
        mps_slab_unlock(pool);
        return NGX_ERROR;
    }

    sd = (mps_shdict_node_t *) &node->color;

    node->key = hash;
    sd->key_len = (u_short) key_len;

    sd->user_flags = user_flags;
    sd->value_len = (uint32_t) str_value_len;
    sd->value_type = (uint8_t) value_type;

    p = ngx_copy(sd->data, key, key_len);
    ngx_memcpy(p, str_value_buf, str_value_len);

    printf("mps_shdict_store before mps_rbtree_insert\n");
    mps_rbtree_insert(pool, &dict->rbtree, node);
    printf("mps_shdict_store after mps_rbtree_insert\n");
    mps_queue_insert_head(pool, &dict->lru_queue, &sd->queue);
    mps_slab_unlock(pool);

    return NGX_OK;
}


int
mps_shdict_get(mps_slab_pool_t *pool, const u_char *key,
    size_t key_len, int *value_type, u_char **str_value_buf,
    size_t *str_value_len, double *num_value, int *user_flags,
    int get_stale, int *is_stale, char **err)
{
    uint32_t            hash;
    ngx_int_t           rc;
    mps_shdict_node_t  *sd;
    ngx_str_t           value;

    hash = ngx_crc32_short(key, key_len);

    mps_slab_lock(pool);

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED || (rc == NGX_DONE && !get_stale)) {
        mps_slab_unlock(pool);
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
            mps_slab_unlock(pool);
            return NGX_ERROR;
        }

        if (*value_type == SHDICT_TSTRING) {
            *str_value_buf = malloc(value.len);
            if (*str_value_buf == NULL) {
                mps_slab_unlock(pool);
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
            mps_slab_unlock(pool);
#if 0
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                          "bad lua number value size found for key %*s "
                          "in shared_dict %V: %z", key_len, key,
                          &name, value.len);
#endif
            return NGX_ERROR;
        }

        *str_value_len = value.len;
        ngx_memcpy(num_value, value.data, sizeof(double));
        break;

    case SHDICT_TBOOLEAN:

        if (value.len != sizeof(u_char)) {
            mps_slab_unlock(pool);
#if 0
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                          "bad lua boolean value size found for key %*s "
                          "in shared_dict %V: %z", key_len, key, &name,
                          value.len);
#endif
            return NGX_ERROR;
        }

        ngx_memcpy(*str_value_buf, value.data, value.len);
        break;

    case SHDICT_TLIST:

        mps_slab_unlock(pool);

        *err = "value is a list";
        return NGX_ERROR;

    default:

        mps_slab_unlock(pool);
#if 0
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "bad value type found for key %*s in "
                      "shared_dict %V: %d", key_len, key, &name,
                      *value_type);
#endif
        return NGX_ERROR;
    }

    *user_flags = sd->user_flags;
    dd("user flags: %d", *user_flags);

    mps_slab_unlock(pool);

    if (get_stale) {

        /* always return value, flags, stale */

        *is_stale = (rc == NGX_DONE);
        return NGX_OK;
    }

    return NGX_OK;
}


int
mps_shdict_incr(mps_slab_pool_t *pool, const u_char *key,
    size_t key_len, double *value, char **err, int has_init, double init,
    long init_ttl, int *forcible)
{
    int                 i, n;
    uint32_t            hash;
    ngx_int_t           rc;
    uint64_t            now = 0;
    mps_shdict_t       *dict;
    mps_shdict_node_t  *sd;
    double              num;
    mps_rbtree_node_t  *node;
    u_char             *p;
    mps_queue_t        *queue, *q;

    if (init_ttl > 0) {
        now = mps_clock_time_ms();
    }

    dict = mps_shdict(pool);

    *forcible = 0;

    hash = ngx_crc32_short(key, key_len);

    // dd("looking up key %.*s in shared dict %.*s", (int) key_len, key,
    //    (int) ctx->name.len, ctx->name.data);

    mps_slab_lock(pool);
#if 1
    mps_shdict_expire(pool, dict, 1);
#endif
    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);

    dd("shdict lookup returned %d", (int) rc);

    if (rc == NGX_DECLINED || rc == NGX_DONE) {
        if (!has_init) {
            mps_slab_unlock(pool);
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
#if 0
                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                               "lua shared dict incr: found old entry and "
                               "value size matched, reusing it");
#endif

                mps_queue_remove(pool, &sd->queue);
                mps_queue_insert_head(pool, &dict->lru_queue, &sd->queue);

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
        mps_slab_unlock(pool);
        *err = "not a number";
        return NGX_ERROR;
    }

    mps_queue_remove(pool, &sd->queue);
    mps_queue_insert_head(pool, &dict->lru_queue, &sd->queue);

    dd("setting value type to %d", (int) sd->value_type);

    p = sd->data + key_len;

    ngx_memcpy(&num, p, sizeof(double));
    num += *value;

    ngx_memcpy(p, (double *) &num, sizeof(double));

    mps_slab_unlock(pool);

    *value = num;
    return NGX_OK;

remove:

#if 0
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "lua shared dict incr: found old entry but value size "
                   "NOT matched, removing it first");
#endif

    if (sd->value_type == SHDICT_TLIST) {
        queue = mps_shdict_get_list_head(sd, key_len);

        for (q = mps_queue_head(pool, queue);
             q != mps_queue_sentinel(pool, queue);
             q = mps_queue_next(pool, q))
        {
            p = (u_char *) mps_queue_data(q, mps_shdict_list_node_t,
                                          queue);

            mps_slab_free_locked(pool, p);
        }
    }

    mps_queue_remove(pool, &sd->queue);

    node = (mps_rbtree_node_t *)
                          ((u_char *) sd - offsetof(mps_rbtree_node_t, color));

    mps_rbtree_delete(pool, &dict->rbtree, node);

    mps_slab_free_locked(pool, node);

insert:

#if 0
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "lua shared dict incr: creating a new entry");
#endif

    n = offsetof(mps_rbtree_node_t, color)
        + offsetof(mps_shdict_node_t, data)
        + key_len
        + sizeof(double);

    node = mps_slab_alloc_locked(pool, n);

    if (node == NULL) {

#if 0
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                       "lua shared dict incr: overriding non-expired items "
                       "due to memory shortage for entry \"%*s\"", key_len,
                       key);
#endif

        for (i = 0; i < 30; i++) {
            if (mps_shdict_expire(pool, dict, 0) == 0) {
                break;
            }

            *forcible = 1;

            node = mps_slab_alloc_locked(pool, n);
            if (node != NULL) {
                goto allocated;
            }
        }

        mps_slab_unlock(pool);

        *err = "no memory";
        return NGX_ERROR;
    }

allocated:

    sd = (mps_shdict_node_t *) &node->color;

    node->key = hash;

    sd->key_len = (u_short) key_len;

    sd->value_len = (uint32_t) sizeof(double);

    mps_rbtree_insert(pool, &dict->rbtree, node);

    mps_queue_insert_head(pool, &dict->lru_queue, &sd->queue);

setvalue:

    sd->user_flags = 0;

    if (init_ttl > 0) {
        sd->expires = now + (uint64_t) init_ttl;

    } else {
        sd->expires = 0;
    }

    dd("setting value type to %d", LUA_TNUMBER);

    sd->value_type = (uint8_t) LUA_TNUMBER;

    p = ngx_copy(sd->data, key, key_len);
    ngx_memcpy(p, (double *) &num, sizeof(double));

    mps_slab_unlock(pool);

    *value = num;
    return NGX_OK;
}


int
mps_shdict_flush_all(mps_slab_pool_t *pool)
{
    mps_queue_t        *q;
    mps_shdict_t       *dict;
    mps_shdict_node_t  *sd;

    dict = mps_shdict(pool);

    mps_slab_lock(pool);

    for (q = mps_queue_head(pool, &dict->lru_queue);
         q != mps_queue_sentinel(pool, &dict->lru_queue);
         q = mps_queue_next(pool, q))
    {
        sd = mps_queue_data(q, mps_shdict_node_t, queue);
        sd->expires = 1;
    }

    mps_shdict_expire(pool, dict, 0);

    mps_slab_unlock(pool);

    return NGX_OK;
}


static ngx_int_t
mps_shdict_peek(mps_slab_pool_t *pool, ngx_uint_t hash,
    const u_char *kdata, size_t klen, mps_shdict_node_t **sdp)
{
    ngx_int_t           rc;
    mps_rbtree_node_t  *node, *sentinel;
    mps_shdict_t       *dict;
    mps_shdict_node_t  *sd;

    dict = mps_shdict(pool);
    node = mps_rbtree_node(pool, dict->rbtree.root);
    sentinel = mps_rbtree_node(pool, dict->rbtree.sentinel);

    while (node != sentinel) {

        if (hash < node->key) {
            node = mps_rbtree_node(pool, node->left);
            continue;
        }

        if (hash > node->key) {
            node = mps_rbtree_node(pool, node->right);
            continue;
        }

        /* hash == node->key */

        sd = (mps_shdict_node_t *) &node->color;

        rc = ngx_memn2cmp(kdata, sd->data, klen, (size_t) sd->key_len);

        if (rc == 0) {
            *sdp = sd;

            return NGX_OK;
        }

        node = mps_rbtree_node(pool, (rc < 0) ? node->left : node->right);
    }

    *sdp = NULL;

    return NGX_DECLINED;
}


long
mps_shdict_get_ttl(mps_slab_pool_t *pool, const u_char *key, size_t key_len)
{
    uint32_t            hash;
    uint64_t            expires, now;
    ngx_int_t           rc;
    mps_shdict_node_t  *sd;

    hash = ngx_crc32_short(key, key_len);

    mps_slab_lock(pool);

    rc = mps_shdict_peek(pool, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED) {
        mps_slab_unlock(pool);

        return NGX_DECLINED;
    }

    /* rc == NGX_OK */

    expires = sd->expires;

    mps_slab_unlock(pool);

    if (expires == 0) {
        return 0;
    }

    now = mps_clock_time_ms();

    return expires - now;
}


int
mps_shdict_set_expire(mps_slab_pool_t *pool, const u_char *key, size_t key_len,
    long exptime)
{
    uint32_t            hash;
    ngx_int_t           rc;
    uint64_t            now = 0;
    mps_shdict_node_t  *sd;

    if (exptime > 0) {
        now = mps_clock_time_ms();
    }

    hash = ngx_crc32_short(key, key_len);

    mps_slab_lock(pool);

    rc = mps_shdict_peek(pool, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED) {
        mps_slab_unlock(pool);

        return NGX_DECLINED;
    }

    /* rc == NGX_OK */

    if (exptime > 0) {
        sd->expires = now + (uint64_t) exptime;

    } else {
        sd->expires = 0;
    }

    mps_slab_unlock(pool);

    return NGX_OK;
}


size_t
mps_shdict_capacity(mps_slab_pool_t *pool)
{
    return (size_t) pool->end;
}


size_t
mps_shdict_free_space(mps_slab_pool_t *pool)
{
    size_t   bytes;

    mps_slab_lock(pool);
    bytes = pool->pfree * mps_pagesize;
    mps_slab_unlock(pool);

    return bytes;
}
