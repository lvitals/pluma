#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include "pluma-git-panel.h"
#include "pluma-commands.h"
#include "pluma-window-private.h"
#include "pluma-document.h"
#include "pluma-tab.h"
#include "pluma-view.h"
#include <gtksourceview/gtksource.h>
#include "pluma-pango.h"
#include "pluma-git-status-parser.h"
#include "pluma-git-commit.h"
#include "pluma-git-diff.h"

enum { COL_LABEL, COL_PATH, COL_GROUP, COL_STATUS, COL_IS_GROUP, N_COLS };
enum { GROUP_OUTGOING, GROUP_CONFLICT, GROUP_STAGED, GROUP_CHANGED, GROUP_UNTRACKED, N_GROUPS };

typedef struct _PlumaCommitTextView PlumaCommitTextView;
typedef struct _PlumaCommitTextViewClass PlumaCommitTextViewClass;

struct _PlumaCommitTextView { GtkTextView parent_instance; };
struct _PlumaCommitTextViewClass { GtkTextViewClass parent_class; };

GType pluma_commit_text_view_get_type (void);
G_DEFINE_TYPE (PlumaCommitTextView, pluma_commit_text_view, GTK_TYPE_TEXT_VIEW)

static void
pluma_commit_text_view_get_preferred_width (GtkWidget *widget,
	                                        gint      *minimum_width,
	                                        gint      *natural_width)
{
	/* The panel controls width; buffer contents must only affect wrapped height. */
	if (minimum_width != NULL)
		*minimum_width = 1;
	if (natural_width != NULL)
		*natural_width = 1;
}

static void
pluma_commit_text_view_class_init (PlumaCommitTextViewClass *klass)
{
	GTK_WIDGET_CLASS (klass)->get_preferred_width = pluma_commit_text_view_get_preferred_width;
}

static void
pluma_commit_text_view_init (PlumaCommitTextView *view)
{
}

typedef struct
{
	PlumaGitPanel *panel;
	gboolean show_output;
	gchar *title;
	gchar *commit_after_success;
} GitCall;

struct _PlumaGitPanel
{
	GtkBox parent_instance;
	PlumaWindow *window;
	GtkWidget *branch_label, *summary_label, *tree, *message_entry, *commit_button;
	GtkWidget *commit_count_label, *clear_message_button;
	GtkWidget *open_button, *stage_button, *discard_button;
	GtkWidget *filter_entry;
	gchar *filter_query;
	gchar *last_status_output;
	gsize last_status_length;
	gchar *outgoing_commits;
	gchar *current_branch;
	gchar *upstream;
	gint ahead;
	gint behind;
	GtkTreeStore *store;
	GtkTreeIter groups[N_GROUPS];
	gchar *repo;
	GCancellable *cancellable;
	GFileMonitor *git_monitor;
	GSettings *settings;
	guint refresh_source;
	guint poll_source;
	guint statusbar_context;
	gboolean status_running;
	gboolean refresh_pending;
	gboolean destroyed;
	gboolean amend_mode;
};

G_DEFINE_TYPE (PlumaGitPanel, pluma_git_panel, GTK_TYPE_BOX)

static void monitor_changed (GFileMonitor *monitor, GFile *file, GFile *other,
	                         GFileMonitorEvent event, gpointer data);
static void schedule_refresh (PlumaGitPanel *panel);
static void update_selection_sensitivity (PlumaGitPanel *panel);
static void open_side_by_side_diff (PlumaGitPanel *p);
static gboolean selected_file (PlumaGitPanel *panel, gchar **path, gint *group, gint *status);
static gboolean match_filter (const gchar *str, const gchar *filter);
static gboolean ensure_documents_saved (PlumaGitPanel *panel, const gchar *operation,
	                                    const gchar *relative_path);
static gboolean confirm_action (PlumaGitPanel *panel, const gchar *primary,
	                            const gchar *secondary);
static gchar *prompt_value (PlumaGitPanel *panel, const gchar *title,
	                       const gchar *label);
static gchar *prompt_revision (PlumaGitPanel *panel, const gchar *title,
	                          const gchar *label);
static gboolean prompt_comparison_revisions (PlumaGitPanel *panel,
	                                         gchar **base, gchar **target);
static void run_git (PlumaGitPanel *panel, const gchar * const *argv,
	                gboolean show_output, const gchar *title);
static gboolean selected_diff_is_binary (PlumaGitPanel *panel,
	                                     const gchar *path, gboolean cached);

static const gchar *group_names[N_GROUPS] = { N_("Commits to Push"), N_("Conflicts"), N_("Staged Changes"), N_("Changes"), N_("Untracked") };

static void
source_control_cell_data (GtkTreeViewColumn *column,
	                      GtkCellRenderer   *renderer,
	                      GtkTreeModel      *model,
	                      GtkTreeIter       *iter,
	                      gpointer           data)
{
	gint group = -1;
	gboolean is_group = FALSE;

	gtk_tree_model_get (model, iter, COL_GROUP, &group,
	                    COL_IS_GROUP, &is_group, -1);
	g_object_set (renderer, "ellipsize",
	              !is_group && group == GROUP_OUTGOING
	                  ? PANGO_ELLIPSIZE_NONE
	                  : PANGO_ELLIPSIZE_END,
	              NULL);
}

static gboolean
match_filter (const gchar *str, const gchar *filter)
{
	if (filter == NULL || *filter == '\0')
		return TRUE;
	gchar *str_lower = g_utf8_strdown (str, -1);
	gchar *filter_lower = g_utf8_strdown (filter, -1);
	gboolean res = (strstr (str_lower, filter_lower) != NULL);
	g_free (str_lower);
	g_free (filter_lower);
	return res;
}

static gboolean
ensure_documents_saved (PlumaGitPanel *panel, const gchar *operation,
	                    const gchar *relative_path)
{
	GList *documents = pluma_window_get_documents (panel->window);
	GString *names = g_string_new (NULL);
	gchar *target = relative_path != NULL && panel->repo != NULL
	              ? g_canonicalize_filename (relative_path, panel->repo) : NULL;
	guint modified = 0;

	for (GList *item = documents; item != NULL; item = item->next)
	{
		PlumaDocument *document = PLUMA_DOCUMENT (item->data);
		GFile *location;
		gchar *path;
		gboolean in_repo;

		if (!gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document)))
			continue;
		location = pluma_document_get_location (document);
		path = location != NULL ? g_file_get_path (location) : NULL;
		if (location != NULL)
			g_object_unref (location);
		in_repo = path != NULL && panel->repo != NULL &&
		          g_str_has_prefix (path, panel->repo) &&
		          (path[strlen (panel->repo)] == '\0' || path[strlen (panel->repo)] == G_DIR_SEPARATOR);
		if (path == NULL || panel->repo == NULL ||
		    (!in_repo && g_strcmp0 (path, target) != 0) ||
		    (target != NULL && g_strcmp0 (path, target) != 0))
		{
			g_free (path);
			continue;
		}
		if (modified++ < 5)
		{
			gchar *display = g_filename_display_basename (path);
			g_string_append_printf (names, "\n• %s", display);
			g_free (display);
		}
		g_free (path);
	}
	g_list_free (documents);
	g_free (target);

	if (modified > 0)
	{
		GtkWidget *dialog;
		gchar *primary = g_strdup_printf (_("Save modified files before %s"), operation);
		if (modified > 5)
			g_string_append_printf (names, _("\n• and %u more"), modified - 5);
		dialog = gtk_message_dialog_new (GTK_WINDOW (panel->window), GTK_DIALOG_MODAL,
		                                 GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
		                                 "%s", primary);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		                                          _("This Git operation could overwrite unsaved editor contents:%s"),
		                                          names->str);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		g_free (primary);
		g_string_free (names, TRUE);
		return FALSE;
	}
	g_string_free (names, TRUE);
	return TRUE;
}

static gchar *
find_repository (PlumaGitPanel *panel)
{
	GFile *file = _pluma_window_get_default_location (panel->window);
	gchar *path = file ? g_file_get_path (file) : NULL;
	if (file) g_object_unref (file);
	if (path == NULL)
	{
		PlumaDocument *doc = pluma_window_get_active_document (panel->window);
		GFile *location = doc ? pluma_document_get_location (doc) : NULL;
		if (location) { file = g_file_get_parent (location); path = g_file_get_path (file); g_object_unref (file); g_object_unref (location); }
	}
	if (path == NULL) path = g_get_current_dir ();
	while (path != NULL)
	{
		gchar *marker = g_build_filename (path, ".git", NULL);
		gboolean found = g_file_test (marker, G_FILE_TEST_EXISTS); g_free (marker);
		if (found) return path;
		gchar *parent = g_path_get_dirname (path);
		if (g_strcmp0 (parent, path) == 0) { g_free (parent); g_free (path); return NULL; }
		g_free (path); path = parent;
	}
	return NULL;
}

static GSubprocess *
spawn_git (PlumaGitPanel *panel, const gchar * const *argv, GError **error)
{
	GSubprocessLauncher *launcher;
	GSubprocess *process;
	if (panel->repo == NULL) return NULL;
	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_set_cwd (launcher, panel->repo);
	g_subprocess_launcher_setenv (launcher, "LC_ALL", "C", TRUE);
	process = g_subprocess_launcher_spawnv (launcher, argv, error);
	g_object_unref (launcher);
	return process;
}

static void
reset_model (PlumaGitPanel *panel)
{
	gtk_tree_store_clear (panel->store);
	for (gint i = 0; i < N_GROUPS; i++)
	{
		gtk_tree_store_append (panel->store, &panel->groups[i], NULL);
		gtk_tree_store_set (panel->store, &panel->groups[i], COL_LABEL, _(group_names[i]), COL_GROUP, i, COL_IS_GROUP, TRUE, -1);
	}
	update_selection_sensitivity (panel);
}

static const gchar *
status_description (gchar status)
{
	switch (status) { case 'A': return _("Added"); case 'D': return _("Deleted"); case 'R': return _("Renamed"); case 'C': return _("Copied"); case 'T': return _("Type changed"); default: return _("Modified"); }
}

static void
append_file (PlumaGitPanel *panel, gint group, const gchar *path, gchar status)
{
	GtkTreeIter iter; gchar *label = g_strdup_printf ("%c  %s", status, path);
	gtk_tree_store_append (panel->store, &iter, &panel->groups[group]);
	gtk_tree_store_set (panel->store, &iter, COL_LABEL, label, COL_PATH, path, COL_GROUP, group,
	                    COL_STATUS, (gint) status, COL_IS_GROUP, FALSE, -1);
	gtk_tree_store_set (panel->store, &iter, COL_LABEL, label, -1);
	gtk_widget_set_tooltip_text (panel->tree, status_description (status));
	g_free (label);
}

static void
append_outgoing_commit (PlumaGitPanel *panel, const gchar *commit)
{
	GtkTreeIter iter;
	gchar *label = g_strdup_printf ("↑  %s", commit);

	gtk_tree_store_append (panel->store, &iter, &panel->groups[GROUP_OUTGOING]);
	/* COL_PATH stays NULL so file actions are not enabled for commit rows. */
	gtk_tree_store_set (panel->store, &iter, COL_LABEL, label,
	                    COL_GROUP, GROUP_OUTGOING, COL_IS_GROUP, FALSE, -1);
	g_free (label);
}

