
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _MPS_RBTREE_H_INCLUDED_
#define _MPS_RBTREE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include "mps_slab.h"
#include "mps_log.h"

typedef ngx_uint_t mps_rbtree_key_t;
typedef ngx_int_t mps_rbtree_key_int_t;

typedef struct mps_rbtree_node_s mps_rbtree_node_t;

struct mps_rbtree_node_s {
    mps_rbtree_key_t key;
    mps_ptroff_t left;
    mps_ptroff_t right;
    mps_ptroff_t parent;
    u_char color;
    u_char data;
};

#define mps_rbtree_node(pool, off) ((mps_rbtree_node_t *)mps_ptr(pool, off))

typedef struct mps_rbtree_s mps_rbtree_t;

typedef void (*mps_rbtree_insert_pt)(mps_slab_pool_t *pool,
                                     mps_rbtree_node_t *root,
                                     mps_rbtree_node_t *node,
                                     mps_rbtree_node_t *sentinel);

typedef uintptr_t mps_rbtree_insert_type_id_t;

enum {
    MPS_RBTREE_INSERT_TYPE_ID_STANDARD,
    MPS_RBTREE_INSERT_TYPE_ID_TIMER,
    MPS_RBTREE_INSERT_TYPE_ID_LUADICT,
    MPS_RBTREE_INSERT_TYPE_ID_COUNT,
};

struct mps_rbtree_s {
    mps_ptroff_t root;
    mps_ptroff_t sentinel;
    mps_rbtree_insert_type_id_t insert;
};

#define mps_rbtree(pool, off) ((mps_rbtree_t *)mps_ptr(pool, off))

#define mps_rbtree_data(node, type, link)                                      \
    (type *)((u_char *)(node)-offsetof(type, link))

void mps_rbtree_init(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                     mps_rbtree_node_t *sentinel,
                     mps_rbtree_insert_type_id_t insert_type_id);
void mps_rbtree_insert(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                       mps_rbtree_node_t *node);
void mps_rbtree_delete(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                       mps_rbtree_node_t *node);
void mps_rbtree_insert_value(mps_slab_pool_t *pool, mps_rbtree_node_t *root,
                             mps_rbtree_node_t *node,
                             mps_rbtree_node_t *sentinel);
void mps_rbtree_insert_timer_value(mps_slab_pool_t *pool,
                                   mps_rbtree_node_t *root,
                                   mps_rbtree_node_t *node,
                                   mps_rbtree_node_t *sentinel);
void mps_shdict_rbtree_insert_value(mps_slab_pool_t *pool,
                                    mps_rbtree_node_t *temp,
                                    mps_rbtree_node_t *node,
                                    mps_rbtree_node_t *sentinel);
mps_rbtree_node_t *mps_rbtree_next(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                                   mps_rbtree_node_t *node);

#define ngx_rbt_red(node) ((node)->color = 1)
#define ngx_rbt_black(node) ((node)->color = 0)
#define ngx_rbt_is_red(node) ((node)->color)
#define ngx_rbt_is_black(node) (!ngx_rbt_is_red(node))
#define ngx_rbt_copy_color(n1, n2) (n1->color = n2->color)

/* a sentinel must be black */

#define mps_rbtree_sentinel_init(node) ngx_rbt_black(node)

static ngx_inline mps_rbtree_node_t *mps_rbtree_min(mps_slab_pool_t *pool,
                                                    mps_rbtree_node_t *node,
                                                    mps_ptroff_t sentinel)
{
    TSDebug(MPS_LOG_TAG, "mps_rbtree_min, node_off=%x, left=%x, sentinel=%x",
            mps_offset(pool, node), node->left, sentinel);
    while (node->left != sentinel) {
        node = mps_rbtree_node(pool, node->left);
        TSDebug(MPS_LOG_TAG, "mps_rbtree_min, updated node_off=%x",
                mps_offset(pool, node));
    }

    TSDebug(MPS_LOG_TAG, "mps_rbtree_min, return node_off=%x",
            mps_offset(pool, node));
    return node;
}

extern int verify_tree_enabled;
void verify_tree(mps_slab_pool_t *pool, mps_rbtree_t *tree);

#endif /* _MPS_RBTREE_H_INCLUDED_ */
