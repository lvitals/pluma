/* Integrated project search for Pluma. */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include "pluma-project-search-panel.h"
#include "pluma-window-private.h"
#include "pluma-document.h"
#include "pluma-tab.h"
#include "pluma-settings.h"
#include "pluma-file-io.h"

enum { COL_TEXT, COL_PATH, COL_LINE, COL_COLUMN, COL_IS_FILE, N_COLS };

struct _PlumaProjectSearchPanel
{
	GtkBox parent_instance;
	PlumaWindow *window;
	GtkWidget *search_entry, *replace_entry, *include_entry, *exclude_entry;
	GtkWidget *case_button, *word_button, *regex_button, *ignored_button;
	GtkWidget *spinner, *status, *tree, *replace_bar;
	GtkTreeStore *store;
	GSubprocess *process;
	GCancellable *cancellable;
	GSettings *settings;
	guint serial;
	gboolean destroyed;
	gint return_page_id;
};

G_DEFINE_TYPE (PlumaProjectSearchPanel, pluma_project_search_panel, GTK_TYPE_BOX)

static GFile *
get_root (PlumaProjectSearchPanel *panel)
{
	GFile *root = NULL;
	PlumaDocument *doc = pluma_window_get_active_document (panel->window);

	if (doc != NULL)
	{
		GFile *location = pluma_document_get_location (doc);
		if (location != NULL)
		{
			root = g_file_get_parent (location);
			g_object_unref (location);
		}
	}

	if (root == NULL)
		root = _pluma_window_get_default_location (panel->window);

	if (root != NULL && g_file_query_file_type (root, 0, NULL) == G_FILE_TYPE_DIRECTORY)
		return root;

	if (root != NULL)
		g_object_unref (root);

	{
		gchar *cwd = g_get_current_dir ();
		root = g_file_new_for_path (cwd);
		g_free (cwd);
		return root;
	}
}

static void
set_busy (PlumaProjectSearchPanel *panel, gboolean busy)
{
	gtk_widget_set_sensitive (panel->search_entry, !busy);
	if (busy)
		gtk_spinner_start (GTK_SPINNER (panel->spinner));
	else
		gtk_spinner_stop (GTK_SPINNER (panel->spinner));
}

static void
add_result (PlumaProjectSearchPanel *panel, GHashTable *parents,
	        const gchar *root, const gchar *path, gint line, gint column, const gchar *text)
{
	GtkTreeIter parent, child;
	gpointer value = g_hash_table_lookup (parents, path);
	gchar *relative, *label;

	if (value == NULL)
	{
		relative = g_filename_display_name (path + (g_str_has_prefix (path, root) ? strlen (root) + 1 : 0));
		gtk_tree_store_append (panel->store, &parent, NULL);
		gtk_tree_store_set (panel->store, &parent, COL_TEXT, relative, COL_PATH, path,
		                    COL_IS_FILE, TRUE, -1);
		value = gtk_tree_iter_copy (&parent);
		g_hash_table_insert (parents, g_strdup (path), value);
		g_free (relative);
	}
	else
		parent = *(GtkTreeIter *) value;

	label = g_strdup_printf ("%d:%d  %s", line, column, text);
	gtk_tree_store_append (panel->store, &child, &parent);
	gtk_tree_store_set (panel->store, &child, COL_TEXT, label, COL_PATH, path,
	                    COL_LINE, line, COL_COLUMN, column, COL_IS_FILE, FALSE, -1);
	g_free (label);
}

static gchar *
decode_text (const gchar *raw_text, gsize raw_len, const gchar *path, GHashTable *encoding_cache)
{
	const PlumaEncoding *enc = g_hash_table_lookup (encoding_cache, path);

	if (enc == NULL)
	{
		gsize dummy_len = 0;
		gchar *contents = pluma_file_read_with_encoding (path, &dummy_len, &enc, NULL);
		g_free (contents);

		if (enc == NULL)
			enc = pluma_encoding_get_utf8 ();

		g_hash_table_insert (encoding_cache, g_strdup (path), (gpointer) enc);
	}

	gchar *utf8 = NULL;
	const gchar *charset = pluma_encoding_get_charset (enc);

	/* Since Ripgrep/Grep output UTF-8 matches for UTF-8 and UTF-16/32 files,
	 * we only need to decode if the file's encoding is not UTF-* */
	if (g_ascii_strncasecmp (charset, "UTF-", 4) == 0)
	{
		utf8 = g_utf8_make_valid (raw_text, raw_len);
	}
	else
	{
		gsize bytes_written = 0;
		utf8 = g_convert (raw_text, raw_len, "UTF-8", charset, NULL, &bytes_written, NULL);
		if (utf8 == NULL)
		{
			utf8 = g_utf8_make_valid (raw_text, raw_len);
		}
	}

	return utf8;
}