static gboolean
find_and_select_path (GtkTreeModel *model, GtkTreePath *path_in, GtkTreeIter *iter, gpointer data)
{
	struct { const gchar *path; gint group; GtkTreeView *tree; gboolean found; } *args = data;
	gchar *iter_path = NULL;
	gint iter_group = 0;
	gboolean is_group = FALSE;

	gtk_tree_model_get (model, iter, COL_PATH, &iter_path, COL_GROUP, &iter_group, COL_IS_GROUP, &is_group, -1);
	if (!is_group && g_strcmp0 (iter_path, args->path) == 0 && iter_group == args->group)
	{
		GtkTreeSelection *sel = gtk_tree_view_get_selection (args->tree);
		gtk_tree_selection_select_iter (sel, iter);
		args->found = TRUE;
		g_free (iter_path);
		return TRUE; // stop search
	}
	g_free (iter_path);
	return FALSE;
}

static void
parse_status (PlumaGitPanel *panel, const guint8 *output, gsize output_length)
{
	gchar *selected_path = NULL;
	gint selected_group = -1;
	gint selected_status = 0;
	gboolean has_selection = selected_file (panel, &selected_path, &selected_group, &selected_status);

	gboolean expanded[N_GROUPS] = {TRUE, TRUE, TRUE, TRUE, TRUE};
	for (gint g = 0; g < N_GROUPS; g++)
	{
		GtkTreePath *p = gtk_tree_model_get_path (GTK_TREE_MODEL (panel->store), &panel->groups[g]);
		if (p)
		{
			expanded[g] = gtk_tree_view_row_expanded (GTK_TREE_VIEW (panel->tree), p);
			gtk_tree_path_free (p);
		}
	}

	g_free (panel->current_branch);
	panel->current_branch = NULL;
	g_free (panel->upstream);
	panel->upstream = NULL;

	guint counts[N_GROUPS] = {0};
	PlumaGitStatus *status = pluma_git_status_parse (output, output_length);
	gboolean detached = g_strcmp0 (status->branch, "(detached)") == 0;
	reset_model (panel);
	panel->current_branch = detached ? NULL : g_strdup (status->branch);
	panel->upstream = g_strdup (status->upstream);
	panel->ahead = status->ahead;
	panel->behind = status->behind;
	for (guint i = 0; i < status->entries->len; i++)
	{
		PlumaGitStatusEntry *entry = g_ptr_array_index (status->entries, i);
		if (entry->kind == '?')
		{
			if (match_filter (entry->path, panel->filter_query))
			{
				append_file (panel, GROUP_UNTRACKED, entry->path, '?');
				counts[GROUP_UNTRACKED]++;
			}
			continue;
		}
		if (entry->kind == '!' || !match_filter (entry->path, panel->filter_query))
			continue;
		if (entry->kind == 'u')
		{
			append_file (panel, GROUP_CONFLICT, entry->path, 'U');
			counts[GROUP_CONFLICT]++;
		}
		else
		{
			if (entry->index_status != '.') { append_file (panel, GROUP_STAGED, entry->path, entry->index_status); counts[GROUP_STAGED]++; }
			if (entry->worktree_status != '.') { append_file (panel, GROUP_CHANGED, entry->path, entry->worktree_status); counts[GROUP_CHANGED]++; }
		}
	}
	if (status->ahead > 0 && panel->outgoing_commits != NULL)
	{
		gchar **commits = g_strsplit (panel->outgoing_commits, "\n", -1);
		for (guint i = 0; commits[i] != NULL; i++)
		{
			if (*commits[i] != '\0' && match_filter (commits[i], panel->filter_query))
			{
				append_outgoing_commit (panel, commits[i]);
				counts[GROUP_OUTGOING]++;
			}
		}
		g_strfreev (commits);
	}
	for (gint i=0;i<N_GROUPS;i++) { gchar *name=g_strdup_printf ("%s (%u)",_(group_names[i]),counts[i]); gtk_tree_store_set(panel->store,&panel->groups[i],COL_LABEL,name,-1); g_free(name); }
	
	for (gint g = 0; g < N_GROUPS; g++)
	{
		GtkTreePath *p = gtk_tree_model_get_path (GTK_TREE_MODEL (panel->store), &panel->groups[g]);
		if (p)
		{
			if (expanded[g])
				gtk_tree_view_expand_row (GTK_TREE_VIEW (panel->tree), p, FALSE);
			else
				gtk_tree_view_collapse_row (GTK_TREE_VIEW (panel->tree), p);
			gtk_tree_path_free (p);
		}
	}

	{
		const gchar *branch_label = detached ? _("Detached HEAD") :
		                            status->branch ? status->branch : _("No commits");
		gchar *head = g_strdup_printf ("%s%s%s", branch_label, status->upstream ? " → " : "", status->upstream ? status->upstream : "");
		guint file_changes = counts[GROUP_CONFLICT] + counts[GROUP_STAGED] +
		                     counts[GROUP_CHANGED] + counts[GROUP_UNTRACKED];
		gchar *summary = g_strdup_printf (_("%u changes  ↑%d ↓%d"), file_changes, status->ahead, status->behind);
		gtk_label_set_text (GTK_LABEL (panel->branch_label), head); gtk_label_set_text (GTK_LABEL (panel->summary_label), summary);
		if (panel->statusbar_context != 0)
		{
			gtk_statusbar_pop (GTK_STATUSBAR (panel->window->priv->statusbar), panel->statusbar_context);
			gtk_statusbar_push (GTK_STATUSBAR (panel->window->priv->statusbar), panel->statusbar_context, summary);
		}
		g_free (head); g_free (summary);
	}
	pluma_git_status_free (status);
	if (has_selection && selected_path != NULL)
	{
		struct { const gchar *path; gint group; GtkTreeView *tree; gboolean found; } args = { selected_path, selected_group, GTK_TREE_VIEW (panel->tree), FALSE };
		gtk_tree_model_foreach (GTK_TREE_MODEL (panel->store), find_and_select_path, &args);
	}
	g_free (selected_path);
	update_selection_sensitivity (panel);
}

static void
status_done (GObject *source, GAsyncResult *result, gpointer data)
{
	PlumaGitPanel *panel = data; GBytes *out=NULL,*err=NULL; GError *error=NULL;
	const guint8 *out_data = NULL; gsize out_length = 0;
	g_subprocess_communicate_finish (G_SUBPROCESS(source),result,&out,&err,&error);
	if (out != NULL) out_data = g_bytes_get_data (out, &out_length);
	if (!panel->destroyed)
	{
		if (error) gtk_label_set_text(GTK_LABEL(panel->summary_label),error->message);
		else if (g_subprocess_get_successful(G_SUBPROCESS(source)))
		{
			const gchar *log_argv[]={"git","log","--format=%h  %s","--max-count=100","@{upstream}..HEAD",NULL};
			GSubprocess *log_process;
			gchar *log_out = NULL;
			GError *log_error = NULL;
			gboolean status_changed;

			log_process = spawn_git (panel, log_argv, &log_error);
			if (log_process != NULL)
			{
				g_subprocess_communicate_utf8 (log_process, NULL, NULL, &log_out, NULL, &log_error);
				g_object_unref (log_process);
			}
			g_clear_error (&log_error);
			status_changed = panel->last_status_length != out_length ||
			                 (out_length > 0 && memcmp (panel->last_status_output, out_data, out_length) != 0) ||
			                 g_strcmp0 (panel->outgoing_commits, log_out) != 0;
			if (status_changed)
			{
				g_free (panel->outgoing_commits);
				panel->outgoing_commits = log_out;
				log_out = NULL;
				g_free (panel->last_status_output);
				panel->last_status_output = out_length > 0 ? g_memdup2 (out_data, out_length) : NULL;
				panel->last_status_length = out_length;
				parse_status(panel,out_data,out_length);
			}
			g_free (log_out);
		}
		else
		{
			gsize err_length = 0; const gchar *err_data = err != NULL ? g_bytes_get_data (err, &err_length) : NULL;
			gchar *message = err_length > 0 ? g_strndup (err_data, err_length) : g_strdup (_("Git status failed"));
			gtk_label_set_text (GTK_LABEL (panel->summary_label), message); g_free (message);
		}
	}
	panel->status_running = FALSE;
	if (panel->refresh_pending && !panel->destroyed) { panel->refresh_pending = FALSE; schedule_refresh (panel); }
	g_clear_error(&error); g_clear_pointer(&out,g_bytes_unref); g_clear_pointer(&err,g_bytes_unref); g_object_unref(panel);
}

void
pluma_git_panel_refresh (PlumaGitPanel *panel)
{
	const gchar *argv[]={"git","status","--porcelain=v2","-z","--branch","--untracked-files=all",NULL}; GError *error=NULL; GSubprocess *process;
	gchar *repo;
	if (panel->status_running) { panel->refresh_pending = TRUE; return; }
	repo=find_repository(panel);
	if (g_strcmp0(repo,panel->repo)!=0)
	{
		GFile *git_file; GError *monitor_error = NULL;
		g_clear_object (&panel->git_monitor);
		g_free(panel->repo); panel->repo=repo; repo=NULL;
		if (panel->repo != NULL)
		{
			gchar *git_path = g_build_filename (panel->repo, ".git", NULL);
			git_file = g_file_new_for_path (git_path); g_free (git_path);
			if (g_file_query_file_type (git_file, 0, NULL) == G_FILE_TYPE_DIRECTORY)
				panel->git_monitor = g_file_monitor_directory (git_file, G_FILE_MONITOR_NONE, NULL, &monitor_error);
			else
				panel->git_monitor = g_file_monitor_file (git_file, G_FILE_MONITOR_NONE, NULL, &monitor_error);
			if (panel->git_monitor) g_signal_connect (panel->git_monitor, "changed", G_CALLBACK (monitor_changed), panel);
			g_clear_error (&monitor_error); g_object_unref (git_file);
		}
	}
	g_free(repo);
	if (!panel->repo) { reset_model(panel); gtk_label_set_text(GTK_LABEL(panel->branch_label),_("No Git repository")); gtk_label_set_text(GTK_LABEL(panel->summary_label),"");if(panel->statusbar_context)gtk_statusbar_pop(GTK_STATUSBAR(panel->window->priv->statusbar),panel->statusbar_context);return; }
	process=spawn_git(panel,argv,&error);
	if (!process) { gtk_label_set_text(GTK_LABEL(panel->summary_label),error?error->message:_("Git is unavailable")); g_clear_error(&error); return; }
	panel->status_running = TRUE;
	g_subprocess_communicate_async(process,NULL,panel->cancellable,status_done,g_object_ref(panel)); g_object_unref(process);
}

static gboolean refresh_timeout (gpointer data) { PlumaGitPanel *p=data; p->refresh_source=0; pluma_git_panel_refresh(p); return G_SOURCE_REMOVE; }
static void schedule_refresh (PlumaGitPanel *p) { if (!p->refresh_source) p->refresh_source=g_timeout_add(250,refresh_timeout,p); }

