#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-tool-functions.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gtksourceview/gtksource.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-view.h>
#include <pluma/pluma-tab.h>
#include <pluma/pluma-utils.h>
#include <pluma/pluma-commands.h>

#include "pluma-tool-capture.h"

typedef struct {
    PlumaToolOutputPanel *panel;
    gchar *tool_name;
    PlumaView *view;           /* may be NULL */
    PlumaDocument *document;   /* document receiving output, or NULL */
    GtkTextMark *pos_mark;     /* growing insert position when document != NULL */
    gboolean guess_language;   /* whole-document output: worth re-detecting the language */
    PlumaToolCapture *capture;
} RunContext;

static void
current_word_bounds (GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end)
{
    GtkTextIter iter;

    gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));
    *start = iter;
    *end = iter;

    if (!gtk_text_iter_starts_word (&iter) &&
        (gtk_text_iter_inside_word (&iter) || gtk_text_iter_ends_word (&iter)))
        gtk_text_iter_backward_word_start (start);

    if (!gtk_text_iter_ends_word (&iter) && gtk_text_iter_inside_word (&iter))
        gtk_text_iter_forward_word_end (end);
}

static void
stdout_to_document (PlumaToolCapture *capture, const gchar *line, gpointer user_data)
{
    RunContext *ctx = user_data;
    GtkTextIter iter;

    gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (ctx->document), &iter, ctx->pos_mark);
    gtk_text_buffer_insert (GTK_TEXT_BUFFER (ctx->document), &iter, line, -1);
}

static void
stdout_to_panel (PlumaToolCapture *capture, const gchar *line, gpointer user_data)
{
    RunContext *ctx = user_data;
    pluma_tool_output_panel_write (ctx->panel, line, PLUMA_TOOL_OUTPUT_TAG_NONE);
}

static void
stderr_to_panel (PlumaToolCapture *capture, const gchar *line, gpointer user_data)
{
    RunContext *ctx = user_data;
    if (!pluma_tool_output_panel_visible (ctx->panel))
        pluma_tool_output_panel_show (ctx->panel);
    pluma_tool_output_panel_write (ctx->panel, line, PLUMA_TOOL_OUTPUT_TAG_ERROR);
}

static void
on_begin_execute (PlumaToolCapture *capture, gpointer user_data)
{
    RunContext *ctx = user_data;
    GdkWindow *text_window;

    if (ctx->view != NULL) {
        text_window = gtk_text_view_get_window (GTK_TEXT_VIEW (ctx->view), GTK_TEXT_WINDOW_TEXT);
        if (text_window != NULL) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            GdkCursor *cursor = gdk_cursor_new (GDK_WATCH);
            G_GNUC_END_IGNORE_DEPRECATIONS
            gdk_window_set_cursor (text_window, cursor);
            g_object_unref (cursor);
        }
    }

    pluma_tool_output_panel_set_running (ctx->panel, TRUE);
    pluma_tool_output_panel_write (ctx->panel, _("Running tool:"), PLUMA_TOOL_OUTPUT_TAG_ITALIC);
    {
        gchar *label = g_strdup_printf (" %s\n\n", ctx->tool_name);
        pluma_tool_output_panel_write (ctx->panel, label, PLUMA_TOOL_OUTPUT_TAG_BOLD);
        g_free (label);
    }
}