static void
search_finished (GObject *source, GAsyncResult *result, gpointer user_data)
{
	PlumaProjectSearchPanel *panel = user_data;
	GError *error = NULL;
	gchar *err = NULL;
	GFile *root_file;
	gchar *root;
	GHashTable *parents;
	guint count = 0;
	GBytes *stdout_bytes = NULL, *stderr_bytes = NULL;

	if (!g_subprocess_communicate_finish (G_SUBPROCESS (source), result, &stdout_bytes, &stderr_bytes, &error))
	{
		if (G_SUBPROCESS (source) != panel->process)
		{
			g_clear_error (&error); g_object_unref (panel); return;
		}
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			gtk_label_set_text (GTK_LABEL (panel->status), error->message);
		g_clear_error (&error);
		goto done;
	}
	if (panel->destroyed)
	{
		if (stdout_bytes != NULL) g_bytes_unref (stdout_bytes);
		if (stderr_bytes != NULL) g_bytes_unref (stderr_bytes);
		g_object_unref (panel);
		return;
	}
	if (G_SUBPROCESS (source) != panel->process)
	{
		if (stdout_bytes != NULL) g_bytes_unref (stdout_bytes);
		if (stderr_bytes != NULL) g_bytes_unref (stderr_bytes);
		g_object_unref (panel);
		return;
	}

	err = pluma_file_bytes_to_utf8 (stderr_bytes);

	root_file = get_root (panel);
	root = g_file_get_path (root_file);
	g_object_unref (root_file);
	parents = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) gtk_tree_iter_free);

	if (stdout_bytes != NULL)
	{
		gsize size = 0;
		const gchar *data = g_bytes_get_data (stdout_bytes, &size);
		GHashTable *encoding_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

		const gchar *line_start = data;
		const gchar *data_end = data + size;

		while (line_start < data_end)
		{
			const gchar *line_end = memchr (line_start, '\n', data_end - line_start);
			if (line_end == NULL)
				line_end = data_end;

			gsize line_len = line_end - line_start;
			if (line_len > 0 && line_start[line_len - 1] == '\r')
				line_len--;

			const gchar *p1 = memchr (line_start, ':', line_len);
			if (p1 != NULL)
			{
				const gchar *p2 = memchr (p1 + 1, ':', line_len - (p1 + 1 - line_start));
				if (p2 != NULL)
				{
					const gchar *p3 = memchr (p2 + 1, ':', line_len - (p2 + 1 - line_start));
					const gchar *text_start = NULL;
					gchar *path = g_strndup (line_start, p1 - line_start);
					gint line_num = atoi (p1 + 1);
					gint col_num = 0;
					gsize text_len = 0;
					gboolean is_rg = FALSE;

					if (p3 != NULL)
					{
						gboolean line_digits = TRUE;
						for (const gchar *c = p1 + 1; c < p2; c++)
						{
							if (*c < '0' || *c > '9') { line_digits = FALSE; break; }
						}
						gboolean col_digits = TRUE;
						for (const gchar *c = p2 + 1; c < p3; c++)
						{
							if (*c < '0' || *c > '9') { col_digits = FALSE; break; }
						}
						if (line_digits && col_digits)
							is_rg = TRUE;
					}

					if (is_rg)
					{
						col_num = atoi (p2 + 1);
						text_start = p3 + 1;
						text_len = line_start + line_len - text_start;
					}
					else
					{
						col_num = 1;
						text_start = p2 + 1;
						text_len = line_start + line_len - text_start;
					}

					gchar *absolute = g_canonicalize_filename (path, root);
					gchar *decoded_text = decode_text (text_start, text_len, absolute, encoding_cache);

					add_result (panel, parents, root, absolute, line_num, col_num, decoded_text);

					g_free (absolute);
					g_free (path);
					g_free (decoded_text);
					count++;
				}
			}

			line_start = line_end + 1;
		}

		g_hash_table_unref (encoding_cache);
		g_bytes_unref (stdout_bytes);
	}

	gtk_tree_view_expand_all (GTK_TREE_VIEW (panel->tree));
	{
		gchar *status = (count == 0 && err != NULL && *err != '\0') ?
		                g_strdup (g_strstrip (err)) :
		                g_strdup_printf (ngettext ("%u result", "%u results", count), count);
		gtk_label_set_text (GTK_LABEL (panel->status), status);
		g_free (status);
	}
	g_hash_table_unref (parents); g_free (root);