static void
call_done (GObject *source, GAsyncResult *result, gpointer data)
{
	GitCall *call=data; gchar *out=NULL,*err=NULL; GError *error=NULL;
	g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source),result,&out,&err,&error);
	if (!call->panel->destroyed)
	{
		if (call->show_output)
		{
			gchar *display_out = out;
			gboolean free_display_out = FALSE;
			if (!display_out || !*display_out)
			{
				if (g_strcmp0 (call->title, _("Git Tags")) == 0)
					display_out = g_strdup (_("No tags found."));
				else if (g_strcmp0 (call->title, _("Git Stashes")) == 0)
					display_out = g_strdup (_("No stashes found."));
				else if (g_strcmp0 (call->title, _("Git Remotes")) == 0)
					display_out = g_strdup (_("No remotes configured."));
				else if (g_strcmp0 (call->title, _("Git Branches")) == 0)
					display_out = g_strdup (_("No branches found."));
				else if (g_strcmp0 (call->title, _("Git History")) == 0)
					display_out = g_strdup (_("No commits found."));
				else
					display_out = g_strdup (_("No output."));
				free_display_out = TRUE;
			}
			
			if (display_out && *display_out)
			{
				PlumaTab *tab=pluma_window_create_tab(call->panel->window,TRUE); PlumaDocument *doc=pluma_tab_get_document(tab);
				pluma_tab_set_reusable (tab, FALSE);

				gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc),display_out,-1);
				gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc), FALSE);
				
				gchar *display_title = g_strdup (call->title ? call->title : _("Git Output"));
				pluma_document_set_short_name_for_display (doc, display_title);
				g_free (display_title);
				
				if (call->title && g_str_has_prefix (call->title, "Diff:"))
				{
					GtkSourceLanguage *language=gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(),"diff");
					if(language)pluma_document_set_language(doc,language);
				}
				
				GtkTextView *view = GTK_TEXT_VIEW(pluma_tab_get_view(tab));
				gtk_text_view_set_editable(view,FALSE);
				gtk_text_view_set_cursor_visible(view,FALSE);
			}
			
			if (free_display_out)
				g_free (display_out);
		}
		if (error || !g_subprocess_get_successful(G_SUBPROCESS(source))) gtk_label_set_text(GTK_LABEL(call->panel->summary_label),error?error->message:(err&&*err?err:_("Git operation failed")));
		else
		{
			if (call->commit_after_success != NULL)
			{
				const gchar *commit_argv[] = {"git", "commit", "-m",
				                              call->commit_after_success, NULL};
				run_git (call->panel, commit_argv, FALSE, "commit");
			}
			else if (g_strcmp0 (call->title, "commit") == 0 ||
			         g_strcmp0 (call->title, "amend") == 0)
			{
				GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (call->panel->message_entry));
				gtk_text_buffer_set_text (buf, "", -1);
				call->panel->amend_mode = FALSE;
				gtk_button_set_label (GTK_BUTTON (call->panel->commit_button), _("Commit"));
			}
			if (call->commit_after_success == NULL)
				schedule_refresh(call->panel);
		}
	}
	g_clear_error(&error);g_free(out);g_free(err);g_free(call->title);g_free(call->commit_after_success);g_object_unref(call->panel);g_free(call);
}

static void
run_git (PlumaGitPanel *panel, const gchar * const *argv, gboolean show_output, const gchar *title)
{
	GError *error=NULL; GSubprocess *process=spawn_git(panel,argv,&error); GitCall *call;
	if(!process){gtk_label_set_text(GTK_LABEL(panel->summary_label),error?error->message:_("Git is unavailable"));g_clear_error(&error);return;}
	call=g_new0(GitCall,1);call->panel=g_object_ref(panel);call->show_output=show_output;call->title=g_strdup(title);
	g_subprocess_communicate_utf8_async(process,NULL,panel->cancellable,call_done,call);g_object_unref(process);
}

static void
stage_all_then_commit (PlumaGitPanel *panel, const gchar *message)
{
	const gchar *argv[] = {"git", "add", "-A", NULL};
	GError *error = NULL;
	GSubprocess *process = spawn_git (panel, argv, &error);
	GitCall *call;

	if (process == NULL)
	{
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    error != NULL ? error->message : _("Git is unavailable"));
		g_clear_error (&error);
		return;
	}
	call = g_new0 (GitCall, 1);
	call->panel = g_object_ref (panel);
	call->commit_after_success = g_strdup (message);
	g_subprocess_communicate_utf8_async (process, NULL, panel->cancellable,
	                                     call_done, call);
	g_object_unref (process);
}

static gboolean
selected_file (PlumaGitPanel *panel, gchar **path, gint *group, gint *status)
{
	GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->tree));GtkTreeModel *model;GtkTreeIter iter;gboolean is_group;
	if(!gtk_tree_selection_get_selected(selection,&model,&iter))return FALSE;
	gtk_tree_model_get(model,&iter,COL_PATH,path,COL_GROUP,group,COL_STATUS,status,COL_IS_GROUP,&is_group,-1);return !is_group&&*path!=NULL;
}

static void stage_selected (PlumaGitPanel *p) { gchar *path;gint g,s;if(selected_file(p,&path,&g,&s)){const gchar *a[]={"git","add","--",path,NULL};run_git(p,a,FALSE,NULL);g_free(path);} }
static void unstage_selected (PlumaGitPanel *p) { gchar *path;gint g,s;if(selected_file(p,&path,&g,&s)){const gchar *a[]={"git","reset","-q","HEAD","--",path,NULL};run_git(p,a,FALSE,NULL);g_free(path);} }
static void diff_selected (PlumaGitPanel *p) { gchar *path;gint g,s;if(selected_file(p,&path,&g,&s)){if(selected_diff_is_binary(p,path,g==GROUP_STAGED)){gtk_label_set_text(GTK_LABEL(p->summary_label),_("Binary files cannot be displayed as a text diff."));g_free(path);return;}const gchar *a1[]={"git","diff","--cached","--",path,NULL};const gchar *a2[]={"git","diff","--",path,NULL};gchar *title=g_strconcat("Diff: ",path,NULL);run_git(p,g==GROUP_STAGED?a1:a2,TRUE,title);g_free(title);g_free(path);} }
static void open_selected (PlumaGitPanel *p) { gchar *path;gint g,s;if(selected_file(p,&path,&g,&s)){gchar *full=g_build_filename(p->repo,path,NULL);gchar *uri=g_filename_to_uri(full,NULL,NULL);pluma_commands_load_uri(p->window,uri,NULL,0);g_free(uri);g_free(full);g_free(path);} }

static void discard_selected (PlumaGitPanel *p)
{
	gchar *path;gint g,s;GtkWidget *d;if(!selected_file(p,&path,&g,&s)){g_free(path);return;}
	if (g != GROUP_UNTRACKED && !ensure_documents_saved (p, _("discarding changes"), path)) { g_free (path); return; }
	d=gtk_message_dialog_new(GTK_WINDOW(p->window),GTK_DIALOG_MODAL,GTK_MESSAGE_WARNING,GTK_BUTTONS_CANCEL,
	                         g==GROUP_UNTRACKED?_("Permanently delete untracked file “%s”?"):_("Discard changes to “%s”?"),path);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),"%s",g==GROUP_UNTRACKED?_("This file is not tracked by Git and cannot be restored by Pluma."):_("Changes in the working tree will be lost."));
	gtk_dialog_add_button(GTK_DIALOG(d),g==GROUP_UNTRACKED?_("Delete Permanently"):_("Discard"),GTK_RESPONSE_ACCEPT);
	if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT)
	{
		if(g==GROUP_UNTRACKED){gchar*full=g_build_filename(p->repo,path,NULL);GFile*file=g_file_new_for_path(full);GError*error=NULL;if(!g_file_delete(file,NULL,&error))gtk_label_set_text(GTK_LABEL(p->summary_label),error->message);else schedule_refresh(p);g_clear_error(&error);g_object_unref(file);g_free(full);}
		else{const gchar *a[]={"git","restore","--worktree","--",path,NULL};run_git(p,a,FALSE,NULL);}
	}
	gtk_widget_destroy(d);g_free(path);
}

typedef struct {
	GtkAdjustment *adj_left;
	GtkAdjustment *adj_right;
	gboolean syncing;
} ScrollSync;

static void
on_scroll_left (GtkAdjustment *adj, gpointer data)
{
	ScrollSync *sync = data;
	if (sync->syncing) return;
	sync->syncing = TRUE;
	gtk_adjustment_set_value (sync->adj_right, gtk_adjustment_get_value (adj));
	sync->syncing = FALSE;
}

static void
on_scroll_right (GtkAdjustment *adj, gpointer data)
{
	ScrollSync *sync = data;
	if (sync->syncing) return;
	sync->syncing = TRUE;
	gtk_adjustment_set_value (sync->adj_left, gtk_adjustment_get_value (adj));
	sync->syncing = FALSE;
}

static void
on_tab_destroy_disconnect_sync (GtkWidget *widget, gpointer data)
{
	ScrollSync *sync = data;
	if (sync->adj_left)
		g_signal_handlers_disconnect_by_func (sync->adj_left, on_scroll_left, sync);
	if (sync->adj_right)
		g_signal_handlers_disconnect_by_func (sync->adj_right, on_scroll_right, sync);
	g_free (sync);
}

static gchar *
get_git_output (PlumaGitPanel *panel, const gchar * const *argv)
{
	GError *error = NULL;
	GSubprocess *process = spawn_git (panel, argv, &error);
	gchar *out = NULL;
	if (process)
	{
		g_subprocess_communicate_utf8 (process, NULL, NULL, &out, NULL, &error);
		g_object_unref (process);
	}
	if (error)
		g_clear_error (&error);
	return out;
}

static gboolean
selected_diff_is_binary (PlumaGitPanel *panel, const gchar *path, gboolean cached)
{
	const gchar *worktree_argv[] = {"git", "diff", "--numstat", "--", path, NULL};
	const gchar *cached_argv[] = {"git", "diff", "--cached", "--numstat", "--", path, NULL};
	gchar *output = get_git_output (panel, cached ? cached_argv : worktree_argv);
	gboolean binary = pluma_git_numstat_has_binary (output);
	g_free (output);
	return binary;
}

static void
run_git_with_input (PlumaGitPanel *panel, const gchar * const *argv,
	                const gchar *input)
{
	GSubprocessLauncher *launcher;
	GSubprocess *process;
	GitCall *call;
	GError *error = NULL;

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE |
	                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                                      G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_set_cwd (launcher, panel->repo);
	g_subprocess_launcher_setenv (launcher, "LC_ALL", "C", TRUE);
	process = g_subprocess_launcher_spawnv (launcher, argv, &error);
	g_object_unref (launcher);
	if (process == NULL)
	{
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    error != NULL ? error->message : _("Git is unavailable"));
		g_clear_error (&error);
		return;
	}
	call = g_new0 (GitCall, 1);
	call->panel = g_object_ref (panel);
	g_subprocess_communicate_utf8_async (process, input, panel->cancellable,
	                                     call_done, call);
	g_object_unref (process);
}

static gchar *
choose_hunk_patch (PlumaGitPanel *panel, const gchar *diff, const gchar *title)
{
	GPtrArray *patches = g_ptr_array_new_with_free_func (g_free);
	GPtrArray *labels = g_ptr_array_new_with_free_func (g_free);
	GString *header = g_string_new (NULL);
	GString *current = NULL;
	gchar **lines = g_strsplit (diff != NULL ? diff : "", "\n", -1);
	GtkWidget *dialog, *box, *combo;
	gchar *selected = NULL;

	for (guint i = 0; lines[i] != NULL; i++)
	{
		if (g_str_has_prefix (lines[i], "@@ "))
		{
			if (current != NULL)
				g_ptr_array_add (patches, g_string_free (current, FALSE));
			current = g_string_new (header->str);
			g_ptr_array_add (labels, g_strdup (lines[i]));
		}
		if (current != NULL)
			g_string_append_printf (current, "%s\n", lines[i]);
		else
			g_string_append_printf (header, "%s\n", lines[i]);
	}
	if (current != NULL)
		g_ptr_array_add (patches, g_string_free (current, FALSE));
	g_string_free (header, TRUE);
	g_strfreev (lines);

	if (patches->len == 0)
	{
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("No textual hunks are available for this file."));
		goto out;
	}
	dialog = gtk_dialog_new_with_buttons (title, GTK_WINDOW (panel->window),
	                                      GTK_DIALOG_MODAL,
	                                      _("Cancel"), GTK_RESPONSE_CANCEL,
	                                      _("Continue"), GTK_RESPONSE_ACCEPT, NULL);
	box = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_set_border_width (GTK_CONTAINER (box), 8);
	combo = gtk_combo_box_text_new ();
	for (guint i = 0; i < labels->len; i++)
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), g_ptr_array_index (labels, i));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
	gtk_box_pack_start (GTK_BOX (box), combo, FALSE, FALSE, 0);
	gtk_widget_show_all (dialog);
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gint active = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
		if (active >= 0 && (guint) active < patches->len)
			selected = g_strdup (g_ptr_array_index (patches, active));
	}
	gtk_widget_destroy (dialog);
