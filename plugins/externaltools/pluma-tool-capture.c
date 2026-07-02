#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-tool-capture.h"

#include <string.h>
#include <signal.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>

struct _PlumaToolCapture {
    gchar *command;
    gboolean needs_shell;
    gchar *cwd;
    GHashTable *env;      /* extra name -> value, on top of the inherited environment */
    gchar *input_text;

    GSubprocess *subprocess;
    GCancellable *cancellable;
    gint pending_async;
    gint exit_code;
    gboolean tried_killing;

    PlumaToolCaptureLineFunc  on_stdout_line;
    PlumaToolCaptureLineFunc  on_stderr_line;
    PlumaToolCaptureBeginFunc on_begin_execute;
    PlumaToolCaptureEndFunc   on_end_execute;
    gpointer user_data;
};

PlumaToolCapture *
pluma_tool_capture_new (const gchar *command, gboolean needs_shell, const gchar *cwd)
{
    PlumaToolCapture *capture = g_new0 (PlumaToolCapture, 1);
    capture->command = g_strdup (command);
    capture->needs_shell = needs_shell;
    capture->cwd = g_strdup (cwd);
    capture->env = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    return capture;
}

void
pluma_tool_capture_free (PlumaToolCapture *capture)
{
    if (capture == NULL)
        return;

    if (capture->cancellable != NULL) {
        g_cancellable_cancel (capture->cancellable);
        g_object_unref (capture->cancellable);
    }
    g_clear_object (&capture->subprocess);
    g_hash_table_unref (capture->env);
    g_free (capture->command);
    g_free (capture->cwd);
    g_free (capture->input_text);
    g_free (capture);
}

void
pluma_tool_capture_set_env (PlumaToolCapture *capture, const gchar *name, const gchar *value)
{
    g_hash_table_insert (capture->env, g_strdup (name), g_strdup (value));
}

void
pluma_tool_capture_set_input (PlumaToolCapture *capture, const gchar *text)
{
    g_free (capture->input_text);
    capture->input_text = (text != NULL) ? g_strdup (text) : NULL;
}

void
pluma_tool_capture_set_callbacks (PlumaToolCapture         *capture,
                                   PlumaToolCaptureLineFunc  stdout_line,
                                   PlumaToolCaptureLineFunc  stderr_line,
                                   PlumaToolCaptureBeginFunc begin_execute,
                                   PlumaToolCaptureEndFunc   end_execute,
                                   gpointer                  user_data)
{
    capture->on_stdout_line = stdout_line;
    capture->on_stderr_line = stderr_line;
    capture->on_begin_execute = begin_execute;
    capture->on_end_execute = end_execute;
    capture->user_data = user_data;
}

static void
maybe_finish (PlumaToolCapture *capture)
{
    capture->pending_async--;
    if (capture->pending_async <= 0 && capture->on_end_execute != NULL)
        capture->on_end_execute (capture, capture->exit_code, capture->user_data);
}

/* Tool output is arbitrary bytes, not guaranteed UTF-8 (and a multibyte
 * sequence can be split across a read boundary). GtkTextBuffer asserts on
 * invalid UTF-8, so make sure whatever we hand to callbacks is always
 * valid: try as-is, then the locale encoding, then replace bad bytes. */
static gchar *
sanitize_utf8 (const gchar *text)
{
    gchar *converted;
    GString *out;
    const gchar *p, *end;

    if (g_utf8_validate (text, -1, NULL))
        return g_strdup (text);

    converted = g_locale_to_utf8 (text, -1, NULL, NULL, NULL);
    if (converted != NULL) {
        if (g_utf8_validate (converted, -1, NULL))
            return converted;
        g_free (converted);
    }

    out = g_string_new (NULL);
    p = text;
    while (!g_utf8_validate (p, -1, &end)) {
        g_string_append_len (out, p, end - p);
        g_string_append (out, "\xef\xbf\xbd"); /* U+FFFD replacement character */
        p = end + 1;
    }
    g_string_append (out, p);
    return g_string_free (out, FALSE);
}

static void
on_line_read (GObject *source, GAsyncResult *result, gpointer user_data)
{
    PlumaToolCapture *capture = user_data;
    GDataInputStream *stream = G_DATA_INPUT_STREAM (source);
    GError *error = NULL;
    gsize length = 0;
    gchar *line = g_data_input_stream_read_line_finish (stream, result, &length, &error);
    gboolean is_stdout = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (stream), "pluma-tool-is-stdout"));

    if (error != NULL)
        g_error_free (error);

    if (line != NULL) {
        gchar *safe_line = sanitize_utf8 (line);
        gchar *with_nl = g_strconcat (safe_line, "\n", NULL);
        PlumaToolCaptureLineFunc func = is_stdout ? capture->on_stdout_line : capture->on_stderr_line;

        if (func != NULL)
            func (capture, with_nl, capture->user_data);

        g_free (with_nl);
        g_free (safe_line);
        g_free (line);
        g_data_input_stream_read_line_async (stream, G_PRIORITY_DEFAULT, capture->cancellable, on_line_read, capture);
    } else {
        g_object_unref (stream);
        maybe_finish (capture);
    }
}

