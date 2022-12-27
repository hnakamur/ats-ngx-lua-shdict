
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "mps_rbtree.h"
#include "mps_log.h"

/*
 * The red-black tree code is based on the algorithm described in
 * the "Introduction to Algorithms" by Cormen, Leiserson and Rivest.
 */

static ngx_inline void mps_rbtree_left_rotate(mps_slab_pool_t *pool,
                                              mps_ptroff_t *root,
                                              mps_ptroff_t sentinel,
                                              mps_rbtree_node_t *node);
static ngx_inline void mps_rbtree_right_rotate(mps_slab_pool_t *pool,
                                               mps_ptroff_t *root,
                                               mps_ptroff_t sentinel,
                                               mps_rbtree_node_t *node);

static mps_rbtree_insert_pt insert_values[MPS_RBTREE_INSERT_TYPE_ID_COUNT] = {
    mps_rbtree_insert_value,
    mps_rbtree_insert_timer_value,
    mps_shdict_rbtree_insert_value,
};

void mps_rbtree_init(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                     mps_rbtree_node_t *sentinel,
                     mps_rbtree_insert_type_id_t insert_type_id)
{
    if (insert_type_id >= MPS_RBTREE_INSERT_TYPE_ID_COUNT) {
        TSEmergency("mps_rbtree_insert invalid tree insert type: %ld\n",
                    insert_type_id);
        return;
    }
    mps_rbtree_sentinel_init(sentinel);
    tree->root = mps_offset(pool, sentinel);
    tree->sentinel = mps_offset(pool, sentinel);
    tree->insert = insert_type_id;
    TSDebug(MPS_LOG_TAG,
            "mps_rbtree_init, tree=%p, root_off=0x%0x, sentinel=%p, insert=%d",
            tree, tree->root, sentinel, tree->insert);
}

void mps_rbtree_insert(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                       mps_rbtree_node_t *node)
{
    mps_ptroff_t *root, sentinel;
    mps_rbtree_node_t *temp, *parent, *grand_parent;
    mps_rbtree_insert_pt insert_value;

    /* a binary tree insert */

    root = &tree->root;
    sentinel = tree->sentinel;

    if (*root == sentinel) {
        node->parent = mps_nulloff;
        node->left = sentinel;
        TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node->left, __FILE__,
                __LINE__);
        node->right = sentinel;
        ngx_rbt_black(node);
        *root = mps_offset(pool, node);

        return;
    }

    if (tree->insert >= MPS_RBTREE_INSERT_TYPE_ID_COUNT) {
        TSError("mps_rbtree_insert invalid tree insert type: %ld\n",
                tree->insert);
        return;
    }
    insert_value = insert_values[tree->insert];
#if 0    
    verify_tree(pool, tree);
    TSDebug(MPS_LOG_TAG, "mps_rbtree_insert after verify before insert_value "
                         "----------------------");
#endif
    insert_value(pool, mps_rbtree_node(pool, *root), node,
                 mps_rbtree_node(pool, sentinel));
    TSDebug(MPS_LOG_TAG,
            "mps_rbtree_insert after insert_value, node=%x, left=%x, right=%x, "
            "parent=%x",
            mps_offset(pool, node), node->left, node->right, node->parent);
#if 0    
    verify_tree(pool, tree);
    TSDebug(MPS_LOG_TAG, "mps_rbtree_insert after verify after insert_value "
                         "----------------------");