done:
	set_busy (panel, FALSE);
	g_clear_object (&panel->process);
	g_clear_object (&panel->cancellable);
	g_free (err);
	g_object_unref (panel);
}

static void
append_globs (GPtrArray *args, const gchar *value, gboolean exclude)
{
	gchar **parts = g_strsplit_set (value, ",;", -1);
	for (guint i = 0; parts[i] != NULL; i++)
	{
		gchar *item = g_strstrip (parts[i]);
		if (*item)
		{
			g_ptr_array_add (args, g_strdup ("-g"));
			g_ptr_array_add (args, g_strdup_printf (exclude ? "!%s" : "%s", item));
		}
	}
	g_strfreev (parts);
}

static void
append_grep_globs (GPtrArray *args, const gchar *value, gboolean exclude)
{
	gchar **parts = g_strsplit_set (value, ",;", -1);
	for (guint i = 0; parts[i] != NULL; i++)
	{
		gchar *item = g_strstrip (parts[i]);
		if (*item)
			g_ptr_array_add (args, g_strdup_printf (exclude ? "--exclude=%s" : "--include=%s", item));
	}
	g_strfreev (parts);
}

static void
get_multichar_patterns (const gchar *query, gboolean is_regex, gchar **out_rg, gchar **out_grep)
{
	gchar *utf8_pat = NULL;
	gchar *latin1_pat_rg = NULL;
	gchar *latin1_pat_grep = NULL;

	if (is_regex)
		utf8_pat = g_strdup (query);
	else
		utf8_pat = g_regex_escape_string (query, -1);

	/* Try to convert to Latin1/CP1252 */
	gsize bytes_written = 0;
	gchar *latin1_bytes = g_convert (query, -1, "ISO-8859-1", "UTF-8", NULL, &bytes_written, NULL);

	if (latin1_bytes != NULL)
	{
		/* Check if it actually contains any non-ASCII characters */
		gboolean has_non_ascii = FALSE;
		for (gsize i = 0; i < bytes_written; i++)
		{
			if ((guchar)latin1_bytes[i] >= 128)
			{
				has_non_ascii = TRUE;
				break;
			}
		}

		if (has_non_ascii)
		{
			GString *s_rg = g_string_new ("(?-u)");
			GString *s_grep = g_string_new ("");
			for (gsize i = 0; i < bytes_written; i++)
			{
				guchar c = (guchar)latin1_bytes[i];
				if (c >= 128)
				{
					g_string_append_printf (s_rg, "\\x%02x", c);
					g_string_append_c (s_grep, (gchar)c);
				}
				else
				{
					if (!is_regex && strchr ("\\^$.|?*+()[]{}", c) != NULL)
					{
						g_string_append_c (s_rg, '\\');
						g_string_append_c (s_grep, '\\');
					}
					g_string_append_c (s_rg, c);
					g_string_append_c (s_grep, c);
				}
			}
			latin1_pat_rg = g_string_free (s_rg, FALSE);
			latin1_pat_grep = g_string_free (s_grep, FALSE);
		}
		g_free (latin1_bytes);
	}

	if (latin1_pat_rg != NULL)
	{
		*out_rg = g_strdup_printf ("(?:%s)|(?:%s)", utf8_pat, latin1_pat_rg);
		*out_grep = g_strdup_printf ("(?:%s)|(?:%s)", utf8_pat, latin1_pat_grep);
		g_free (utf8_pat);
		g_free (latin1_pat_rg);
		g_free (latin1_pat_grep);
	}
	else
	{
		*out_rg = utf8_pat;
		*out_grep = g_strdup (utf8_pat);
	}
}

