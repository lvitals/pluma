/*
 * pluma-sourcecodebrowser-plugin.c
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

#include "pluma-sourcecodebrowser-plugin.h"
#include "pluma-sourcecodebrowser-plugin-panel.h"
#include "pluma-sourcecodebrowser-plugin-ctags.h"

#include <stdlib.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <libpeas-gtk/peas-gtk-configurable.h>

#include <pluma/pluma-window-activatable.h>
#include <pluma/pluma-window.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-view.h>
#include <pluma/pluma-panel.h>
#include <pluma/pluma-debug.h>

#define SCB_SCHEMA "org.mate.pluma.plugins.sourcecodebrowser"

struct _PlumaSourcecodebrowserPluginPrivate
{
	PlumaWindow *window;
	GtkWidget   *panel;
	GSettings   *settings;

	gboolean     load_remote_files;
	gboolean     ctags_available;
	gboolean     is_loaded;

	gulong       settings_changed_id;
	gulong       panel_draw_id;
	gulong       panel_tag_activated_id;
	gulong       window_active_tab_changed_id;
	gulong       window_active_tab_state_changed_id;
	gulong       window_tab_removed_id;
};

enum
{
	PROP_0,
	PROP_WINDOW
};

static void pluma_window_activatable_iface_init (PlumaWindowActivatableInterface *iface);
static void peas_gtk_configurable_iface_init (PeasGtkConfigurableInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaSourcecodebrowserPlugin,
                                pluma_sourcecodebrowser_plugin,
                                PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_ADD_PRIVATE_DYNAMIC (PlumaSourcecodebrowserPlugin)
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                               pluma_window_activatable_iface_init)
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PEAS_GTK_TYPE_CONFIGURABLE,
                                                               peas_gtk_configurable_iface_init))

static void
reload_active_document_symbols (PlumaSourcecodebrowserPlugin *plugin)
{
	PlumaSourcecodebrowserPluginPrivate *priv = plugin->priv;
	PlumaSourcecodebrowserPluginPanel *panel = PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL (priv->panel);
	PlumaPanel *right_panel;
	PlumaDocument *document;

	pluma_sourcecodebrowser_plugin_panel_clear (panel);
	priv->is_loaded = FALSE;

	right_panel = pluma_window_get_right_panel (priv->window);
	if (!pluma_panel_item_is_active (right_panel, priv->panel))
		return;

	document = pluma_window_get_active_document (priv->window);

	if (document != NULL)
	{
		GFile *location = pluma_document_get_location (document);

		if (location != NULL)
		{
			gchar *uri = g_file_get_uri (location);

			if (g_file_is_native (location))
			{
				gchar *filename = g_file_get_path (location);

				pluma_sourcecodebrowser_plugin_panel_parse_file (panel, filename, uri);

				g_free (filename);
			}
			else if (priv->load_remote_files)
			{
				GError *error = NULL;
				gchar *tmp_dir;

				tmp_dir = g_dir_make_tmp ("pluma-scbXXXXXX", &error);

				if (tmp_dir != NULL)
				{
					gchar *basename = g_file_get_basename (location);
					gchar *tmp_path = g_build_filename (tmp_dir, basename, NULL);
					GtkTextIter start, end;
					gchar *contents;

					gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (document), &start);
					gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (document), &end);
					contents = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (document), &start, &end, TRUE);

					if (g_file_set_contents (tmp_path, contents, -1, &error))
					{
						pluma_sourcecodebrowser_plugin_panel_parse_file (panel, tmp_path, uri);
					}
					else
					{
						g_warning ("Could not write temporary file for '%s': %s", uri, error->message);
						g_error_free (error);
					}

					g_free (contents);
					g_remove (tmp_path);
					g_free (tmp_path);
					g_free (basename);
					g_rmdir (tmp_dir);
				}
				else
				{
					g_warning ("Could not create temporary directory: %s", error->message);
					g_error_free (error);
				}

				g_free (tmp_dir);
			}

			g_free (uri);
			g_object_unref (location);
		}
	}

	priv->is_loaded = TRUE;
}

static void
on_settings_changed (GSettings *settings, const gchar *key, gpointer user_data)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (user_data);
	PlumaSourcecodebrowserPluginPrivate *priv = plugin->priv;

	if (g_strcmp0 (key, "load-remote-files") == 0)
		priv->load_remote_files = g_settings_get_boolean (settings, key);

	if (priv->panel != NULL)
	{
		pluma_sourcecodebrowser_plugin_panel_reset_expanded_rows (PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL (priv->panel));
		reload_active_document_symbols (plugin);
	}
}

static gboolean
on_panel_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (user_data);

	if (!plugin->priv->is_loaded)
		reload_active_document_symbols (plugin);

	return FALSE;
}

static void
on_panel_tag_activated (PlumaSourcecodebrowserPluginPanel *panel,
                        const gchar                       *uri,
                        const gchar                       *line,
                        gpointer                           user_data)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (user_data);
	PlumaDocument *document;
	PlumaView *view;

	pluma_debug_message (DEBUG_PLUGINS, "%s, line %s.", uri, line);

	document = pluma_window_get_active_document (plugin->priv->window);
	view = pluma_window_get_active_view (plugin->priv->window);

	if (document == NULL || view == NULL)
		return;

	/* ctags line numbers are 1-based. */
	pluma_document_goto_line (document, atoi (line) - 1);
	pluma_view_scroll_to_cursor (view);
}