#endif
    mps_rbtree_node_t *node2 = node;

    /* re-balance tree */

    while (node != mps_rbtree_node(pool, *root) &&
           ngx_rbt_is_red(mps_rbtree_node(pool, node->parent))) {
        parent = mps_rbtree_node(pool, node->parent);
        grand_parent = mps_rbtree_node(pool, parent->parent);
        if (node->parent == grand_parent->left) {
            temp = mps_rbtree_node(pool, grand_parent->right);

            if (ngx_rbt_is_red(temp)) {
                ngx_rbt_black(parent);
                ngx_rbt_black(temp);
                ngx_rbt_red(grand_parent);
                node = grand_parent;

            } else {
                if (node == mps_rbtree_node(pool, parent->right)) {
                    node = parent;
                    mps_rbtree_left_rotate(pool, root, sentinel, node);
                    parent = mps_rbtree_node(pool, node->parent);
                    grand_parent = mps_rbtree_node(pool, parent->parent);
                }

                ngx_rbt_black(parent);
                ngx_rbt_red(grand_parent);
                mps_rbtree_right_rotate(pool, root, sentinel, grand_parent);
            }

        } else {
            temp = mps_rbtree_node(pool, grand_parent->left);

            if (ngx_rbt_is_red(temp)) {
                ngx_rbt_black(parent);
                ngx_rbt_black(temp);
                ngx_rbt_red(grand_parent);
                node = grand_parent;

            } else {
                if (node == mps_rbtree_node(pool, parent->left)) {
                    node = parent;
                    mps_rbtree_right_rotate(pool, root, sentinel, node);
                    parent = mps_rbtree_node(pool, node->parent);
                    grand_parent = mps_rbtree_node(pool, parent->parent);
                }

                ngx_rbt_black(parent);
                ngx_rbt_red(grand_parent);
                mps_rbtree_left_rotate(pool, root, sentinel, grand_parent);
            }
        }
    }

    ngx_rbt_black(mps_rbtree_node(pool, *root));
#if 0    
    TSDebug(MPS_LOG_TAG, "mps_rbtree_insert verify before exiting -----------");
    verify_tree(pool, tree);
#endif
    TSDebug(MPS_LOG_TAG,
            "mps_rbtree_insert exiting, node2=%x, left=%x, right=%x, parent=%x",
            mps_offset(pool, node2), node2->left, node2->right, node2->parent);
}

void mps_rbtree_insert_value(mps_slab_pool_t *pool, mps_rbtree_node_t *temp,
                             mps_rbtree_node_t *node,
                             mps_rbtree_node_t *sentinel)
{
    mps_ptroff_t *p, s;

    s = mps_offset(pool, sentinel);

    for (;;) {

        p = (node->key < temp->key) ? &temp->left : &temp->right;

        if (*p == s) {
            break;
        }

        temp = mps_rbtree_node(pool, *p);
    }

    *p = mps_offset(pool, node);
    node->parent = mps_offset(pool, temp);
    node->left = s;
    TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node->left, __FILE__,
            __LINE__);
    node->right = s;
    ngx_rbt_red(node);
}

void mps_rbtree_insert_timer_value(mps_slab_pool_t *pool,
                                   mps_rbtree_node_t *temp,
                                   mps_rbtree_node_t *node,
                                   mps_rbtree_node_t *sentinel)
{
    mps_ptroff_t *p, s;

    s = mps_offset(pool, sentinel);

    for (;;) {

        /*
         * Timer values
         * 1) are spread in small range, usually several minutes,
         * 2) and overflow each 49 days, if milliseconds are stored in 32 bits.
         * The comparison takes into account that overflow.
         */

        /*  node->key < temp->key */

        p = ((mps_rbtree_key_int_t)(node->key - temp->key) < 0) ? &temp->left
                                                                : &temp->right;

        if (*p == s) {
            break;
        }

        temp = mps_rbtree_node(pool, *p);
    }

    *p = mps_offset(pool, node);
    node->parent = mps_offset(pool, temp);
    node->left = s;
    TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node->left, __FILE__,
            __LINE__);
    node->right = s;
    ngx_rbt_red(node);
}