static void
start_search (PlumaProjectSearchPanel *panel)
{
	const gchar *query = gtk_entry_get_text (GTK_ENTRY (panel->search_entry));
	GFile *root_file;
	gchar *root;
	GPtrArray *args;
	gchar *rg_path;
	GSubprocessLauncher *launcher;
	GError *error = NULL;
	gchar *pattern_rg = NULL;
	gchar *pattern_grep = NULL;
	gboolean is_regex_search;
	gboolean use_regex_matching;

	if (query == NULL || *query == '\0')
		return;

	g_settings_set_string (panel->settings, "project-search-text", query);
	g_settings_set_string (panel->settings, "project-search-include", gtk_entry_get_text (GTK_ENTRY (panel->include_entry)));
	g_settings_set_string (panel->settings, "project-search-exclude", gtk_entry_get_text (GTK_ENTRY (panel->exclude_entry)));
	g_settings_set_boolean (panel->settings, "project-search-match-case", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->case_button)));
	g_settings_set_boolean (panel->settings, "project-search-whole-word", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->word_button)));
	g_settings_set_boolean (panel->settings, "project-search-regex", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->regex_button)));
	g_settings_set_boolean (panel->settings, "project-search-include-ignored", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->ignored_button)));

	if (panel->cancellable) g_cancellable_cancel (panel->cancellable);
	g_clear_object (&panel->process);
	g_clear_object (&panel->cancellable);
	panel->serial++;
	gtk_tree_store_clear (panel->store);
	gtk_label_set_text (GTK_LABEL (panel->status), _("Searching…"));
	set_busy (panel, TRUE);

	is_regex_search = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->regex_button));
	get_multichar_patterns (query, is_regex_search, &pattern_rg, &pattern_grep);
	use_regex_matching = is_regex_search || g_str_has_prefix (pattern_rg, "(?:");

	root_file = get_root (panel); root = g_file_get_path (root_file); g_object_unref (root_file);
	args = g_ptr_array_new_with_free_func (g_free);
	rg_path = g_find_program_in_path ("rg");
	if (rg_path != NULL)
	{
		g_ptr_array_add (args, rg_path); g_ptr_array_add (args, g_strdup ("--vimgrep"));
		g_ptr_array_add (args, g_strdup ("--color=never")); g_ptr_array_add (args, g_strdup ("--no-heading"));
		g_ptr_array_add (args, g_strdup ("--max-count=10000"));
		if (!use_regex_matching) g_ptr_array_add (args, g_strdup ("-F"));
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->case_button))) g_ptr_array_add (args, g_strdup ("-s"));
		else g_ptr_array_add (args, g_strdup ("-i"));
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->word_button))) g_ptr_array_add (args, g_strdup ("-w"));
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->ignored_button))) g_ptr_array_add (args, g_strdup ("--no-ignore"));
		append_globs (args, gtk_entry_get_text (GTK_ENTRY (panel->include_entry)), FALSE);
		append_globs (args, gtk_entry_get_text (GTK_ENTRY (panel->exclude_entry)), TRUE);
		g_ptr_array_add (args, g_strdup ("--")); g_ptr_array_add (args, pattern_rg);
		g_ptr_array_add (args, g_strdup (".")); g_ptr_array_add (args, NULL);
	}
	else
	{
		g_ptr_array_add (args, g_strdup ("grep")); g_ptr_array_add (args, g_strdup ("-RanH"));
		g_ptr_array_add (args, g_strdup ("--exclude-dir=.git"));
		if (!use_regex_matching) g_ptr_array_add (args, g_strdup ("-F")); else g_ptr_array_add (args, g_strdup ("-E"));
		if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->case_button))) g_ptr_array_add (args, g_strdup ("-i"));
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->word_button))) g_ptr_array_add (args, g_strdup ("-w"));
		append_grep_globs (args, gtk_entry_get_text (GTK_ENTRY (panel->include_entry)), FALSE);
		append_grep_globs (args, gtk_entry_get_text (GTK_ENTRY (panel->exclude_entry)), TRUE);
		g_ptr_array_add (args, g_strdup ("--")); g_ptr_array_add (args, pattern_grep);
		g_ptr_array_add (args, g_strdup (".")); g_ptr_array_add (args, NULL);
	}

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_set_cwd (launcher, root);
	panel->process = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) args->pdata, &error);
	g_object_unref (launcher);
	g_ptr_array_unref (args); g_free (root);
	if (panel->process == NULL)
	{
		gtk_label_set_text (GTK_LABEL (panel->status), error->message);
		g_clear_error (&error); set_busy (panel, FALSE); return;
	}
	panel->cancellable = g_cancellable_new ();
	g_subprocess_communicate_async (panel->process, NULL, panel->cancellable, search_finished, g_object_ref (panel));
}