static void
on_window_active_tab_changed (PlumaWindow *window, PlumaTab *tab, gpointer user_data)
{
	reload_active_document_symbols (PLUMA_SOURCECODEBROWSER_PLUGIN (user_data));
}

static void
on_window_active_tab_state_changed (PlumaWindow *window, gpointer user_data)
{
	reload_active_document_symbols (PLUMA_SOURCECODEBROWSER_PLUGIN (user_data));
}

static void
on_window_tab_removed (PlumaWindow *window, PlumaTab *tab, gpointer user_data)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (user_data);

	if (pluma_window_get_active_document (window) == NULL)
		pluma_sourcecodebrowser_plugin_panel_clear (PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL (plugin->priv->panel));
}

static GtkWidget *
pluma_sourcecodebrowser_plugin_create_configure_widget (PeasGtkConfigurable *configurable)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (configurable);
	GtkWidget *box;
	GtkWidget *check;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *entry;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);

	check = gtk_check_button_new_with_mnemonic (_("Show _line numbers in tree"));
	g_settings_bind (plugin->priv->settings, "show-line-numbers", check, "active", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (box), check, FALSE, FALSE, 0);

	check = gtk_check_button_new_with_mnemonic (_("Load symbols from _remote files"));
	g_settings_bind (plugin->priv->settings, "load-remote-files", check, "active", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (box), check, FALSE, FALSE, 0);

	check = gtk_check_button_new_with_mnemonic (_("Start with rows _expanded"));
	g_settings_bind (plugin->priv->settings, "expand-rows", check, "active", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (box), check, FALSE, FALSE, 0);

	check = gtk_check_button_new_with_mnemonic (_("_Sort list alphabetically"));
	g_settings_bind (plugin->priv->settings, "sort-list", check, "active", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (box), check, FALSE, FALSE, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

	label = gtk_label_new (_("ctags executable"));
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	entry = gtk_entry_new ();
	g_settings_bind (plugin->priv->settings, "ctags-executable", entry, "text", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (box), hbox, FALSE, FALSE, 0);

	gtk_widget_show_all (box);

	return box;
}

static void
pluma_sourcecodebrowser_plugin_init (PlumaSourcecodebrowserPlugin *plugin)
{
	pluma_debug_message (DEBUG_PLUGINS, "PlumaSourcecodebrowserPlugin initializing");

	plugin->priv = pluma_sourcecodebrowser_plugin_get_instance_private (plugin);

	plugin->priv->settings = g_settings_new (SCB_SCHEMA);
	plugin->priv->load_remote_files = TRUE;
}