out:
	g_ptr_array_unref (patches);
	g_ptr_array_unref (labels);
	return selected;
}

static void
apply_selected_hunk (PlumaGitPanel *panel, gboolean reverse, gboolean cached,
	                 gboolean destructive)
{
	gchar *path = NULL, *diff = NULL, *patch = NULL;
	gint group, status;
	const gchar *diff_worktree[] = {"git", "diff", "--no-ext-diff", "--", NULL, NULL};
	const gchar *diff_cached[] = {"git", "diff", "--cached", "--no-ext-diff", "--", NULL, NULL};
	const gchar *apply_stage[] = {"git", "apply", "--cached", "--whitespace=nowarn", "-", NULL};
	const gchar *apply_unstage[] = {"git", "apply", "--cached", "--reverse", "--whitespace=nowarn", "-", NULL};
	const gchar *apply_discard[] = {"git", "apply", "--reverse", "--whitespace=nowarn", "-", NULL};

	if (!selected_file (panel, &path, &group, &status))
		return;
	if (destructive && !ensure_documents_saved (panel, _("discarding a hunk"), path))
		goto out;
	if (cached)
	{
		diff_cached[5] = path;
		diff = get_git_output (panel, diff_cached);
	}
	else
	{
		diff_worktree[4] = path;
		diff = get_git_output (panel, diff_worktree);
	}
	patch = choose_hunk_patch (panel, diff,
	                          destructive ? _("Discard Hunk") :
	                          reverse ? _("Unstage Hunk") : _("Stage Hunk"));
	if (patch == NULL)
		goto out;
	if (destructive && !confirm_action (panel, _("Discard the selected hunk?"),
	                                   _("The selected working-tree changes will be lost.")))
		goto out;
	run_git_with_input (panel, destructive ? apply_discard :
	                           reverse ? apply_unstage : apply_stage, patch);
out:
	g_free (patch);
	g_free (diff);
	g_free (path);
}

static void stage_hunk_selected (PlumaGitPanel *panel) { apply_selected_hunk (panel, FALSE, FALSE, FALSE); }
static void unstage_hunk_selected (PlumaGitPanel *panel) { apply_selected_hunk (panel, TRUE, TRUE, FALSE); }
static void discard_hunk_selected (PlumaGitPanel *panel) { apply_selected_hunk (panel, TRUE, FALSE, TRUE); }



static gboolean
is_dark_theme (GtkSourceStyleScheme *scheme)
{
	if (!scheme)
		return FALSE;
	const gchar *id = gtk_source_style_scheme_get_id (scheme);
	if (id && (g_strrstr (id, "dark") || g_strrstr (id, "oblivion") || g_strrstr (id, "night") || g_strrstr (id, "matrix")))
		return TRUE;
	GtkSourceStyle *style = gtk_source_style_scheme_get_style (scheme, "text");
	if (!style)
		return FALSE;
	gchar *background = NULL;
	g_object_get (style, "background", &background, NULL);
	if (background)
	{
		GdkRGBA color;
		if (gdk_rgba_parse (&color, background))
		{
			double luminance = 0.2126 * color.red + 0.7152 * color.green + 0.0722 * color.blue;
			g_free (background);
			return (luminance < 0.5);
		}
		g_free (background);
	}
	return FALSE;
}

static void
open_side_by_side_diff (PlumaGitPanel *p)
{
	gchar *path = NULL;
	gint g = 0, s = 0;
	if (!selected_file (p, &path, &g, &s))
		return;
	if (selected_diff_is_binary (p, path, g == GROUP_STAGED))
	{
		gtk_label_set_text (GTK_LABEL (p->summary_label),
		                    _("Binary files cannot be displayed as a side-by-side text diff."));
		g_free (path);
		return;
	}
	
	PlumaDocument *active_doc = pluma_window_get_active_document (p->window);
	GtkSourceStyleScheme *style_scheme = NULL;
	if (active_doc)
		style_scheme = gtk_source_buffer_get_style_scheme (GTK_SOURCE_BUFFER (active_doc));

	gchar *left_content = NULL;
	gchar *right_content = NULL;
	
	if (g == GROUP_STAGED)
	{
		gchar *ref_head = g_strconcat ("HEAD:", path, NULL);
		const gchar *a_left[] = {"git", "show", ref_head, NULL};
		left_content = get_git_output (p, a_left);
		g_free (ref_head);
		
		gchar *ref_index = g_strconcat (":", path, NULL);
		const gchar *a_right[] = {"git", "show", ref_index, NULL};
		right_content = get_git_output (p, a_right);
		g_free (ref_index);
	}
	else if (g == GROUP_CHANGED)
	{
		gchar *ref_index = g_strconcat (":", path, NULL);
		const gchar *a_left[] = {"git", "show", ref_index, NULL};
		left_content = get_git_output (p, a_left);
		g_free (ref_index);
		
		gchar *full_path = g_build_filename (p->repo, path, NULL);
		g_file_get_contents (full_path, &right_content, NULL, NULL);
		g_free (full_path);
	}
	
	GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default ();
	GtkSourceLanguage *language = gtk_source_language_manager_guess_language (manager, path, NULL);
	
	PlumaTab *tab = pluma_window_create_tab (p->window, TRUE);
	PlumaDocument *doc = pluma_tab_get_document (tab);
	pluma_tab_set_reusable (tab, FALSE);

	gchar *display_title = g_strconcat ("Diff: ", path, NULL);
	pluma_document_set_short_name_for_display (doc, display_title);
	g_free (display_title);
	

	GList *children = gtk_container_get_children (GTK_CONTAINER (tab));
	for (GList *l = children; l != NULL; l = l->next)
	{
		if (GTK_IS_OVERLAY (l->data))
		{
			gtk_widget_set_no_show_all (GTK_WIDGET (l->data), TRUE);
			gtk_widget_hide (GTK_WIDGET (l->data));
		}
	}
	g_list_free (children);
	
	GtkWidget *labels_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (labels_box), 4);
	
	GtkWidget *label_left = gtk_label_new (g == GROUP_STAGED ? _("Original (HEAD)") : _("Staged"));
	gtk_label_set_xalign (GTK_LABEL (label_left), 0.1);
	GtkWidget *label_right = gtk_label_new (g == GROUP_STAGED ? _("Staged") : _("Modified (Working Tree)"));
	gtk_label_set_xalign (GTK_LABEL (label_right), 0.1);
	
	gtk_box_pack_start (GTK_BOX (labels_box), label_left, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (labels_box), label_right, TRUE, TRUE, 0);
	
	gtk_box_pack_start (GTK_BOX (tab), labels_box, FALSE, FALSE, 0);
	
	GtkWidget *paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start (GTK_BOX (tab), paned, TRUE, TRUE, 0);
	
	PlumaDocument *doc_left = pluma_document_new ();
	PlumaDocument *doc_right = pluma_document_new ();
	GtkWidget *view_left = pluma_view_new (doc_left);
	GtkWidget *view_right = pluma_view_new (doc_right);
	g_object_unref (doc_left);
	g_object_unref (doc_right);
	
	g_object_set_data (G_OBJECT (doc_left), "PLUMA_TAB_KEY", tab);
	g_object_set_data (G_OBJECT (doc_right), "PLUMA_TAB_KEY", tab);
	g_object_set_data (G_OBJECT (view_left), "PLUMA_TAB_KEY", tab);
	g_object_set_data (G_OBJECT (view_right), "PLUMA_TAB_KEY", tab);
	
	gtk_text_view_set_editable (GTK_TEXT_VIEW (view_left), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view_left), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (view_left), TRUE);
	gtk_source_view_set_show_line_numbers (GTK_SOURCE_VIEW (view_left), TRUE);
	
	gtk_text_view_set_editable (GTK_TEXT_VIEW (view_right), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view_right), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (view_right), TRUE);
	gtk_source_view_set_show_line_numbers (GTK_SOURCE_VIEW (view_right), TRUE);
	
	if (language)
	{
		gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (doc_left), language);
		gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (doc_right), language);
	}

	if (style_scheme)
	{
		gtk_source_buffer_set_style_scheme (GTK_SOURCE_BUFFER (doc_left), style_scheme);
		gtk_source_buffer_set_style_scheme (GTK_SOURCE_BUFFER (doc_right), style_scheme);
	}

	GSettings *editor_settings = g_settings_new (PLUMA_SCHEMA_ID);
	gchar *font_name = NULL;
	if (!g_settings_get_boolean (editor_settings, "use-default-font"))
	{
		font_name = g_settings_get_string (editor_settings, "editor-font");
	}
	else
	{
		GSettings *interface_settings = g_settings_new ("org.mate.interface");
		if (interface_settings)
		{
			font_name = g_settings_get_string (interface_settings, "monospace-font-name");
			g_object_unref (interface_settings);
		}
	}
	g_object_unref (editor_settings);

	if (font_name)
	{
		PangoFontDescription *desc = pango_font_description_from_string (font_name);
		if (desc)
		{
			gchar *font_css_str = pluma_pango_font_description_to_css (desc);
			if (font_css_str)
			{
				gchar *css = g_strdup_printf ("textview { %s }", font_css_str);
				GtkCssProvider *provider = gtk_css_provider_new ();
				gtk_css_provider_load_from_data (provider, css, -1, NULL);
				gtk_style_context_add_provider (gtk_widget_get_style_context (view_left),
				                                GTK_STYLE_PROVIDER (provider),
				                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
				gtk_style_context_add_provider (gtk_widget_get_style_context (view_right),
				                                GTK_STYLE_PROVIDER (provider),
				                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
				g_object_unref (provider);
				g_free (css);
				g_free (font_css_str);
			}
			pango_font_description_free (desc);
		}
		g_free (font_name);
	}
	
	gchar **left_lines = g_strsplit (left_content ? left_content : "", "\n", -1);
	gchar **right_lines = g_strsplit (right_content ? right_content : "", "\n", -1);
	guint N = g_strv_length (left_lines);
	guint M = g_strv_length (right_lines);
	
	GString *left_out = g_string_new ("");
	GString *right_out = g_string_new ("");
	GArray *deleted_lines = g_array_new (FALSE, FALSE, sizeof(int));
	GArray *added_lines = g_array_new (FALSE, FALSE, sizeof(int));
	
	if (N <= 3000 && M <= 3000)
	{
		int **dp = g_malloc ((N + 1) * sizeof(int *));
		for (guint i = 0; i <= N; i++)
			dp[i] = g_malloc0 ((M + 1) * sizeof(int));
		
		for (guint i = 1; i <= N; i++)
		{
			for (guint j = 1; j <= M; j++)
			{
				if (g_strcmp0 (left_lines[i-1], right_lines[j-1]) == 0)
					dp[i][j] = dp[i-1][j-1] + 1;
				else
					dp[i][j] = MAX (dp[i-1][j], dp[i][j-1]);
			}
		}
		
		GArray *actions = g_array_new (FALSE, FALSE, sizeof(int));
		gint i = N, j = M;
		while (i > 0 || j > 0)
		{
			if (i > 0 && j > 0 && g_strcmp0 (left_lines[i-1], right_lines[j-1]) == 0)
			{
				int act = 0; // match
				g_array_append_val (actions, act);
				i--; j--;
			}
			else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j]))
			{
				int act = 2; // addition
				g_array_append_val (actions, act);
				j--;
			}
			else
			{
				int act = 1; // deletion
				g_array_append_val (actions, act);
				i--;
			}
		}
		
		for (guint k = 0; k <= N; k++)
			g_free (dp[k]);
		g_free (dp);
		
		gint left_idx = 0;
		gint right_idx = 0;
		gint current_line = 0;
		
		for (gint k = (gint)actions->len - 1; k >= 0; k--)
		{
			int act = g_array_index (actions, int, k);
			if (act == 0)
			{
				g_string_append_printf (left_out, "%s\n", left_lines[left_idx]);
				g_string_append_printf (right_out, "%s\n", right_lines[right_idx]);
				left_idx++;
				right_idx++;
			}
			else if (act == 1)
			{
				g_string_append_printf (left_out, "%s\n", left_lines[left_idx]);
				g_string_append (right_out, "\n");
				g_array_append_val (deleted_lines, current_line);
				left_idx++;
			}
			else if (act == 2)
			{
				g_string_append (left_out, "\n");
				g_string_append_printf (right_out, "%s\n", right_lines[right_idx]);
				g_array_append_val (added_lines, current_line);
				right_idx++;
			}
			current_line++;
		}
		g_array_unref (actions);
	}
	else
	{
		for (guint left_idx = 0; left_idx < N; left_idx++)
			g_string_append_printf (left_out, "%s\n", left_lines[left_idx]);
		for (guint right_idx = 0; right_idx < M; right_idx++)
			g_string_append_printf (right_out, "%s\n", right_lines[right_idx]);
	}
	
	g_strfreev (left_lines);
	g_strfreev (right_lines);
	
	GtkTextBuffer *left_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view_left));
	gtk_text_buffer_set_text (left_buffer, left_out->str, -1);
	g_string_free (left_out, TRUE);
	
	gboolean is_dark = is_dark_theme (style_scheme);
	const gchar *deleted_bg = is_dark ? "#451e20" : "#ffeef0";
	const gchar *added_bg = is_dark ? "#1b3220" : "#e6ffed";

	if (deleted_lines->len > 0)
	{
		GtkTextTag *tag_deleted = gtk_text_buffer_create_tag (left_buffer, "deleted",
		                                                      "paragraph-background", deleted_bg,
		                                                      NULL);
		for (guint k = 0; k < deleted_lines->len; k++)
		{
			gint line_num = g_array_index (deleted_lines, gint, k);
			GtkTextIter start, end;
			gtk_text_buffer_get_iter_at_line (left_buffer, &start, line_num);
			end = start;
			gtk_text_iter_forward_to_line_end (&end);
			gtk_text_buffer_apply_tag (left_buffer, tag_deleted, &start, &end);
		}
	}
	g_array_unref (deleted_lines);
	
	GtkTextBuffer *right_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view_right));
	gtk_text_buffer_set_text (right_buffer, right_out->str, -1);
	g_string_free (right_out, TRUE);
	
	if (added_lines->len > 0)
	{
		GtkTextTag *tag_added = gtk_text_buffer_create_tag (right_buffer, "added",
		                                                    "paragraph-background", added_bg,
		                                                    NULL);
		for (guint k = 0; k < added_lines->len; k++)
		{
			gint line_num = g_array_index (added_lines, gint, k);
			GtkTextIter start, end;
			gtk_text_buffer_get_iter_at_line (right_buffer, &start, line_num);
			end = start;
			gtk_text_iter_forward_to_line_end (&end);
			gtk_text_buffer_apply_tag (right_buffer, tag_added, &start, &end);
		}
	}
	g_array_unref (added_lines);
	
	GtkWidget *scroll_left = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (scroll_left), view_left);
	gtk_paned_pack1 (GTK_PANED (paned), scroll_left, TRUE, TRUE);
	
	GtkWidget *scroll_right = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (scroll_right), view_right);
	gtk_paned_pack2 (GTK_PANED (paned), scroll_right, TRUE, TRUE);
	
	ScrollSync *sync_v = g_new0 (ScrollSync, 1);
	sync_v->adj_left = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll_left));
	sync_v->adj_right = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll_right));
	
	g_signal_connect (sync_v->adj_left, "value-changed", G_CALLBACK (on_scroll_left), sync_v);
	g_signal_connect (sync_v->adj_right, "value-changed", G_CALLBACK (on_scroll_right), sync_v);
	
	ScrollSync *sync_h = g_new0 (ScrollSync, 1);
	sync_h->adj_left = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (scroll_left));
	sync_h->adj_right = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (scroll_right));
	
	g_signal_connect (sync_h->adj_left, "value-changed", G_CALLBACK (on_scroll_left), sync_h);
	g_signal_connect (sync_h->adj_right, "value-changed", G_CALLBACK (on_scroll_right), sync_h);
	
	g_signal_connect (tab, "destroy", G_CALLBACK (on_tab_destroy_disconnect_sync), sync_v);
	g_signal_connect (tab, "destroy", G_CALLBACK (on_tab_destroy_disconnect_sync), sync_h);
	
	g_free (left_content);
	g_free (right_content);
	g_free (path);
	
	gtk_widget_show_all (GTK_WIDGET (tab));
}

