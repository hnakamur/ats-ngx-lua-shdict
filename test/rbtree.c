#include "unity.h"
#include "mps_rbtree.h"
#include "mps_log.h"

#define SHM_SIZE (4096 * 5)
#define SHM_NAME "/test_tree1"

extern void delete_shm_file(const char *name);

typedef struct {
    mps_rbtree_t rbtree;
    mps_rbtree_node_t sentinel;
} test_rbtree_t;

static mps_err_t rbtree_standard_on_init(mps_slab_pool_t *pool)
{
    test_rbtree_t *tree;
    mps_err_t err;

    mps_log_status("rbtree_standard_on_init start");
    tree = mps_slab_alloc(pool, sizeof(test_rbtree_t));
    if (!tree) {
        mps_log_error("rbtree_standard_on_init: mps_slab_alloc failed");
        return ENOMEM;
    }

    pool->data = mps_offset(pool, tree);
    err = mps_rbtree_init(pool, &tree->rbtree, &tree->sentinel,
                          MPS_RBTREE_INSERT_TYPE_ID_STANDARD);
    if (err != 0) {
        return err;
    }

    pool->log_nomem = 0;

    mps_log_status("rbtree_standard_on_init exit");
    return 0;
}

static mps_err_t rbtree_timer_on_init(mps_slab_pool_t *pool)
{
    test_rbtree_t *tree;
    mps_err_t err;

    mps_log_status("rbtree_timer_on_init start");
    tree = mps_slab_alloc(pool, sizeof(test_rbtree_t));
    if (!tree) {
        mps_log_error("rbtree_timer_on_init: mps_slab_alloc failed");
        return ENOMEM;
    }

    pool->data = mps_offset(pool, tree);
    err = mps_rbtree_init(pool, &tree->rbtree, &tree->sentinel,
                          MPS_RBTREE_INSERT_TYPE_ID_TIMER);
    if (err != 0) {
        return err;
    }

    pool->log_nomem = 0;

    mps_log_status("rbtree_timer_on_init exit");
    return 0;
}

static mps_err_t rbtree_bad_insert_type_on_init(mps_slab_pool_t *pool)
{
    test_rbtree_t *tree;
    mps_err_t err;

    mps_log_status("rbtree_bad_insert_type_on_init start");
    tree = mps_slab_alloc(pool, sizeof(test_rbtree_t));
    if (!tree) {
        mps_log_error("rbtree_bad_insert_type_on_init: mps_slab_alloc failed");
        return ENOMEM;
    }

    pool->data = mps_offset(pool, tree);
    mps_log_debug(
        "rbtree_test",
        "calling mps_rbtree_init with MPS_RBTREE_INSERT_TYPE_ID_COUNT");
    err = mps_rbtree_init(pool, &tree->rbtree, &tree->sentinel,
                          MPS_RBTREE_INSERT_TYPE_ID_COUNT);
    if (err != 0) {
        return err;
    }

    pool->log_nomem = 0;

    mps_log_status("rbtree_bad_insert_type_on_init exit");
    return 0;
}

static mps_rbtree_node_t *rbtree_insert(mps_slab_pool_t *pool,
                                        mps_rbtree_t *tree,
                                        mps_rbtree_key_t key, u_char data)
{
    mps_rbtree_node_t *node = mps_slab_calloc(pool, sizeof(mps_rbtree_node_t));
    mps_log_debug("rbtree_test", "node=%p (0x%lx)", node,
                  mps_offset(pool, node));
    if (node == NULL) {
        return NULL;
    }

    node->key = key;
    node->data = data;
    mps_rbtree_insert(pool, tree, node);
    return node;
}

static mps_rbtree_node_t *
rbtree_lookup(mps_slab_pool_t *pool, mps_rbtree_t *tree, mps_rbtree_key_t key)
{
    mps_rbtree_node_t *node, *sentinel;

    node = mps_rbtree_node(pool, tree->root);
    sentinel = mps_rbtree_node(pool, tree->sentinel);

    while (node != sentinel) {
        if (key < node->key) {
            node = mps_rbtree_node(pool, node->left);
            continue;
        }

        if (key > node->key) {
            node = mps_rbtree_node(pool, node->right);
            continue;
        }

        return node;
    }

    return NULL;
}