static void verify_tree_node(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                             mps_rbtree_node_t *node)
{
    if (node->left == mps_nulloff) {
        TSError("verify_tree_node, node->left is null, node=%x",
                mps_offset(pool, node));
    }
    if (node->right == mps_nulloff) {
        TSError("verify_tree_node, node->right is null, node=%x",
                mps_offset(pool, node));
    }
    if (node->left == mps_nulloff || node->right == mps_nulloff) {
        return;
    }

    if (node->left != tree->sentinel) {
        TSStatus("calling verify_tree_node, node=%x, left=%x",
                 mps_offset(pool, node), node->left);
        verify_tree_node(pool, tree, mps_rbtree_node(pool, node->left));
    }
    if (node->right != tree->sentinel) {
        TSStatus("calling verify_tree_node, node=%x, right=%x",
                 mps_offset(pool, node), node->right);
        verify_tree_node(pool, tree, mps_rbtree_node(pool, node->right));
    }
}

int verify_tree_enabled = 0;

void verify_tree(mps_slab_pool_t *pool, mps_rbtree_t *tree)
{
    mps_rbtree_node_t *root;

    if (!verify_tree_enabled) {
        return;
    }

    root = mps_rbtree_node(pool, tree->root);
    TSStatus("verify_tree start, root=%x", tree->root);
    verify_tree_node(pool, tree, root);
    TSStatus("verify_tree exit, root=%x", tree->root);
}

