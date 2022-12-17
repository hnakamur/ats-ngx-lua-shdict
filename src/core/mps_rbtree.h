
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _MPS_RBTREE_H_INCLUDED_
#define _MPS_RBTREE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_uint_t  mps_rbtree_key_t;
typedef ngx_int_t   mps_rbtree_key_int_t;


typedef struct mps_rbtree_node_s  mps_rbtree_node_t;

struct mps_rbtree_node_s {
    mps_rbtree_key_t       key;
    mps_rbtree_node_t     *left;
    mps_rbtree_node_t     *right;
    mps_rbtree_node_t     *parent;
    u_char                 color;
    u_char                 data;
};


typedef struct mps_rbtree_s  mps_rbtree_t;

typedef void (*mps_rbtree_insert_pt) (mps_rbtree_node_t *root,
    mps_rbtree_node_t *node, mps_rbtree_node_t *sentinel);

struct mps_rbtree_s {
    mps_rbtree_node_t     *root;
    mps_rbtree_node_t     *sentinel;
    mps_rbtree_insert_pt   insert;
};


#define mps_rbtree_init(tree, s, i)                                           \
    mps_rbtree_sentinel_init(s);                                              \
    (tree)->root = s;                                                         \
    (tree)->sentinel = s;                                                     \
    (tree)->insert = i

#define mps_rbtree_data(node, type, link)                                     \
    (type *) ((u_char *) (node) - offsetof(type, link))


void mps_rbtree_insert(mps_rbtree_t *tree, mps_rbtree_node_t *node);
void mps_rbtree_delete(mps_rbtree_t *tree, mps_rbtree_node_t *node);
void mps_rbtree_insert_value(mps_rbtree_node_t *root, mps_rbtree_node_t *node,
    mps_rbtree_node_t *sentinel);
void mps_rbtree_insert_timer_value(mps_rbtree_node_t *root,
    mps_rbtree_node_t *node, mps_rbtree_node_t *sentinel);
mps_rbtree_node_t *mps_rbtree_next(mps_rbtree_t *tree,
    mps_rbtree_node_t *node);


#define ngx_rbt_red(node)               ((node)->color = 1)
#define ngx_rbt_black(node)             ((node)->color = 0)
#define ngx_rbt_is_red(node)            ((node)->color)
#define ngx_rbt_is_black(node)          (!ngx_rbt_is_red(node))
#define ngx_rbt_copy_color(n1, n2)      (n1->color = n2->color)


/* a sentinel must be black */

#define mps_rbtree_sentinel_init(node)  ngx_rbt_black(node)


static ngx_inline mps_rbtree_node_t *
mps_rbtree_min(mps_rbtree_node_t *node, mps_rbtree_node_t *sentinel)
{
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}


#endif /* _MPS_RBTREE_H_INCLUDED_ */