static void
update_selection_sensitivity (PlumaGitPanel *panel)
{
	gchar *path = NULL;
	gint group = 0;
	gint status = 0;
	gboolean has_file = selected_file (panel, &path, &group, &status);

	if (panel->open_button == NULL || panel->stage_button == NULL || panel->discard_button == NULL)
	{
		g_free (path);
		return;
	}

	if (has_file)
	{
		gtk_widget_set_sensitive (panel->open_button, TRUE);
		gtk_widget_set_sensitive (panel->discard_button, (group == GROUP_CHANGED || group == GROUP_UNTRACKED));
		if (group == GROUP_STAGED)
		{
			gtk_button_set_image (GTK_BUTTON (panel->stage_button), gtk_image_new_from_icon_name ("list-remove", GTK_ICON_SIZE_MENU));
			gtk_widget_set_tooltip_text (panel->stage_button, _("Unstage selected file"));
		}
		else
		{
			gtk_button_set_image (GTK_BUTTON (panel->stage_button), gtk_image_new_from_icon_name ("list-add", GTK_ICON_SIZE_MENU));
			gtk_widget_set_tooltip_text (panel->stage_button, _("Stage selected file"));
		}
		gtk_widget_set_sensitive (panel->stage_button, TRUE);
		g_free (path);
	}
	else
	{
		gtk_widget_set_sensitive (panel->open_button, FALSE);
		gtk_widget_set_sensitive (panel->stage_button, FALSE);
		gtk_widget_set_sensitive (panel->discard_button, FALSE);
	}
}

static void
selection_changed_cb (GtkTreeSelection *selection, gpointer data)
{
	PlumaGitPanel *panel = data;
	update_selection_sensitivity (panel);
}

static void
stage_clicked_cb (PlumaGitPanel *p)
{
	gchar *path;
	gint g, s;
	if (selected_file (p, &path, &g, &s))
	{
		if (g != GROUP_UNTRACKED && selected_diff_is_binary (p, path, g == GROUP_STAGED))
		{
			gtk_label_set_text (GTK_LABEL (p->summary_label),
			                    _("Binary files cannot be displayed as a text diff."));
			g_free (path);
			return;
		}
		if (g == GROUP_STAGED)
		{
			const gchar *a[] = {"git", "reset", "-q", "HEAD", "--", path, NULL};
			run_git (p, a, FALSE, NULL);
		}
		else
		{
			const gchar *a[] = {"git", "add", "--", path, NULL};
			run_git (p, a, FALSE, NULL);
		}
		g_free (path);
	}
}

static void row_activated(GtkTreeView*v,GtkTreePath*path,GtkTreeViewColumn*c,gpointer data){PlumaGitPanel*p=data;gchar*file;gint group,status;if(selected_file(p,&file,&group,&status)){g_free(file);if(group==GROUP_UNTRACKED)open_selected(p);else open_side_by_side_diff(p);}}
static void
compare_file_reference (PlumaGitPanel *panel)
{
	gchar *path = NULL, *reference;
	gint group, status;

	if (!selected_file (panel, &path, &group, &status))
		return;
	reference = prompt_revision (panel, _("Compare File"),
	                             _("Branch, tag, or commit"));
	if (reference != NULL)
	{
		const gchar *argv[] = {"git", "diff", reference, "--", path, NULL};
		gchar *title = g_strdup_printf (_("Diff: %s against %s"), path, reference);
		run_git (panel, argv, TRUE, title);
		g_free (title);
		g_free (reference);
	}
	g_free (path);
}
static gboolean menu_popup(GtkWidget*w,GdkEventButton*e,gpointer data)
{
	PlumaGitPanel*p=data;GtkWidget*m,*i;GtkTreePath*tree_path=NULL;gchar*path=NULL;gint g,s;
	if(e->button!=3)return FALSE;
	if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w),(gint)e->x,(gint)e->y,&tree_path,NULL,NULL,NULL)){gtk_tree_view_set_cursor(GTK_TREE_VIEW(w),tree_path,NULL,FALSE);gtk_tree_path_free(tree_path);}
	if(!selected_file(p,&path,&g,&s)){g_free(path);return FALSE;}g_free(path);m=gtk_menu_new();
#define ITEM(label,func) i=gtk_menu_item_new_with_label(label);g_signal_connect_swapped(i,"activate",G_CALLBACK(func),p);gtk_menu_shell_append(GTK_MENU_SHELL(m),i)
	ITEM(_("Open File"),open_selected);
	if(g!=GROUP_UNTRACKED) {
		ITEM(_("Open Unified Diff"),diff_selected);
		ITEM(_("Open Side-by-Side Diff"),open_side_by_side_diff);
		ITEM(_("Compare with Reference…"),compare_file_reference);
	}
	if(g==GROUP_STAGED) {
		ITEM(_("Unstage"),unstage_selected);
		ITEM(_("Unstage Hunk…"),unstage_hunk_selected);
	}
	else { ITEM(_("Stage"),stage_selected); }
	if(g==GROUP_CHANGED) {
		ITEM(_("Stage Hunk…"),stage_hunk_selected);
		ITEM(_("Discard Changes"),discard_selected);
		ITEM(_("Discard Hunk…"),discard_hunk_selected);
	}
	if(g==GROUP_UNTRACKED) { ITEM(_("Delete Permanently"),discard_selected); }
#undef ITEM
	gtk_widget_show_all(m);gtk_menu_popup_at_pointer(GTK_MENU(m),(GdkEvent*)e);return TRUE;
}

