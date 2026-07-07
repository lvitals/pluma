#include "config.h"
#include "pluma-markdown-preview-plugin.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-panel.h>
#include <pluma/pluma-window.h>
#include <pluma/pluma-window-activatable.h>

#define UPDATE_DELAY_MS 500

enum {
    BLOCK_NONE,
    BLOCK_PARAGRAPH,
    BLOCK_TABLE
};

typedef struct {
    gchar **cells;
    int n_cells;
} TableRow;

struct _PlumaMarkdownPreviewPlugin
{
    PeasExtensionBase parent_instance;
    PlumaWindow *window;
    GtkWidget *preview;      /* GtkScrolledWindow */
    GtkWidget *viewport;     /* GtkViewport */
    GtkWidget *container;    /* GtkBox (vertical) */
    GtkActionGroup *action_group;
    guint ui_id;
    guint update_id;
    PlumaDocument *document;
    gulong changed_id;
    GtkCssProvider *css_provider;
};

static void window_activatable_iface_init (PlumaWindowActivatableInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaMarkdownPreviewPlugin,
                                pluma_markdown_preview_plugin,
                                PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                               window_activatable_iface_init))

enum { PROP_0, PROP_WINDOW };

static gboolean
is_dark_theme (GtkWidget *widget)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    GdkRGBA *bg = NULL;
    gboolean is_dark = FALSE;

    gtk_style_context_get (context, GTK_STATE_FLAG_NORMAL,
                           GTK_STYLE_PROPERTY_BACKGROUND_COLOR, &bg,
                           NULL);

    if (bg != NULL) {
        double luminance = 0.2126 * bg->red + 0.7152 * bg->green + 0.0722 * bg->blue;
        is_dark = (luminance < 0.5);
        gdk_rgba_free (bg);
    }

    return is_dark;
}

static const gchar *
lookup_emoji (const gchar *name)
{
    if (g_strcmp0 (name, "smile") == 0) return "😀";
    if (g_strcmp0 (name, "rocket") == 0) return "🚀";
    if (g_strcmp0 (name, "warning") == 0) return "⚠️";
    if (g_strcmp0 (name, "check") == 0 || g_strcmp0 (name, "tick") == 0) return "✓";
    if (g_strcmp0 (name, "cross") == 0) return "✗";
    return NULL;
}

