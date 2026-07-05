/*
 * pluma-sourcecodebrowser-plugin-panel.c
 * This file is part of pluma
 *
 * Copyright (c) 2011, Micah Carrick
 * Copyright (C) 2020-2021 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "pluma-sourcecodebrowser-plugin-panel.h"
#include "pluma-sourcecodebrowser-plugin-ctags.h"

enum
{
	COL_ICON,
	COL_NAME,
	COL_KIND,
	COL_URI,
	COL_LINE,
	COL_MARKUP,
	N_COLUMNS
};

enum
{
	TAG_ACTIVATED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_SHOW_LINE_NUMBERS,
	PROP_EXPAND_ROWS,
	PROP_SORT_LIST,
	PROP_CTAGS_EXECUTABLE
};

static guint signals[LAST_SIGNAL];

struct _PlumaSourcecodebrowserPluginPanelPrivate
{
	gchar *data_dir;
	gchar *icon_dir;

	GtkWidget *treeview;
	GtkTreeStore *store;

	GHashTable *pixbufs;       /* icon name -> GdkPixbuf* */
	GHashTable *expanded_rows; /* uri -> GList<gchar* tree path string> */
	gchar *current_uri;

	gboolean show_line_numbers;
	gboolean expand_rows;
	gboolean sort_list;
	gchar *ctags_executable;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaSourcecodebrowserPluginPanel,
                                pluma_sourcecodebrowser_plugin_panel,
                                GTK_TYPE_BOX,
                                0,
                                G_ADD_PRIVATE_DYNAMIC (PlumaSourcecodebrowserPluginPanel))

static void
string_list_free (gpointer data)
{
	g_list_free_full ((GList *) data, g_free);
}

static GdkPixbuf *
get_pixbuf (PlumaSourcecodebrowserPluginPanel *panel, const gchar *icon_name)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;
	GdkPixbuf *pixbuf;
	gchar *filename;
	gchar *path;

	pixbuf = g_hash_table_lookup (priv->pixbufs, icon_name);
	if (pixbuf != NULL)
		return pixbuf;

	filename = g_strconcat (icon_name, ".png", NULL);
	path = g_build_filename (priv->icon_dir, filename, NULL);
	g_free (filename);

	if (g_file_test (path, G_FILE_TEST_EXISTS))
	{
		GError *error = NULL;

		pixbuf = gdk_pixbuf_new_from_file (path, &error);
		if (pixbuf == NULL)
		{
			g_warning ("Could not load pixbuf for icon '%s': %s", icon_name, error->message);
			g_error_free (error);
		}
	}

	g_free (path);

	if (pixbuf == NULL)
	{
		if (strcmp (icon_name, "missing-image") == 0)
			return NULL;

		pixbuf = get_pixbuf (panel, "missing-image");
		if (pixbuf != NULL)
			g_object_ref (pixbuf);
	}

	if (pixbuf != NULL)
		g_hash_table_insert (priv->pixbufs, g_strdup (icon_name), pixbuf);

	return pixbuf;
}

/* Finds the child of @parent (or a root node if @parent is NULL) whose
 * "kind" column equals @kind, creating a new group node for it if none
 * exists yet. */
static void
get_kind_iter (PlumaSourcecodebrowserPluginPanel *panel,
               GtkTreeIter                       *parent,
               const gchar                       *kind,
               const gchar                       *uri,
               GtkTreeIter                       *result)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;
	GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
	GtkTreeIter iter;
	gboolean valid;
	GdkPixbuf *pixbuf;
	gchar *icon_name;
	gchar *group;
	gchar *markup;

	valid = gtk_tree_model_iter_children (model, &iter, parent);

	while (valid)
	{
		gchar *row_kind = NULL;

		gtk_tree_model_get (model, &iter, COL_KIND, &row_kind, -1);

		if (g_strcmp0 (row_kind, kind) == 0)
		{
			g_free (row_kind);
			*result = iter;
			return;
		}

		g_free (row_kind);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	icon_name = scb_kind_icon_name (kind);
	pixbuf = get_pixbuf (panel, icon_name);
	g_free (icon_name);

	group = scb_kind_group_name (kind);
	markup = g_markup_printf_escaped ("<i>%s</i>", group);

	gtk_tree_store_append (priv->store, result, parent);
	gtk_tree_store_set (priv->store, result,
	                    COL_ICON, pixbuf,
	                    COL_NAME, group,
	                    COL_KIND, kind,
	                    COL_URI, uri,
	                    COL_LINE, NULL,
	                    COL_MARKUP, markup,
	                    -1);

	g_free (group);
	g_free (markup);
}

