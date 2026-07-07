#include "markdown-ast.h"

ASTNode *
ast_node_new (ASTNodeType type)
{
    ASTNode *node = g_new0 (ASTNode, 1);
    node->type = type;
    return node;
}

void
ast_node_free (ASTNode *node)
{
    if (node == NULL)
        return;

    g_free (node->text);
    g_free (node->extra);
    g_free (node->alignments);

    if (node->children != NULL) {
        g_list_free_full (node->children, (GDestroyNotify) ast_node_free);
    }

    g_free (node);
}

void
ast_node_append_child (ASTNode *parent, ASTNode *child)
{
    if (parent == NULL || child == NULL)
        return;
    parent->children = g_list_append (parent->children, child);
    child->parent = parent;
}
