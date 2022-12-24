
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
    mps_ptroff_t *root, mps_ptroff_t sentinel, mps_rbtree_node_t *node);
static ngx_inline void mps_rbtree_right_rotate(mps_slab_pool_t *pool,
    mps_ptroff_t *root, mps_ptroff_t sentinel, mps_rbtree_node_t *node);


static mps_rbtree_insert_pt insert_values[MPS_RBTREE_INSERT_TYPE_ID_COUNT] = {
    mps_rbtree_insert_value,
    mps_rbtree_insert_timer_value,
    mps_shdict_rbtree_insert_value,
};


void
mps_rbtree_insert(mps_slab_pool_t *pool, mps_rbtree_t *tree,
    mps_rbtree_node_t *node)
{
    mps_ptroff_t        *root, sentinel;
    mps_rbtree_node_t   *temp, *parent, *grand_parent;
    mps_rbtree_insert_pt insert_value;

    /* a binary tree insert */

    root = &tree->root;
    sentinel = tree->sentinel;

    if (*root == sentinel) {
        node->parent = mps_nulloff;
        node->left = sentinel;
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
    insert_value(pool, mps_rbtree_node(pool, *root), node,
                 mps_rbtree_node(pool, sentinel));

    /* re-balance tree */

    while (node != mps_rbtree_node(pool, *root)
           && ngx_rbt_is_red(parent = mps_rbtree_node(pool, node->parent)))
    {
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
                }

                ngx_rbt_black(parent);
                ngx_rbt_red(grand_parent);
                mps_rbtree_left_rotate(pool, root, sentinel, grand_parent);
            }
        }
    }

    ngx_rbt_black(mps_rbtree_node(pool, *root));
}


void
mps_rbtree_insert_value(mps_slab_pool_t *pool, mps_rbtree_node_t *temp,
    mps_rbtree_node_t *node, mps_rbtree_node_t *sentinel)
{
    mps_ptroff_t   *p, s;

    s = mps_offset(pool, sentinel);

    for ( ;; ) {

        p = (node->key < temp->key) ? &temp->left : &temp->right;

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


void
mps_rbtree_insert_timer_value(mps_slab_pool_t *pool, mps_rbtree_node_t *temp,
    mps_rbtree_node_t *node, mps_rbtree_node_t *sentinel)
{
    mps_ptroff_t   *p, s;

    s = mps_offset(pool, sentinel);

    for ( ;; ) {

        /*
         * Timer values
         * 1) are spread in small range, usually several minutes,
         * 2) and overflow each 49 days, if milliseconds are stored in 32 bits.
         * The comparison takes into account that overflow.
         */

        /*  node->key < temp->key */

        p = ((mps_rbtree_key_int_t) (node->key - temp->key) < 0)
            ? &temp->left : &temp->right;

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


void
mps_rbtree_delete(mps_slab_pool_t *pool, mps_rbtree_t *tree,
    mps_rbtree_node_t *node)
{
    ngx_uint_t           red;
    mps_ptroff_t        *root, sentinel;
    mps_rbtree_node_t   *subst, *temp, *w, *subst_parent, *node_parent,
                        *subst_left, *subst_right, *temp_parent, *w_left,
                        *w_right;

    /* a binary tree delete */

    root = &tree->root;
    sentinel = tree->sentinel;

    if (node->left == sentinel) {
        temp = mps_rbtree_node(pool, node->right);
        subst = node;

    } else if (node->right == sentinel) {
        temp = mps_rbtree_node(pool, node->left);
        subst = node;

    } else {
        subst = mps_rbtree_min(pool, mps_rbtree_node(pool, node->right),
                               sentinel);
        temp = mps_rbtree_node(pool, subst->right);
    }

    if (subst == mps_rbtree_node(pool, *root)) {
        *root = mps_offset(pool, temp);
        ngx_rbt_black(temp);

        /* DEBUG stuff */
        node->left = mps_nulloff;
        node->right = mps_nulloff;
        node->parent = mps_nulloff;
        node->key = 0;

        return;
    }

    red = ngx_rbt_is_red(subst);

    subst_parent = mps_rbtree_node(pool, subst->parent);
    if (subst == mps_rbtree_node(pool, subst_parent->left)) {
        subst_parent->left = mps_offset(pool, temp);

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
        subst->right = node->right;
        subst->parent = node->parent;
        ngx_rbt_copy_color(subst, node);

        if (node == mps_rbtree_node(pool, *root)) {
            *root = mps_offset(pool, subst);

        } else {
            node_parent = mps_rbtree_node(pool, node->parent);

            if (node == mps_rbtree_node(pool, node_parent->left)) {
                node_parent->left = mps_offset(pool, subst);

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
    node->right = mps_nulloff;
    node->parent = mps_nulloff;
    node->key = 0;

    if (red) {
        return;
    }

    /* a delete fixup */

    while (temp != mps_rbtree_node(pool, *root) && ngx_rbt_is_black(temp)) {

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
                }

                ngx_rbt_copy_color(w, temp_parent);
                ngx_rbt_black(temp_parent);
                w_right = mps_rbtree_node(pool, w->right);
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
                }

                ngx_rbt_copy_color(w, temp_parent);
                ngx_rbt_black(temp_parent);
                w_left = mps_rbtree_node(pool, w->left);
                ngx_rbt_black(w_left);
                mps_rbtree_right_rotate(pool, root, sentinel, temp_parent);
                temp = mps_rbtree_node(pool, *root);
            }
        }
    }

    ngx_rbt_black(temp);
}


static ngx_inline void
mps_rbtree_left_rotate(mps_slab_pool_t *pool, mps_ptroff_t *root,
    mps_ptroff_t sentinel, mps_rbtree_node_t *node)
{
    mps_rbtree_node_t  *temp, *temp_left, *node_parent;

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

        } else {
            node_parent->right = mps_offset(pool, temp);
        }
    }

    temp->left = mps_offset(pool, node);
    node->parent = mps_offset(pool, temp);
}


static ngx_inline void
mps_rbtree_right_rotate(mps_slab_pool_t *pool, mps_ptroff_t *root,
    mps_ptroff_t sentinel, mps_rbtree_node_t *node)
{
    mps_rbtree_node_t  *temp, *temp_right, *node_parent;

    temp = mps_rbtree_node(pool, node->left);
    node->left = temp->right;

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
        }
    }

    temp->right = mps_offset(pool, node);
    node->parent = mps_offset(pool, temp);
}


mps_rbtree_node_t *
mps_rbtree_next(mps_slab_pool_t *pool, mps_rbtree_t *tree,
    mps_rbtree_node_t *node)
{
    mps_rbtree_node_t  *root, *parent;
    mps_ptroff_t        sentinel;

    sentinel = tree->sentinel;

    if (node->right != sentinel) {
        return mps_rbtree_min(pool, mps_rbtree_node(pool, node->right),
                              sentinel);
    }

    root = mps_rbtree_node(pool, tree->root);

    for ( ;; ) {
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
