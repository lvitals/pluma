#include "pluma-tool-library.h"

#include <string.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

static gchar *
read_property (const gchar *contents, const gchar *name)
{
    gchar **lines = g_strsplit (contents, "\n", -1);
    gchar *prefix = g_strdup_printf ("# %s=", name);
    gchar *value = NULL;
    guint i;
    for (i = 0; lines[i] != NULL; i++)
        if (g_str_has_prefix (lines[i], prefix)) {
            value = g_strdup (lines[i] + strlen (prefix));
            break;
        }
    g_free (prefix);
    g_strfreev (lines);
    return value;
}

PlumaTool *
pluma_tool_load (const gchar *path, GError **error)
{
    gchar *contents = NULL;
    PlumaTool *tool;
    gchar *languages;
    if (!g_file_get_contents (path, &contents, NULL, error))
        return NULL;
    if (strstr (contents, "# [Pluma Tool]") == NULL) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Missing [Pluma Tool] metadata in %s", path);
        g_free (contents);
        return NULL;
    }
    tool = g_new0 (PlumaTool, 1);
    tool->path = g_strdup (path);
    tool->filename = g_path_get_basename (path);
    tool->name = read_property (contents, "Name");
    tool->comment = read_property (contents, "Comment");
    tool->input = read_property (contents, "Input");
    tool->output = read_property (contents, "Output");
    tool->save_files = read_property (contents, "Save-files");
    tool->shortcut = read_property (contents, "Shortcut");
    tool->applicability = read_property (contents, "Applicability");
    languages = read_property (contents, "Languages");
    if (languages != NULL && *languages != '\0')
        tool->languages = g_strsplit_set (languages, ",; ", -1);
    if (tool->name == NULL) tool->name = g_strdup (tool->filename);
    if (tool->input == NULL) tool->input = g_strdup ("nothing");
    if (tool->output == NULL) tool->output = g_strdup ("output-panel");
    if (tool->save_files == NULL) tool->save_files = g_strdup ("nothing");
    if (tool->applicability == NULL) tool->applicability = g_strdup ("all");
    g_free (languages);
    g_free (contents);
    return tool;
}

void
pluma_tool_free (PlumaTool *tool)
{
    if (tool == NULL)
        return;
    g_free (tool->path); g_free (tool->filename); g_free (tool->name); g_free (tool->comment);
    g_free (tool->input); g_free (tool->output); g_free (tool->save_files);
    g_free (tool->shortcut); g_free (tool->applicability); g_strfreev (tool->languages);
    g_free (tool);
}

static void
load_dir (GHashTable *by_name, const gchar *directory, gboolean local)
{
    GDir *dir = g_dir_open (directory, 0, NULL);
    const gchar *name;
    if (dir == NULL)
        return;
    while ((name = g_dir_read_name (dir)) != NULL) {
        gchar *path = g_build_filename (directory, name, NULL);
        PlumaTool *existing;

        if (!g_file_test (path, G_FILE_TEST_IS_REGULAR) ||
            !g_file_test (path, G_FILE_TEST_IS_EXECUTABLE)) {
            g_free (path);
            continue;
        }

        existing = g_hash_table_lookup (by_name, name);
        if (existing != NULL) {
            if (local)
                existing->is_local = TRUE;
            else
                existing->is_global = TRUE;
        } else {
            PlumaTool *tool = pluma_tool_load (path, NULL);
            if (tool != NULL) {
                tool->is_local = local;
                tool->is_global = !local;
                g_hash_table_insert (by_name, g_strdup (name), tool);
            }
        }
        g_free (path);
    }
    g_dir_close (dir);
}

GPtrArray *
pluma_tool_library_load (const gchar *system_dir, const gchar *user_dir)
{
    GHashTable *by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray *tools = g_ptr_array_new_with_free_func ((GDestroyNotify) pluma_tool_free);
    GHashTableIter iter;
    gpointer key, value;

    /* User tools override system tools with the same file name; a system
     * copy loaded second only flags is_global on the already-loaded tool. */
    load_dir (by_name, user_dir, TRUE);
    load_dir (by_name, system_dir, FALSE);

    g_hash_table_iter_init (&iter, by_name);
    while (g_hash_table_iter_next (&iter, &key, &value))
        g_ptr_array_add (tools, value);

    g_hash_table_unref (by_name);
    return tools;
}

