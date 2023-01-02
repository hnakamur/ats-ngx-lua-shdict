#include "mps_shdict.h"
#include "mps_log.h"

#ifdef DDEBUG

#define dd(...)                                                                \
    fprintf(stderr, "lua *** %s: ", __func__);                                 \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__)

#else

#define dd(...)

#endif

static pthread_once_t dicts_lock_initialized = PTHREAD_ONCE_INIT;
static pthread_mutex_t dicts_lock;
static int dicts_count = 0;
static mps_shdict_t *dicts = NULL;

#define MPS_SHDICT_LEFT 0x0001
#define MPS_SHDICT_RIGHT 0x0002

static int mps_shdict_push_helper(mps_shdict_t *dict, int direction,
                                  const u_char *key, size_t key_len,
                                  int value_type, const u_char *str_value_buf,
                                  size_t str_value_len, double num_value,
                                  char **errmsg);
static int mps_shdict_pop_helper(mps_shdict_t *dict, int direction,
                                 const u_char *key, size_t key_len,
                                 int *value_type, u_char **str_value_buf,
                                 size_t *str_value_len, double *num_value,
                                 char **errmsg);

static void mps_shdict_init_dicts_lock()
{
    pthread_mutexattr_t attr;
    int rc;

    rc = pthread_mutexattr_init(&attr);
    if (rc != 0) {
        mps_log_error(
            "mps_shdict_init_dicts_lock: pthread_mutexattr_init: err=%s",
            strerror(rc));
        return;
    }

    rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (rc != 0) {
        mps_log_error(
            "mps_shdict_init_dicts_lock: pthread_mutexattr_setrobust: err=%s",
            strerror(rc));
        return;
    }

    rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
        mps_log_error(
            "mps_shdict_init_dicts_lock: pthread_mutexattr_setpshared: err=%s",
            strerror(rc));
        return;
    }

    rc = pthread_mutex_init(&dicts_lock, &attr);
    if (rc != 0) {
        mps_log_error("mps_shdict_init_dicts_lock: pthread_mutex_init: err=%s",
                      strerror(rc));
        return;
    }

    rc = pthread_mutexattr_destroy(&attr);
    if (rc != 0) {
        mps_log_error(
            "mps_shdict_init_dicts_lock: pthread_mutexattr_destroy: err=%s",
            strerror(rc));
        return;
    }
}

static mps_shdict_t *find_dict_by_pathname(const char *pathname)
{
    size_t pathname_len;
    int i;

    pathname_len = strlen(pathname);
    for (i = 0; i < dicts_count; i++) {
        if (!ngx_memn2cmp(dicts[i].name.data, (const u_char *)pathname,
                          dicts[i].name.len, pathname_len)) {
            return &dicts[i];
        }
    }

    return NULL;
}

static int index_of_dict(mps_shdict_t *dict)
{
    int i;

    for (i = 0; i < dicts_count; i++) {
        if (&dicts[i] == dict) {
            return i;
        }
    }

    return -1;
}

static int mps_shdict_expire(mps_slab_pool_t *pool, mps_shdict_tree_t *tree,
                             ngx_uint_t n);

/* Store op flags */

#define MPS_SHDICT_ADD 0x0001
#define MPS_SHDICT_REPLACE 0x0002
#define MPS_SHDICT_SAFE_STORE 0x0004

static inline uint64_t msec_from_timespec(const struct timespec *ts)
{
    return (uint64_t)ts->tv_sec * 1000 + (uint64_t)ts->tv_nsec / 1000000;
}

static inline uint64_t mps_clock_time_ms()
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        mps_log_error("clock_gettime, %s", strerror(errno));
        return 0;
    }

    return msec_from_timespec(&ts);
}

static ngx_inline mps_queue_t *mps_shdict_get_list_head(mps_shdict_node_t *sd,
                                                        size_t len)
{
    return (mps_queue_t *)ngx_align_ptr(((u_char *)&sd->data + len),
                                        NGX_ALIGNMENT);
}