/* Finds the child of @parent whose "name" column equals @name, without
 * creating anything. Used to locate the tree row for a previously inserted
 * tag (e.g. a class) so its members can be nested underneath it. */
static gboolean
find_named_iter (PlumaSourcecodebrowserPluginPanel *panel,
                  GtkTreeIter                       *parent,
                  const gchar                       *name,
                  GtkTreeIter                       *result)
{
	GtkTreeModel *model = GTK_TREE_MODEL (panel->priv->store);
	GtkTreeIter iter;
	gboolean valid;

	valid = gtk_tree_model_iter_children (model, &iter, parent);

	while (valid)
	{
		gchar *row_name = NULL;

		gtk_tree_model_get (model, &iter, COL_NAME, &row_name, -1);

		if (g_strcmp0 (row_name, name) == 0)
		{
			g_free (row_name);
			*result = iter;
			return TRUE;
		}

		g_free (row_name);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	return FALSE;
}

static ScbTag *
find_tag_by_name (GList *tags, const gchar *name)
{
	GList *l;

	for (l = tags; l != NULL; l = l->next)
	{
		ScbTag *tag = l->data;

		if (g_strcmp0 (tag->name, name) == 0)
			return tag;
	}

	return NULL;
}

static void
append_tag_row (PlumaSourcecodebrowserPluginPanel *panel,
                GtkTreeIter                       *parent,
                ScbTag                            *tag,
                const gchar                       *uri)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;
	const gchar *line = scb_tag_get_field (tag, "line");
	GdkPixbuf *pixbuf;
	gchar *icon_name;
	gchar *markup;
	GtkTreeIter new_iter;

	icon_name = scb_kind_icon_name (tag->kind);
	pixbuf = get_pixbuf (panel, icon_name);
	g_free (icon_name);

	if (line != NULL && priv->show_line_numbers)
		markup = g_markup_printf_escaped ("%s [%s]", tag->name, line);
	else
		markup = g_markup_escape_text (tag->name, -1);

	gtk_tree_store_append (priv->store, &new_iter, parent);
	gtk_tree_store_set (priv->store, &new_iter,
	                    COL_ICON, pixbuf,
	                    COL_NAME, tag->name,
	                    COL_KIND, tag->kind,
	                    COL_URI, uri,
	                    COL_LINE, line,
	                    COL_MARKUP, markup,
	                    -1);

	g_free (markup);
}

/* ctags names the "owning scope" field after the kind of the owner:
 * "class:Foo" for a C++/Java class member, but "struct:Point"/"union:U"/
 * "enum:Color" for plain C. Check them all so C structs/unions/enums nest
 * their members the same way C++ classes do. */
static const gchar *
get_scope_field (ScbTag *tag)
{
	static const gchar *scope_keys[] = { "class", "struct", "union", "enum", "interface", NULL };
	gint i;

	for (i = 0; scope_keys[i] != NULL; i++)
	{
		const gchar *value = scb_tag_get_field (tag, scope_keys[i]);

		if (value != NULL)
			return value;
	}

	return NULL;
}