static void commit_clicked(GtkButton*b,gpointer data)
{
	PlumaGitPanel*p=data;
	GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->message_entry));
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds (buf, &start, &end);
	gchar *message = gtk_text_buffer_get_text (buf, &start, &end, FALSE);
	if (pluma_git_commit_message_is_valid (message))
	{
		if (p->amend_mode)
		{
			if (confirm_action (p, _("Amend the last commit?"),
			                    _("The last commit will be replaced and its hash will change.")))
			{
				const gchar *a[]={"git","commit","--amend","-m",message,NULL};
				run_git(p,a,FALSE,"amend");
			}
		}
		else
		{
			const gchar*a[]={"git","commit","-m",message,NULL};
			run_git(p,a,FALSE,"commit");
		}
	}
	g_free (message);
}
static void
stage_all_commit_clicked (GtkMenuItem *item, gpointer data)
{
	PlumaGitPanel *panel = data;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (panel->message_entry));
	GtkTextIter start, end;
	gchar *message;

	gtk_text_buffer_get_bounds (buffer, &start, &end);
	message = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	if (pluma_git_commit_message_is_valid (message))
		stage_all_then_commit (panel, message);
	else
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("Enter a valid commit message first."));
	g_free (message);
}
static void
amend_commit_clicked (GtkMenuItem *item, gpointer data)
{
	PlumaGitPanel *panel = data;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (panel->message_entry));
	const gchar *argv[] = {"git", "log", "-1", "--format=%B", NULL};
	gchar *message = get_git_output (panel, argv);

	if (!pluma_git_commit_message_is_valid (message))
	{
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("There is no commit message available to amend."));
	}
	else
	{
		g_strchomp (message);
		panel->amend_mode = TRUE;
		gtk_text_buffer_set_text (buffer, message, -1);
		gtk_button_set_label (GTK_BUTTON (panel->commit_button), _("Amend Commit"));
		gtk_widget_grab_focus (panel->message_entry);
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("Edit the message, then select Amend Commit."));
	}
	g_free (message);
}
static void
commit_with_option (PlumaGitPanel *panel, const gchar *option)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (panel->message_entry));
	GtkTextIter start, end;
	gchar *message;

	gtk_text_buffer_get_bounds (buffer, &start, &end);
	message = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	if (pluma_git_commit_message_is_valid (message))
	{
		const gchar *argv[] = {"git", "commit", option, "-m", message, NULL};
		run_git (panel, argv, FALSE, "commit");
	}
	else
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("Enter a valid commit message first."));
	g_free (message);
}

static void
commit_signoff_clicked (GtkMenuItem *item, gpointer data)
{
	commit_with_option (data, "--signoff");
}

static void
commit_signed_clicked (GtkMenuItem *item, gpointer data)
{
	commit_with_option (data, "-S");
}
static void stage_all_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","add","-A",NULL};run_git(p,a,FALSE,NULL);}
static void refresh_clicked(GtkButton*b,gpointer data){pluma_git_panel_refresh(data);}
static void history_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","log","--graph","--decorate","--oneline","--all","-n","500",NULL};run_git(p,a,TRUE,_("Git History"));}
static void
current_file_history_clicked (GtkMenuItem *item, gpointer data)
{
	PlumaGitPanel *panel = data;
	PlumaDocument *document = pluma_window_get_active_document (panel->window);
	GFile *location = document != NULL ? pluma_document_get_location (document) : NULL;
	GFile *root = panel->repo != NULL ? g_file_new_for_path (panel->repo) : NULL;
	gchar *relative = location != NULL && root != NULL
	                ? g_file_get_relative_path (root, location) : NULL;

	if (relative == NULL)
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("The active document is not inside the Git repository."));
	else
	{
		const gchar *argv[] = {"git", "log", "--follow", "--date=short",
		                       "--format=%h  %ad  %an  %s", "--", relative, NULL};
		gchar *title = g_strdup_printf (_("History: %s"), relative);
		run_git (panel, argv, TRUE, title);
		g_free (title);
	}
	g_free (relative);
	g_clear_object (&root);
	g_clear_object (&location);
}
static void branches_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","branch","-vv","--all",NULL};run_git(p,a,TRUE,_("Git Branches"));}
static void tags_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","tag","-n",NULL};run_git(p,a,TRUE,_("Git Tags"));}
static void remotes_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","remote","-v",NULL};run_git(p,a,TRUE,_("Git Remotes"));}
static void fetch_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","fetch","--all","--prune",NULL};run_git(p,a,FALSE,NULL);}
static void pull_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;if(!ensure_documents_saved(p,_("pulling changes"),NULL))return;const gchar*a[]={"git","pull",NULL};run_git(p,a,FALSE,NULL);}
static void push_clicked(GtkButton*b,gpointer data)
{
	PlumaGitPanel*p=data;
	if (p->upstream && *p->upstream && p->ahead > 0 && p->behind > 0)
	{
		if (confirm_action (p, _("The local and remote histories have diverged."),
		                    _("If this divergence was caused by amend, force-with-lease can safely replace the remote commit unless the remote changed again.")))
		{
			const gchar *a[]={"git","push","--force-with-lease",NULL};
			run_git(p,a,FALSE,NULL);
		}
		return;
	}
	if (p->upstream && *p->upstream)
	{
		const gchar*a[]={"git","push",NULL};
		run_git(p,a,FALSE,NULL);
	}
	else if (p->current_branch && *p->current_branch)
	{
		const gchar*a[]={"git","push","-u","origin",p->current_branch,NULL};
		run_git(p,a,FALSE,NULL);
	}
	else
	{
		const gchar*a[]={"git","push",NULL};
		run_git(p,a,FALSE,NULL);
	}
}
static void
force_push_with_lease_clicked (GtkMenuItem *item, gpointer data)
{
	PlumaGitPanel *panel = data;

	if (panel->upstream == NULL || *panel->upstream == '\0')
	{
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("Publish the branch before using force push with lease."));
		return;
	}
	if (confirm_action (panel, _("Force push the amended history?"),
	                    _("Force-with-lease updates the remote only if nobody else has changed it.")))
	{
		const gchar *argv[] = {"git", "push", "--force-with-lease", NULL};
		run_git (panel, argv, FALSE, NULL);
	}
}
static void stash_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","stash","push","-u",NULL};run_git(p,a,FALSE,NULL);}
static void stash_pop_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;if(!ensure_documents_saved(p,_("applying a stash"),NULL))return;const gchar*a[]={"git","stash","pop",NULL};run_git(p,a,FALSE,NULL);}
static void stash_list_clicked(GtkButton*b,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","stash","list",NULL};run_git(p,a,TRUE,_("Git Stashes"));}
static void diff_selected_clicked(GtkButton*b,gpointer data)
{
	PlumaGitPanel*p=data;
	gchar *path = NULL;
	gint g = 0, s = 0;
	if (selected_file (p, &path, &g, &s))
	{
		if (g == GROUP_STAGED)
		{
			const gchar*a[]={"git","diff","--cached","--",path,NULL};
			gchar *title=g_strconcat("Diff: ",path,NULL);
			run_git(p,a,TRUE,title);
			g_free(title);
		}
		else if (g == GROUP_CHANGED)
		{
			const gchar*a[]={"git","diff","--",path,NULL};
			gchar *title=g_strconcat("Diff: ",path,NULL);
			run_git(p,a,TRUE,title);
			g_free(title);
		}
		else if (g == GROUP_UNTRACKED)
		{
			gchar *full_path = g_build_filename (p->repo, path, NULL);
			const gchar*a[]={"git","diff","--no-index","--","/dev/null",full_path,NULL};
			gchar *title=g_strconcat("Diff: ",path,NULL);
			run_git(p,a,TRUE,title);
			g_free(title);
			g_free(full_path);
		}
		g_free (path);
	}
	else
	{
		gtk_label_set_text (GTK_LABEL (p->summary_label), _("No file selected in Source Control to diff."));
	}
}

static gchar *
prompt_value(PlumaGitPanel*p,const gchar*title,const gchar*label)
{
	GtkWidget*d=gtk_dialog_new_with_buttons(title,GTK_WINDOW(p->window),GTK_DIALOG_MODAL,_("Cancel"),GTK_RESPONSE_CANCEL,_("Continue"),GTK_RESPONSE_ACCEPT,NULL);
	GtkWidget*box=gtk_dialog_get_content_area(GTK_DIALOG(d));GtkWidget*entry=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(entry),label);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_box_pack_start(GTK_BOX(box),entry,FALSE,FALSE,0);gtk_widget_show_all(d);gchar*value=NULL;
	if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT &&
	    *gtk_entry_get_text (GTK_ENTRY (entry)))
		value = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	gtk_widget_destroy (d);
	return value;
}
static gboolean
revision_is_valid (PlumaGitPanel *panel, const gchar *revision)
{
	gchar *commit = g_strconcat (revision, "^{commit}", NULL);
	const gchar *argv[] = {"git", "rev-parse", "--verify", "--quiet",
	                       "--end-of-options", commit, NULL};
	GError *error = NULL;
	GSubprocess *process = spawn_git (panel, argv, &error);
	gboolean valid = FALSE;

	if (process != NULL)
	{
		g_subprocess_communicate_utf8 (process, NULL, NULL, NULL, NULL, &error);
		valid = error == NULL && g_subprocess_get_successful (process);
		g_object_unref (process);
	}
	g_clear_error (&error);
	g_free (commit);
	return valid;
}

static gchar *
prompt_revision (PlumaGitPanel *panel, const gchar *title, const gchar *label)
{
	while (TRUE)
	{
		gchar *revision = prompt_value (panel, title, label);
		if (revision == NULL)
			return NULL;
		g_strstrip (revision);
		if (revision_is_valid (panel, revision))
			return revision;
		gtk_label_set_text (GTK_LABEL (panel->summary_label),
		                    _("The branch, tag, or commit could not be resolved."));
		g_free (revision);
	}
}