static void
start_reading (PlumaToolCapture *capture, GInputStream *pipe, gboolean is_stdout)
{
    GDataInputStream *stream = g_data_input_stream_new (pipe);
    g_object_set_data (G_OBJECT (stream), "pluma-tool-is-stdout", GINT_TO_POINTER (is_stdout));
    g_data_input_stream_read_line_async (stream, G_PRIORITY_DEFAULT, capture->cancellable, on_line_read, capture);
}

static void
on_stdin_write_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GOutputStream *stream = G_OUTPUT_STREAM (source);
    GError *error = NULL;

    g_output_stream_write_all_finish (stream, result, NULL, &error);
    if (error != NULL)
        g_error_free (error);

    g_output_stream_close (stream, NULL, NULL);
    g_object_unref (stream);
}

static void
on_wait_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
    PlumaToolCapture *capture = user_data;
    GError *error = NULL;

    g_subprocess_wait_finish (capture->subprocess, result, &error);
    if (error != NULL)
        g_error_free (error);

    capture->exit_code = g_subprocess_get_exit_status (capture->subprocess);
    maybe_finish (capture);
}

void
pluma_tool_capture_execute (PlumaToolCapture *capture)
{
    GPtrArray *argv;
    GSubprocessLauncher *launcher;
    GSubprocessFlags flags;
    GError *error = NULL;
    GHashTableIter iter;
    gpointer key, value;

    if (capture->command == NULL)
        return;

    argv = g_ptr_array_new ();
    if (capture->needs_shell) {
        g_ptr_array_add (argv, (gpointer) "/bin/sh");
        g_ptr_array_add (argv, (gpointer) capture->command);
    } else {
        g_ptr_array_add (argv, (gpointer) capture->command);
    }
    g_ptr_array_add (argv, NULL);

    flags = G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE;
    if (capture->input_text != NULL)
        flags |= G_SUBPROCESS_FLAGS_STDIN_PIPE;

    launcher = g_subprocess_launcher_new (flags);
    if (capture->cwd != NULL)
        g_subprocess_launcher_set_cwd (launcher, capture->cwd);

    g_hash_table_iter_init (&iter, capture->env);
    while (g_hash_table_iter_next (&iter, &key, &value))
        g_subprocess_launcher_setenv (launcher, key, value, TRUE);

    capture->subprocess = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv->pdata, &error);
    g_object_unref (launcher);
    g_ptr_array_free (argv, TRUE);

    if (capture->subprocess == NULL) {
        gchar *message = g_strdup_printf (_("Could not execute command: %s"), error->message);
        if (capture->on_stderr_line != NULL)
            capture->on_stderr_line (capture, message, capture->user_data);
        g_free (message);
        g_error_free (error);
        if (capture->on_end_execute != NULL)
            capture->on_end_execute (capture, -1, capture->user_data);
        return;
    }

    capture->cancellable = g_cancellable_new ();

    if (capture->on_begin_execute != NULL)
        capture->on_begin_execute (capture, capture->user_data);

    /* Gate end-execute on process exit plus both output streams reaching
     * EOF, so all stdout/stderr lines are delivered before it fires. */
    capture->pending_async = 3;

    start_reading (capture, g_subprocess_get_stdout_pipe (capture->subprocess), TRUE);
    start_reading (capture, g_subprocess_get_stderr_pipe (capture->subprocess), FALSE);

    if (capture->input_text != NULL) {
        GOutputStream *stdin_pipe = g_object_ref (g_subprocess_get_stdin_pipe (capture->subprocess));
        g_output_stream_write_all_async (stdin_pipe, capture->input_text, strlen (capture->input_text),
                                         G_PRIORITY_DEFAULT, capture->cancellable, on_stdin_write_done, NULL);
    }

    g_subprocess_wait_async (capture->subprocess, capture->cancellable, on_wait_done, capture);
}

void
pluma_tool_capture_stop (PlumaToolCapture *capture)
{
    if (capture->subprocess == NULL)
        return;

    if (!capture->tried_killing) {
        g_subprocess_send_signal (capture->subprocess, SIGTERM);
        capture->tried_killing = TRUE;
    } else {
        g_subprocess_send_signal (capture->subprocess, SIGKILL);
    }
}