gchar *
pluma_tool_get_script (PlumaTool *tool)
{
    gchar *contents = NULL;
    gchar **lines;
    GPtrArray *out;
    guint i = 0;
    gboolean found_marker = FALSE;
    gchar *joined;

    if (tool->path == NULL || !g_file_get_contents (tool->path, &contents, NULL, NULL))
        return g_strdup ("#!/bin/sh\n");

    lines = g_strsplit (contents, "\n", -1);
    g_free (contents);

    out = g_ptr_array_new ();

    /* Lines before the "# [Pluma Tool]" marker (shebang, modeline, etc). */
    for (; lines[i] != NULL; i++) {
        if (g_str_has_prefix (lines[i], "# [Pluma Tool]")) {
            found_marker = TRUE;
            i++;
            break;
        }
        g_ptr_array_add (out, lines[i]);
    }

    /* Properties block: silently skip "## comment" and "# Key=value"
     * lines; the first line matching neither ends the block. */
    if (found_marker) {
        for (; lines[i] != NULL; i++) {
            gchar *trimmed;

            if (g_str_has_prefix (lines[i], "##"))
                continue;
            if (g_str_has_prefix (lines[i], "# ") && strchr (lines[i], '=') != NULL)
                continue;

            trimmed = g_strdup (lines[i]);
            g_strstrip (trimmed);
            if (*trimmed != '\0')
                g_ptr_array_add (out, lines[i]);
            g_free (trimmed);
            i++;
            break;
        }
    }

    /* Everything after the block, verbatim. */
    for (; lines[i] != NULL; i++)
        g_ptr_array_add (out, lines[i]);

    g_ptr_array_add (out, NULL);
    joined = g_strjoinv ("\n", (gchar **) out->pdata);
    g_ptr_array_free (out, TRUE);
    g_strfreev (lines);
    return joined;
}

gboolean
pluma_tool_has_hash_bang (PlumaTool *tool)
{
    gchar *contents = NULL;
    gchar **lines;
    gboolean result = TRUE;
    guint i;

    if (tool->path == NULL || !g_file_get_contents (tool->path, &contents, NULL, NULL))
        return TRUE;

    lines = g_strsplit (contents, "\n", -1);
    g_free (contents);

    for (i = 0; lines[i] != NULL; i++) {
        gchar *trimmed = g_strdup (lines[i]);
        g_strstrip (trimmed);
        if (*trimmed == '\0') {
            g_free (trimmed);
            continue;
        }
        result = g_str_has_prefix (lines[i], "#!");
        g_free (trimmed);
        break;
    }

    g_strfreev (lines);
    return result;
}

static void
dump_properties (GString *out, PlumaTool *tool)
{
    g_string_append (out, "# [Pluma Tool]\n");
    if (tool->name != NULL)
        g_string_append_printf (out, "# Name=%s\n", tool->name);
    if (tool->comment != NULL)
        g_string_append_printf (out, "# Comment=%s\n", tool->comment);
    if (tool->shortcut != NULL)
        g_string_append_printf (out, "# Shortcut=%s\n", tool->shortcut);
    if (tool->input != NULL)
        g_string_append_printf (out, "# Input=%s\n", tool->input);
    if (tool->output != NULL)
        g_string_append_printf (out, "# Output=%s\n", tool->output);
    if (tool->save_files != NULL)
        g_string_append_printf (out, "# Save-files=%s\n", tool->save_files);
    if (tool->applicability != NULL)
        g_string_append_printf (out, "# Applicability=%s\n", tool->applicability);
    if (tool->languages != NULL && tool->languages[0] != NULL) {
        gchar *joined = g_strjoinv (",", tool->languages);
        g_string_append_printf (out, "# Languages=%s\n", joined);
        g_free (joined);
    }
}

gboolean
pluma_tool_save_with_script (PlumaTool *tool, const gchar *user_dir, const gchar *script)
{
    gchar **lines;
    GString *header, *content, *out;
    gboolean in_header = TRUE;
    guint i;
    gboolean ok;

    if (tool->filename == NULL)
        pluma_tool_autoset_filename (tool, user_dir);

    header = g_string_new (NULL);
    content = g_string_new (NULL);

    lines = g_strsplit (script != NULL ? script : "", "\n", -1);
    for (i = 0; lines[i] != NULL; i++) {
        gchar *trimmed;

        if (!in_header) {
            g_string_append_printf (content, "%s\n", lines[i]);
            continue;
        }

        if (g_str_has_prefix (lines[i], "#!")) {
            g_string_append_printf (header, "%s\n", lines[i]);
            continue;
        }

        trimmed = g_strdup (lines[i]);
        g_strstrip (trimmed);
        if (g_str_has_prefix (trimmed, "#") &&
            (strstr (lines[i], "-*-") != NULL || strstr (lines[i], "ex:") != NULL ||
             strstr (lines[i], "vi:") != NULL || strstr (lines[i], "vim:") != NULL)) {
            g_string_append_printf (header, "%s\n", lines[i]);
            g_free (trimmed);
            continue;
        }
        g_free (trimmed);

        g_string_append_printf (content, "%s\n", lines[i]);
        in_header = FALSE;
    }
    g_strfreev (lines);

    out = g_string_new (header->str);
    dump_properties (out, tool);
    g_string_append_c (out, '\n');
    g_string_append (out, content->str);

    g_string_free (header, TRUE);
    g_string_free (content, TRUE);

    g_mkdir_with_parents (user_dir, 0700);
    g_free (tool->path);
    tool->path = g_build_filename (user_dir, tool->filename, NULL);

    ok = g_file_set_contents (tool->path, out->str, out->len, NULL);
    g_string_free (out, TRUE);

    if (ok) {
        g_chmod (tool->path, 0750);
        tool->is_local = TRUE;
    }
    return ok;
}