static void show_tree_node(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                           mps_rbtree_node_t *node);

void show_tree(mps_slab_pool_t *pool, mps_rbtree_t *tree)
{
    mps_rbtree_node_t *root;

    root = mps_rbtree_node(pool, tree->root);
    mps_log_status("show_tree start, root=%lx, sentinel=%lx", tree->root,
                   tree->sentinel);
    show_tree_node(pool, tree, root);
    mps_log_status("show_tree exit, root=%lx", tree->root);
}

static void show_tree_node(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                           mps_rbtree_node_t *node)
{
    if (node->left == mps_nulloff) {
        mps_log_error("show_tree_node, node->left is null, node=%x",
                      mps_offset(pool, node));
    }
    if (node->right == mps_nulloff) {
        mps_log_error("show_tree_node, node->right is null, node=%x",
                      mps_offset(pool, node));
    }
    if (node->left == mps_nulloff || node->right == mps_nulloff) {
        return;
    }

    mps_log_status("show_tree_node, node=%lx, key=%ld, left=%lx, right=%lx",
                   mps_offset(pool, node), node->key, node->left, node->right);
    if (node->left != tree->sentinel) {
        show_tree_node(pool, tree, mps_rbtree_node(pool, node->left));
    }
    if (node->right != tree->sentinel) {
        show_tree_node(pool, tree, mps_rbtree_node(pool, node->right));
    }
}

static int index_of_nodes_by_key(mps_rbtree_node_t **nodes, int count,
                                 mps_rbtree_key_t key)
{
    for (int i = 0; i < count; i++) {
        if (nodes[i]->key == key) {
            return i;
        }
    }
    return -1;
}