static gchar *
markdown_to_pango_markup (const gchar *text, gboolean is_dark)
{
    gboolean bold = FALSE;
    gboolean italic = FALSE;
    gboolean strikethrough = FALSE;
    gboolean code = FALSE;

    GString *result = g_string_new ("");
    const gchar *p = text;

    while (*p != '\0') {
        if (code) {
            if (*p == '\\' && *(p + 1) == '`') {
                g_string_append (result, "`");
                p += 2;
            } else if (*p == '`') {
                g_string_append (result, "</span>");
                code = FALSE;
                p++;
            } else {
                if (*p == '&') g_string_append (result, "&amp;");
                else if (*p == '<') g_string_append (result, "&lt;");
                else if (*p == '>') g_string_append (result, "&gt;");
                else g_string_append_c (result, *p);
                p++;
            }
        } else {
            /* Escaped characters */
            if (*p == '\\' && *(p + 1) != '\0') {
                char c = *(p + 1);
                if (c == '&') g_string_append (result, "&amp;");
                else if (c == '<') g_string_append (result, "&lt;");
                else if (c == '>') g_string_append (result, "&gt;");
                else g_string_append_c (result, c);
                p += 2;
            }
            /* HTML inline tags */
            else if (g_str_has_prefix (p, "<b>") || g_str_has_prefix (p, "<strong>")) {
                g_string_append (result, "<b>");
                bold = TRUE;
                p += (g_str_has_prefix (p, "<b>") ? 3 : 8);
            } else if (g_str_has_prefix (p, "</b>") || g_str_has_prefix (p, "</strong>")) {
                g_string_append (result, "</b>");
                bold = FALSE;
                p += (g_str_has_prefix (p, "</b>") ? 4 : 9);
            } else if (g_str_has_prefix (p, "<i>") || g_str_has_prefix (p, "<em>")) {
                g_string_append (result, "<i>");
                italic = TRUE;
                p += (g_str_has_prefix (p, "<i>") ? 3 : 4);
            } else if (g_str_has_prefix (p, "</i>") || g_str_has_prefix (p, "</em>")) {
                g_string_append (result, "</i>");
                italic = FALSE;
                p += (g_str_has_prefix (p, "</i>") ? 4 : 5);
            } else if (*p == '<' && strchr (p, '>') != NULL && !g_str_has_prefix (p, "<http")) {
                const gchar *end = strchr (p, '>');
                p = end + 1;
            }
            /* Links: [text](url) */
            else if (*p == '[') {
                const gchar *close_bracket = strchr (p, ']');
                if (close_bracket != NULL && *(close_bracket + 1) == '(') {
                    const gchar *close_paren = strchr (close_bracket, ')');
                    if (close_paren != NULL) {
                        gchar *link_text = g_strndup (p + 1, close_bracket - (p + 1));
                        gchar *url = g_strndup (close_bracket + 2, close_paren - (close_bracket + 2));

                        gchar *escaped_text = markdown_to_pango_markup (link_text, is_dark);
                        g_string_append_printf (result, "<a href=\"%s\">%s</a>", url, escaped_text);

                        g_free (link_text);
                        g_free (url);
                        g_free (escaped_text);
                        p = close_paren + 1;
                        continue;
                    }
                }
                g_string_append (result, "[");
                p++;
            }
            /* Autolinks: <url> */
            else if (*p == '<' && g_str_has_prefix (p + 1, "http")) {
                const gchar *close_bracket = strchr (p, '>');
                if (close_bracket != NULL) {
                    gchar *url = g_strndup (p + 1, close_bracket - (p + 1));
                    g_string_append_printf (result, "<a href=\"%s\">%s</a>", url, url);
                    g_free (url);
                    p = close_bracket + 1;
                    continue;
                }
                g_string_append (result, "&lt;");
                p++;
            }
            /* Plain URLs */
            else if (g_str_has_prefix (p, "http://") || g_str_has_prefix (p, "https://")) {
                const gchar *end = p;
                while (*end != '\0' && *end != ' ' && *end != '\n' && *end != '\r' && *end != ')' && *end != ']' && *end != '<' && *end != '>') {
                    end++;
                }
                gchar *url = g_strndup (p, end - p);
                g_string_append_printf (result, "<a href=\"%s\">%s</a>", url, url);
                g_free (url);
                p = end;
            }
            /* Bold / Italic asterisks/underscores */
            else if (*p == '*' && *(p + 1) == '*' && *(p + 2) == '*') {
                g_string_append (result, bold && italic ? "</i></b>" : "<b><i>");
                bold = !bold;
                italic = !italic;
                p += 3;
            } else if (*p == '*' && *(p + 1) == '*') {
                g_string_append (result, bold ? "</b>" : "<b>");
                bold = !bold;
                p += 2;
            } else if (*p == '_' && *(p + 1) == '_') {
                g_string_append (result, bold ? "</b>" : "<b>");
                bold = !bold;
                p += 2;
            } else if (*p == '*') {
                g_string_append (result, italic ? "</i>" : "<i>");
                italic = !italic;
                p += 1;
            } else if (*p == '_') {
                g_string_append (result, italic ? "</i>" : "<i>");
                italic = !italic;
                p += 1;
            }
            /* Strikethrough ~~ */
            else if (*p == '~' && *(p + 1) == '~') {
                g_string_append (result, strikethrough ? "</s>" : "<s>");
                strikethrough = !strikethrough;
                p += 2;
            }
            /* Inline code ` */
            else if (*p == '`') {
                const gchar *bg = is_dark ? "#2d2f31" : "#f1f3f4";
                const gchar *fg = is_dark ? "#f28b82" : "#c7254e";
                g_string_append_printf (result, "<span font_family=\"monospace\" background=\"%s\" foreground=\"%s\">", bg, fg);
                code = TRUE;
                p += 1;
            }
            /* Emojis */
            else if (*p == ':') {
                const gchar *next_colon = strchr (p + 1, ':');
                if (next_colon != NULL && next_colon - p < 20) {
                    gchar *emoji_name = g_strndup (p + 1, next_colon - (p + 1));
                    const gchar *emoji_char = lookup_emoji (emoji_name);
                    g_free (emoji_name);
                    if (emoji_char != NULL) {
                        g_string_append (result, emoji_char);
                        p = next_colon + 1;
                        continue;
                    }
                }
                g_string_append_c (result, *p);
                p++;
            } else {
                if (*p == '&') g_string_append (result, "&amp;");
                else if (*p == '<') g_string_append (result, "&lt;");
                else if (*p == '>') g_string_append (result, "&gt;");
                else g_string_append_c (result, *p);
                p++;
            }
        }
    }

    return g_string_free (result, FALSE);
}

static GdkPixbuf *
load_local_image (GFile *doc_location, const gchar *url)
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

static gboolean
is_horizontal_rule (const gchar *line)
{
    const gchar *p = line;
    while (*p == ' ') p++;

    char c = *p;
    if (c != '-' && c != '*' && c != '_')
        return FALSE;

    int count = 0;
    while (*p != '\0') {
        if (*p == c) {
            count++;
        } else if (*p != ' ' && *p != '\r' && *p != '\n') {
            return FALSE;
        }
        p++;
    }
    return count >= 3;
}

