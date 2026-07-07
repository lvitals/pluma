#ifndef MARKDOWN_AST_H
#define MARKDOWN_AST_H

#include <glib.h>

typedef enum {
    AST_NODE_DOCUMENT,
    AST_NODE_HEADING,
    AST_NODE_PARAGRAPH,
    AST_NODE_LIST,
    AST_NODE_LIST_ITEM,
    AST_NODE_CODE_BLOCK,
    AST_NODE_BLOCK_QUOTE,
    AST_NODE_TABLE,
    AST_NODE_TABLE_ROW,
    AST_NODE_TABLE_CELL,
    AST_NODE_IMAGE,
    AST_NODE_HORIZONTAL_RULE
} ASTNodeType;

typedef struct _ASTNode ASTNode;

struct _ASTNode {
    ASTNodeType type;
    ASTNode *parent;
    GList *children; /* List of ASTNode* */

    /* Node data */
    gchar *text;      /* Used for Paragraph, Heading, ListItem, BlockQuote, CodeBlock, Image alt */
    gchar *extra;     /* Used for CodeBlock lang, Image URL, ListItem ordered prefix */

    int level;        /* Used for Heading level, List indentation depth */
    gboolean bool_val;/* Used for ListItem is_ordered */

    /* Table & List details packed */
    int *alignments;  /* alignments or metadata */
    int n_columns;    /* columns count */
};

ASTNode *ast_node_new (ASTNodeType type);
void ast_node_free (ASTNode *node);
void ast_node_append_child (ASTNode *parent, ASTNode *child);

#endif /* MARKDOWN_AST_H */