void mps_rbtree_delete(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                       mps_rbtree_node_t *node)
{
    ngx_uint_t red;
    mps_ptroff_t *root, sentinel;
    mps_rbtree_node_t *subst, *temp, *w, *subst_parent, *node_parent,
        *subst_left, *subst_right, *temp_parent, *w_left, *w_right;

    /* a binary tree delete */

    root = &tree->root;
    sentinel = tree->sentinel;
    TSDebug(MPS_LOG_TAG,
            "mps_rbtree_delete tree=%p, root_off=0x%x, root=%p, "
            "sentinel_off=0x%x node=%p",
            tree, *root, mps_rbtree_node(pool, *root), sentinel, node);

    TSDebug(MPS_LOG_TAG,
            "mps_rbtree_delete node->left=%x, node->right=%x, sentinel=%x",
            node->left, node->right, sentinel);
    verify_tree(pool, tree);
    TSDebug(MPS_LOG_TAG, "mps_rbtree_delete after verify_tree at start");

    if (node->left == sentinel) {
        temp = mps_rbtree_node(pool, node->right);
        subst = node;

    } else if (node->right == sentinel) {
        temp = mps_rbtree_node(pool, node->left);
        subst = node;

    } else {
        subst =
            mps_rbtree_min(pool, mps_rbtree_node(pool, node->right), sentinel);
        temp = mps_rbtree_node(pool, subst->right);
    }

    TSDebug(MPS_LOG_TAG, "mps_rbtree_delete tree=%p, subst=%p", tree, subst);

    if (subst == mps_rbtree_node(pool, *root)) {
        *root = mps_offset(pool, temp);
        ngx_rbt_black(temp);

        /* DEBUG stuff */
        node->left = mps_nulloff;
        TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node->left, __FILE__,
                __LINE__);
        node->right = mps_nulloff;
        node->parent = mps_nulloff;
        node->key = 0;

        verify_tree(pool, tree);
        return;
    }

    TSDebug(MPS_LOG_TAG, "mps_rbtree_delete tree=%p #2", tree);

    red = ngx_rbt_is_red(subst);

    subst_parent = mps_rbtree_node(pool, subst->parent);
    if (subst == mps_rbtree_node(pool, subst_parent->left)) {
        subst_parent->left = mps_offset(pool, temp);
        TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", subst_parent->left,
                __FILE__, __LINE__);

    } else {
        subst_parent->right = mps_offset(pool, temp);
    }

    if (subst == node) {
        temp->parent = subst->parent;

    } else {

        if (subst->parent == mps_offset(pool, node)) {
            temp->parent = mps_offset(pool, subst);

        } else {
            temp->parent = subst->parent;
        }

        subst->left = node->left;
        TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", subst->left, __FILE__,
                __LINE__);
        subst->right = node->right;
        subst->parent = node->parent;
        ngx_rbt_copy_color(subst, node);

        if (node == mps_rbtree_node(pool, *root)) {
            *root = mps_offset(pool, subst);

        } else {
            node_parent = mps_rbtree_node(pool, node->parent);

            if (node == mps_rbtree_node(pool, node_parent->left)) {
                node_parent->left = mps_offset(pool, subst);
                TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d",
                        node_parent->left, __FILE__, __LINE__);

            } else {
                node_parent->right = mps_offset(pool, subst);
            }
        }

        if (subst->left != sentinel) {
            subst_left = mps_rbtree_node(pool, subst->left);
            subst_left->parent = mps_offset(pool, subst);
        }

        if (subst->right != sentinel) {
            subst_right = mps_rbtree_node(pool, subst->right);
            subst_right->parent = mps_offset(pool, subst);
        }
    }

    /* DEBUG stuff */
    node->left = mps_nulloff;
    TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node->left, __FILE__,
            __LINE__);
    node->right = mps_nulloff;
    node->parent = mps_nulloff;
    node->key = 0;

    TSDebug(MPS_LOG_TAG, "mps_rbtree_delete tree=%p after debug#1", tree);

    if (red) {
        verify_tree(pool, tree);
        return;
    }

    /* a delete fixup */
    TSDebug(MPS_LOG_TAG, "mps_rbtree_delete tree=%p, deleting fixup", tree);

    while (temp != mps_rbtree_node(pool, *root) && ngx_rbt_is_black(temp)) {
        TSDebug(MPS_LOG_TAG, "mps_rbtree_delete tree=%p, while loop temp=%p",
                tree, temp);

        temp_parent = mps_rbtree_node(pool, temp->parent);
        if (temp == mps_rbtree_node(pool, temp_parent->left)) {
            w = mps_rbtree_node(pool, temp_parent->right);

            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp_parent);
                mps_rbtree_left_rotate(pool, root, sentinel, temp_parent);
                temp_parent = mps_rbtree_node(pool, temp->parent);
                w = mps_rbtree_node(pool, temp_parent->right);
            }

            w_left = mps_rbtree_node(pool, w->left);
            w_right = mps_rbtree_node(pool, w->right);

            if (ngx_rbt_is_black(w_left) && ngx_rbt_is_black(w_right)) {
                ngx_rbt_red(w);
                temp = temp_parent;

            } else {
                if (ngx_rbt_is_black(w_right)) {
                    ngx_rbt_black(w_left);
                    ngx_rbt_red(w);
                    mps_rbtree_right_rotate(pool, root, sentinel, w);
                    temp_parent = mps_rbtree_node(pool, temp->parent);
                    w = mps_rbtree_node(pool, temp_parent->right);
                    w_right = mps_rbtree_node(pool, w->right);
                }

                ngx_rbt_copy_color(w, temp_parent);
                ngx_rbt_black(temp_parent);
                ngx_rbt_black(w_right);
                mps_rbtree_left_rotate(pool, root, sentinel, temp_parent);
                temp = mps_rbtree_node(pool, *root);
            }

        } else {
            w = mps_rbtree_node(pool, temp_parent->left);

            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp_parent);
                mps_rbtree_right_rotate(pool, root, sentinel, temp_parent);
                temp_parent = mps_rbtree_node(pool, temp->parent);
                w = mps_rbtree_node(pool, temp_parent->left);
            }

            w_left = mps_rbtree_node(pool, w->left);
            w_right = mps_rbtree_node(pool, w->right);

            if (ngx_rbt_is_black(w_left) && ngx_rbt_is_black(w_right)) {
                ngx_rbt_red(w);
                temp = temp_parent;

            } else {
                if (ngx_rbt_is_black(w_left)) {
                    ngx_rbt_black(w_right);
                    ngx_rbt_red(w);
                    mps_rbtree_left_rotate(pool, root, sentinel, w);
                    temp_parent = mps_rbtree_node(pool, temp->parent);
                    w = mps_rbtree_node(pool, temp_parent->left);
                    w_left = mps_rbtree_node(pool, w->left);
                }

                ngx_rbt_copy_color(w, temp_parent);
                ngx_rbt_black(temp_parent);
                ngx_rbt_black(w_left);
                mps_rbtree_right_rotate(pool, root, sentinel, temp_parent);
                temp = mps_rbtree_node(pool, *root);
            }
        }
    }

    ngx_rbt_black(temp);
    verify_tree(pool, tree);
}