static void
render_heading (GtkBox *container, int level, const gchar *text, gboolean is_dark)
{
    GtkWidget *label = gtk_label_new (NULL);
    gchar *pango_text = markdown_to_pango_markup (text, is_dark);

    const gchar *size;
    if (level == 1) size = "xx-large";
    else if (level == 2) size = "x-large";
    else if (level == 3) size = "large";
    else if (level == 4) size = "medium";
    else size = "small";

    const gchar *fg_color = is_dark ? "#8ab4f8" : "#1d3557";
    gchar *markup = g_strdup_printf ("<span size=\"%s\" weight=\"bold\" foreground=\"%s\">%s</span>",
                                     size, fg_color, pango_text);

    gtk_label_set_markup (GTK_LABEL (label), markup);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_halign (label, GTK_ALIGN_START);

    gtk_widget_set_margin_top (label, 12);
    gtk_widget_set_margin_bottom (label, 6);

    gtk_box_pack_start (container, label, FALSE, FALSE, 0);
    g_free (pango_text);
    g_free (markup);
}

static void
render_paragraph (GtkBox *container, const gchar *text, gboolean is_dark)
{
    GtkWidget *label = gtk_label_new (NULL);
    gchar *pango_text = markdown_to_pango_markup (text, is_dark);

    gtk_label_set_markup (GTK_LABEL (label), pango_text);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_halign (label, GTK_ALIGN_START);

    gtk_widget_set_margin_bottom (label, 8);

    gtk_box_pack_start (container, label, FALSE, FALSE, 0);
    g_free (pango_text);
}

static void
render_list_item (GtkBox *container, int leading_spaces, const gchar *content,
                  gboolean is_ordered, const gchar *ordered_num,
                  gboolean is_checkbox, gboolean is_checked, gboolean is_dark)
{
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

    gtk_widget_set_margin_start (hbox, leading_spaces * 8 + 8);
    gtk_widget_set_margin_bottom (hbox, 4);

    if (is_checkbox) {
        GtkWidget *chk = gtk_check_button_new ();
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chk), is_checked);
        gtk_widget_set_sensitive (chk, FALSE);
        gtk_box_pack_start (GTK_BOX (hbox), chk, FALSE, FALSE, 0);
    } else {
        GtkWidget *bullet_label = gtk_label_new (NULL);
        gchar *bullet_text;
        if (is_ordered) {
            bullet_text = g_strdup_printf ("%s. ", ordered_num);
        } else {
            int depth = leading_spaces / 2;
            if (depth == 0) bullet_text = g_strdup ("• ");
            else if (depth == 1) bullet_text = g_strdup ("◦ ");
            else bullet_text = g_strdup ("▪ ");
        }
        gtk_label_set_markup (GTK_LABEL (bullet_label), bullet_text);
        gtk_box_pack_start (GTK_BOX (hbox), bullet_label, FALSE, FALSE, 0);
        g_free (bullet_text);
    }

    GtkWidget *content_label = gtk_label_new (NULL);
    gchar *pango_text = markdown_to_pango_markup (content, is_dark);
    gtk_label_set_markup (GTK_LABEL (content_label), pango_text);
    gtk_label_set_line_wrap (GTK_LABEL (content_label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (content_label), 0.0);
    gtk_widget_set_halign (content_label, GTK_ALIGN_START);

    gtk_box_pack_start (GTK_BOX (hbox), content_label, TRUE, TRUE, 0);

    gtk_box_pack_start (container, hbox, FALSE, FALSE, 0);
    g_free (pango_text);
}

static void
render_code_block (GtkBox *container, const gchar *code_text, const gchar *lang, gboolean is_dark)
{
    if (lang != NULL && *lang != '\0') {
        gchar *lang_cap = g_strdup (lang);
        lang_cap[0] = g_ascii_toupper (lang_cap[0]);
        gchar *header_text = g_strdup_printf ("<span size=\"small\" weight=\"bold\" foreground=\"#888888\">Code Block: %s</span>", lang_cap);
        GtkWidget *header = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (header), header_text);
        gtk_widget_set_halign (header, GTK_ALIGN_START);
        gtk_widget_set_margin_bottom (header, 2);
        gtk_box_pack_start (container, header, FALSE, FALSE, 0);
        g_free (lang_cap);
        g_free (header_text);
    }

    GtkWidget *text_view = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text_view), FALSE);

    GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
    gtk_text_buffer_set_text (buf, code_text, -1);

    GtkStyleContext *ctx = gtk_widget_get_style_context (text_view);
    gtk_style_context_add_class (ctx, "markdown-code");
    gtk_style_context_add_class (ctx, is_dark ? "markdown-code-dark" : "markdown-code-light");

    gtk_widget_set_margin_bottom (text_view, 10);

    gtk_box_pack_start (container, text_view, FALSE, FALSE, 0);
}

static void
render_blockquote (GtkBox *container, const gchar *text, int quote_level, gboolean is_dark)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkStyleContext *ctx = gtk_widget_get_style_context (box);
    gtk_style_context_add_class (ctx, "markdown-quote");

    gtk_widget_set_margin_start (box, (quote_level - 1) * 16 + 8);
    gtk_widget_set_margin_bottom (box, 8);

    GtkWidget *label = gtk_label_new (NULL);
    gchar *pango_text = markdown_to_pango_markup (text, is_dark);
    gchar *quoted_markup = g_strdup_printf ("<span style=\"italic\" foreground=\"%s\">%s</span>",
                                            is_dark ? "#9aa0a6" : "#5f6368", pango_text);

    gtk_label_set_markup (GTK_LABEL (label), quoted_markup);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_halign (label, GTK_ALIGN_START);

    gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

    gtk_box_pack_start (container, box, FALSE, FALSE, 0);
    g_free (pango_text);
    g_free (quoted_markup);
}