static void
load_tags (PlumaSourcecodebrowserPluginPanel *panel, GList *tags, const gchar *uri)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;
	GList *l;
	GList *saved_paths;

	/* Root-level tags first. */
	for (l = tags; l != NULL; l = l->next)
	{
		ScbTag *tag = l->data;

		if (get_scope_field (tag) == NULL)
		{
			GtkTreeIter kind_iter;

			get_kind_iter (panel, NULL, tag->kind, uri, &kind_iter);
			append_tag_row (panel, &kind_iter, tag, uri);
		}
	}

	/* Second-level tags (members of a class/struct/union/enum). Deeper
	 * nesting, such as classes defined inside other classes, is not
	 * handled. */
	for (l = tags; l != NULL; l = l->next)
	{
		ScbTag *tag = l->data;
		const gchar *class_name = get_scope_field (tag);

		if (class_name != NULL && strchr (class_name, '.') == NULL)
		{
			ScbTag *parent_tag = find_tag_by_name (tags, class_name);
			GtkTreeIter top_kind_iter, parent_iter, sub_kind_iter;
			gboolean have_parent_iter;

			if (parent_tag == NULL)
				continue;

			get_kind_iter (panel, NULL, parent_tag->kind, uri, &top_kind_iter);
			have_parent_iter = find_named_iter (panel, &top_kind_iter, parent_tag->name, &parent_iter);

			get_kind_iter (panel, have_parent_iter ? &parent_iter : NULL, tag->kind, uri, &sub_kind_iter);
			append_tag_row (panel, &sub_kind_iter, tag, uri);
		}
	}

	if (priv->sort_list)
		gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->store), COL_NAME, GTK_SORT_ASCENDING);

	saved_paths = g_hash_table_lookup (priv->expanded_rows, uri);
	if (saved_paths != NULL)
	{
		for (l = saved_paths; l != NULL; l = l->next)
		{
			GtkTreePath *path = gtk_tree_path_new_from_string ((const gchar *) l->data);

			if (path != NULL)
			{
				gtk_tree_view_expand_row (GTK_TREE_VIEW (priv->treeview), path, FALSE);
				gtk_tree_path_free (path);
			}
		}
	}
	else if (priv->expand_rows)
	{
		gtk_tree_view_expand_all (GTK_TREE_VIEW (priv->treeview));
	}
}

static void
save_expanded_rows_mapping_func (GtkTreeView *tree_view, GtkTreePath *path, gpointer data)
{
	GList **list = data;

	*list = g_list_prepend (*list, gtk_tree_path_to_string (path));
}

static void
save_expanded_rows (PlumaSourcecodebrowserPluginPanel *panel)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;
	GList *paths = NULL;

	if (priv->current_uri == NULL)
		return;

	gtk_tree_view_map_expanded_rows (GTK_TREE_VIEW (priv->treeview),
	                                 save_expanded_rows_mapping_func,
	                                 &paths);

	g_hash_table_replace (priv->expanded_rows, g_strdup (priv->current_uri), paths);
}

static void
row_activated_cb (GtkTreeView       *tree_view,
                  GtkTreePath       *path,
                  GtkTreeViewColumn *column,
                  gpointer           user_data)
{
	PlumaSourcecodebrowserPluginPanel *panel = PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL (user_data);
	GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
	GtkTreeIter iter;
	gchar *uri = NULL;
	gchar *line = NULL;

	if (!gtk_tree_model_get_iter (model, &iter, path))
		return;

	gtk_tree_model_get (model, &iter, COL_URI, &uri, COL_LINE, &line, -1);

	if (uri != NULL && line != NULL)
		g_signal_emit (panel, signals[TAG_ACTIVATED], 0, uri, line);

	g_free (uri);
	g_free (line);
}

void
pluma_sourcecodebrowser_plugin_panel_clear (PlumaSourcecodebrowserPluginPanel *panel)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv;

	g_return_if_fail (PLUMA_IS_SOURCECODEBROWSER_PLUGIN_PANEL (panel));

	priv = panel->priv;

	if (priv->expand_rows)
		save_expanded_rows (panel);

	gtk_tree_store_clear (priv->store);
}