static void
pluma_sourcecodebrowser_plugin_dispose (GObject *object)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (object);

	pluma_debug_message (DEBUG_PLUGINS, "PlumaSourcecodebrowserPlugin disposing");

	if (plugin->priv->window != NULL)
	{
		g_object_unref (plugin->priv->window);
		plugin->priv->window = NULL;
	}

	G_OBJECT_CLASS (pluma_sourcecodebrowser_plugin_parent_class)->dispose (object);
}

static void
pluma_sourcecodebrowser_plugin_finalize (GObject *object)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (object);

	pluma_debug_message (DEBUG_PLUGINS, "PlumaSourcecodebrowserPlugin finalizing");

	g_object_unref (plugin->priv->settings);

	G_OBJECT_CLASS (pluma_sourcecodebrowser_plugin_parent_class)->finalize (object);
}

static void
pluma_sourcecodebrowser_plugin_activate (PlumaWindowActivatable *activatable)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (activatable);
	PlumaSourcecodebrowserPluginPrivate *priv = plugin->priv;
	PlumaPanel *right_panel;
	gchar *data_dir;
	gchar *ctags_executable;
	gchar *ctags_version;
	gchar *icon_path;
	GtkWidget *icon;

	pluma_debug (DEBUG_PLUGINS);

	right_panel = pluma_window_get_right_panel (priv->window);

	data_dir = peas_extension_base_get_data_dir (PEAS_EXTENSION_BASE (plugin));

	priv->panel = pluma_sourcecodebrowser_plugin_panel_new (data_dir);

	g_settings_bind (priv->settings, "show-line-numbers", priv->panel, "show-line-numbers", G_SETTINGS_BIND_GET);
	g_settings_bind (priv->settings, "expand-rows", priv->panel, "expand-rows", G_SETTINGS_BIND_GET);
	g_settings_bind (priv->settings, "sort-list", priv->panel, "sort-list", G_SETTINGS_BIND_GET);
	g_settings_bind (priv->settings, "ctags-executable", priv->panel, "ctags-executable", G_SETTINGS_BIND_GET);

	priv->load_remote_files = g_settings_get_boolean (priv->settings, "load-remote-files");

	ctags_executable = g_settings_get_string (priv->settings, "ctags-executable");
	ctags_version = scb_ctags_get_version (ctags_executable);
	priv->ctags_available = (ctags_version != NULL);
	if (!priv->ctags_available)
		g_warning ("Could not find ctags executable: %s", ctags_executable);
	g_free (ctags_version);
	g_free (ctags_executable);

	icon_path = g_build_filename (data_dir, "icons", "source-code-browser.png", NULL);
	icon = gtk_image_new_from_file (icon_path);
	g_free (icon_path);
	g_free (data_dir);

	pluma_panel_add_item (right_panel, priv->panel, _("Source Code Browser"), icon);

	priv->settings_changed_id = g_signal_connect (priv->settings, "changed",
	                                              G_CALLBACK (on_settings_changed), plugin);
	priv->panel_draw_id = g_signal_connect (priv->panel, "draw",
	                                        G_CALLBACK (on_panel_draw), plugin);

	if (priv->ctags_available)
	{
		priv->panel_tag_activated_id = g_signal_connect (priv->panel, "tag-activated",
		                                                 G_CALLBACK (on_panel_tag_activated), plugin);
		priv->window_active_tab_changed_id = g_signal_connect (priv->window, "active-tab-changed",
		                                                       G_CALLBACK (on_window_active_tab_changed), plugin);
		priv->window_active_tab_state_changed_id = g_signal_connect (priv->window, "active-tab-state-changed",
		                                                             G_CALLBACK (on_window_active_tab_state_changed), plugin);
		priv->window_tab_removed_id = g_signal_connect (priv->window, "tab-removed",
		                                                G_CALLBACK (on_window_tab_removed), plugin);
	}
	else
	{
		gtk_widget_set_sensitive (priv->panel, FALSE);
	}
}