static void
search_changed (GtkSearchEntry *entry, gpointer data)
{
	PlumaProjectSearchPanel *panel = data;
	const gchar *query = gtk_entry_get_text (GTK_ENTRY (entry));
	if (query == NULL || *query == '\0')
	{
		if (panel->cancellable)
			g_cancellable_cancel (panel->cancellable);
		g_clear_object (&panel->process);
		g_clear_object (&panel->cancellable);
		gtk_tree_store_clear (panel->store);
		gtk_label_set_text (GTK_LABEL (panel->status), "");
		set_busy (panel, FALSE);
	}
}

static void search_activate (GtkEntry *entry, gpointer data) { start_search (data); }
static void search_clicked (GtkButton *button, gpointer data) { start_search (data); }
static void cancel_clicked (GtkButton *button, gpointer data) { PlumaProjectSearchPanel *p=data; if (p->cancellable) g_cancellable_cancel (p->cancellable); }
static void expand_clicked (GtkButton *button, gpointer data) { gtk_tree_view_expand_all (GTK_TREE_VIEW (PLUMA_PROJECT_SEARCH_PANEL (data)->tree)); }
static void collapse_clicked (GtkButton *button, gpointer data) { gtk_tree_view_collapse_all (GTK_TREE_VIEW (PLUMA_PROJECT_SEARCH_PANEL (data)->tree)); }

static PlumaDocument *
find_document_by_path (PlumaProjectSearchPanel *panel, const gchar *path)
{
	GList *documents = pluma_window_get_documents (panel->window);
	PlumaDocument *found_doc = NULL;

	for (GList *item = documents; item != NULL; item = item->next)
	{
		GFile *location = pluma_document_get_location (PLUMA_DOCUMENT (item->data));
		if (location != NULL)
		{
			gchar *document_path = g_file_get_path (location);
			if (g_strcmp0 (document_path, path) == 0)
			{
				found_doc = PLUMA_DOCUMENT (item->data);
				g_free (document_path);
				g_object_unref (location);
				break;
			}
			g_free (document_path);
			g_object_unref (location);
		}
	}
	g_list_free (documents);
	return found_doc;
}

static gboolean
replace_file (PlumaProjectSearchPanel *panel, const gchar *path, GRegex *regex, const gchar *replacement, GError **error)
{
	gchar *contents = NULL;
	gchar *updated = NULL;
	gsize length = 0;
	const PlumaEncoding *encoding = NULL;
	gboolean success = FALSE;
	PlumaDocument *doc = find_document_by_path (panel, path);

	if (doc != NULL)
	{
		GtkTextBuffer *buffer = GTK_TEXT_BUFFER (doc);
		GtkTextIter start, end;
		gtk_text_buffer_get_bounds (buffer, &start, &end);
		contents = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
		length = strlen (contents);
	}
	else
	{
		contents = pluma_file_read_with_encoding (path, &length, &encoding, error);
		if (contents == NULL)
			return FALSE;
	}

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->regex_button)))
		updated = g_regex_replace (regex, contents, length, 0, replacement, 0, error);
	else
		updated = g_regex_replace_literal (regex, contents, length, 0, replacement, 0, error);

	if (updated != NULL)
	{
		if (doc != NULL)
		{
			GtkTextBuffer *buffer = GTK_TEXT_BUFFER (doc);
			GtkTextIter start, end;
			gtk_text_buffer_begin_user_action (buffer);
			gtk_text_buffer_get_bounds (buffer, &start, &end);
			gtk_text_buffer_delete (buffer, &start, &end);
			gtk_text_buffer_get_bounds (buffer, &start, &end);
			gtk_text_buffer_insert (buffer, &start, updated, -1);
			gtk_text_buffer_end_user_action (buffer);
			success = TRUE;
		}
		else
		{
			success = pluma_file_write_with_encoding (path, updated, encoding, error);
		}
	}

	g_free (contents);
	g_free (updated);
	return success;
}

