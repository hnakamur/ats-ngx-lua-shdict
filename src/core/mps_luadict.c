#include "mps_luadict.h"
#include <assert.h>

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
    printf("mps_luadict_on_init pool=%p, dict=%lu, dict->rbtree=%lu, dict->sentinel=%lu, dict->lru_queue=%lu\n",
        pool, mps_slab_to_off(pool, dict), mps_slab_to_off(pool, &dict->rbtree),
        mps_slab_to_off(pool, &dict->sentinel), mps_slab_to_off(pool, &dict->lru_queue));

    pool->data = mps_slab_to_off(pool, dict);
    mps_rbtree_init(pool, &dict->rbtree, &dict->sentinel,
        mps_luadict_rbtree_insert);
    assert(dict->rbtree.root == mps_slab_to_off(pool, &dict->sentinel));
    assert(dict->rbtree.sentinel == mps_slab_to_off(pool, &dict->sentinel));
    assert(ngx_rbt_is_black(&dict->sentinel));

    mps_queue_init(pool, &dict->lru_queue);
    assert(dict->lru_queue.prev == mps_slab_to_off(pool, &dict->lru_queue));
    assert(dict->lru_queue.next == mps_slab_to_off(pool, &dict->lru_queue));
}

void *
mps_luadict_open_or_create(const char *shm_name, size_t shm_size)
{
    return mps_slab_open_or_create(shm_name, shm_size, mps_luadict_on_init);
}
