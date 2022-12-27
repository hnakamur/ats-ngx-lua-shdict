#include "mps_rbtree.h"

int verify_tree_enabled = 0;

static void verify_tree_node(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                             mps_rbtree_node_t *node);

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
