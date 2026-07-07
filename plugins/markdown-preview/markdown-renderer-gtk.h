#ifndef MARKDOWN_RENDERER_GTK_H
#define MARKDOWN_RENDERER_GTK_H

#include <gtk/gtk.h>
#include "markdown-ast.h"

void markdown_renderer_gtk_render (GtkBox *container, ASTNode *root, gboolean is_dark, GFile *doc_location);

#endif /* MARKDOWN_RENDERER_GTK_H */