void
pluma_sourcecodebrowser_plugin_panel_parse_file (PlumaSourcecodebrowserPluginPanel *panel,
                                                  const gchar                       *path,
                                                  const gchar                       *uri)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv;
	GList *tags;
	GError *error = NULL;

	g_return_if_fail (PLUMA_IS_SOURCECODEBROWSER_PLUGIN_PANEL (panel));
	g_return_if_fail (path != NULL);
	g_return_if_fail (uri != NULL);

	priv = panel->priv;

	g_free (priv->current_uri);
	priv->current_uri = g_strdup (uri);

	tags = scb_ctags_parse_file (priv->ctags_executable, path, &error);

	if (error != NULL)
	{
		g_warning ("Could not execute ctags: %s (executable=%s)",
		          error->message, priv->ctags_executable);
		g_error_free (error);
		return;
	}

	load_tags (panel, tags, uri);
	scb_tag_list_free (tags);
}

void
pluma_sourcecodebrowser_plugin_panel_reset_expanded_rows (PlumaSourcecodebrowserPluginPanel *panel)
{
	g_return_if_fail (PLUMA_IS_SOURCECODEBROWSER_PLUGIN_PANEL (panel));

	g_hash_table_remove_all (panel->priv->expanded_rows);
}

static void
pluma_sourcecodebrowser_plugin_panel_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PlumaSourcecodebrowserPluginPanel *panel = PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL (object);
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;

	switch (prop_id)
	{
		case PROP_SHOW_LINE_NUMBERS:
			priv->show_line_numbers = g_value_get_boolean (value);
			break;
		case PROP_EXPAND_ROWS:
			priv->expand_rows = g_value_get_boolean (value);
			break;
		case PROP_SORT_LIST:
			priv->sort_list = g_value_get_boolean (value);
			break;
		case PROP_CTAGS_EXECUTABLE:
			g_free (priv->ctags_executable);
			priv->ctags_executable = g_value_dup_string (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
pluma_sourcecodebrowser_plugin_panel_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PlumaSourcecodebrowserPluginPanel *panel = PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL (object);
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;

	switch (prop_id)
	{
		case PROP_SHOW_LINE_NUMBERS:
			g_value_set_boolean (value, priv->show_line_numbers);
			break;
		case PROP_EXPAND_ROWS:
			g_value_set_boolean (value, priv->expand_rows);
			break;
		case PROP_SORT_LIST:
			g_value_set_boolean (value, priv->sort_list);
			break;
		case PROP_CTAGS_EXECUTABLE:
			g_value_set_string (value, priv->ctags_executable);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
pluma_sourcecodebrowser_plugin_panel_finalize (GObject *object)
{
	PlumaSourcecodebrowserPluginPanel *panel = PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL (object);
	PlumaSourcecodebrowserPluginPanelPrivate *priv = panel->priv;

	g_free (priv->data_dir);
	g_free (priv->icon_dir);
	g_free (priv->current_uri);
	g_free (priv->ctags_executable);
	g_hash_table_destroy (priv->pixbufs);
	g_hash_table_destroy (priv->expanded_rows);

	G_OBJECT_CLASS (pluma_sourcecodebrowser_plugin_panel_parent_class)->finalize (object);
}

static void
pluma_sourcecodebrowser_plugin_panel_init (PlumaSourcecodebrowserPluginPanel *panel)
{
	PlumaSourcecodebrowserPluginPanelPrivate *priv;
	GtkWidget *sw;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;

	panel->priv = pluma_sourcecodebrowser_plugin_panel_get_instance_private (panel);
	priv = panel->priv;

	priv->data_dir = NULL;
	priv->icon_dir = NULL;
	priv->current_uri = NULL;
	priv->pixbufs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	priv->expanded_rows = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, string_list_free);

	priv->show_line_numbers = FALSE;
	priv->expand_rows = TRUE;
	priv->sort_list = TRUE;
	priv->ctags_executable = g_strdup ("ctags");

	gtk_orientable_set_orientation (GTK_ORIENTABLE (panel), GTK_ORIENTATION_VERTICAL);

	priv->store = gtk_tree_store_new (N_COLUMNS,
	                                  GDK_TYPE_PIXBUF,
	                                  G_TYPE_STRING,
	                                  G_TYPE_STRING,
	                                  G_TYPE_STRING,
	                                  G_TYPE_STRING,
	                                  G_TYPE_STRING);

	priv->treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (priv->store));
	g_object_unref (priv->store);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (priv->treeview), FALSE);
	gtk_tree_view_set_activate_on_single_click (GTK_TREE_VIEW (priv->treeview), TRUE);

	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (column, "Symbol");

	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, cell, FALSE);
	gtk_tree_view_column_add_attribute (column, cell, "pixbuf", COL_ICON);

	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, cell, TRUE);
	gtk_tree_view_column_add_attribute (column, cell, "markup", COL_MARKUP);

	gtk_tree_view_append_column (GTK_TREE_VIEW (priv->treeview), column);

	g_signal_connect (priv->treeview, "row-activated", G_CALLBACK (row_activated_cb), panel);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (sw), priv->treeview);

	gtk_box_pack_start (GTK_BOX (panel), sw, TRUE, TRUE, 0);

	gtk_widget_show_all (GTK_WIDGET (panel));
}