static void
render_table_grid (GtkBox *container, const gchar *table_text, gboolean is_dark)
{
    gchar **lines = g_strsplit (table_text, "\n", -1);
    int n_lines = 0;
    while (lines[n_lines] != NULL) n_lines++;
    if (n_lines == 0) {
        g_strfreev (lines);
        return;
    }
    if (g_strcmp0 (lines[n_lines - 1], "") == 0) n_lines--;

    TableRow *rows = g_new0 (TableRow, n_lines);
    int max_cols = 0;

    for (int r = 0; r < n_lines; r++) {
        gchar *line = lines[r];
        gchar **raw_cells = g_strsplit (line, "|", -1);
        int raw_count = 0;
        while (raw_cells[raw_count] != NULL) raw_count++;

        int start_idx = 0;
        int end_idx = raw_count;
        if (raw_count > 0 && g_strcmp0 (g_strstrip (g_strdup (raw_cells[0])), "") == 0) {
            start_idx = 1;
        }
        if (raw_count > start_idx && g_strcmp0 (g_strstrip (g_strdup (raw_cells[raw_count - 1])), "") == 0) {
            end_idx = raw_count - 1;
        }

        int n_cells = end_idx - start_idx;
        if (n_cells < 0) n_cells = 0;

        rows[r].cells = g_new0 (gchar *, n_cells);
        rows[r].n_cells = n_cells;

        for (int c = 0; c < n_cells; c++) {
            rows[r].cells[c] = g_strdup (g_strstrip (raw_cells[start_idx + c]));
        }

        if (n_cells > max_cols) max_cols = n_cells;
        g_strfreev (raw_cells);
    }

    if (max_cols == 0) {
        for (int r = 0; r < n_lines; r++) {
            for (int c = 0; c < rows[r].n_cells; c++) g_free (rows[r].cells[c]);
            g_free (rows[r].cells);
        }
        g_free (rows);
        g_strfreev (lines);
        return;
    }

    int *alignments = g_new0 (int, max_cols);
    if (n_lines > 1 && rows[1].n_cells > 0) {
        for (int c = 0; c < max_cols; c++) {
            if (c < rows[1].n_cells) {
                gchar *cell = rows[1].cells[c];
                gboolean left_colon = g_str_has_prefix (cell, ":");
                gboolean right_colon = g_str_has_suffix (cell, ":");
                if (left_colon && right_colon) alignments[c] = 1;
                else if (right_colon) alignments[c] = 2;
                else alignments[c] = 0;
            }
        }
    }

    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 0);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 0);

    GtkStyleContext *grid_ctx = gtk_widget_get_style_context (grid);
    gtk_style_context_add_class (grid_ctx, "markdown-table");

    int grid_row = 0;
    for (int r = 0; r < n_lines; r++) {
        if (r == 1) continue;

        gboolean is_header = (r == 0);

        for (int c = 0; c < max_cols; c++) {
            gchar *cell_text = (c < rows[r].n_cells) ? rows[r].cells[c] : "";

            GtkWidget *cell_label = gtk_label_new (NULL);
            gchar *pango_text = markdown_to_pango_markup (cell_text, is_dark);

            gchar *markup;
            if (is_header) {
                markup = g_strdup_printf ("<b>%s</b>", pango_text);
            } else {
                markup = g_strdup (pango_text);
            }
            gtk_label_set_markup (GTK_LABEL (cell_label), markup);
            g_free (markup);
            g_free (pango_text);

            if (alignments[c] == 0) {
                gtk_widget_set_halign (cell_label, GTK_ALIGN_START);
                gtk_label_set_xalign (GTK_LABEL (cell_label), 0.0);
            } else if (alignments[c] == 2) {
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
            gtk_grid_attach (GTK_GRID (grid), cell_box, c, grid_row, 1, 1);
        }
        grid_row++;
    }

    gtk_widget_set_margin_bottom (grid, 12);
    gtk_box_pack_start (container, grid, FALSE, FALSE, 0);

    g_free (alignments);
    for (int r = 0; r < n_lines; r++) {
        for (int c = 0; c < rows[r].n_cells; c++) g_free (rows[r].cells[c]);
        g_free (rows[r].cells);
    }
    g_free (rows);
    g_strfreev (lines);
}

static void
render_separator (GtkBox *container, gboolean is_dark)
{
    GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    GtkStyleContext *sep_ctx = gtk_widget_get_style_context (sep);
    gtk_style_context_add_class (sep_ctx, is_dark ? "markdown-sep-dark" : "markdown-sep-light");

    gtk_widget_set_margin_top (sep, 8);
    gtk_widget_set_margin_bottom (sep, 8);
    gtk_box_pack_start (container, sep, FALSE, FALSE, 0);
}

