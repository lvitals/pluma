#ifndef MARKDOWN_PARSER_H
#define MARKDOWN_PARSER_H

#include "markdown-ast.h"

void markdown_parser_trim (const gchar *str, const gchar **start, const gchar **end);
ASTNode *markdown_parser_parse (const gchar *text);

#endif /* MARKDOWN_PARSER_H */