static void
on_end_execute (PlumaToolCapture *capture, gint exit_code, gpointer user_data)
{
    RunContext *ctx = user_data;

    pluma_tool_output_panel_set_running (ctx->panel, FALSE);

    if (ctx->document != NULL) {
        if (ctx->guess_language) {
            GtkTextIter start, end;
            gchar *sample;
            gchar *content_type;
            GtkSourceLanguageManager *lmanager;
            GtkSourceLanguage *language;

            gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (ctx->document), &start);
            end = start;
            gtk_text_iter_forward_chars (&end, 300);
            sample = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (ctx->document), &start, &end, FALSE);

            content_type = g_content_type_guess (NULL, (const guchar *) sample, strlen (sample), NULL);
            lmanager = gtk_source_language_manager_get_default ();
            language = gtk_source_language_manager_guess_language (lmanager, NULL, content_type);
            if (language != NULL)
                gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (ctx->document), language);

            g_free (content_type);
            g_free (sample);
        }

        gtk_text_buffer_delete_mark (GTK_TEXT_BUFFER (ctx->document), ctx->pos_mark);
        gtk_text_buffer_end_user_action (GTK_TEXT_BUFFER (ctx->document));

        if (ctx->view != NULL) {
            GdkWindow *text_window = gtk_text_view_get_window (GTK_TEXT_VIEW (ctx->view), GTK_TEXT_WINDOW_TEXT);
            if (text_window != NULL) {
                G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                GdkCursor *cursor = gdk_cursor_new (GDK_XTERM);
                G_GNUC_END_IGNORE_DEPRECATIONS
                gdk_window_set_cursor (text_window, cursor);
                g_object_unref (cursor);
            }
            gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (ctx->view), TRUE);
            gtk_text_view_set_editable (GTK_TEXT_VIEW (ctx->view), TRUE);
        }
    }

    if (exit_code == 0) {
        pluma_tool_output_panel_write (ctx->panel, "\n", PLUMA_TOOL_OUTPUT_TAG_ITALIC);
        pluma_tool_output_panel_write (ctx->panel, _("Done."), PLUMA_TOOL_OUTPUT_TAG_ITALIC);
        pluma_tool_output_panel_write (ctx->panel, "\n", PLUMA_TOOL_OUTPUT_TAG_ITALIC);
    } else {
        gchar *code_str;
        pluma_tool_output_panel_write (ctx->panel, "\n", PLUMA_TOOL_OUTPUT_TAG_ITALIC);
        pluma_tool_output_panel_write (ctx->panel, _("Exited:"), PLUMA_TOOL_OUTPUT_TAG_ITALIC);
        code_str = g_strdup_printf (" %d\n", exit_code);
        pluma_tool_output_panel_write (ctx->panel, code_str, PLUMA_TOOL_OUTPUT_TAG_BOLD);
        g_free (code_str);
    }

    pluma_tool_output_panel_set_process (ctx->panel, NULL);
    pluma_tool_capture_free (ctx->capture);
    g_clear_object (&ctx->view);
    g_clear_object (&ctx->document);
    pluma_tool_output_panel_unref (ctx->panel);
    g_free (ctx->tool_name);
    g_free (ctx);
}