static void
pluma_sourcecodebrowser_plugin_deactivate (PlumaWindowActivatable *activatable)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (activatable);
	PlumaSourcecodebrowserPluginPrivate *priv = plugin->priv;
	PlumaPanel *right_panel;

	pluma_debug (DEBUG_PLUGINS);

	if (priv->settings_changed_id != 0)
	{
		g_signal_handler_disconnect (priv->settings, priv->settings_changed_id);
		priv->settings_changed_id = 0;
	}

	if (priv->panel_draw_id != 0)
	{
		g_signal_handler_disconnect (priv->panel, priv->panel_draw_id);
		priv->panel_draw_id = 0;
	}

	if (priv->panel_tag_activated_id != 0)
	{
		g_signal_handler_disconnect (priv->panel, priv->panel_tag_activated_id);
		priv->panel_tag_activated_id = 0;
	}

	if (priv->window_active_tab_changed_id != 0)
	{
		g_signal_handler_disconnect (priv->window, priv->window_active_tab_changed_id);
		priv->window_active_tab_changed_id = 0;
	}

	if (priv->window_active_tab_state_changed_id != 0)
	{
		g_signal_handler_disconnect (priv->window, priv->window_active_tab_state_changed_id);
		priv->window_active_tab_state_changed_id = 0;
	}

	if (priv->window_tab_removed_id != 0)
	{
		g_signal_handler_disconnect (priv->window, priv->window_tab_removed_id);
		priv->window_tab_removed_id = 0;
	}

	right_panel = pluma_window_get_right_panel (priv->window);
	pluma_panel_remove_item (right_panel, priv->panel);
	priv->panel = NULL;
}

static void
pluma_sourcecodebrowser_plugin_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (object);

	switch (prop_id)
	{
		case PROP_WINDOW:
			plugin->priv->window = PLUMA_WINDOW (g_value_dup_object (value));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
pluma_sourcecodebrowser_plugin_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
	PlumaSourcecodebrowserPlugin *plugin = PLUMA_SOURCECODEBROWSER_PLUGIN (object);

	switch (prop_id)
	{
		case PROP_WINDOW:
			g_value_set_object (value, plugin->priv->window);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
pluma_sourcecodebrowser_plugin_class_init (PlumaSourcecodebrowserPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pluma_sourcecodebrowser_plugin_finalize;
	object_class->dispose = pluma_sourcecodebrowser_plugin_dispose;
	object_class->set_property = pluma_sourcecodebrowser_plugin_set_property;
	object_class->get_property = pluma_sourcecodebrowser_plugin_get_property;

	g_object_class_override_property (object_class, PROP_WINDOW, "window");
}

static void
pluma_sourcecodebrowser_plugin_class_finalize (PlumaSourcecodebrowserPluginClass *klass)
{
	/* dummy function - used by G_DEFINE_DYNAMIC_TYPE_EXTENDED */
}

static void
pluma_window_activatable_iface_init (PlumaWindowActivatableInterface *iface)
{
	iface->activate = pluma_sourcecodebrowser_plugin_activate;
	iface->deactivate = pluma_sourcecodebrowser_plugin_deactivate;
}

static void
peas_gtk_configurable_iface_init (PeasGtkConfigurableInterface *iface)
{
	iface->create_configure_widget = pluma_sourcecodebrowser_plugin_create_configure_widget;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	pluma_sourcecodebrowser_plugin_register_type (G_TYPE_MODULE (module));
	_pluma_sourcecodebrowser_plugin_panel_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
	                                            PLUMA_TYPE_WINDOW_ACTIVATABLE,
	                                            PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN);

	peas_object_module_register_extension_type (module,
	                                            PEAS_GTK_TYPE_CONFIGURABLE,
	                                            PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN);
}