static void
render_image (GtkBox *container, const gchar *alt, const gchar *url, GFile *doc_location)
{
    GdkPixbuf *pixbuf = load_local_image (doc_location, url);
    if (pixbuf != NULL) {
        GtkWidget *img = gtk_image_new_from_pixbuf (pixbuf);
        gtk_widget_set_halign (img, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_bottom (img, 8);
        gtk_box_pack_start (container, img, FALSE, FALSE, 0);
        g_object_unref (pixbuf);
    } else {
        GtkWidget *lbl = gtk_label_new (NULL);
        gchar *markup = g_strdup_printf ("<i>📷 [%s]</i>", alt);
        gtk_label_set_markup (GTK_LABEL (lbl), markup);
        gtk_widget_set_halign (lbl, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_bottom (lbl, 8);
        gtk_box_pack_start (container, lbl, FALSE, FALSE, 0);
        g_free (markup);
    }
}

static void
flush_accumulator (GtkBox *container,
                   GString *accumulator,
                   int block_type,
                   gboolean is_dark)
{
    if (accumulator->len == 0)
        return;

    if (block_type == BLOCK_PARAGRAPH) {
        gchar *text = g_strstrip (g_strdup (accumulator->str));
        render_paragraph (container, text, is_dark);
        g_free (text);
    } else if (block_type == BLOCK_TABLE) {
        render_table_grid (container, accumulator->str, is_dark);
    }

    g_string_truncate (accumulator, 0);
}

static gboolean
is_markdown (PlumaDocument *document)
{
    GtkSourceLanguage *language;
    GFile *location;
    gchar *basename = NULL;
    gchar *name;
    gboolean result = FALSE;

    if (document == NULL)
        return FALSE;

    language = pluma_document_get_language (document);
    if (language != NULL && g_strcmp0 (gtk_source_language_get_id (language), "markdown") == 0)
        return TRUE;

    location = pluma_document_get_location (document);
    if (location == NULL)
        return FALSE;
    if (g_file_peek_path (location) == NULL)
        basename = g_file_get_basename (location);
    name = g_ascii_strdown (basename != NULL ? basename : g_file_peek_path (location), -1);
    result = g_str_has_suffix (name, ".md") || g_str_has_suffix (name, ".markdown");
    g_free (basename);
    g_free (name);
    return result;
}

static void
load_welcome (PlumaMarkdownPreviewPlugin *self)
{
    gtk_container_foreach (GTK_CONTAINER (self->container), (GtkCallback) gtk_widget_destroy, NULL);

    gboolean is_dark = is_dark_theme (self->container);

    GtkWidget *welcome_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign (welcome_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (welcome_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top (welcome_box, 48);

    GdkPixbuf *pixbuf = NULL;
    GtkIconTheme *theme = gtk_icon_theme_get_default ();
    if (gtk_icon_theme_has_icon (theme, "text-x-markdown")) {
        pixbuf = gtk_icon_theme_load_icon (theme, "text-x-markdown", 64, 0, NULL);
    } else if (gtk_icon_theme_has_icon (theme, "accessories-text-editor")) {
        pixbuf = gtk_icon_theme_load_icon (theme, "accessories-text-editor", 64, 0, NULL);
    } else {
        pixbuf = gtk_icon_theme_load_icon (theme, "document-open", 64, 0, NULL);
    }

    if (pixbuf != NULL) {
        GtkWidget *img = gtk_image_new_from_pixbuf (pixbuf);
        gtk_box_pack_start (GTK_BOX (welcome_box), img, FALSE, FALSE, 0);
        g_object_unref (pixbuf);
    }

    GtkWidget *title_lbl = gtk_label_new (NULL);
    gchar *title_markup = g_strdup_printf ("<span size=\"x-large\" weight=\"bold\" foreground=\"%s\">Markdown Preview</span>",
                                           is_dark ? "#8ab4f8" : "#1d3557");
    gtk_label_set_markup (GTK_LABEL (title_lbl), title_markup);
    gtk_box_pack_start (GTK_BOX (welcome_box), title_lbl, FALSE, FALSE, 0);
    g_free (title_markup);

    GtkWidget *desc_lbl = gtk_label_new (_("Open a Markdown file (.md or .markdown) to preview it."));
    gtk_box_pack_start (GTK_BOX (welcome_box), desc_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (self->container), welcome_box, TRUE, TRUE, 0);
    gtk_widget_show_all (self->container);
}

static gboolean
update_preview (gpointer data)
{
    PlumaMarkdownPreviewPlugin *self = data;
    GtkTextIter start, end;
    gchar *text;
    gboolean in_code_block = FALSE;
    GString *code_accumulator = NULL;
    gchar *code_lang = NULL;
    int current_block_type = BLOCK_NONE;
    GString *accumulator = g_string_new ("");
    GFile *doc_location = NULL;
    gboolean is_dark = FALSE;

    self->update_id = 0;

    if (self->container == NULL) {
        g_string_free (accumulator, TRUE);
        return G_SOURCE_REMOVE;
    }

    is_dark = is_dark_theme (self->container);

    if (self->document != NULL) {
        doc_location = pluma_document_get_location (self->document);
    }

    /* Clear the container */
    gtk_container_foreach (GTK_CONTAINER (self->container), (GtkCallback) gtk_widget_destroy, NULL);

    if (!is_markdown (self->document)) {
        load_welcome (self);
        g_string_free (accumulator, TRUE);
        return G_SOURCE_REMOVE;
    }

    gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->document), &start, &end);
    text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (self->document), &start, &end, FALSE);

    const gchar *p = text;
    while (p != NULL && *p != '\0') {
        const gchar *next = strchr (p, '\n');
        gchar *line;
        if (next != NULL) {
            line = g_strndup (p, next - p);
            p = next + 1;
        } else {
            line = g_strdup (p);
            p = NULL;
        }

        /* Trim trailing CR */
        gsize len = strlen (line);
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }

        /* Check for fenced code blocks */
        if (g_str_has_prefix (line, "```")) {
            flush_accumulator (GTK_BOX (self->container), accumulator, current_block_type, is_dark);
            current_block_type = BLOCK_NONE;

            in_code_block = !in_code_block;
            if (in_code_block) {
                const gchar *lang = line + 3;
                while (*lang == ' ') lang++;
                g_free (code_lang);
                code_lang = g_strdup (lang);
                code_accumulator = g_string_new ("");
            } else {
                if (code_accumulator != NULL) {
                    render_code_block (GTK_BOX (self->container), code_accumulator->str, code_lang, is_dark);
                    g_string_free (code_accumulator, TRUE);
                    code_accumulator = NULL;
                }
                g_free (code_lang);
                code_lang = NULL;
            }
            g_free (line);
            continue;
        }

        if (in_code_block) {
            g_string_append (code_accumulator, line);
            g_string_append_c (code_accumulator, '\n');
            g_free (line);
            continue;
        }

        /* Determine block level traits */
        int leading_spaces = 0;
        const gchar *s = line;
        while (*s == ' ') {
            leading_spaces++;
            s++;
        }

        gboolean is_h_rule = is_horizontal_rule (line);

        int heading_level = 0;
        if (s[0] == '#') {
            const gchar *h = s;
            while (*h == '#') {
                heading_level++;
                h++;
            }
            if (*h != ' ') {
                heading_level = 0;
            }
        }

        gboolean is_list_item = FALSE;
        gboolean is_ordered = FALSE;
        const gchar *content = s;
        gchar *ordered_num = NULL;
        if (g_str_has_prefix (s, "- ") || g_str_has_prefix (s, "* ") || g_str_has_prefix (s, "+ ")) {
            is_list_item = TRUE;
            content = s + 2;
        } else {
            const gchar *o = s;
            while (*o >= '0' && *o <= '9') {
                o++;
            }
            if (o > s && g_str_has_prefix (o, ". ")) {
                is_list_item = TRUE;
                is_ordered = TRUE;
                ordered_num = g_strndup (s, o - s);
                content = o + 2;
            }
        }

        int quote_level = 0;
        if (s[0] == '>') {
            const gchar *q = s;
            while (*q == '>') {
                quote_level++;
                q++;
            }
            if (*q == ' ') q++;
            content = q;
        }

        gboolean is_empty = (g_strcmp0 (g_strstrip (g_strdup (line)), "") == 0);
        gboolean is_table_line = (s[0] == '|');

        gboolean is_image_block = FALSE;
        gchar *img_alt = NULL;
        gchar *img_url = NULL;
        if (s[0] == '!' && s[1] == '[') {
            const gchar *close_bracket = strchr (s, ']');
            if (close_bracket != NULL && *(close_bracket + 1) == '(') {
                const gchar *close_paren = strchr (close_bracket, ')');
                if (close_paren != NULL && *(close_paren + 1) == '\0') {
                    is_image_block = TRUE;
                    img_alt = g_strndup (s + 2, close_bracket - (s + 2));
                    img_url = g_strndup (close_bracket + 2, close_paren - (close_bracket + 2));
                }
            }
        }

        int new_block_type = BLOCK_NONE;
        if (is_table_line) {
            new_block_type = BLOCK_TABLE;
        } else if (!is_empty && heading_level == 0 && !is_list_item && quote_level == 0 && !is_h_rule && !is_image_block) {
            new_block_type = BLOCK_PARAGRAPH;
        }

        if (new_block_type != current_block_type) {
            flush_accumulator (GTK_BOX (self->container), accumulator, current_block_type, is_dark);
            current_block_type = new_block_type;
        }

        if (current_block_type == BLOCK_PARAGRAPH) {
            if (accumulator->len > 0) {
                g_string_append_c (accumulator, ' ');
            }
            g_string_append (accumulator, line);
        } else if (current_block_type == BLOCK_TABLE) {
            g_string_append (accumulator, line);
            g_string_append_c (accumulator, '\n');
        } else {
            flush_accumulator (GTK_BOX (self->container), accumulator, BLOCK_NONE, is_dark);

            if (is_empty) {
                GtkWidget *spacer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
                gtk_widget_set_size_request (spacer, -1, 6);
                gtk_box_pack_start (GTK_BOX (self->container), spacer, FALSE, FALSE, 0);
            } else if (is_h_rule) {
                render_separator (GTK_BOX (self->container), is_dark);
            } else if (is_image_block) {
                render_image (GTK_BOX (self->container), img_alt, img_url, doc_location);
            } else if (heading_level > 0) {
                const gchar *title_text = s + heading_level;
                while (*title_text == ' ') title_text++;
                render_heading (GTK_BOX (self->container), heading_level, title_text, is_dark);
            } else if (is_list_item) {
                while (*content == ' ') content++;

                gboolean is_checkbox = FALSE;
                gboolean is_checked = FALSE;
                if (g_str_has_prefix (content, "[ ] ")) {
                    is_checkbox = TRUE;
                    content += 4;
                } else if (g_str_has_prefix (content, "[x] ") || g_str_has_prefix (content, "[X] ")) {
                    is_checkbox = TRUE;
                    is_checked = TRUE;
                    content += 4;
                }

                render_list_item (GTK_BOX (self->container), leading_spaces, content,
                                 is_ordered, ordered_num,
                                 is_checkbox, is_checked, is_dark);
            } else if (quote_level > 0) {
                while (*content == ' ') content++;
                render_blockquote (GTK_BOX (self->container), content, quote_level, is_dark);
            }
        }

        g_free (ordered_num);
        g_free (img_alt);
        g_free (img_url);
        g_free (line);
    }

    flush_accumulator (GTK_BOX (self->container), accumulator, current_block_type, is_dark);

    gtk_widget_show_all (self->container);

    g_string_free (accumulator, TRUE);
    g_free (text);

    return G_SOURCE_REMOVE;
}