void mps_shdict_rbtree_insert_value(mps_slab_pool_t *pool,
                                    mps_rbtree_node_t *temp,
                                    mps_rbtree_node_t *node,
                                    mps_rbtree_node_t *sentinel)
{
    mps_ptroff_t *p, s;
    mps_shdict_node_t *sdn, *sdnt;

    s = mps_offset(pool, sentinel);

    for (;;) {
        if (node->key < temp->key) {

            p = &temp->left;

        } else if (node->key > temp->key) {

            p = &temp->right;

        } else { /* node->key == temp->key */

            sdn = (mps_shdict_node_t *)&node->color;
            sdnt = (mps_shdict_node_t *)&temp->color;

            p = ngx_memn2cmp(sdn->data, sdnt->data, sdn->key_len,
                             sdnt->key_len) < 0
                    ? &temp->left
                    : &temp->right;
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

static mps_err_t mps_shdict_on_init(mps_slab_pool_t *pool)
{
    mps_shdict_tree_t *dict;
    mps_err_t err;

    dict = mps_slab_alloc(pool, sizeof(mps_shdict_tree_t));
    if (!dict) {
        mps_log_error("mps_shdict_on_init: mps_slab_alloc failed");
        return ENOMEM;
    }

    pool->data = mps_offset(pool, dict);
    err = mps_rbtree_init(pool, &dict->rbtree, &dict->sentinel,
                          MPS_RBTREE_INSERT_TYPE_ID_LUADICT);
    if (err != 0) {
        return err;
    }
    mps_queue_init(pool, &dict->lru_queue);

    pool->log_nomem = 0;
    return 0;
}

mps_shdict_t *mps_shdict_open_or_create(const char *pathname, size_t shm_size,
                                        size_t min_shift, mode_t mode)
{
    int rc;
    mps_shdict_t *dict, *new_dicts;
    char *pathname_copy;
    mps_slab_pool_t *pool;

    rc = pthread_once(&dicts_lock_initialized, mps_shdict_init_dicts_lock);
    if (rc != 0) {
        mps_log_error(
            "mps_shdict_open_or_create: mps_shdict_init_dicts_lock: err=%s",
            strerror(rc));
        return NULL;
    }

    pthread_mutex_lock(&dicts_lock);

    dict = find_dict_by_pathname(pathname);
    if (dict) {
        pthread_mutex_unlock(&dicts_lock);
        return dict;
    }

    pool = mps_slab_open_or_create(pathname, shm_size, min_shift, mode,
                                   mps_shdict_on_init);
    if (pool == NULL) {
        pthread_mutex_unlock(&dicts_lock);
        return NULL;
    }

    pathname_copy = strdup(pathname);
    if (pathname_copy == NULL) {
        pthread_mutex_unlock(&dicts_lock);
        return NULL;
    }

    new_dicts = realloc(dicts, sizeof(mps_shdict_t) * (dicts_count + 1));
    if (new_dicts == NULL) {
        free(pathname_copy);
        pthread_mutex_unlock(&dicts_lock);
        return NULL;
    }

    dicts = new_dicts;
    dict = &dicts[dicts_count];
    dicts_count++;

    dict->pool = pool;
    dict->name.len = strlen(pathname);
    dict->name.data = (u_char *)pathname_copy;
    dict->size = shm_size;

    pthread_mutex_unlock(&dicts_lock);

    return dict;
}

void mps_shdict_close(mps_shdict_t *dict)
{
    int rc, i;
    mps_shdict_t *new_dicts;
    u_char *dict_name_data;

    rc = pthread_once(&dicts_lock_initialized, mps_shdict_init_dicts_lock);
    if (rc != 0) {
        mps_log_error("mps_shdict_close: mps_shdict_init_dicts_lock: err=%s",
                      strerror(rc));
        return;
    }

    pthread_mutex_lock(&dicts_lock);

    i = index_of_dict(dict);

    if (i == -1) {
        pthread_mutex_unlock(&dicts_lock);
        return;
    }

    mps_slab_close(dict->pool, dict->size);

    dict_name_data = dict->name.data;

    dicts[i] = dicts[dicts_count - 1];

    if (dicts_count == 1) {
        free(dicts);
        new_dicts = NULL;

    } else {
        new_dicts = realloc(dicts, sizeof(mps_shdict_t) * (dicts_count - 1));
        if (new_dicts == NULL) {
            mps_log_error("mps_shdict_close: realloc failed: err=%s",
                          strerror(rc));
            pthread_mutex_unlock(&dicts_lock);
            return;
        }
    }

    dicts = new_dicts;
    dicts_count--;
    free(dict_name_data);

    pthread_mutex_unlock(&dicts_lock);
}

static ngx_int_t mps_shdict_lookup(mps_slab_pool_t *pool, ngx_uint_t hash,
                                   const u_char *kdata, size_t klen,
                                   mps_shdict_node_t **sdp)
{
    mps_shdict_tree_t *tree;
    ngx_int_t rc;
    uint64_t now;
    int64_t ms;
    mps_rbtree_node_t *node, *sentinel;
    mps_shdict_node_t *sd;

    tree = mps_shdict_tree(pool);

    node = mps_rbtree_node(pool, tree->rbtree.root);
    sentinel = mps_rbtree_node(pool, tree->rbtree.sentinel);

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

        sd = (mps_shdict_node_t *)&node->color;

        rc = ngx_memn2cmp(kdata, sd->data, klen, (size_t)sd->key_len);

        if (rc == 0) {
            mps_queue_remove(pool, &sd->queue);
            mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

            *sdp = sd;

            dd("node expires: %lld", (long long)sd->expires);

            if (sd->expires != 0) {
                now = mps_clock_time_ms();
                ms = sd->expires - now;

                dd("time to live: %lld", (long long)ms);

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

static int mps_shdict_expire(mps_slab_pool_t *pool, mps_shdict_tree_t *tree,
                             ngx_uint_t n)
{
    uint64_t now;
    mps_queue_t *q, *list_queue, *lq;
    int64_t ms;
    mps_rbtree_node_t *node;
    mps_shdict_node_t *sd;
    int freed = 0;
    mps_shdict_list_node_t *lnode;

    now = mps_clock_time_ms();

    /*
     * n == 1 deletes one or two expired entries
     * n == 0 deletes oldest entry by force
     *        and one or two zero rate entries
     */

    while (n < 3) {

        if (mps_queue_empty(pool, &tree->lru_queue)) {
            return freed;
        }

        q = mps_queue_last(pool, &tree->lru_queue);

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

        if (sd->value_type == MPS_SHDICT_TLIST) {
            list_queue = mps_shdict_get_list_head(sd, sd->key_len);

            for (lq = mps_queue_head(pool, list_queue);
                 lq != mps_queue_sentinel(pool, list_queue);
                 lq = mps_queue_next(pool, lq)) {
                lnode = mps_queue_data(lq, mps_shdict_list_node_t, queue);

                mps_slab_free_locked(pool, lnode);
            }
        }

        mps_queue_remove(pool, q);

        node = (mps_rbtree_node_t *)((u_char *)sd -
                                     offsetof(mps_rbtree_node_t, color));

        mps_rbtree_delete(pool, &tree->rbtree, node);

        mps_slab_free_locked(pool, node);

        freed++;
    }

    return freed;
}

/* This function is exported for Lua. */

int mps_shdict_store(mps_shdict_t *dict, int op, const u_char *key,
                     size_t key_len, int value_type,
                     const u_char *str_value_buf, size_t str_value_len,
                     double num_value, long exptime, int user_flags,
                     char **errmsg, int *forcible)
{
    mps_slab_pool_t *pool;
    mps_shdict_tree_t *tree;
    int i, n;
    uint32_t hash;
    ngx_int_t rc;
    mps_queue_t *queue, *q;
    mps_rbtree_node_t *node;
    mps_shdict_node_t *sd;
    u_char c, *p;
    uint64_t now;

    pool = dict->pool;
    tree = mps_shdict_tree(pool);

    *forcible = 0;

    hash = ngx_murmur_hash2(key, key_len);

    switch (value_type) {

    case MPS_SHDICT_TSTRING:
        /* do nothing */
        break;

    case MPS_SHDICT_TNUMBER:
        dd("num value: %lf", num_value);
        str_value_buf = (u_char *)&num_value;
        str_value_len = sizeof(double);
        break;

    case MPS_SHDICT_TBOOLEAN:
        c = num_value ? 1 : 0;
        str_value_buf = &c;
        str_value_len = sizeof(u_char);
        break;

    case MPS_SHDICT_TNIL:
        if (op & (MPS_SHDICT_ADD | MPS_SHDICT_REPLACE)) {
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
    mps_shdict_expire(pool, tree, 1);
#endif

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);
    dd("lookup returns %d", (int)rc);

    if (op & MPS_SHDICT_REPLACE) {

        if (rc == NGX_DECLINED || rc == NGX_DONE) {
            mps_slab_unlock(pool);
            *errmsg = "not found";
            return NGX_DECLINED;
        }

        /* rc == NGX_OK */

        goto replace;
    }

    if (op & MPS_SHDICT_ADD) {

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

        if (value_type == MPS_SHDICT_TNIL) {
            goto remove;
        }

    replace:

        if (str_value_buf && str_value_len == (size_t)sd->value_len &&
            sd->value_type != MPS_SHDICT_TLIST) {

            mps_log_debug(MPS_LOG_TAG,
                          "lua shared tree set in dict \"%.*s\": "
                          "found old entry and value size matched, reusing it",
                          (int)dict->name.len, dict->name.data);

            mps_queue_remove(pool, &sd->queue);
            mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

            if (exptime > 0) {
                sd->expires = mps_clock_time_ms() + (uint64_t)exptime;

            } else {
                sd->expires = 0;
            }

            sd->user_flags = user_flags;

            dd("setting value type to %d", value_type);

            sd->value_type = (uint8_t)value_type;

            ngx_memcpy(sd->data + key_len, str_value_buf, str_value_len);

            mps_slab_unlock(pool);

            return NGX_OK;
        }

        mps_log_debug(
            MPS_LOG_TAG,
            "lua shared dict set in dict \"%.*s\": "
            "found old entry but value size NOT matched, removing it first",
            (int)dict->name.len, dict->name.data);

    remove:

        if (sd->value_type == MPS_SHDICT_TLIST) {
            queue = mps_shdict_get_list_head(sd, key_len);

            for (q = mps_queue_head(pool, queue);
                 q != mps_queue_sentinel(pool, queue);
                 q = mps_queue_next(pool, q)) {
                p = (u_char *)mps_queue_data(q, mps_shdict_list_node_t, queue);

                mps_slab_free_locked(pool, p);
            }
        }

        mps_queue_remove(pool, &sd->queue);

        node = (mps_rbtree_node_t *)((u_char *)sd -
                                     offsetof(mps_rbtree_node_t, color));

        mps_rbtree_delete(pool, &tree->rbtree, node);

        mps_slab_free_locked(pool, node);
    }

insert:

    /* rc == NGX_DECLINED or value size unmatch */

    if (str_value_buf == NULL) {
        mps_slab_unlock(pool);
        return NGX_OK;
    }

    mps_log_debug(MPS_LOG_TAG,
                  "lua shared dict set in dict \"%.*s\": creating a new entry",
                  (int)dict->name.len, dict->name.data);

    n = offsetof(mps_rbtree_node_t, color) + offsetof(mps_shdict_node_t, data) +
        key_len + str_value_len;

    node = mps_slab_alloc_locked(pool, n);

    if (node == NULL) {

        if (op & MPS_SHDICT_SAFE_STORE) {
            mps_slab_unlock(pool);

            *errmsg = "no memory";
            return NGX_ERROR;
        }

        mps_log_debug(MPS_LOG_TAG,
                      "lua shared dict set: overriding non-expired items "
                      "due to memory shortage for entry \"%.*s\"",
                      (int)key_len, key);

        for (i = 0; i < 30; i++) {
            if (mps_shdict_expire(pool, tree, 0) == 0) {
                break;
            }

            *forcible = 1;

            node = mps_slab_alloc_locked(pool, n);
            if (node != NULL) {
                goto allocated;
            }
        }

        mps_slab_unlock(pool);

        *errmsg = "no memory";
        return NGX_ERROR;
    }

allocated:

    sd = (mps_shdict_node_t *)&node->color;

    node->key = hash;
    sd->key_len = (u_short)key_len;

    if (exptime > 0) {
        now = mps_clock_time_ms();
        sd->expires = now + (uint64_t)exptime;

    } else {
        sd->expires = 0;
    }

    sd->user_flags = user_flags;
    sd->value_len = (uint32_t)str_value_len;
    dd("setting value type to %d", value_type);
    sd->value_type = (uint8_t)value_type;

    p = ngx_copy(sd->data, key, key_len);
    ngx_memcpy(p, str_value_buf, str_value_len);

    mps_rbtree_insert(pool, &tree->rbtree, node);
    mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);
    mps_slab_unlock(pool);

    return NGX_OK;
}

int mps_shdict_set(mps_shdict_t *dict, const u_char *key, size_t key_len,
                   int value_type, const u_char *str_value_buf,
                   size_t str_value_len, double num_value, long exptime,
                   int user_flags, char **errmsg, int *forcible)
{
    return mps_shdict_store(dict, 0, key, key_len, value_type, str_value_buf,
                            str_value_len, num_value, exptime, user_flags,
                            errmsg, forcible);
}

int mps_shdict_safe_set(mps_shdict_t *dict, const u_char *key, size_t key_len,
                        int value_type, const u_char *str_value_buf,
                        size_t str_value_len, double num_value, long exptime,
                        int user_flags, char **errmsg, int *forcible)
{
    return mps_shdict_store(dict, MPS_SHDICT_SAFE_STORE, key, key_len,
                            value_type, str_value_buf, str_value_len, num_value,
                            exptime, user_flags, errmsg, forcible);
}

int mps_shdict_add(mps_shdict_t *dict, const u_char *key, size_t key_len,
                   int value_type, const u_char *str_value_buf,
                   size_t str_value_len, double num_value, long exptime,
                   int user_flags, char **errmsg, int *forcible)
{
    return mps_shdict_store(dict, MPS_SHDICT_ADD, key, key_len, value_type,
                            str_value_buf, str_value_len, num_value, exptime,
                            user_flags, errmsg, forcible);
}

int mps_shdict_safe_add(mps_shdict_t *dict, const u_char *key, size_t key_len,
                        int value_type, const u_char *str_value_buf,
                        size_t str_value_len, double num_value, long exptime,
                        int user_flags, char **errmsg, int *forcible)
{
    return mps_shdict_store(dict, MPS_SHDICT_ADD | MPS_SHDICT_SAFE_STORE, key,
                            key_len, value_type, str_value_buf, str_value_len,
                            num_value, exptime, user_flags, errmsg, forcible);
}

int mps_shdict_replace(mps_shdict_t *dict, const u_char *key, size_t key_len,
                       int value_type, const u_char *str_value_buf,
                       size_t str_value_len, double num_value, long exptime,
                       int user_flags, char **errmsg, int *forcible)
{
    return mps_shdict_store(dict, MPS_SHDICT_REPLACE, key, key_len, value_type,
                            str_value_buf, str_value_len, num_value, exptime,
                            user_flags, errmsg, forcible);
}

int mps_shdict_delete(mps_shdict_t *dict, const u_char *key, size_t key_len)
{
    int forcible = 0;
    return mps_shdict_store(dict, 0, key, key_len, MPS_SHDICT_TNIL, NULL, 0, 0,
                            0, 0, NULL, &forcible);
}

int mps_shdict_get(mps_shdict_t *dict, const u_char *key, size_t key_len,
                   int *value_type, u_char **str_value_buf,
                   size_t *str_value_len, double *num_value, int *user_flags,
                   int get_stale, int *is_stale, char **err)
{
    mps_slab_pool_t *pool;
    uint32_t hash;
    ngx_int_t rc;
    mps_shdict_node_t *sd;
    ngx_str_t value;

    hash = ngx_murmur_hash2(key, key_len);

    pool = dict->pool;
    mps_slab_lock(pool);

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED || (rc == NGX_DONE && !get_stale)) {
        mps_slab_unlock(pool);
        *value_type = MPS_SHDICT_TNIL;
        return NGX_OK;
    }

    *value_type = sd->value_type;

    dd("data: %p", sd->data);
    dd("key len: %d", (int)sd->key_len);

    value.data = sd->data + sd->key_len;
    value.len = (size_t)sd->value_len;

    if (*str_value_len < (size_t)value.len) {
        if (*value_type == MPS_SHDICT_TBOOLEAN) {
            mps_slab_unlock(pool);
            return NGX_ERROR;
        }

        if (*value_type == MPS_SHDICT_TSTRING) {
            *str_value_buf = malloc(value.len);
            if (*str_value_buf == NULL) {
                mps_slab_unlock(pool);
                return NGX_ERROR;
            }
        }
    }

    switch (*value_type) {

    case MPS_SHDICT_TSTRING:
        *str_value_len = value.len;
        ngx_memcpy(*str_value_buf, value.data, value.len);
        break;

    case MPS_SHDICT_TNUMBER:

        if (value.len != sizeof(double)) {
            mps_slab_unlock(pool);
            mps_log_error("bad lua number value size found for key %.*s"
                          "in dict \"%.*s\": %lu",
                          (int)key_len, key, (int)dict->name.len,
                          dict->name.data, value.len);
            return NGX_ERROR;
        }

        *str_value_len = value.len;
        ngx_memcpy(num_value, value.data, sizeof(double));
        break;

    case MPS_SHDICT_TBOOLEAN:

        if (value.len != sizeof(u_char)) {
            mps_slab_unlock(pool);
            mps_log_error("bad lua boolean value size found for key %.*s"
                          "in dict \"%.*s\": %lu",
                          (int)key_len, key, (int)dict->name.len,
                          dict->name.data, value.len);
            return NGX_ERROR;
        }

        ngx_memcpy(*str_value_buf, value.data, value.len);
        break;

    case MPS_SHDICT_TLIST:

        mps_slab_unlock(pool);

        *err = "value is a list";
        return NGX_ERROR;

    default:

        mps_slab_unlock(pool);
        mps_log_error("bad value type found for key %.*s"
                      " in dict \"%.*s\": %d",
                      (int)key_len, key, (int)dict->name.len, dict->name.data,
                      *value_type);
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

int mps_shdict_incr(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    double *value, char **err, int has_init, double init,
                    long init_ttl, int *forcible)
{
    mps_slab_pool_t *pool;
    int i, n;
    uint32_t hash;
    ngx_int_t rc;
    uint64_t now = 0;
    mps_shdict_tree_t *tree;
    mps_shdict_node_t *sd;
    double num;
    mps_rbtree_node_t *node;
    u_char *p;
    mps_queue_t *queue, *q;

    if (init_ttl > 0) {
        now = mps_clock_time_ms();
    }

    pool = dict->pool;
    tree = mps_shdict_tree(pool);

    *forcible = 0;

    hash = ngx_murmur_hash2(key, key_len);

    // dd("looking up key %.*s in shared dict %.*s", (int) key_len, key,
    //    (int) ctx->name.len, ctx->name.data);

    mps_slab_lock(pool);
#if 1
    mps_shdict_expire(pool, tree, 1);
#endif

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);

    dd("shdict lookup returned %d", (int)rc);

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

            if ((size_t)sd->value_len == sizeof(double) &&
                sd->value_type != MPS_SHDICT_TLIST) {
                mps_log_debug(
                    MPS_LOG_TAG,
                    "lua shared dict incr in dict \"%.*s\": "
                    "found old entry and value size matched, reusing it",
                    (int)dict->name.len, dict->name.data);

                mps_queue_remove(pool, &sd->queue);
                mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

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

    if (sd->value_type != MPS_SHDICT_TNUMBER ||
        sd->value_len != sizeof(double)) {
        mps_slab_unlock(pool);
        *err = "not a number";
        return NGX_ERROR;
    }

    mps_queue_remove(pool, &sd->queue);
    mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

    dd("setting value type to %d", (int)sd->value_type);

    p = sd->data + key_len;

    ngx_memcpy(&num, p, sizeof(double));
    num += *value;

    ngx_memcpy(p, (double *)&num, sizeof(double));

    mps_slab_unlock(pool);

    *value = num;
    return NGX_OK;

remove:

    mps_log_debug(
        MPS_LOG_TAG,
        "lua shared dict incr in dict \"%.*s\": "
        "found old entry but value size NOT matched, removing it first",
        (int)dict->name.len, dict->name.data);

    if (sd->value_type == MPS_SHDICT_TLIST) {
        queue = mps_shdict_get_list_head(sd, key_len);

        for (q = mps_queue_head(pool, queue);
             q != mps_queue_sentinel(pool, queue);
             q = mps_queue_next(pool, q)) {
            p = (u_char *)mps_queue_data(q, mps_shdict_list_node_t, queue);

            mps_slab_free_locked(pool, p);
        }
    }

    mps_queue_remove(pool, &sd->queue);

    node = (mps_rbtree_node_t *)((u_char *)sd -
                                 offsetof(mps_rbtree_node_t, color));

    mps_rbtree_delete(pool, &tree->rbtree, node);
    mps_slab_free_locked(pool, node);

insert:

    mps_log_debug(MPS_LOG_TAG,
                  "lua shared dict incr in dict \"%.*s\": creating a new entry",
                  (int)dict->name.len, dict->name.data);

    n = offsetof(mps_rbtree_node_t, color) + offsetof(mps_shdict_node_t, data) +
        key_len + sizeof(double);

    node = mps_slab_alloc_locked(pool, n);

    if (node == NULL) {

        mps_log_debug(MPS_LOG_TAG,
                      "lua shared dict incr in dict \"%.*s\": overriding "
                      "non-expired items "
                      "due to memory shortage for entry \"%.*s\"",
                      (int)dict->name.len, dict->name.data, (int)key_len, key);

        for (i = 0; i < 30; i++) {
            if (mps_shdict_expire(pool, tree, 0) == 0) {
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

    sd = (mps_shdict_node_t *)&node->color;

    node->key = hash;

    sd->key_len = (u_short)key_len;

    sd->value_len = (uint32_t)sizeof(double);

    mps_rbtree_insert(pool, &tree->rbtree, node);

    mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

setvalue:

    sd->user_flags = 0;

    if (init_ttl > 0) {
        sd->expires = now + (uint64_t)init_ttl;

    } else {
        sd->expires = 0;
    }

    dd("setting value type to %d", LUA_TNUMBER);

    sd->value_type = (uint8_t)LUA_TNUMBER;

    p = ngx_copy(sd->data, key, key_len);
    ngx_memcpy(p, (double *)&num, sizeof(double));

    mps_slab_unlock(pool);

    *value = num;
    return NGX_OK;
}

int mps_shdict_flush_all(mps_shdict_t *dict)
{
    mps_slab_pool_t *pool;
    mps_queue_t *q;
    mps_shdict_tree_t *tree;
    mps_shdict_node_t *sd;

    pool = dict->pool;
    tree = mps_shdict_tree(pool);

    mps_slab_lock(pool);

    for (q = mps_queue_head(pool, &tree->lru_queue);
         q != mps_queue_sentinel(pool, &tree->lru_queue);
         q = mps_queue_next(pool, q)) {
        sd = mps_queue_data(q, mps_shdict_node_t, queue);
        sd->expires = 1;
    }

    mps_shdict_expire(pool, tree, 0);

    mps_slab_unlock(pool);

    return NGX_OK;
}

static ngx_int_t mps_shdict_peek(mps_slab_pool_t *pool, ngx_uint_t hash,
                                 const u_char *kdata, size_t klen,
                                 mps_shdict_node_t **sdp)
{
    ngx_int_t rc;
    mps_rbtree_node_t *node, *sentinel;
    mps_shdict_tree_t *tree;
    mps_shdict_node_t *sd;

    tree = mps_shdict_tree(pool);
    node = mps_rbtree_node(pool, tree->rbtree.root);
    sentinel = mps_rbtree_node(pool, tree->rbtree.sentinel);

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

        sd = (mps_shdict_node_t *)&node->color;

        rc = ngx_memn2cmp(kdata, sd->data, klen, (size_t)sd->key_len);

        if (rc == 0) {
            *sdp = sd;

            return NGX_OK;
        }

        node = mps_rbtree_node(pool, (rc < 0) ? node->left : node->right);
    }

    *sdp = NULL;

    return NGX_DECLINED;
}

long mps_shdict_get_ttl(mps_shdict_t *dict, const u_char *key, size_t key_len)
{
    mps_slab_pool_t *pool;
    uint32_t hash;
    uint64_t expires, now;
    ngx_int_t rc;
    mps_shdict_node_t *sd;

    hash = ngx_murmur_hash2(key, key_len);

    pool = dict->pool;
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

int mps_shdict_set_expire(mps_shdict_t *dict, const u_char *key, size_t key_len,
                          long exptime)
{
    mps_slab_pool_t *pool;
    uint32_t hash;
    ngx_int_t rc;
    uint64_t now = 0;
    mps_shdict_node_t *sd;

    if (exptime > 0) {
        now = mps_clock_time_ms();
    }

    hash = ngx_murmur_hash2(key, key_len);

    pool = dict->pool;
    mps_slab_lock(pool);

    rc = mps_shdict_peek(pool, hash, key, key_len, &sd);

    if (rc == NGX_DECLINED) {
        mps_slab_unlock(pool);

        return NGX_DECLINED;
    }

    /* rc == NGX_OK */

    if (exptime > 0) {
        sd->expires = now + (uint64_t)exptime;

    } else {
        sd->expires = 0;
    }

    mps_slab_unlock(pool);

    return NGX_OK;
}

int mps_shdict_lpush(mps_shdict_t *dict, const u_char *key, size_t key_len,
                     int value_type, const u_char *str_value_buf,
                     size_t str_value_len, double num_value, char **errmsg)
{
    return mps_shdict_push_helper(dict, MPS_SHDICT_LEFT, key, key_len,
                                  value_type, str_value_buf, str_value_len,
                                  num_value, errmsg);
}

int mps_shdict_rpush(mps_shdict_t *dict, const u_char *key, size_t key_len,
                     int value_type, const u_char *str_value_buf,
                     size_t str_value_len, double num_value, char **errmsg)
{
    return mps_shdict_push_helper(dict, MPS_SHDICT_RIGHT, key, key_len,
                                  value_type, str_value_buf, str_value_len,
                                  num_value, errmsg);
}

static int mps_shdict_push_helper(mps_shdict_t *dict, int direction,
                                  const u_char *key, size_t key_len,
                                  int value_type, const u_char *str_value_buf,
                                  size_t str_value_len, double num_value,
                                  char **errmsg)
{
    mps_slab_pool_t *pool;
    mps_shdict_tree_t *tree;
    int n;
    uint32_t hash;
    ngx_int_t rc;
    mps_shdict_node_t *sd;
    mps_rbtree_node_t *node;
    mps_queue_t *queue, *q;
    mps_shdict_list_node_t *lnode;

    pool = dict->pool;
    tree = mps_shdict_tree(pool);

    hash = ngx_murmur_hash2(key, key_len);

    switch (value_type) {

    case MPS_SHDICT_TSTRING:
        /* do nothing */
        break;

    case MPS_SHDICT_TNUMBER:
        dd("num value: %lf", num_value);
        str_value_buf = (u_char *)&num_value;
        str_value_len = sizeof(double);
        break;

    default:
        *errmsg = "unsupported value type";
        return NGX_ERROR;
    }

    mps_slab_lock(pool);

#if 1
    mps_shdict_expire(pool, tree, 1);
#endif

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);

    dd("shdict lookup returned %d", (int)rc);

    /* exists but expired */

    if (rc == NGX_DONE) {

        if (sd->value_type != MPS_SHDICT_TLIST) {
            /* TODO: reuse when length matched */

            mps_log_debug(MPS_LOG_TAG,
                          "lua shared dict push: found old entry and value "
                          "type not matched, remove it first");

            mps_queue_remove(pool, &sd->queue);

            node = (mps_rbtree_node_t *)((u_char *)sd -
                                         offsetof(mps_rbtree_node_t, color));

            mps_rbtree_delete(pool, &tree->rbtree, node);

            mps_slab_free_locked(pool, node);

            dd("go to init_list");
            goto init_list;
        }

        mps_log_debug(MPS_LOG_TAG,
                      "lua shared dict push: found old entry and value "
                      "type matched, reusing it");

        sd->expires = 0;
        sd->value_len = 0;
        /* free list nodes */

        queue = mps_shdict_get_list_head(sd, key_len);

        for (q = mps_queue_head(pool, queue);
             q != mps_queue_sentinel(pool, queue);
             q = mps_queue_next(pool, q)) {
            /* TODO: reuse matched size list node */
            lnode = mps_queue_data(q, mps_shdict_list_node_t, queue);
            mps_slab_free_locked(pool, lnode);
        }

        mps_queue_init(pool, queue);

        mps_queue_remove(pool, &sd->queue);
        mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

        dd("go to push_node");
        goto push_node;
    }

    /* exists and not expired */

    if (rc == NGX_OK) {

        if (sd->value_type != MPS_SHDICT_TLIST) {
            mps_slab_unlock(pool);

            *errmsg = "value not a list";
            return NGX_ERROR;
        }

        queue = mps_shdict_get_list_head(sd, key_len);

        mps_queue_remove(pool, &sd->queue);
        mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

        dd("go to push_node");
        goto push_node;
    }

    /* rc == NGX_DECLINED, not found */

init_list:

    mps_log_debug(MPS_LOG_TAG, "lua shared dict list: creating a new entry");

    /* NOTICE: we assume the begin point aligned in slab, be careful */
    n = offsetof(mps_rbtree_node_t, color) + offsetof(mps_shdict_node_t, data) +
        key_len + sizeof(mps_queue_t);

    dd("length before aligned: %d", n);

    n = (int)(uintptr_t)ngx_align_ptr(n, NGX_ALIGNMENT);

    dd("length after aligned: %d", n);

    node = mps_slab_alloc_locked(pool, n);

    if (node == NULL) {
        mps_slab_unlock(pool);

        *errmsg = "no memory";
        return NGX_ERROR;
    }

    sd = (mps_shdict_node_t *)&node->color;

    queue = mps_shdict_get_list_head(sd, key_len);

    node->key = hash;
    sd->key_len = (u_short)key_len;

    sd->expires = 0;

    sd->value_len = 0;

    dd("setting value type to %d", (int)MPS_SHDICT_TLIST);

    sd->value_type = (uint8_t)MPS_SHDICT_TLIST;

    ngx_memcpy(sd->data, key, key_len);

    mps_queue_init(pool, queue);

    mps_rbtree_insert(pool, &tree->rbtree, node);

    mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

push_node:

    mps_log_debug(MPS_LOG_TAG,
                  "lua shared dict list: creating a new list node");

    n = offsetof(mps_shdict_list_node_t, data) + str_value_len;

    dd("list node length: %d", n);

    lnode = mps_slab_alloc_locked(pool, n);

    if (lnode == NULL) {

        if (sd->value_len == 0) {

            mps_log_debug(MPS_LOG_TAG,
                          "lua shared dict list: no memory for create"
                          " list node and list empty, remove it");

            mps_queue_remove(pool, &sd->queue);

            node = (mps_rbtree_node_t *)((u_char *)sd -
                                         offsetof(mps_rbtree_node_t, color));

            mps_rbtree_delete(pool, &tree->rbtree, node);

            mps_slab_free_locked(pool, node);
        }

        mps_slab_unlock(pool);

        *errmsg = "no memory";
        return NGX_ERROR;
    }

    dd("setting list length to %d", sd->value_len + 1);

    sd->value_len = sd->value_len + 1;

    dd("setting list node value length to %d", (int)str_value_len);

    lnode->value_len = (uint32_t)str_value_len;

    dd("setting list node value type to %d", value_type);

    lnode->value_type = (uint8_t)value_type;

    ngx_memcpy(lnode->data, str_value_buf, str_value_len);

    if (direction == MPS_SHDICT_LEFT) {
        mps_queue_insert_head(pool, queue, &lnode->queue);

    } else {
        mps_queue_insert_tail(pool, queue, &lnode->queue);
    }

    mps_slab_unlock(pool);

    return sd->value_len;
}

int mps_shdict_lpop(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    int *value_type, u_char **str_value_buf,
                    size_t *str_value_len, double *num_value, char **errmsg)
{
    return mps_shdict_pop_helper(dict, MPS_SHDICT_LEFT, key, key_len,
                                 value_type, str_value_buf, str_value_len,
                                 num_value, errmsg);
}

int mps_shdict_rpop(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    int *value_type, u_char **str_value_buf,
                    size_t *str_value_len, double *num_value, char **errmsg)
{
    return mps_shdict_pop_helper(dict, MPS_SHDICT_RIGHT, key, key_len,
                                 value_type, str_value_buf, str_value_len,
                                 num_value, errmsg);
}

static int mps_shdict_pop_helper(mps_shdict_t *dict, int direction,
                                 const u_char *key, size_t key_len,
                                 int *value_type, u_char **str_value_buf,
                                 size_t *str_value_len, double *num_value,
                                 char **errmsg)
{
    mps_slab_pool_t *pool;
    mps_shdict_tree_t *tree;
    uint32_t hash;
    ngx_int_t rc;
    mps_shdict_node_t *sd;
    mps_rbtree_node_t *node;
    mps_queue_t *queue;
    mps_shdict_list_node_t *lnode;
    ngx_str_t value;

    pool = dict->pool;
    tree = mps_shdict_tree(pool);

    hash = ngx_murmur_hash2(key, key_len);

    mps_slab_lock(pool);

#if 1
    mps_shdict_expire(pool, tree, 1);
#endif

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);

    dd("shdict lookup returned %d", (int)rc);

    if (rc == NGX_DECLINED || rc == NGX_DONE) {
        mps_slab_unlock(pool);

        *value_type = MPS_SHDICT_TNIL;
        return NGX_OK;
    }

    /* rc == NGX_OK */

    if (sd->value_type != MPS_SHDICT_TLIST) {
        mps_slab_unlock(pool);

        *errmsg = "value not a list";
        return NGX_ERROR;
    }

    if (sd->value_len <= 0) {
        mps_slab_unlock(pool);

        *errmsg = "bad empty value";
        return NGX_ERROR;
    }

    queue = mps_shdict_get_list_head(sd, key_len);

    if (direction == MPS_SHDICT_LEFT) {
        queue = mps_queue_head(pool, queue);

    } else {
        queue = mps_queue_last(pool, queue);
    }

    lnode = mps_queue_data(queue, mps_shdict_list_node_t, queue);

    *value_type = lnode->value_type;

    dd("data: %p", lnode->data);
    dd("value len: %d", (int)sd->value_len);

    value.data = lnode->data;
    value.len = (size_t)lnode->value_len;

    switch (*value_type) {

    case MPS_SHDICT_TSTRING:
        if (*str_value_len < (size_t)value.len) {
            *str_value_buf = malloc(value.len);
            if (*str_value_buf == NULL) {
                mps_slab_unlock(pool);

                *errmsg = "no memory";
                return NGX_ERROR;
            }
        }
        *str_value_len = value.len;
        ngx_memcpy(*str_value_buf, value.data, value.len);
        break;

    case MPS_SHDICT_TNUMBER:
        if (value.len != sizeof(double)) {
            mps_slab_unlock(pool);

            *errmsg = "bad list number value size";
            return NGX_ERROR;
        }

        *str_value_len = value.len;
        ngx_memcpy(num_value, value.data, sizeof(double));
        break;

    default:
        mps_slab_unlock(pool);

        *errmsg = "bad list node value type";
        return NGX_ERROR;
    }

    mps_queue_remove(pool, queue);

    mps_slab_free_locked(pool, lnode);

    if (sd->value_len == 1) {
        mps_log_debug(MPS_LOG_TAG,
                      "lua shared dict list: empty node after pop, "
                      "remove it");

        mps_queue_remove(pool, &sd->queue);

        node = (mps_rbtree_node_t *)((u_char *)sd -
                                     offsetof(mps_rbtree_node_t, color));

        mps_rbtree_delete(pool, &tree->rbtree, node);

        mps_slab_free_locked(pool, node);

    } else {
        sd->value_len = sd->value_len - 1;

        mps_queue_remove(pool, &sd->queue);
        mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);
    }

    mps_slab_unlock(pool);

    return NGX_OK;
}

int mps_shdict_llen(mps_shdict_t *dict, const u_char *key, size_t key_len,
                    char **errmsg)
{
    mps_slab_pool_t *pool;
    mps_shdict_tree_t *tree;
    uint32_t hash;
    ngx_int_t rc;
    mps_shdict_node_t *sd;

    hash = ngx_murmur_hash2(key, key_len);

    pool = dict->pool;
    tree = mps_shdict_tree(pool);

    mps_slab_lock(pool);

#if 1
    mps_shdict_expire(pool, tree, 1);
#endif

    rc = mps_shdict_lookup(pool, hash, key, key_len, &sd);

    dd("shdict lookup returned %d", (int)rc);

    if (rc == NGX_OK) {

        if (sd->value_type != MPS_SHDICT_TLIST) {
            mps_slab_unlock(pool);

            *errmsg = "value not a list";
            return NGX_ERROR;
        }

        mps_queue_remove(pool, &sd->queue);
        mps_queue_insert_head(pool, &tree->lru_queue, &sd->queue);

        mps_slab_unlock(pool);

        return sd->value_len;
    }

    mps_slab_unlock(pool);

    return 0;
}

size_t mps_shdict_capacity(mps_shdict_t *dict)
{
    mps_slab_pool_t *pool;

    pool = dict->pool;
    return (size_t)pool->end;
}

size_t mps_shdict_free_space(mps_shdict_t *dict)
{
    mps_slab_pool_t *pool;
    size_t bytes;

    pool = dict->pool;
    mps_slab_lock(pool);
    bytes = pool->pfree * mps_pagesize;
    mps_slab_unlock(pool);

    return bytes;
}