static void
replace_all_clicked (GtkButton *button, gpointer data)
{
	PlumaProjectSearchPanel *panel = data;
	const gchar *query = gtk_entry_get_text (GTK_ENTRY (panel->search_entry));
	const gchar *replacement = gtk_entry_get_text (GTK_ENTRY (panel->replace_entry));
	GtkWidget *dialog;
	GtkTreeIter iter;
	GRegex *regex;
	GRegexCompileFlags flags = G_REGEX_OPTIMIZE;
	gchar *pattern;
	guint changed = 0, failed = 0;
	GHashTable *processed;

	if (*query == '\0' || !gtk_tree_model_get_iter_first (GTK_TREE_MODEL (panel->store), &iter)) return;
	dialog = gtk_message_dialog_new (GTK_WINDOW (panel->window), GTK_DIALOG_MODAL,
	                                 GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL,
	                                 "%s", _("Replace all results?"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
	                                          _("The result list is the preview. Open files will be updated in the editor."));
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Replace All"), GTK_RESPONSE_ACCEPT);
	if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT) { gtk_widget_destroy (dialog); return; }
	gtk_widget_destroy (dialog);

	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->regex_button))) pattern = g_regex_escape_string (query, -1);
	else pattern = g_strdup (query);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->word_button)))
	{
		gchar *wrapped = g_strdup_printf ("\\b(?:%s)\\b", pattern); g_free (pattern); pattern = wrapped;
	}
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->case_button))) flags |= G_REGEX_CASELESS;
	regex = g_regex_new (pattern, flags, 0, NULL); g_free (pattern);
	if (regex == NULL) return;

	processed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	do
	{
		gchar *path = NULL; GError *error = NULL;
		gtk_tree_model_get (GTK_TREE_MODEL (panel->store), &iter, COL_PATH, &path, -1);
		if (path != NULL && g_hash_table_lookup (processed, path) == NULL)
		{
			g_hash_table_insert (processed, g_strdup (path), GINT_TO_POINTER (1));
			if (replace_file (panel, path, regex, replacement, &error))
				changed++;
			else
				failed++;
		}
		g_clear_error (&error); g_free (path);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (panel->store), &iter));
	g_hash_table_unref (processed);
	g_regex_unref (regex);
	{
		gchar *summary = g_strdup_printf (_("%u files changed; %u failures"), changed, failed);
		gtk_label_set_text (GTK_LABEL (panel->status), summary); g_free (summary);
	}
	start_search (panel);
}

static void
row_activated (GtkTreeView *view, GtkTreePath *tree_path, GtkTreeViewColumn *column, gpointer data)
{
	PlumaProjectSearchPanel *panel = data;
	GtkTreeIter iter; gchar *path = NULL; gint line = 0; gboolean is_file;
	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (panel->store), &iter, tree_path)) return;
	gtk_tree_model_get (GTK_TREE_MODEL (panel->store), &iter, COL_PATH, &path, COL_LINE, &line, COL_IS_FILE, &is_file, -1);
	if (!is_file && path)
	{
		gchar *uri = g_filename_to_uri (path, NULL, NULL);
		pluma_window_create_tab_from_uri (panel->window, uri, NULL, MAX (line, 1), TRUE, TRUE);
		g_free (uri);
	}
	g_free (path);
}

/* Remembers which side-panel tab (Documents/File Browser/Source Control)
 * was active before find-in-files switched to Search, so Escape can restore
 * it. page_id is the same g_str_hash()-of-label id used throughout
 * pluma-panel.c (see _pluma_panel_get/set_active_item_by_id()); 0 means
 * "nothing to restore". */
void
pluma_project_search_panel_remember_return_page (PlumaProjectSearchPanel *panel,
                                                  gint                     page_id)
{
	panel->return_page_id = page_id;
}

/* Escape dismisses the search -- it must not hide the side panel, since
 * that panel is shared with Documents/File Browser/Source Control and
 * closing it over an Escape typed while searching would take those away
 * too. The search entry's own unconsumed Escape would otherwise bubble up
 * to PlumaPanel's Escape->"close" keybinding and hide the whole panel, and
 * a GtkTreeView with focus (e.g. a result row selected with the keyboard)
 * has no reason to let Escape propagate that far either. Stop it here in
 * both cases: switch back to whatever tab (typically File Browser) was
 * active before the search was opened, or fall back to focusing the active
 * document if there was nothing to return to. */