static void
schedule_update (PlumaMarkdownPreviewPlugin *self)
{
    if (self->update_id != 0)
        g_source_remove (self->update_id);
    self->update_id = g_timeout_add (UPDATE_DELAY_MS, update_preview, self);
}

static void
document_changed (GtkTextBuffer *buffer, PlumaMarkdownPreviewPlugin *self)
{
    schedule_update (self);
}

static void
set_document (PlumaMarkdownPreviewPlugin *self, PlumaDocument *document)
{
    if (self->document == document)
        return;
    if (self->document != NULL && self->changed_id != 0)
        g_signal_handler_disconnect (self->document, self->changed_id);
    g_set_object (&self->document, document);
    self->changed_id = document == NULL ? 0 :
        g_signal_connect (document, "changed", G_CALLBACK (document_changed), self);
}

static void
show_preview (GtkAction *action, PlumaMarkdownPreviewPlugin *self)
{
    PlumaPanel *panel = pluma_window_get_right_panel (self->window);
    gtk_widget_show (GTK_WIDGET (panel));
    pluma_panel_activate_item (panel, self->preview);
    update_preview (self);
}

static void
style_updated_cb (GtkWidget *widget, gpointer data)
{
    PlumaMarkdownPreviewPlugin *self = data;
    update_preview (self);
}