static gboolean
prompt_comparison_revisions (PlumaGitPanel *panel, gchar **base, gchar **target)
{
	GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Compare References"),
	                                                GTK_WINDOW (panel->window),
	                                                GTK_DIALOG_MODAL,
	                                                _("Cancel"), GTK_RESPONSE_CANCEL,
	                                                _("Compare"), GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *grid = gtk_grid_new ();
	GtkWidget *base_entry = gtk_entry_new ();
	GtkWidget *target_entry = gtk_entry_new ();
	GtkWidget *error_label = gtk_label_new (NULL);
	gboolean accepted = FALSE;

	*base = NULL;
	*target = NULL;
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 8);
	gtk_grid_attach (GTK_GRID (grid), gtk_label_new (_("Base")), 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), base_entry, 1, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), gtk_label_new (_("Target")), 0, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), target_entry, 1, 1, 1, 1);
	gtk_widget_set_halign (error_label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), error_label, 0, 2, 2, 1);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
	                    grid, TRUE, TRUE, 0);
	gtk_widget_show_all (dialog);

	while (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *candidate_base = g_strdup (gtk_entry_get_text (GTK_ENTRY (base_entry)));
		gchar *candidate_target = g_strdup (gtk_entry_get_text (GTK_ENTRY (target_entry)));
		g_strstrip (candidate_base);
		g_strstrip (candidate_target);
		if (revision_is_valid (panel, candidate_base) &&
		    revision_is_valid (panel, candidate_target))
		{
			*base = candidate_base;
			*target = candidate_target;
			accepted = TRUE;
			break;
		}
		gtk_label_set_text (GTK_LABEL (error_label),
		                    _("Both values must resolve to a branch, tag, or commit."));
		g_free (candidate_base);
		g_free (candidate_target);
	}
	gtk_widget_destroy (dialog);
	return accepted;
}
static void
stash_diff_clicked (GtkMenuItem *item, gpointer data)
{
	PlumaGitPanel *panel = data;
	gchar *reference = prompt_value (panel, _("View Stash Diff"),
	                                 _("Stash reference (for example: stash@{0})"));
	if (reference != NULL)
	{
		const gchar *argv[] = {"git", "stash", "show", "--patch", "--stat",
		                       reference, NULL};
		gchar *title = g_strdup_printf ("Diff: %s", reference);
		run_git (panel, argv, TRUE, title);
		g_free (title);
		g_free (reference);
	}
}
static void
compare_references_clicked (GtkMenuItem *item, gpointer data)
{
	PlumaGitPanel *panel = data;
	gchar *base, *target;

	if (prompt_comparison_revisions (panel, &base, &target))
	{
		const gchar *argv[] = {"git", "diff", base, target, NULL};
		gchar *title = g_strdup_printf (_("Diff: %s against %s"), base, target);
		run_git (panel, argv, TRUE, title);
		g_free (title);
		g_free (target);
		g_free (base);
	}
}
static gboolean confirm_action(PlumaGitPanel*p,const gchar*primary,const gchar*secondary)
{GtkWidget*d=gtk_message_dialog_new(GTK_WINDOW(p->window),GTK_DIALOG_MODAL,GTK_MESSAGE_WARNING,GTK_BUTTONS_CANCEL,"%s",primary);gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),"%s",secondary);gtk_dialog_add_button(GTK_DIALOG(d),_("Continue"),GTK_RESPONSE_ACCEPT);gboolean ok=gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT;gtk_widget_destroy(d);return ok;}
static void new_branch(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Create Branch"),_("Branch name"));if(v){const gchar*a[]={"git","switch","-c",v,NULL};run_git(p,a,FALSE,NULL);g_free(v);}}
static void switch_branch(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Switch Branch"),_("Branch or reference"));if(v){if(ensure_documents_saved(p,_("switching branches"),NULL)){const gchar*a[]={"git","switch",v,NULL};run_git(p,a,FALSE,NULL);}g_free(v);}}
static void delete_branch(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Delete Branch"),_("Local branch name"));if(v){gchar*q=g_strdup_printf(_("Delete local branch “%s”?"),v);if(confirm_action(p,q,_("The branch is deleted only if Git considers it fully merged."))){const gchar*a[]={"git","branch","-d",v,NULL};run_git(p,a,FALSE,NULL);}g_free(q);g_free(v);}}
static void new_tag(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Create Tag"),_("Tag name"));if(v){const gchar*a[]={"git","tag",v,NULL};run_git(p,a,FALSE,NULL);g_free(v);}}
static void add_remote(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*n=prompt_value(p,_("Add Remote"),_("Remote name"));if(n){gchar*u=prompt_value(p,_("Add Remote"),_("Remote URL"));if(u){const gchar*a[]={"git","remote","add",n,u,NULL};run_git(p,a,FALSE,NULL);g_free(u);}g_free(n);}}
static void remove_remote(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Remove Remote"),_("Remote name"));if(v){gchar*q=g_strdup_printf(_("Remove remote “%s”?"),v);if(confirm_action(p,q,_("Local remote-tracking configuration will be removed."))){const gchar*a[]={"git","remote","remove",v,NULL};run_git(p,a,FALSE,NULL);}g_free(q);g_free(v);}}
static void merge_ref(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Merge"),_("Branch or commit"));if(v){if(ensure_documents_saved(p,_("merging"),NULL)){const gchar*a[]={"git","merge",v,NULL};run_git(p,a,FALSE,NULL);}g_free(v);}}
static void rebase_ref(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Rebase"),_("Upstream branch or commit"));if(v&&ensure_documents_saved(p,_("rebasing"),NULL)&&confirm_action(p,_("Rebase the current branch?"),_("Commits will be replayed and conflicts may require manual resolution."))){const gchar*a[]={"git","rebase",v,NULL};run_git(p,a,FALSE,NULL);}g_free(v);}
static void cherry_pick_ref(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Cherry-pick"),_("Commit hash"));if(v){if(ensure_documents_saved(p,_("cherry-picking"),NULL)){const gchar*a[]={"git","cherry-pick",v,NULL};run_git(p,a,FALSE,NULL);}g_free(v);}}
static void revert_ref(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;gchar*v=prompt_value(p,_("Revert Commit"),_("Commit hash"));if(v&&ensure_documents_saved(p,_("reverting a commit"),NULL)&&confirm_action(p,_("Create a revert commit?"),_("Git will create a new commit that reverses the selected commit."))){const gchar*a[]={"git","revert","--no-edit",v,NULL};run_git(p,a,FALSE,NULL);}g_free(v);}
static void merge_continue(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","merge","--continue",NULL};run_git(p,a,FALSE,NULL);}
static void merge_abort(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;if(ensure_documents_saved(p,_("aborting the merge"),NULL)&&confirm_action(p,_("Abort merge?"),_("Merge progress and conflict resolutions will be discarded."))){const gchar*a[]={"git","merge","--abort",NULL};run_git(p,a,FALSE,NULL);}}
static void rebase_continue(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;const gchar*a[]={"git","-c","core.editor=true","rebase","--continue",NULL};run_git(p,a,FALSE,NULL);}
static void rebase_abort(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;if(ensure_documents_saved(p,_("aborting the rebase"),NULL)&&confirm_action(p,_("Abort rebase?"),_("The branch will return to its state before the rebase."))){const gchar*a[]={"git","rebase","--abort",NULL};run_git(p,a,FALSE,NULL);}}
static void cherry_abort(GtkMenuItem*i,gpointer data){PlumaGitPanel*p=data;if(ensure_documents_saved(p,_("aborting the cherry-pick"),NULL)&&confirm_action(p,_("Abort cherry-pick?"),_("Cherry-pick progress will be discarded."))){const gchar*a[]={"git","cherry-pick","--abort",NULL};run_git(p,a,FALSE,NULL);}}
static void add_more_item(GtkWidget*menu,const gchar*label,GCallback callback,PlumaGitPanel*p){GtkWidget*i=gtk_menu_item_new_with_label(label);g_signal_connect(i,"activate",callback,p);gtk_menu_shell_append(GTK_MENU_SHELL(menu),i);}
static void monitor_changed(GFileMonitor*m,GFile*f,GFile*o,GFileMonitorEvent e,gpointer data){schedule_refresh(data);}
static gboolean poll_status(gpointer data){PlumaGitPanel*p=data;if(gtk_widget_get_mapped(GTK_WIDGET(p)))schedule_refresh(p);return G_SOURCE_CONTINUE;}

static void
filter_changed_cb (GtkSearchEntry *entry, gpointer data)
{
	PlumaGitPanel *panel = data;
	g_free (panel->filter_query);
	const gchar *text = gtk_entry_get_text (GTK_ENTRY (entry));
	panel->filter_query = (text && *text) ? g_strdup (text) : NULL;
	parse_status (panel, (const guint8 *) panel->last_status_output,
	              panel->last_status_length);
}

static void
pluma_git_panel_dispose(GObject*object)
{
	PlumaGitPanel*p=PLUMA_GIT_PANEL(object);p->destroyed=TRUE;if(p->cancellable)g_cancellable_cancel(p->cancellable);if(p->refresh_source){g_source_remove(p->refresh_source);p->refresh_source=0;}if(p->poll_source){g_source_remove(p->poll_source);p->poll_source=0;}g_clear_object(&p->git_monitor);g_clear_object(&p->cancellable);g_clear_pointer(&p->repo,g_free);
	g_clear_pointer (&p->filter_query, g_free);
	g_clear_pointer (&p->last_status_output, g_free);
	g_clear_pointer (&p->outgoing_commits, g_free);
	g_clear_pointer (&p->current_branch, g_free);
	g_clear_pointer (&p->upstream, g_free);
	g_clear_object (&p->settings);
	G_OBJECT_CLASS(pluma_git_panel_parent_class)->dispose(object);
}
static void pluma_git_panel_class_init(PlumaGitPanelClass*k){G_OBJECT_CLASS(k)->dispose=pluma_git_panel_dispose;}

static void
on_commit_message_buffer_changed (GtkTextBuffer *buffer, gpointer data)
{
	PlumaGitPanel *panel = data;
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	gchar *text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	gboolean valid = pluma_git_commit_message_is_valid (text);
	guint length = pluma_git_commit_message_length (text);
	guint limit = g_settings_get_uint (panel->settings, "git-commit-message-limit");
	gchar *counter = g_strdup_printf ("%u/%u", length, limit);
	GtkStyleContext *context = gtk_widget_get_style_context (panel->commit_count_label);

	gtk_widget_set_visible (panel->clear_message_button, text != NULL && *text != '\0');
	gtk_widget_set_sensitive (panel->commit_button, valid);
	gtk_label_set_text (GTK_LABEL (panel->commit_count_label), counter);
	if (length > limit)
		gtk_style_context_add_class (context, GTK_STYLE_CLASS_ERROR);
	else
		gtk_style_context_remove_class (context, GTK_STYLE_CLASS_ERROR);
	g_free (counter);
	g_free (text);
}

static void
on_clear_btn_clicked (GtkButton *btn, gpointer data)
{
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER (data);
	gtk_text_buffer_set_text (buffer, "", -1);
}

static gboolean
on_message_view_draw (GtkWidget *widget, cairo_t *cr, gpointer data)
{
	GtkTextView *text_view = GTK_TEXT_VIEW (widget);
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (text_view);
	if (gtk_text_buffer_get_char_count (buffer) == 0)
	{
		GtkStyleContext *context = gtk_widget_get_style_context (widget);
		PangoLayout *layout = gtk_widget_create_pango_layout (widget, _("Commit message"));
		GdkRGBA color;
		gtk_style_context_get_color (context, gtk_style_context_get_state (context), &color);
		color.alpha = 0.45;
		gdk_cairo_set_source_rgba (cr, &color);
		int left_margin = gtk_text_view_get_left_margin (text_view);
		int top_margin = gtk_text_view_get_top_margin (text_view);
		cairo_move_to (cr, left_margin + 2, top_margin);
		pango_cairo_show_layout (cr, layout);
		g_object_unref (layout);
	}
	return FALSE;
}

static void
pluma_git_panel_init(PlumaGitPanel*p)
{
	GtkWidget*row,*button,*scroll,*more,*menu;GtkCellRenderer*r;GtkTreeViewColumn*c;
	gtk_orientable_set_orientation(GTK_ORIENTABLE(p),GTK_ORIENTATION_VERTICAL);gtk_box_set_spacing(GTK_BOX(p),4);gtk_container_set_border_width(GTK_CONTAINER(p),6);p->cancellable=g_cancellable_new();p->poll_source=g_timeout_add_seconds(2,poll_status,p);
	p->settings = g_settings_new (PLUMA_SCHEMA_ID);
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);p->branch_label=gtk_label_new("");gtk_label_set_xalign(GTK_LABEL(p->branch_label),0);button=gtk_button_new_from_icon_name("view-refresh",GTK_ICON_SIZE_MENU);gtk_widget_set_tooltip_text(button,_("Refresh Git status"));g_signal_connect(button,"clicked",G_CALLBACK(refresh_clicked),p);gtk_box_pack_start(GTK_BOX(row),p->branch_label,TRUE,TRUE,0);gtk_box_pack_end(GTK_BOX(row),button,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(p),row,FALSE,FALSE,0);
	p->filter_entry = gtk_search_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (p->filter_entry), _("Filter changes"));
	gtk_widget_set_tooltip_text (p->filter_entry, _("Filter changes by file name"));
	g_signal_connect (p->filter_entry, "search-changed", G_CALLBACK (filter_changed_cb), p);
	gtk_box_pack_start (GTK_BOX (p), p->filter_entry, FALSE, FALSE, 0);
	p->summary_label=gtk_label_new("");gtk_label_set_xalign(GTK_LABEL(p->summary_label),0);gtk_box_pack_start(GTK_BOX(p),p->summary_label,FALSE,FALSE,0);

	GtkWidget *main_paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
	gtk_box_pack_start (GTK_BOX (p), main_paned, TRUE, TRUE, 0);

	GtkWidget *top_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_paned_pack1 (GTK_PANED (main_paned), top_box, TRUE, FALSE);

	p->store=gtk_tree_store_new(N_COLS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_INT,G_TYPE_BOOLEAN);p->tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(p->store));gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(p->tree),FALSE);r=gtk_cell_renderer_text_new();
	c=gtk_tree_view_column_new_with_attributes(_("Source Control"),r,"text",COL_LABEL,NULL);
	gtk_tree_view_column_set_cell_data_func (c, r, source_control_cell_data, NULL, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(p->tree),c);g_signal_connect(p->tree,"row-activated",G_CALLBACK(row_activated),p);g_signal_connect(p->tree,"button-press-event",G_CALLBACK(menu_popup),p);scroll=gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll),p->tree);
	gtk_box_pack_start(GTK_BOX(top_box),scroll,TRUE,TRUE,0);
	atk_object_set_name(gtk_widget_get_accessible(p->tree),_("Git changes"));
	
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
	button=gtk_button_new_with_label(_("Stage All"));
	gtk_widget_set_tooltip_text(button,_("Stage all changes"));
	g_signal_connect(button,"clicked",G_CALLBACK(stage_all_clicked),p);
	gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0);

	p->open_button=gtk_button_new_from_icon_name("document-open",GTK_ICON_SIZE_MENU);
	gtk_widget_set_tooltip_text(p->open_button,_("Open selected file"));
	g_signal_connect_swapped(p->open_button,"clicked",G_CALLBACK(open_selected),p);
	gtk_box_pack_start(GTK_BOX(row),p->open_button,FALSE,FALSE,0);

	p->stage_button=gtk_button_new_from_icon_name("list-add",GTK_ICON_SIZE_MENU);
	gtk_widget_set_tooltip_text(p->stage_button,_("Stage selected file"));
	g_signal_connect_swapped(p->stage_button,"clicked",G_CALLBACK(stage_clicked_cb),p);
	gtk_box_pack_start(GTK_BOX(row),p->stage_button,FALSE,FALSE,0);

	p->discard_button=gtk_button_new_from_icon_name("edit-clear",GTK_ICON_SIZE_MENU);
	gtk_widget_set_tooltip_text(p->discard_button,_("Discard changes in selected file"));
	g_signal_connect_swapped(p->discard_button,"clicked",G_CALLBACK(discard_selected),p);
	gtk_box_pack_start(GTK_BOX(row),p->discard_button,FALSE,FALSE,0);

	gtk_box_pack_start(GTK_BOX(top_box),row,FALSE,FALSE,0);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->tree)),"changed",G_CALLBACK(selection_changed_cb),p);

	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
	gtk_box_set_homogeneous(GTK_BOX(row),TRUE);