void
pluma_tool_run (PlumaWindow *window, PlumaToolOutputPanel *panel, PlumaTool *tool)
{
    gchar *cwd;
    GHashTable *env;
    PlumaView *view;
    PlumaDocument *document = NULL;
    PlumaToolCapture *capture;
    RunContext *ctx;
    GHashTableIter it;
    gpointer key, value;
    GList *docs, *l;
    GString *documents_uri, *documents_path;

    cwd = g_get_current_dir ();
    if (cwd == NULL)
        cwd = g_strdup (g_get_home_dir ());

    env = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert (env, g_strdup ("PLUMA_CWD"), g_strdup (cwd));

    view = pluma_window_get_active_view (window);
    if (view != NULL)
        document = PLUMA_DOCUMENT (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));

    if (document != NULL) {
        GtkTextBuffer *buffer = GTK_TEXT_BUFFER (document);
        GtkTextIter iter, end;
        gchar *uri;

        gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));
        g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_LINE_NUMBER"),
                             g_strdup_printf ("%d", gtk_text_iter_get_line (&iter) + 1));

        gtk_text_iter_set_line_offset (&iter, 0);
        end = iter;
        if (!gtk_text_iter_ends_line (&end))
            gtk_text_iter_forward_to_line_end (&end);
        g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_LINE"), gtk_text_iter_get_text (&iter, &end));

        if (g_strcmp0 (tool->input, "selection") != 0 && g_strcmp0 (tool->input, "selection-document") != 0) {
            GtkTextIter sel_start, sel_end;
            if (gtk_text_buffer_get_selection_bounds (buffer, &sel_start, &sel_end))
                g_hash_table_insert (env, g_strdup ("PLUMA_SELECTED_TEXT"),
                                     gtk_text_iter_get_text (&sel_start, &sel_end));
        }

        {
            GtkTextIter word_start, word_end;
            current_word_bounds (buffer, &word_start, &word_end);
            g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_WORD"),
                                 gtk_text_iter_get_text (&word_start, &word_end));
        }

        {
            gchar *mime_type = pluma_document_get_mime_type (document);
            if (mime_type != NULL)
                g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_DOCUMENT_TYPE"), mime_type);
        }

        uri = pluma_document_get_uri (document);
        if (uri != NULL) {
            GFile *gfile = g_file_new_for_uri (uri);
            gchar *scheme = g_file_get_uri_scheme (gfile);
            gchar *name = g_file_get_basename (gfile);

            g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_DOCUMENT_URI"), g_strdup (uri));
            g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_DOCUMENT_NAME"), name);
            if (scheme != NULL)
                g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_DOCUMENT_SCHEME"), scheme);

            if (pluma_utils_uri_has_file_scheme (uri)) {
                gchar *path = g_file_get_path (gfile);
                gchar *dir = g_path_get_dirname (path);

                g_free (cwd);
                cwd = g_strdup (dir);

                g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_DOCUMENT_PATH"), path);
                g_hash_table_insert (env, g_strdup ("PLUMA_CURRENT_DOCUMENT_DIR"), dir);
            }
            g_object_unref (gfile);
            g_free (uri);
        }
    }

    documents_uri = g_string_new (NULL);
    documents_path = g_string_new (NULL);
    docs = pluma_window_get_documents (window);
    for (l = docs; l != NULL; l = l->next) {
        gchar *uri = pluma_document_get_uri (PLUMA_DOCUMENT (l->data));
        if (uri == NULL)
            continue;
        if (documents_uri->len > 0)
            g_string_append_c (documents_uri, ' ');
        g_string_append (documents_uri, uri);

        if (pluma_utils_uri_has_file_scheme (uri)) {
            GFile *gfile = g_file_new_for_uri (uri);
            gchar *path = g_file_get_path (gfile);
            if (path != NULL) {
                if (documents_path->len > 0)
                    g_string_append_c (documents_path, ' ');
                g_string_append (documents_path, path);
            }
            g_free (path);
            g_object_unref (gfile);
        }
        g_free (uri);
    }
    g_list_free (docs);
    g_hash_table_insert (env, g_strdup ("PLUMA_DOCUMENTS_URI"), g_string_free (documents_uri, FALSE));
    g_hash_table_insert (env, g_strdup ("PLUMA_DOCUMENTS_PATH"), g_string_free (documents_path, FALSE));

    capture = pluma_tool_capture_new (tool->path, !pluma_tool_has_hash_bang (tool), cwd);
    g_hash_table_iter_init (&it, env);
    while (g_hash_table_iter_next (&it, &key, &value))
        pluma_tool_capture_set_env (capture, key, value);
    g_hash_table_unref (env);
    g_free (cwd);

    pluma_tool_output_panel_clear (panel);
    if (g_strcmp0 (tool->output, "output-panel") == 0)
        pluma_tool_output_panel_show (panel);
    pluma_tool_output_panel_set_process (panel, capture);

    ctx = g_new0 (RunContext, 1);
    ctx->panel = pluma_tool_output_panel_ref (panel);
    ctx->tool_name = g_strdup (tool->name);
    ctx->view = view != NULL ? g_object_ref (view) : NULL;
    ctx->capture = capture;

    /* Input */
    if (g_strcmp0 (tool->input, "nothing") != 0 && document != NULL) {
        GtkTextBuffer *buffer = GTK_TEXT_BUFFER (document);
        GtkTextIter start, end;
        gboolean have_range = TRUE;

        if (g_strcmp0 (tool->input, "document") == 0) {
            gtk_text_buffer_get_bounds (buffer, &start, &end);
        } else if (g_strcmp0 (tool->input, "selection") == 0 ||
                  g_strcmp0 (tool->input, "selection-document") == 0) {
            if (!gtk_text_buffer_get_selection_bounds (buffer, &start, &end)) {
                if (g_strcmp0 (tool->input, "selection-document") == 0) {
                    gtk_text_buffer_get_bounds (buffer, &start, &end);
                    if (g_strcmp0 (tool->output, "replace-selection") == 0)
                        gtk_text_buffer_select_range (buffer, &start, &end);
                } else {
                    gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
                    end = start;
                }
            }
        } else if (g_strcmp0 (tool->input, "line") == 0) {
            gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
            end = start;
            if (!gtk_text_iter_starts_line (&start))
                gtk_text_iter_set_line_offset (&start, 0);
            if (!gtk_text_iter_ends_line (&end))
                gtk_text_iter_forward_to_line_end (&end);
        } else if (g_strcmp0 (tool->input, "word") == 0) {
            gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
            if (!gtk_text_iter_inside_word (&start)) {
                pluma_tool_output_panel_write (panel, _("You must be inside a word to run this command"),
                                               PLUMA_TOOL_OUTPUT_TAG_ERROR);
                pluma_tool_capture_free (capture);
                g_clear_object (&ctx->view);
                pluma_tool_output_panel_unref (ctx->panel);
                g_free (ctx->tool_name);
                g_free (ctx);
                return;
            }
            current_word_bounds (buffer, &start, &end);
        } else {
            have_range = FALSE;
        }

        if (have_range) {
            gchar *input_text = gtk_text_iter_get_text (&start, &end);
            pluma_tool_capture_set_input (capture, input_text);
            g_free (input_text);
        }
    }

    /* Output */
    if (g_strcmp0 (tool->output, "new-document") == 0) {
        PlumaTab *tab = pluma_window_create_tab (window, TRUE);
        PlumaView *new_view = pluma_tab_get_view (tab);
        GtkTextIter start;

        ctx->document = g_object_ref (pluma_tab_get_document (tab));
        g_clear_object (&ctx->view);
        ctx->view = g_object_ref (new_view);
        ctx->guess_language = TRUE;

        gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (ctx->document), &start);
        ctx->pos_mark = gtk_text_buffer_create_mark (GTK_TEXT_BUFFER (ctx->document), NULL, &start, FALSE);

        gtk_text_buffer_begin_user_action (GTK_TEXT_BUFFER (ctx->document));
        gtk_text_view_set_editable (GTK_TEXT_VIEW (new_view), FALSE);
        gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (new_view), FALSE);

        pluma_tool_capture_set_callbacks (capture, stdout_to_document, stderr_to_panel,
                                          on_begin_execute, on_end_execute, ctx);
    } else if (document != NULL && g_strcmp0 (tool->output, "output-panel") != 0 &&
              g_strcmp0 (tool->output, "nothing") != 0) {
        GtkTextBuffer *buffer = GTK_TEXT_BUFFER (document);
        GtkTextIter pos;

        ctx->document = g_object_ref (document);
        ctx->guess_language = (g_strcmp0 (tool->output, "replace-document") == 0);

        gtk_text_buffer_begin_user_action (buffer);
        gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
        gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);

        if (g_strcmp0 (tool->output, "insert") == 0) {
            gtk_text_buffer_get_iter_at_mark (buffer, &pos, gtk_text_buffer_get_insert (buffer));
        } else if (g_strcmp0 (tool->output, "replace-selection") == 0) {
            gtk_text_buffer_delete_selection (buffer, FALSE, FALSE);
            gtk_text_buffer_get_iter_at_mark (buffer, &pos, gtk_text_buffer_get_insert (buffer));
        } else if (g_strcmp0 (tool->output, "replace-document") == 0) {
            gtk_text_buffer_set_text (buffer, "", -1);
            gtk_text_buffer_get_end_iter (buffer, &pos);
        } else {
            /* append-document, or any other/unknown value */
            gtk_text_buffer_get_end_iter (buffer, &pos);
        }

        ctx->pos_mark = gtk_text_buffer_create_mark (buffer, NULL, &pos, FALSE);

        pluma_tool_capture_set_callbacks (capture, stdout_to_document, stderr_to_panel,
                                          on_begin_execute, on_end_execute, ctx);
    } else if (g_strcmp0 (tool->output, "nothing") != 0) {
        pluma_tool_capture_set_callbacks (capture, stdout_to_panel, stderr_to_panel,
                                          on_begin_execute, on_end_execute, ctx);
    } else {
        pluma_tool_capture_set_callbacks (capture, NULL, stderr_to_panel,
                                          on_begin_execute, on_end_execute, ctx);
    }

    pluma_tool_capture_execute (capture);
}