static gboolean
search_panel_escape_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	PlumaProjectSearchPanel *panel = data;

	if (event->keyval != GDK_KEY_Escape)
		return GDK_EVENT_PROPAGATE;

	if (panel->return_page_id != 0)
	{
		GtkWidget *side_panel = gtk_widget_get_ancestor (GTK_WIDGET (panel), PLUMA_TYPE_PANEL);

		if (side_panel != NULL)
		{
			_pluma_panel_set_active_item_by_id (PLUMA_PANEL (side_panel), panel->return_page_id);
			gtk_widget_grab_focus (side_panel);
			return GDK_EVENT_STOP;
		}
	}

	{
		PlumaView *view = pluma_window_get_active_view (panel->window);

		if (view != NULL)
			gtk_widget_grab_focus (GTK_WIDGET (view));
	}

	return GDK_EVENT_STOP;
}

static GtkWidget *toggle (const gchar *label, const gchar *tip)
{
	GtkWidget *button = gtk_toggle_button_new_with_label (label);
	gtk_widget_set_tooltip_text (button, tip); return button;
}

static void panel_destroyed (GtkWidget *widget, gpointer data)
{
	PlumaProjectSearchPanel *panel = data;
	panel->destroyed = TRUE;
	if (panel->cancellable) g_cancellable_cancel (panel->cancellable);
}

static void
pluma_project_search_panel_dispose (GObject *object)
{
	PlumaProjectSearchPanel *panel = PLUMA_PROJECT_SEARCH_PANEL (object);
	if (panel->cancellable) g_cancellable_cancel (panel->cancellable);
	g_clear_object (&panel->process); g_clear_object (&panel->cancellable);
	g_clear_object (&panel->settings);
	G_OBJECT_CLASS (pluma_project_search_panel_parent_class)->dispose (object);
}

static void pluma_project_search_panel_class_init (PlumaProjectSearchPanelClass *klass)
{ G_OBJECT_CLASS (klass)->dispose = pluma_project_search_panel_dispose; }

