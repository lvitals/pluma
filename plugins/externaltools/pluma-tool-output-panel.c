#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-tool-output-panel.h"

#include <glib/gi18n-lib.h>
#include <pluma/pluma-utils.h>
#include <pluma/pluma-panel.h>
#include <pluma/pluma-commands.h>

#include "pluma-tool-link-parser.h"
#include "pluma-tool-file-lookup.h"

struct _PlumaToolOutputPanel {
    gint ref_count;
    PlumaWindow *window;
    GtkWidget *panel_widget; /* "output-panel" from outputpanel.ui */
    GtkWidget *view;
    GtkWidget *stop_button;

    GtkTextTag *normal_tag;
    GtkTextTag *error_tag;
    GtkTextTag *italic_tag;
    GtkTextTag *bold_tag;
    GtkTextTag *invalid_link_tag;
    GtkTextTag *link_tag;

    GdkCursor *link_cursor;
    GdkCursor *normal_cursor;

    PlumaToolCapture *process; /* not owned */
    GList *links;              /* PlumaToolLink*, offsets into the buffer */
};

static PlumaToolLink *
get_link_at_location (PlumaToolOutputPanel *panel, gint x, gint y)
{
    GtkTextIter iter;
    gint buf_x, buf_y;
    gint offset;
    GList *l;
    gboolean over_text;

    gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (panel->view), GTK_TEXT_WINDOW_TEXT,
                                           x, y, &buf_x, &buf_y);
    over_text = gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (panel->view), &iter, buf_x, buf_y);
    if (!over_text)
        return NULL;

    offset = gtk_text_iter_get_offset (&iter);

    for (l = panel->links; l != NULL; l = l->next) {
        PlumaToolLink *lnk = l->data;
        if (offset >= lnk->start && offset <= lnk->end)
            return lnk;
    }
    return NULL;
}

static void
update_cursor_style (PlumaToolOutputPanel *panel, gint x, gint y)
{
    GdkWindow *window = gtk_text_view_get_window (GTK_TEXT_VIEW (panel->view), GTK_TEXT_WINDOW_TEXT);
    PlumaToolLink *lnk = get_link_at_location (panel, x, y);
    gdk_window_set_cursor (window, lnk != NULL ? panel->link_cursor : panel->normal_cursor);
}

static gboolean
on_view_motion_notify_event (GtkWidget *widget, GdkEventMotion *event, PlumaToolOutputPanel *panel)
{
    if (event->window == gtk_text_view_get_window (GTK_TEXT_VIEW (panel->view), GTK_TEXT_WINDOW_TEXT))
        update_cursor_style (panel, (gint) event->x, (gint) event->y);
    return FALSE;
}

static gboolean
on_view_button_press_event (GtkWidget *widget, GdkEventButton *event, PlumaToolOutputPanel *panel)
{
    PlumaToolLink *lnk;
    GFile *file;

    if (event->button != 1 || event->type != GDK_BUTTON_PRESS ||
        event->window != gtk_text_view_get_window (GTK_TEXT_VIEW (panel->view), GTK_TEXT_WINDOW_TEXT))
        return FALSE;

    lnk = get_link_at_location (panel, (gint) event->x, (gint) event->y);
    if (lnk == NULL)
        return FALSE;

    file = pluma_tool_file_lookup (lnk->path);
    if (file != NULL) {
        gchar *uri = g_file_get_uri (file);
        pluma_commands_load_uri (panel->window, uri, NULL, lnk->line_nr);
        g_free (uri);
        g_object_unref (file);
    }
    return FALSE;
}

static void
on_stop_clicked (GtkButton *button, PlumaToolOutputPanel *panel)
{
    if (panel->process != NULL) {
        pluma_tool_output_panel_write (panel, "\n", PLUMA_TOOL_OUTPUT_TAG_ITALIC);
        pluma_tool_output_panel_write (panel, _("Stopped."), PLUMA_TOOL_OUTPUT_TAG_ITALIC);
        pluma_tool_output_panel_write (panel, "\n", PLUMA_TOOL_OUTPUT_TAG_ITALIC);
        pluma_tool_capture_stop (panel->process);
    }
}