static ngx_inline void mps_rbtree_left_rotate(mps_slab_pool_t *pool,
                                              mps_ptroff_t *root,
                                              mps_ptroff_t sentinel,
                                              mps_rbtree_node_t *node)
{
    mps_rbtree_node_t *temp, *temp_left, *node_parent;

    temp = mps_rbtree_node(pool, node->right);
    node->right = temp->left;

    if (temp->left != sentinel) {
        temp_left = mps_rbtree_node(pool, temp->left);
        temp_left->parent = mps_offset(pool, node);
    }

    temp->parent = node->parent;

    if (node == mps_rbtree_node(pool, *root)) {
        *root = mps_offset(pool, temp);

    } else {
        node_parent = mps_rbtree_node(pool, node->parent);

        if (node == mps_rbtree_node(pool, node_parent->left)) {
            node_parent->left = mps_offset(pool, temp);
            TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node_parent->left,
                    __FILE__, __LINE__);

        } else {
            node_parent->right = mps_offset(pool, temp);
        }
    }

    temp->left = mps_offset(pool, node);
    TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", temp->left, __FILE__,
            __LINE__);
    node->parent = mps_offset(pool, temp);
}

static ngx_inline void mps_rbtree_right_rotate(mps_slab_pool_t *pool,
                                               mps_ptroff_t *root,
                                               mps_ptroff_t sentinel,
                                               mps_rbtree_node_t *node)
{
    mps_rbtree_node_t *temp, *temp_right, *node_parent;

    temp = mps_rbtree_node(pool, node->left);
    node->left = temp->right;
    TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node->left, __FILE__,
            __LINE__);

    if (temp->right != sentinel) {
        temp_right = mps_rbtree_node(pool, temp->right);
        temp_right->parent = mps_offset(pool, node);
    }

    temp->parent = node->parent;

    if (node == mps_rbtree_node(pool, *root)) {
        *root = mps_offset(pool, temp);

    } else {
        node_parent = mps_rbtree_node(pool, node->parent);

        if (node == mps_rbtree_node(pool, node_parent->right)) {
            node_parent->right = mps_offset(pool, temp);

        } else {
            node_parent->left = mps_offset(pool, temp);
            TSDebug(MPS_LOG_TAG, "updated left=%x, at %s:%d", node_parent->left,
                    __FILE__, __LINE__);
        }
    }

    temp->right = mps_offset(pool, node);
    node->parent = mps_offset(pool, temp);
}

mps_rbtree_node_t *mps_rbtree_next(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                                   mps_rbtree_node_t *node)
{
    mps_rbtree_node_t *root, *parent;
    mps_ptroff_t sentinel;

    sentinel = tree->sentinel;

    if (node->right != sentinel) {
        return mps_rbtree_min(pool, mps_rbtree_node(pool, node->right),
                              sentinel);
    }

    root = mps_rbtree_node(pool, tree->root);

    for (;;) {
        parent = mps_rbtree_node(pool, node->parent);

        if (node == root) {
            return NULL;
        }

        if (node == mps_rbtree_node(pool, parent->left)) {
            return parent;
        }

        node = parent;
    }
}
