#include "markdown-renderer-gtk.h"
#include "markdown-inline.h"
#include <gtksourceview/gtksource.h>
#include <string.h>

static GdkPixbuf *
load_renderer_image (GFile *doc_location, const gchar *url)
{
    if (doc_location == NULL || url == NULL)
        return NULL;

    GFile *parent = g_file_get_parent (doc_location);
    if (parent == NULL)
        return NULL;

    GFile *img_file = g_file_get_child (parent, url);
    g_object_unref (parent);

    gchar *path = g_file_get_path (img_file);
    g_object_unref (img_file);

    if (path == NULL)
        return NULL;

    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (path, &error);
    g_free (path);

    if (error != NULL) {
        g_error_free (error);
        return NULL;
    }

    int w = gdk_pixbuf_get_width (pixbuf);
    int h = gdk_pixbuf_get_height (pixbuf);
    if (w > 300) {
        int new_w = 300;
        int new_h = (h * 300) / w;
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pixbuf, new_w, new_h, GDK_INTERP_BILINEAR);
        g_object_unref (pixbuf);
        pixbuf = scaled;
    }

    return pixbuf;
}

static void
render_node_to_gtk (GtkBox *container, ASTNode *node, gboolean is_dark, GFile *doc_location)
{
    if (node == NULL)
        return;

    switch (node->type) {
        case AST_NODE_HEADING: {
            GtkWidget *label = gtk_label_new (NULL);
            GList *inline_list = markdown_inline_parse (node->text);
            gchar *pango_text = markdown_inline_to_pango_markup (inline_list, is_dark);
            markdown_inline_free_list (inline_list);

            GtkStyleContext *lbl_ctx = gtk_widget_get_style_context (label);
            gtk_style_context_add_class (lbl_ctx, "markdown-heading");
            
            gchar *class_name = g_strdup_printf ("markdown-heading-%d", node->level);
            gtk_style_context_add_class (lbl_ctx, class_name);
            g_free (class_name);

            gtk_label_set_markup (GTK_LABEL (label), pango_text);
            gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
            gtk_label_set_xalign (GTK_LABEL (label), 0.0);
            gtk_widget_set_halign (label, GTK_ALIGN_START);

            gtk_box_pack_start (container, label, FALSE, FALSE, 0);
            g_free (pango_text);
            break;
        }
        case AST_NODE_PARAGRAPH: {
            if (node->text == NULL) {
                GtkWidget *spacer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
                gtk_widget_set_size_request (spacer, -1, 6);
                gtk_box_pack_start (container, spacer, FALSE, FALSE, 0);
            } else {
                GtkWidget *label = gtk_label_new (NULL);
                GList *inline_list = markdown_inline_parse (node->text);
                gchar *pango_text = markdown_inline_to_pango_markup (inline_list, is_dark);
                markdown_inline_free_list (inline_list);

                gtk_label_set_markup (GTK_LABEL (label), pango_text);
                gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
                gtk_label_set_xalign (GTK_LABEL (label), 0.0);
                gtk_widget_set_halign (label, GTK_ALIGN_START);

                gtk_widget_set_margin_bottom (label, 8);

                gtk_box_pack_start (container, label, FALSE, FALSE, 0);
                g_free (pango_text);
            }
            break;
        }
        case AST_NODE_LIST_ITEM: {
            GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_set_margin_start (hbox, node->level * 8 + 8);
            gtk_widget_set_margin_bottom (hbox, 4);

            gboolean is_checkbox = node->alignments != NULL ? node->alignments[0] : FALSE;
            gboolean is_checked = node->alignments != NULL ? node->alignments[1] : FALSE;

            if (is_checkbox) {
                GtkWidget *chk = gtk_check_button_new ();
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chk), is_checked);
                gtk_widget_set_sensitive (chk, FALSE);
                gtk_box_pack_start (GTK_BOX (hbox), chk, FALSE, FALSE, 0);
            } else {
                GtkWidget *bullet_label = gtk_label_new (NULL);
                gchar *bullet_text;
                if (node->bool_val) {
                    bullet_text = g_strdup_printf ("%s. ", node->extra);
                } else {
                    int depth = node->level / 2;
                    if (depth == 0) bullet_text = g_strdup ("• ");
                    else if (depth == 1) bullet_text = g_strdup ("◦ ");
                    else bullet_text = g_strdup ("▪ ");
                }
                gtk_label_set_markup (GTK_LABEL (bullet_label), bullet_text);
                gtk_box_pack_start (GTK_BOX (hbox), bullet_label, FALSE, FALSE, 0);
                g_free (bullet_text);
            }

            GtkWidget *content_label = gtk_label_new (NULL);
            GList *inline_list = markdown_inline_parse (node->text);
            gchar *pango_text = markdown_inline_to_pango_markup (inline_list, is_dark);
            markdown_inline_free_list (inline_list);

            gtk_label_set_markup (GTK_LABEL (content_label), pango_text);
            gtk_label_set_line_wrap (GTK_LABEL (content_label), TRUE);
            gtk_label_set_xalign (GTK_LABEL (content_label), 0.0);
            gtk_widget_set_halign (content_label, GTK_ALIGN_START);

            gtk_box_pack_start (GTK_BOX (hbox), content_label, TRUE, TRUE, 0);
            gtk_box_pack_start (container, hbox, FALSE, FALSE, 0);
            g_free (pango_text);
            break;
        }
        case AST_NODE_CODE_BLOCK: {
            GtkWidget *wrapper = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
            GtkStyleContext *wrap_ctx = gtk_widget_get_style_context (wrapper);
            gtk_style_context_add_class (wrap_ctx, "markdown-code-wrapper");

            if (node->extra != NULL && *(node->extra) != '\0') {
                gchar *lang_cap = g_strdup (node->extra);
                lang_cap[0] = g_ascii_toupper (lang_cap[0]);
                GtkWidget *lbl = gtk_label_new (lang_cap);
                GtkStyleContext *lbl_ctx = gtk_widget_get_style_context (lbl);
                gtk_style_context_add_class (lbl_ctx, "markdown-code-label");
                gtk_widget_set_halign (lbl, GTK_ALIGN_START);
                gtk_box_pack_start (GTK_BOX (wrapper), lbl, FALSE, FALSE, 0);
                g_free (lang_cap);
            }

            GtkWidget *scr = gtk_scrolled_window_new (NULL, NULL);
            gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scr), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
            gtk_widget_set_hexpand (scr, TRUE);
            GtkStyleContext *scr_ctx = gtk_widget_get_style_context (scr);
            gtk_style_context_add_class (scr_ctx, "markdown-code-block");
            gtk_style_context_add_class (scr_ctx, is_dark ? "markdown-code-dark" : "markdown-code-light");

            GtkWidget *text_view = gtk_source_view_new ();
            gtk_widget_set_hexpand (text_view, TRUE);
            GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
            gtk_text_buffer_set_text (buf, node->text, -1);

            if (node->extra != NULL && *(node->extra) != '\0') {
                GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
                gchar *lang_lower = g_ascii_strdown (node->extra, -1);
                GtkSourceLanguage *language = gtk_source_language_manager_get_language (lm, lang_lower);
                g_free (lang_lower);
                if (language != NULL) {
                    gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (buf), language);
                }
            }

            gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
            gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text_view), FALSE);

            gtk_container_add (GTK_CONTAINER (scr), text_view);
            gtk_box_pack_start (GTK_BOX (wrapper), scr, FALSE, FALSE, 0);
            
            gtk_box_pack_start (container, wrapper, FALSE, FALSE, 0);
            break;
        }
        case AST_NODE_BLOCK_QUOTE: {
            GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
            GtkStyleContext *ctx = gtk_widget_get_style_context (box);
            gtk_style_context_add_class (ctx, "markdown-blockquote");
            if (node->level > 1) {
                gtk_style_context_add_class (ctx, "markdown-blockquote-nested");
            }

            GList *child = node->children;
            while (child != NULL) {
                render_node_to_gtk (GTK_BOX (box), (ASTNode *) child->data, is_dark, doc_location);
                child = child->next;
            }

            gtk_box_pack_start (container, box, FALSE, FALSE, 0);
            break;
        }
        case AST_NODE_HORIZONTAL_RULE: {
            GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
            GtkStyleContext *sep_ctx = gtk_widget_get_style_context (sep);
            gtk_style_context_add_class (sep_ctx, is_dark ? "markdown-sep-dark" : "markdown-sep-light");

            gtk_widget_set_margin_top (sep, 8);
            gtk_widget_set_margin_bottom (sep, 8);
            gtk_box_pack_start (container, sep, FALSE, FALSE, 0);
            break;
        }
        case AST_NODE_IMAGE: {
            GdkPixbuf *pixbuf = load_renderer_image (doc_location, node->extra);
            if (pixbuf != NULL) {
                GtkWidget *img = gtk_image_new_from_pixbuf (pixbuf);
                gtk_widget_set_halign (img, GTK_ALIGN_CENTER);
                gtk_widget_set_margin_bottom (img, 8);
                gtk_box_pack_start (container, img, FALSE, FALSE, 0);
                g_object_unref (pixbuf);
            } else {
                GtkWidget *lbl = gtk_label_new (NULL);
                gchar *markup = g_strdup_printf ("<i>📷 [%s]</i>", node->text);
                gtk_label_set_markup (GTK_LABEL (lbl), markup);
                gtk_widget_set_halign (lbl, GTK_ALIGN_CENTER);
                gtk_widget_set_margin_bottom (lbl, 8);
                gtk_box_pack_start (container, lbl, FALSE, FALSE, 0);
                g_free (markup);
            }
            break;
        }
        case AST_NODE_TABLE: {
            GtkWidget *grid = gtk_grid_new ();
            gtk_grid_set_row_spacing (GTK_GRID (grid), 0);
            gtk_grid_set_column_spacing (GTK_GRID (grid), 0);

            GtkStyleContext *grid_ctx = gtk_widget_get_style_context (grid);
            gtk_style_context_add_class (grid_ctx, "markdown-table");

            GList *row_item = node->children;
            int grid_row = 0;
            while (row_item != NULL) {
                ASTNode *row = (ASTNode *) row_item->data;
                gboolean is_header = (grid_row == 0);

                GList *cell_item = row->children;
                int c_idx = 0;
                while (cell_item != NULL && c_idx < node->n_columns) {
                    ASTNode *cell = (ASTNode *) cell_item->data;

                    GtkWidget *cell_label = gtk_label_new (NULL);
                    GList *inline_list = markdown_inline_parse (cell->text);
                    gchar *pango_text = markdown_inline_to_pango_markup (inline_list, is_dark);
                    markdown_inline_free_list (inline_list);

                    gchar *markup;
                    if (is_header) {
                        markup = g_strdup_printf ("<b>%s</b>", pango_text);
                    } else {
                        markup = g_strdup (pango_text);
                    }
                    gtk_label_set_markup (GTK_LABEL (cell_label), markup);
                    g_free (markup);
                    g_free (pango_text);

                    int alignment = node->alignments != NULL ? node->alignments[c_idx] : 0;
                    if (alignment == 0) {
                        gtk_widget_set_halign (cell_label, GTK_ALIGN_START);
                        gtk_label_set_xalign (GTK_LABEL (cell_label), 0.0);
                    } else if (alignment == 2) {
                        gtk_widget_set_halign (cell_label, GTK_ALIGN_END);
                        gtk_label_set_xalign (GTK_LABEL (cell_label), 1.0);
                    } else {
                        gtk_widget_set_halign (cell_label, GTK_ALIGN_CENTER);
                        gtk_label_set_xalign (GTK_LABEL (cell_label), 0.5);
                    }

                    GtkWidget *cell_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
                    GtkStyleContext *cell_ctx = gtk_widget_get_style_context (cell_box);
                    gtk_style_context_add_class (cell_ctx, "markdown-table-cell");
                    gtk_style_context_add_class (cell_ctx, is_dark ? "markdown-table-cell-dark" : "markdown-table-cell-light");
                    if (is_header) {
                        gtk_style_context_add_class (cell_ctx, "markdown-table-header");
                        gtk_style_context_add_class (cell_ctx, is_dark ? "markdown-table-header-dark" : "markdown-table-header-light");
                    }

                    gtk_box_pack_start (GTK_BOX (cell_box), cell_label, TRUE, TRUE, 0);
                    gtk_grid_attach (GTK_GRID (grid), cell_box, c_idx, grid_row, 1, 1);

                    c_idx++;
                    cell_item = cell_item->next;
                }
                grid_row++;
                row_item = row_item->next;
            }

            gtk_widget_set_margin_bottom (grid, 12);
            gtk_box_pack_start (container, grid, FALSE, FALSE, 0);
            break;
        }
        default:
            break;
    }
}

void
markdown_renderer_gtk_render (GtkBox *container, ASTNode *root, gboolean is_dark, GFile *doc_location)
{
    if (root == NULL || root->type != AST_NODE_DOCUMENT)
        return;

    GList *child = root->children;
    while (child != NULL) {
        render_node_to_gtk (container, (ASTNode *) child->data, is_dark, doc_location);
        child = child->next;
    }
}