PlumaToolOutputPanel *
pluma_tool_output_panel_new (const gchar *data_dir, PlumaWindow *window)
{
    PlumaToolOutputPanel *panel;
    gchar *ui_file;
    gchar *root_objects[] = { "output-panel", NULL };
    GtkWidget *error_widget = NULL;
    GtkTextBuffer *buffer;
    PangoFontDescription *font;

    panel = g_new0 (PlumaToolOutputPanel, 1);
    panel->ref_count = 1;
    panel->window = window;

    ui_file = g_build_filename (data_dir, "ui", "outputpanel.ui", NULL);
    if (!pluma_utils_get_ui_objects (ui_file, root_objects, &error_widget,
                                     "output-panel", &panel->panel_widget,
                                     "view", &panel->view,
                                     "stop", &panel->stop_button,
                                     NULL)) {
        g_warning ("External Tools: could not load outputpanel.ui from %s", ui_file);
        if (error_widget != NULL)
            gtk_widget_destroy (error_widget);
        g_free (ui_file);
        g_free (panel);
        return NULL;
    }
    g_free (ui_file);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    font = pango_font_description_from_string ("Monospace");
    gtk_widget_override_font (panel->view, font);
    pango_font_description_free (font);
    G_GNUC_END_IGNORE_DEPRECATIONS

    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (panel->view));
    panel->normal_tag = gtk_text_buffer_create_tag (buffer, "normal", NULL);
    panel->error_tag = gtk_text_buffer_create_tag (buffer, "error", "foreground", "red", NULL);
    panel->italic_tag = gtk_text_buffer_create_tag (buffer, "italic", "style", PANGO_STYLE_OBLIQUE, NULL);
    panel->bold_tag = gtk_text_buffer_create_tag (buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    panel->invalid_link_tag = gtk_text_buffer_create_tag (buffer, "invalid_link", NULL);
    panel->link_tag = gtk_text_buffer_create_tag (buffer, "link", "underline", PANGO_UNDERLINE_SINGLE, NULL);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    panel->link_cursor = gdk_cursor_new (GDK_HAND2);
    panel->normal_cursor = gdk_cursor_new (GDK_XTERM);
    G_GNUC_END_IGNORE_DEPRECATIONS

    g_signal_connect (panel->stop_button, "clicked", G_CALLBACK (on_stop_clicked), panel);
    g_signal_connect (panel->view, "motion-notify-event", G_CALLBACK (on_view_motion_notify_event), panel);
    g_signal_connect (panel->view, "button-press-event", G_CALLBACK (on_view_button_press_event), panel);

    return panel;
}

void
pluma_tool_output_panel_unref (PlumaToolOutputPanel *panel)
{
    if (panel == NULL)
        return;
    if (!g_atomic_int_dec_and_test (&panel->ref_count))
        return;
    g_list_free_full (panel->links, (GDestroyNotify) pluma_tool_link_free);
    g_clear_object (&panel->link_cursor);
    g_clear_object (&panel->normal_cursor);
    if (panel->panel_widget != NULL) {
        gtk_widget_destroy (panel->panel_widget);
        g_object_unref (panel->panel_widget);
    }
    g_free (panel);
}

PlumaToolOutputPanel *
pluma_tool_output_panel_ref (PlumaToolOutputPanel *panel)
{
    g_return_val_if_fail (panel != NULL, NULL);
    g_atomic_int_inc (&panel->ref_count);
    return panel;
}

void
pluma_tool_output_panel_free (PlumaToolOutputPanel *panel)
{
    pluma_tool_output_panel_unref (panel);
}

GtkWidget *
pluma_tool_output_panel_get_widget (PlumaToolOutputPanel *panel)
{
    return panel->panel_widget;
}

void
pluma_tool_output_panel_set_process (PlumaToolOutputPanel *panel, PlumaToolCapture *process)
{
    panel->process = process;
}

void
pluma_tool_output_panel_set_running (PlumaToolOutputPanel *panel, gboolean running)
{
    gtk_widget_set_sensitive (panel->stop_button, running);
}