static void
activate (PlumaWindowActivatable *activatable)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (activatable);
    PlumaPanel *panel = pluma_window_get_right_panel (self->window);
    GtkUIManager *manager = pluma_window_get_ui_manager (self->window);
    const GtkActionEntry entries[] = {
        { "MarkdownPreview", NULL, N_("Show Markdown Preview"), NULL,
          N_("Preview the current Markdown document"), G_CALLBACK (show_preview) },
        { "MarkdownUpdate", NULL, N_("Update Markdown Preview"), NULL,
          N_("Update the Markdown preview"), G_CALLBACK (show_preview) }
    };

    self->preview = gtk_scrolled_window_new (NULL, NULL);

    self->viewport = gtk_viewport_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (self->preview), self->viewport);

    self->container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start (self->container, 16);
    gtk_widget_set_margin_end (self->container, 16);
    gtk_widget_set_margin_top (self->container, 16);
    gtk_widget_set_margin_bottom (self->container, 16);

    gtk_container_add (GTK_CONTAINER (self->viewport), self->container);

    self->css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (self->css_provider,
        ".markdown-code {"
        "  padding: 8px;"
        "  font-family: monospace;"
        "  border-radius: 4px;"
        "}"
        ".markdown-code-light {"
        "  background-color: #f6f6f6;"
        "  color: #24292e;"
        "}"
        ".markdown-code-dark {"
        "  background-color: #2d3139;"
        "  color: #e1e4e8;"
        "}"
        ".markdown-table {"
        "  border: none;"
        "}"
        ".markdown-table-cell-light {"
        "  border: 1px solid #e1e4e8;"
        "  padding: 8px 12px;"
        "}"
        ".markdown-table-cell-dark {"
        "  border: 1px solid #444c56;"
        "  padding: 8px 12px;"
        "}"
        ".markdown-table-header-light {"
        "  background-color: #e1e4e8;"
        "}"
        ".markdown-table-header-dark {"
        "  background-color: #2d3139;"
        "}"
        ".markdown-quote {"
        "  border-left: 4px solid #3465a4;"
        "  padding-left: 12px;"
        "}"
        ".markdown-sep-light {"
        "  background-color: #e1e4e8;"
        "  min-height: 1px;"
        "}"
        ".markdown-sep-dark {"
        "  background-color: #444c56;"
        "  min-height: 1px;"
        "}",
        -1,
        NULL);

    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                               GTK_STYLE_PROVIDER (self->css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_signal_connect (self->container, "style-updated", G_CALLBACK (style_updated_cb), self);

    pluma_panel_add_item_with_icon (panel, self->preview, _("Markdown Preview"), "text-x-markdown");
    gtk_widget_show_all (self->preview);
    load_welcome (self);

    self->action_group = gtk_action_group_new ("MarkdownPreviewActions");
    gtk_action_group_set_translation_domain (self->action_group, GETTEXT_PACKAGE);
    gtk_action_group_add_actions (self->action_group, entries, G_N_ELEMENTS (entries), self);
    gtk_ui_manager_insert_action_group (manager, self->action_group, -1);
    self->ui_id = gtk_ui_manager_new_merge_id (manager);
    gtk_ui_manager_add_ui (manager, self->ui_id, "/MenuBar/ToolsMenu/ToolsOps_4",
                           "MarkdownPreview", "MarkdownPreview", GTK_UI_MANAGER_MENUITEM, FALSE);
    gtk_ui_manager_add_ui (manager, self->ui_id, "/MenuBar/ToolsMenu/ToolsOps_4",
                           "MarkdownUpdate", "MarkdownUpdate", GTK_UI_MANAGER_MENUITEM, FALSE);
}