#define TOOL(icon,tip,cb) button=gtk_button_new_from_icon_name(icon,GTK_ICON_SIZE_MENU);gtk_widget_set_tooltip_text(button,tip);g_signal_connect(button,"clicked",G_CALLBACK(cb),p);gtk_box_pack_start(GTK_BOX(row),button,TRUE,TRUE,0)
	TOOL("document-open-recent",_("History"),history_clicked);TOOL("vcs-branch",_("Branches"),branches_clicked);TOOL("bookmark-new",_("Tags"),tags_clicked);TOOL("network-server",_("Remotes"),remotes_clicked);TOOL("package-x-generic",_("Stashes"),stash_list_clicked);TOOL("text-x-patch",_("Diff selected file"),diff_selected_clicked);
#undef TOOL
	gtk_widget_set_margin_bottom (row, 6);
	gtk_box_pack_start(GTK_BOX(top_box),row,FALSE,FALSE,0);

	GtkWidget *commit_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_size_request (commit_box, 1, -1);
	gtk_widget_set_hexpand (commit_box, TRUE);
	gtk_paned_pack2 (GTK_PANED (main_paned), commit_box, FALSE, FALSE);

	GtkWidget *overlay = gtk_overlay_new ();
	gtk_widget_set_size_request (overlay, 1, -1);
	gtk_widget_set_hexpand (overlay, TRUE);
	gtk_widget_set_margin_top (overlay, 6);
	gtk_widget_set_margin_bottom (overlay, 6);
	GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (sw), FALSE);
	gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW (sw), 1);
	gtk_scrolled_window_set_max_content_width (GTK_SCROLLED_WINDOW (sw), 1);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);
	gtk_widget_set_size_request (sw, 1, 24);

	GtkWidget *message_view = g_object_new (pluma_commit_text_view_get_type (), NULL);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (message_view), GTK_WRAP_CHAR);
	/* Long unbroken messages must wrap instead of increasing the panel's minimum width. */
	gtk_widget_set_size_request (message_view, 1, -1);
	gtk_widget_set_hexpand (message_view, TRUE);
	gtk_text_view_set_top_margin (GTK_TEXT_VIEW (message_view), 0);
	gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (message_view), 0);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (message_view), 4);
	/* Keep text clear of the overlayed clear button. */
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (message_view), 34);
	g_signal_connect_after (message_view, "draw", G_CALLBACK (on_message_view_draw), NULL);
	gtk_container_add (GTK_CONTAINER (sw), message_view);
	gtk_container_add (GTK_CONTAINER (overlay), sw);

	GtkWidget *clear_btn = gtk_button_new_from_icon_name ("edit-clear-symbolic", GTK_ICON_SIZE_MENU);
	gtk_widget_set_tooltip_text (clear_btn, _("Clear text"));
	gtk_widget_set_halign (clear_btn, GTK_ALIGN_END);
	gtk_widget_set_valign (clear_btn, GTK_ALIGN_END);
	gtk_widget_set_margin_end (clear_btn, 4);
	gtk_widget_set_margin_bottom (clear_btn, 4);
	gtk_widget_set_no_show_all (clear_btn, TRUE);
	gtk_overlay_add_overlay (GTK_OVERLAY (overlay), clear_btn);
	gtk_widget_set_visible (clear_btn, FALSE);

	GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (message_view));
	g_signal_connect (clear_btn, "clicked", G_CALLBACK (on_clear_btn_clicked), buf);

	p->message_entry = message_view;
	p->clear_message_button = clear_btn;

	p->commit_button = gtk_button_new_with_label (_("Commit"));
	gtk_widget_set_tooltip_text (p->commit_button, _("Commit staged changes"));
	g_signal_connect (p->commit_button, "clicked", G_CALLBACK (commit_clicked), p);
	p->commit_count_label = gtk_label_new (NULL);
	gtk_widget_set_tooltip_text (p->commit_count_label, _("Commit message character count and recommended limit"));
	g_signal_connect (buf, "changed", G_CALLBACK (on_commit_message_buffer_changed), p);
	on_commit_message_buffer_changed (buf, p);

	gtk_box_pack_start (GTK_BOX (commit_box), overlay, TRUE, TRUE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_set_homogeneous (GTK_BOX (row), TRUE);
	GtkWidget *commit_spacer = gtk_label_new (NULL);
	gtk_widget_set_halign (p->commit_count_label, GTK_ALIGN_END);
	gtk_box_pack_start (GTK_BOX (row), commit_spacer, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (row), p->commit_button, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (row), p->commit_count_label, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (commit_box), row, FALSE, FALSE, 0);

	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
	gtk_box_set_homogeneous(GTK_BOX(row),TRUE);
#define ACTION(label,tip,cb) button=gtk_button_new_with_label(label);gtk_widget_set_tooltip_text(button,tip);g_signal_connect(button,"clicked",G_CALLBACK(cb),p);gtk_box_pack_start(GTK_BOX(row),button,TRUE,TRUE,0)
	ACTION(_("Pull"),_("Pull the current branch"),pull_clicked);
	ACTION(_("Push"),_("Push the current branch"),push_clicked);
	ACTION(_("Fetch"),_("Fetch all remotes and prune deleted refs"),fetch_clicked);
#undef ACTION
	gtk_box_pack_start(GTK_BOX(commit_box),row,FALSE,FALSE,0);

	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
	gtk_box_set_homogeneous(GTK_BOX(row),TRUE);
#define ACTION(label,tip,cb) button=gtk_button_new_with_label(label);gtk_widget_set_tooltip_text(button,tip);g_signal_connect(button,"clicked",G_CALLBACK(cb),p);gtk_box_pack_start(GTK_BOX(row),button,TRUE,TRUE,0)
	ACTION(_("Stash"),_("Stash tracked and untracked changes"),stash_clicked);
	ACTION(_("Pop"),_("Apply and remove the latest stash"),stash_pop_clicked);
#undef ACTION
	more=gtk_menu_button_new();gtk_button_set_label(GTK_BUTTON(more),_("More"));gtk_widget_set_tooltip_text(more,_("More Git actions"));menu=gtk_menu_new();
	add_more_item(menu,_("Stage All and Commit"),G_CALLBACK(stage_all_commit_clicked),p);
	add_more_item(menu,_("Amend Last Commit…"),G_CALLBACK(amend_commit_clicked),p);
	add_more_item(menu,_("Commit with Sign-off"),G_CALLBACK(commit_signoff_clicked),p);
	add_more_item(menu,_("Create Signed Commit"),G_CALLBACK(commit_signed_clicked),p);
	add_more_item(menu,_("Current File History"),G_CALLBACK(current_file_history_clicked),p);
	add_more_item(menu,_("Compare Two References…"),G_CALLBACK(compare_references_clicked),p);
	add_more_item(menu,_("Force Push with Lease…"),G_CALLBACK(force_push_with_lease_clicked),p);
	add_more_item(menu,_("View Stash Diff…"),G_CALLBACK(stash_diff_clicked),p);
	add_more_item(menu,_("Create Branch…"),G_CALLBACK(new_branch),p);add_more_item(menu,_("Switch Branch…"),G_CALLBACK(switch_branch),p);add_more_item(menu,_("Delete Branch…"),G_CALLBACK(delete_branch),p);add_more_item(menu,_("Create Tag…"),G_CALLBACK(new_tag),p);add_more_item(menu,_("Add Remote…"),G_CALLBACK(add_remote),p);add_more_item(menu,_("Remove Remote…"),G_CALLBACK(remove_remote),p);add_more_item(menu,_("Merge…"),G_CALLBACK(merge_ref),p);add_more_item(menu,_("Rebase…"),G_CALLBACK(rebase_ref),p);add_more_item(menu,_("Cherry-pick…"),G_CALLBACK(cherry_pick_ref),p);add_more_item(menu,_("Revert Commit…"),G_CALLBACK(revert_ref),p);add_more_item(menu,_("Continue Merge"),G_CALLBACK(merge_continue),p);add_more_item(menu,_("Abort Merge"),G_CALLBACK(merge_abort),p);add_more_item(menu,_("Continue Rebase"),G_CALLBACK(rebase_continue),p);add_more_item(menu,_("Abort Rebase"),G_CALLBACK(rebase_abort),p);add_more_item(menu,_("Abort Cherry-pick"),G_CALLBACK(cherry_abort),p);gtk_widget_show_all(menu);gtk_menu_button_set_popup(GTK_MENU_BUTTON(more),menu);gtk_box_pack_start(GTK_BOX(row),more,TRUE,TRUE,0);
	gtk_box_pack_start(GTK_BOX(commit_box),row,FALSE,FALSE,0);

	reset_model(p);
	gtk_widget_show_all(GTK_WIDGET(p));
	atk_object_set_name(gtk_widget_get_accessible(p->message_entry),_("Commit message"));
	update_selection_sensitivity(p);
}

GtkWidget *pluma_git_panel_new(PlumaWindow*window){PlumaGitPanel*p=g_object_new(PLUMA_TYPE_GIT_PANEL,NULL);p->window=window;p->statusbar_context=gtk_statusbar_get_context_id(GTK_STATUSBAR(window->priv->statusbar),"git-status");pluma_git_panel_refresh(p);return GTK_WIDGET(p);}