typedef struct {
    PlumaWindow *window;
    PlumaToolOutputPanel *panel;
    PlumaTool *tool;
    guint pending;
    gboolean had_error;
} SaveThenRun;

static void
on_document_saved (PlumaDocument *doc, gpointer error, gpointer user_data)
{
    SaveThenRun *ctx = user_data;

    g_signal_handlers_disconnect_by_func (doc, on_document_saved, ctx);
    if (error != NULL)
        ctx->had_error = TRUE;

    ctx->pending--;
    if (ctx->pending == 0) {
        if (!ctx->had_error)
            pluma_tool_run (ctx->window, ctx->panel, ctx->tool);
        g_free (ctx);
    }
}

static void
save_documents_then_run (PlumaWindow *window, PlumaToolOutputPanel *panel, PlumaTool *tool, GList *docs)
{
    SaveThenRun *ctx;
    GList *l;

    if (docs == NULL) {
        pluma_tool_run (window, panel, tool);
        return;
    }

    ctx = g_new0 (SaveThenRun, 1);
    ctx->window = window;
    ctx->panel = panel;
    ctx->tool = tool;
    ctx->pending = g_list_length (docs);

    for (l = docs; l != NULL; l = l->next) {
        PlumaDocument *doc = PLUMA_DOCUMENT (l->data);
        g_signal_connect (doc, "saved", G_CALLBACK (on_document_saved), ctx);
        pluma_commands_save_document (window, doc);
    }
}

void
pluma_tool_run_from_menu (PlumaWindow *window, PlumaToolOutputPanel *panel, PlumaTool *tool)
{
    if (g_strcmp0 (tool->save_files, "document") == 0) {
        PlumaDocument *doc = pluma_window_get_active_document (window);
        if (doc != NULL) {
            GList *docs = g_list_prepend (NULL, doc);
            save_documents_then_run (window, panel, tool, docs);
            g_list_free (docs);
            return;
        }
    } else if (g_strcmp0 (tool->save_files, "all") == 0) {
        GList *docs = pluma_window_get_documents (window);
        save_documents_then_run (window, panel, tool, docs);
        g_list_free (docs);
        return;
    }

    pluma_tool_run (window, panel, tool);
}