static void
deactivate (PlumaWindowActivatable *activatable)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (activatable);
    GtkUIManager *manager = pluma_window_get_ui_manager (self->window);
    if (self->update_id != 0) {
        g_source_remove (self->update_id);
        self->update_id = 0;
    }
    set_document (self, NULL);

    if (self->css_provider != NULL) {
        gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
                                                      GTK_STYLE_PROVIDER (self->css_provider));
        g_clear_object (&self->css_provider);
    }

    gtk_ui_manager_remove_ui (manager, self->ui_id);
    gtk_ui_manager_remove_action_group (manager, self->action_group);
    g_clear_object (&self->action_group);
    pluma_panel_remove_item (pluma_window_get_right_panel (self->window), self->preview);
    self->preview = NULL;
    self->viewport = NULL;
    self->container = NULL;
}

static void
update_state (PlumaWindowActivatable *activatable)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (activatable);
    set_document (self, pluma_window_get_active_document (self->window));
    schedule_update (self);
}

static void
pluma_markdown_preview_plugin_set_property (GObject *object, guint id,
                                            const GValue *value, GParamSpec *pspec)
{
    if (id == PROP_WINDOW)
        PLUMA_MARKDOWN_PREVIEW_PLUGIN (object)->window = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}

static void
pluma_markdown_preview_plugin_get_property (GObject *object, guint id,
                                            GValue *value, GParamSpec *pspec)
{
    if (id == PROP_WINDOW)
        g_value_set_object (value, PLUMA_MARKDOWN_PREVIEW_PLUGIN (object)->window);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}

static void
pluma_markdown_preview_plugin_dispose (GObject *object)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (object);
    g_clear_object (&self->document);
    g_clear_object (&self->window);
    G_OBJECT_CLASS (pluma_markdown_preview_plugin_parent_class)->dispose (object);
}

static void pluma_markdown_preview_plugin_init (PlumaMarkdownPreviewPlugin *self) {}
static void pluma_markdown_preview_plugin_class_finalize (PlumaMarkdownPreviewPluginClass *klass) {}

static void
pluma_markdown_preview_plugin_class_init (PlumaMarkdownPreviewPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->set_property = pluma_markdown_preview_plugin_set_property;
    object_class->get_property = pluma_markdown_preview_plugin_get_property;
    object_class->dispose = pluma_markdown_preview_plugin_dispose;
    g_object_class_override_property (object_class, PROP_WINDOW, "window");
}

static void
window_activatable_iface_init (PlumaWindowActivatableInterface *iface)
{
    iface->activate = activate;
    iface->deactivate = deactivate;
    iface->update_state = update_state;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
    pluma_markdown_preview_plugin_register_type (G_TYPE_MODULE (module));
    peas_object_module_register_extension_type (module, PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                PLUMA_TYPE_MARKDOWN_PREVIEW_PLUGIN);
}