static void
pluma_project_search_panel_init (PlumaProjectSearchPanel *panel)
{
	GtkWidget *row, *button, *replace_button, *expand_button, *collapse_button, *scroll; GtkTreeViewColumn *column; GtkCellRenderer *renderer;
	gtk_orientable_set_orientation (GTK_ORIENTABLE (panel), GTK_ORIENTATION_VERTICAL);
	g_signal_connect (panel, "destroy", G_CALLBACK (panel_destroyed), panel);
	g_signal_connect (panel, "key-press-event", G_CALLBACK (search_panel_escape_key_press), panel);
	gtk_box_set_spacing (GTK_BOX (panel), 4); gtk_container_set_border_width (GTK_CONTAINER (panel), 6);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	panel->search_entry = gtk_search_entry_new (); gtk_entry_set_placeholder_text (GTK_ENTRY (panel->search_entry), _("Search in files"));
	gtk_widget_set_tooltip_text (panel->search_entry, _("Enter search pattern"));
	button = gtk_button_new_from_icon_name ("edit-find", GTK_ICON_SIZE_MENU);
	gtk_widget_set_tooltip_text (button, _("Search"));
	gtk_box_pack_start (GTK_BOX (row), panel->search_entry, TRUE, TRUE, 0); gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (panel), row, FALSE, FALSE, 0);
	g_signal_connect (panel->search_entry, "activate", G_CALLBACK (search_activate), panel);
	g_signal_connect (panel->search_entry, "search-changed", G_CALLBACK (search_changed), panel);
	g_signal_connect (button, "clicked", G_CALLBACK (search_clicked), panel);

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	panel->replace_entry = gtk_entry_new (); gtk_entry_set_placeholder_text (GTK_ENTRY (panel->replace_entry), _("Replace"));
	gtk_widget_set_tooltip_text (panel->replace_entry, _("Enter replacement text"));
	replace_button = gtk_button_new_with_label (_("Replace All"));
	gtk_widget_set_tooltip_text (replace_button, _("Replace all occurrences"));
	gtk_box_pack_start (GTK_BOX (row), panel->replace_entry, TRUE, TRUE, 0); gtk_box_pack_start (GTK_BOX (row), replace_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (panel), row, FALSE, FALSE, 0);
	g_signal_connect (replace_button, "clicked", G_CALLBACK (replace_all_clicked), panel);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	panel->case_button=toggle ("Aa", _("Match case")); panel->word_button=toggle ("ab", _("Whole word")); panel->regex_button=toggle (".*", _("Regular expression"));
	panel->ignored_button=toggle ("I", _("Include ignored files"));
	gtk_box_pack_start(GTK_BOX(row),panel->case_button,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),panel->word_button,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(row),panel->regex_button,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),panel->ignored_button,FALSE,FALSE,0);
	panel->spinner=gtk_spinner_new(); gtk_box_pack_end(GTK_BOX(row),panel->spinner,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(panel),row,FALSE,FALSE,0);
	panel->include_entry=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(panel->include_entry),_("Files to include (for example: *.c, src/**)"));
	gtk_widget_set_tooltip_text (panel->include_entry, _("Files to include (for example: *.c, src/**)"));
	panel->exclude_entry=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(panel->exclude_entry),_("Files to exclude (for example: build/**)"));
	gtk_widget_set_tooltip_text (panel->exclude_entry, _("Files to exclude (for example: build/**)"));
	panel->settings = g_settings_new (PLUMA_SCHEMA_ID);
	{
		gchar *value;
		value = g_settings_get_string (panel->settings, "project-search-include"); gtk_entry_set_text (GTK_ENTRY (panel->include_entry), value); g_free (value);
		value = g_settings_get_string (panel->settings, "project-search-exclude"); gtk_entry_set_text (GTK_ENTRY (panel->exclude_entry), value); g_free (value);
	}
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (panel->case_button), g_settings_get_boolean (panel->settings, "project-search-match-case"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (panel->word_button), g_settings_get_boolean (panel->settings, "project-search-whole-word"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (panel->regex_button), g_settings_get_boolean (panel->settings, "project-search-regex"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (panel->ignored_button), g_settings_get_boolean (panel->settings, "project-search-include-ignored"));
	gtk_box_pack_start(GTK_BOX(panel),panel->include_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(panel),panel->exclude_entry,FALSE,FALSE,0);
	panel->store=gtk_tree_store_new(N_COLS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_INT,G_TYPE_BOOLEAN);
	panel->tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(panel->store)); gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(panel->tree),FALSE);
	renderer=gtk_cell_renderer_text_new(); column=gtk_tree_view_column_new_with_attributes(_("Results"),renderer,"text",COL_TEXT,NULL); gtk_tree_view_append_column(GTK_TREE_VIEW(panel->tree),column);
	g_signal_connect(panel->tree,"row-activated",G_CALLBACK(row_activated),panel);
	g_signal_connect(panel->tree,"key-press-event",G_CALLBACK(search_panel_escape_key_press),panel);
	scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),panel->tree); gtk_box_pack_start(GTK_BOX(panel),scroll,TRUE,TRUE,0);
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); panel->status=gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(panel->status),0);
	expand_button=gtk_button_new_from_icon_name("list-add",GTK_ICON_SIZE_MENU); gtk_widget_set_tooltip_text(expand_button,_("Expand all")); g_signal_connect(expand_button,"clicked",G_CALLBACK(expand_clicked),panel);
	collapse_button=gtk_button_new_from_icon_name("list-remove",GTK_ICON_SIZE_MENU); gtk_widget_set_tooltip_text(collapse_button,_("Collapse all")); g_signal_connect(collapse_button,"clicked",G_CALLBACK(collapse_clicked),panel);
	button=gtk_button_new_with_label(_("Cancel")); gtk_widget_set_tooltip_text(button,_("Cancel search")); g_signal_connect(button,"clicked",G_CALLBACK(cancel_clicked),panel);
	gtk_box_pack_start(GTK_BOX(row),panel->status,TRUE,TRUE,0); gtk_box_pack_end(GTK_BOX(row),button,FALSE,FALSE,0); gtk_box_pack_end(GTK_BOX(row),collapse_button,FALSE,FALSE,0); gtk_box_pack_end(GTK_BOX(row),expand_button,FALSE,FALSE,0); gtk_box_pack_end(GTK_BOX(panel),row,FALSE,FALSE,0);
	gtk_widget_show_all(GTK_WIDGET(panel));
}

GtkWidget *pluma_project_search_panel_new (PlumaWindow *window)
{ PlumaProjectSearchPanel *panel=g_object_new(PLUMA_TYPE_PROJECT_SEARCH_PANEL,NULL); panel->window=window; return GTK_WIDGET(panel); }

void pluma_project_search_panel_focus (PlumaProjectSearchPanel *panel)
{ gtk_widget_grab_focus(panel->search_entry); }