gboolean
pluma_tool_save (PlumaTool *tool, const gchar *user_dir)
{
    gchar *script = pluma_tool_get_script (tool);
    gboolean ok = pluma_tool_save_with_script (tool, user_dir, script);
    g_free (script);
    return ok;
}

PlumaTool *
pluma_tool_new (void)
{
    PlumaTool *tool = g_new0 (PlumaTool, 1);
    tool->input = g_strdup ("nothing");
    tool->output = g_strdup ("output-panel");
    tool->save_files = g_strdup ("nothing");
    tool->applicability = g_strdup ("all");
    return tool;
}

void
pluma_tool_autoset_filename (PlumaTool *tool, const gchar *user_dir)
{
    gchar *basename, *p;
    gchar *name_try;
    guint i = 2;

    if (tool->filename != NULL)
        return;

    basename = g_utf8_strdown (tool->name != NULL ? tool->name : "tool", -1);
    for (p = basename; *p != '\0'; p++)
        if (*p == ' ' || *p == '/')
            *p = '-';

    name_try = g_strdup (basename);
    while (TRUE) {
        gchar *candidate = g_build_filename (user_dir, name_try, NULL);
        gboolean exists = g_file_test (candidate, G_FILE_TEST_EXISTS);
        g_free (candidate);
        if (!exists)
            break;
        g_free (name_try);
        name_try = g_strdup_printf ("%s-%u", basename, i++);
    }

    g_free (basename);
    tool->filename = name_try;
    g_free (tool->path);
    tool->path = g_build_filename (user_dir, tool->filename, NULL);
}

gboolean
pluma_tool_delete (PlumaTool *tool, const gchar *user_dir)
{
    gchar *path;
    gboolean ok = TRUE;

    if (tool->filename == NULL)
        return TRUE;

    path = g_build_filename (user_dir, tool->filename, NULL);
    if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
        ok = (g_unlink (path) == 0);
    g_free (path);
    return ok;
}

gboolean
pluma_tool_revert (PlumaTool *tool, const gchar *user_dir, const gchar *system_dir)
{
    gchar *user_path, *sys_path;
    PlumaTool *reloaded;

    if (tool->filename == NULL)
        return FALSE;

    user_path = g_build_filename (user_dir, tool->filename, NULL);
    sys_path = g_build_filename (system_dir, tool->filename, NULL);

    if (!g_file_test (user_path, G_FILE_TEST_IS_REGULAR) ||
        !g_file_test (sys_path, G_FILE_TEST_IS_REGULAR)) {
        g_free (user_path);
        g_free (sys_path);
        return FALSE;
    }

    g_unlink (user_path);
    g_free (user_path);

    reloaded = pluma_tool_load (sys_path, NULL);
    g_free (sys_path);

    if (reloaded == NULL)
        return FALSE;

    g_free (tool->path); tool->path = reloaded->path; reloaded->path = NULL;
    g_free (tool->name); tool->name = reloaded->name; reloaded->name = NULL;
    g_free (tool->comment); tool->comment = reloaded->comment; reloaded->comment = NULL;
    g_free (tool->input); tool->input = reloaded->input; reloaded->input = NULL;
    g_free (tool->output); tool->output = reloaded->output; reloaded->output = NULL;
    g_free (tool->save_files); tool->save_files = reloaded->save_files; reloaded->save_files = NULL;
    g_free (tool->shortcut); tool->shortcut = reloaded->shortcut; reloaded->shortcut = NULL;
    g_free (tool->applicability); tool->applicability = reloaded->applicability; reloaded->applicability = NULL;
    g_strfreev (tool->languages); tool->languages = reloaded->languages; reloaded->languages = NULL;
    tool->is_local = FALSE;
    tool->is_global = TRUE;

    pluma_tool_free (reloaded);
    return TRUE;
}