void
pluma_tool_output_panel_clear (PlumaToolOutputPanel *panel)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (panel->view));
    gtk_text_buffer_set_text (buffer, "", -1);
    g_list_free_full (panel->links, (GDestroyNotify) pluma_tool_link_free);
    panel->links = NULL;
}

static gboolean
scroll_to_end_idle (gpointer data)
{
    PlumaToolOutputPanel *panel = data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (panel->view));
    GtkTextIter end_iter;

    gtk_text_buffer_get_end_iter (buffer, &end_iter);
    gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (panel->view), &end_iter, 0.0, FALSE, 0.5, 0.5);
    return G_SOURCE_REMOVE;
}

void
pluma_tool_output_panel_write (PlumaToolOutputPanel *panel, const gchar *text, PlumaToolOutputTag tag_kind)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (panel->view));
    GtkTextIter end_iter;
    GtkTextMark *insert_mark;
    GtkTextIter mark_iter;
    GtkTextTag *tag;
    GList *links, *l;
    gint insert_offset;

    gtk_text_buffer_get_end_iter (buffer, &end_iter);
    insert_mark = gtk_text_buffer_create_mark (buffer, NULL, &end_iter, TRUE);

    switch (tag_kind) {
        case PLUMA_TOOL_OUTPUT_TAG_ERROR:  tag = panel->error_tag;  break;
        case PLUMA_TOOL_OUTPUT_TAG_ITALIC: tag = panel->italic_tag; break;
        case PLUMA_TOOL_OUTPUT_TAG_BOLD:   tag = panel->bold_tag;   break;
        default:                          tag = NULL;              break;
    }

    if (tag == NULL)
        gtk_text_buffer_insert (buffer, &end_iter, text, -1);
    else
        gtk_text_buffer_insert_with_tags (buffer, &end_iter, text, -1, tag, NULL);

    gtk_text_buffer_get_iter_at_mark (buffer, &mark_iter, insert_mark);
    insert_offset = gtk_text_iter_get_offset (&mark_iter);

    links = pluma_tool_link_parse (text);
    for (l = links; l != NULL; l = l->next) {
        PlumaToolLink *lnk = l->data;
        GtkTextIter start_iter, link_end_iter;
        GFile *file;

        gtk_text_buffer_get_iter_at_offset (buffer, &start_iter, insert_offset + lnk->start);
        gtk_text_buffer_get_iter_at_offset (buffer, &link_end_iter, insert_offset + lnk->end);

        file = pluma_tool_file_lookup (lnk->path);
        if (file != NULL) {
            PlumaToolLink *stored = g_new0 (PlumaToolLink, 1);
            stored->path = g_strdup (lnk->path);
            stored->line_nr = lnk->line_nr;
            stored->start = insert_offset + lnk->start;
            stored->end = insert_offset + lnk->end;
            panel->links = g_list_append (panel->links, stored);

            gtk_text_buffer_apply_tag (buffer, panel->link_tag, &start_iter, &link_end_iter);
            g_object_unref (file);
        } else {
            gtk_text_buffer_apply_tag (buffer, panel->invalid_link_tag, &start_iter, &link_end_iter);
        }
    }
    g_list_free_full (links, (GDestroyNotify) pluma_tool_link_free);

    gtk_text_buffer_delete_mark (buffer, insert_mark);

    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                     scroll_to_end_idle,
                     pluma_tool_output_panel_ref (panel),
                     (GDestroyNotify) pluma_tool_output_panel_unref);
}

void
pluma_tool_output_panel_show (PlumaToolOutputPanel *panel)
{
    PlumaPanel *bottom = pluma_window_get_bottom_panel (panel->window);
    gtk_widget_show (GTK_WIDGET (bottom));
    pluma_panel_activate_item (bottom, panel->panel_widget);
}

gboolean
pluma_tool_output_panel_visible (PlumaToolOutputPanel *panel)
{
    PlumaPanel *bottom = pluma_window_get_bottom_panel (panel->window);
    return gtk_widget_get_visible (GTK_WIDGET (bottom)) &&
           pluma_panel_item_is_active (bottom, panel->panel_widget);
}