void test_rbtree_standard(void)
{
    mps_slab_pool_t *pool;

    pool =
        mps_slab_open_or_create(SHM_NAME, SHM_SIZE, MPS_SLAB_DEFAULT_MIN_SHIFT,
                                S_IRUSR | S_IWUSR, rbtree_standard_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    mps_rbtree_t *tree = mps_rbtree(pool, pool->data);

    const int node_count = 9;
    mps_rbtree_key_t keys[9] = {1, 3, 9, 7, 8, 4, 5, 2, 6};
    mps_rbtree_node_t *nodes[node_count];
    for (int i = 0; i < node_count; i++) {
        nodes[i] = rbtree_insert(pool, tree, keys[i], 'd');
        TEST_ASSERT_NOT_NULL(nodes[i]);
    }

    for (int i = 0; i < node_count; i++) {
        mps_rbtree_next(pool, tree, nodes[i]);
    }

    show_tree(pool, tree);
    //         5
    //        / \
    //       /   \
    //      /     \
    //     /       \
    //    3         8
    //   / \       / \
    //  /   \     /   \
    // 1     4   7     9
    //  \       /
    //   2     6

    mps_rbtree_key_t del_keys[9] = {5, 3, 4, 8, 6, 2, 1, 7, 9};
    for (int i = 0; i < node_count; i++) {
        int j = index_of_nodes_by_key(nodes, node_count, del_keys[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, j);
        TEST_ASSERT_LESS_THAN_INT(node_count, j);
        mps_log_status("deleting key=%ld, j=%d -----------------\n",
                       del_keys[i], j);
        mps_rbtree_delete(pool, tree, nodes[j]);
    }

    mps_slab_close(pool, SHM_SIZE);
    delete_shm_file("/dev/shm" SHM_NAME);
}

void test_rbtree_standard_random(void)
{
    mps_slab_pool_t *pool;

    pool =
        mps_slab_open_or_create(SHM_NAME, SHM_SIZE, MPS_SLAB_DEFAULT_MIN_SHIFT,
                                S_IRUSR | S_IWUSR, rbtree_standard_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    mps_rbtree_t *tree = mps_rbtree(pool, pool->data);

    uint32_t seed = 1234, h = seed, rnd, threshold = (1 << 31) + (1 << 18), key;
    const int iterations = 200;
    const int node_count_max = 100;
    int node_count = 0, j;
    mps_rbtree_node_t *nodes[node_count_max];
    ngx_memset(nodes, 0, sizeof(mps_rbtree_node_t *) * node_count_max);
    for (int i = 0; i < iterations;) {
        mps_log_debug("rbtree_test", "i=%d --------------------\n", i);
        if (node_count < node_count_max) {
            if (node_count > 0) {
                rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                h = rnd;
            }
            if (node_count == 0 || rnd >= threshold) {
                do {
                    key =
                        ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                    h = key;
                    mps_log_debug("rbtree_test", "key=%u", key);
                } while (rbtree_lookup(pool, tree, key) != NULL);
                mps_rbtree_node_t *node = rbtree_insert(pool, tree, key, 'd');
                if (node != NULL) {
                    nodes[node_count++] = node;
                    mps_log_debug("rbtree_test",
                                  "inserted node=%x, key=%x, node_count=%d",
                                  mps_offset(pool, node), key, node_count);
                    i++;
                    continue;
                }
            }
        }

        if (node_count == 0) {
            continue;
        }

        // This is not uniformly random, but it is OK for this test.
        rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
        h = rnd;
        j = rnd % node_count;

        mps_rbtree_delete(pool, tree, nodes[j]);
        mps_log_debug("rbtree_test", "deleted node=%x, key=%x, node_count=%d",
                      mps_offset(pool, nodes[j]), nodes[j]->key,
                      node_count - 1);
        nodes[j] = nodes[--node_count];
        i++;
    }

    mps_slab_close(pool, SHM_SIZE);
    delete_shm_file("/dev/shm" SHM_NAME);
}

void test_rbtree_timer_random(void)
{
    mps_slab_pool_t *pool;

    pool =
        mps_slab_open_or_create(SHM_NAME, SHM_SIZE, MPS_SLAB_DEFAULT_MIN_SHIFT,
                                S_IRUSR | S_IWUSR, rbtree_timer_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    mps_rbtree_t *tree = mps_rbtree(pool, pool->data);

    uint32_t seed = 1234, h = seed, rnd, threshold = (1 << 31) + (1 << 18), key;
    const int iterations = 200;
    const int node_count_max = 100;
    int node_count = 0, j;
    mps_rbtree_node_t *nodes[node_count_max];
    ngx_memset(nodes, 0, sizeof(mps_rbtree_node_t *) * node_count_max);
    for (int i = 0; i < iterations;) {
        mps_log_debug("rbtree_test", "i=%d --------------------\n", i);
        if (node_count < node_count_max) {
            if (node_count > 0) {
                rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                h = rnd;
            }
            if (node_count == 0 || rnd >= threshold) {
                do {
                    key =
                        ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                    h = key;
                    mps_log_debug("rbtree_test", "key=%u", key);
                } while (rbtree_lookup(pool, tree, key) != NULL);
                mps_rbtree_node_t *node = rbtree_insert(pool, tree, key, 'd');
                if (node != NULL) {
                    nodes[node_count++] = node;
                    mps_log_debug("rbtree_test",
                                  "inserted node=%x, key=%x, node_count=%d",
                                  mps_offset(pool, node), key, node_count);
                    i++;
                    continue;
                }
            }
        }

        if (node_count == 0) {
            continue;
        }

        // This is not uniformly random, but it is OK for this test.
        rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
        h = rnd;
        j = rnd % node_count;

        mps_rbtree_delete(pool, tree, nodes[j]);
        mps_log_debug("rbtree_test", "deleted node=%x, key=%x, node_count=%d",
                      mps_offset(pool, nodes[j]), nodes[j]->key,
                      node_count - 1);
        nodes[j] = nodes[--node_count];
        i++;
    }

    mps_slab_close(pool, SHM_SIZE);
    delete_shm_file("/dev/shm" SHM_NAME);
}

void test_rbtree_bad_insert_type(void)
{
    mps_slab_pool_t *pool;

    pool = mps_slab_open_or_create(
        SHM_NAME, SHM_SIZE, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        rbtree_bad_insert_type_on_init);
    TEST_ASSERT_NULL(pool);

    delete_shm_file("/dev/shm" SHM_NAME);
}