static void
pluma_sourcecodebrowser_plugin_panel_class_init (PlumaSourcecodebrowserPluginPanelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pluma_sourcecodebrowser_plugin_panel_finalize;
	object_class->set_property = pluma_sourcecodebrowser_plugin_panel_set_property;
	object_class->get_property = pluma_sourcecodebrowser_plugin_panel_get_property;

	g_object_class_install_property (object_class, PROP_SHOW_LINE_NUMBERS,
	                                 g_param_spec_boolean ("show-line-numbers",
	                                                       "Show Line Numbers",
	                                                       "Show the line number of each item in the source tree",
	                                                       FALSE,
	                                                       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_EXPAND_ROWS,
	                                 g_param_spec_boolean ("expand-rows",
	                                                       "Expand Rows",
	                                                       "Expand the entire source tree when initially loaded",
	                                                       TRUE,
	                                                       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_SORT_LIST,
	                                 g_param_spec_boolean ("sort-list",
	                                                       "Sort List",
	                                                       "Sort the list of symbols alphabetically",
	                                                       TRUE,
	                                                       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_CTAGS_EXECUTABLE,
	                                 g_param_spec_string ("ctags-executable",
	                                                     "Ctags Executable",
	                                                     "The executable path for Exuberant Ctags",
	                                                     "ctags",
	                                                     G_PARAM_READWRITE));

	signals[TAG_ACTIVATED] =
	        g_signal_new ("tag-activated",
	                      G_TYPE_FROM_CLASS (klass),
	                      G_SIGNAL_RUN_LAST,
	                      G_STRUCT_OFFSET (PlumaSourcecodebrowserPluginPanelClass, tag_activated),
	                      NULL, NULL, NULL,
	                      G_TYPE_NONE, 2,
	                      G_TYPE_STRING, G_TYPE_STRING);
}

static void
pluma_sourcecodebrowser_plugin_panel_class_finalize (PlumaSourcecodebrowserPluginPanelClass *klass)
{
	/* dummy function - used by G_DEFINE_DYNAMIC_TYPE_EXTENDED */
}

GtkWidget *
pluma_sourcecodebrowser_plugin_panel_new (const gchar *data_dir)
{
	PlumaSourcecodebrowserPluginPanel *panel;

	g_return_val_if_fail (data_dir != NULL, NULL);

	panel = g_object_new (PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN_PANEL, NULL);

	panel->priv->data_dir = g_strdup (data_dir);
	panel->priv->icon_dir = g_build_filename (data_dir, "icons", NULL);

	return GTK_WIDGET (panel);
}

void
_pluma_sourcecodebrowser_plugin_panel_register_type (GTypeModule *type_module)
{
	pluma_sourcecodebrowser_plugin_panel_register_type (type_module);
}
