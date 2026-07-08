/*
 * ##(FILENAME) - ##(DESCRIPTION)
 *
 * Copyright (C) ##(DATE_YEAR) - ##(AUTHOR_FULLNAME)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <pluma/pluma-window.h>
#include <pluma/pluma-window-activatable.h>
#include <pluma/pluma-debug.h>
##ifdef WITH_MENU
#include <pluma/pluma-action-migration.h>
##endif
##ifdef WITH_CONFIGURE_DIALOG
#include <libpeas-gtk/peas-gtk-configurable.h>
##endif

#include "##(PLUGIN_MODULE)-plugin.h"

static void pluma_window_activatable_iface_init (PlumaWindowActivatableInterface *iface);

struct _##(PLUGIN_ID.camel)PluginPrivate
{
	PlumaWindow        *window;

##ifdef WITH_MENU
	/* "##(PLUGIN_MODULE)" is the action-group prefix used by
	 * gtk_widget_insert_action_group() below; actions added here are
	 * reachable as "##(PLUGIN_MODULE).<name>" from menu items and
	 * gtk_application_set_accels_for_action(). */
	GSimpleActionGroup *action_group;
##endif
};

enum {
	PROP_0,
	PROP_OBJECT
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (##(PLUGIN_ID.camel)Plugin,
                                ##(PLUGIN_ID.lower)_plugin,
                                PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_ADD_PRIVATE_DYNAMIC (##(PLUGIN_ID.camel)Plugin)
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                               pluma_window_activatable_iface_init))
##ifdef WITH_MENU
/* Forward-declare your action callbacks above this table, with signature
 * (GSimpleAction *action, GVariant *parameter, gpointer user_data); cast
 * user_data back with ##(PLUGIN_ID.upper)_PLUGIN (user_data). */
static const GActionEntry action_entries[] =
	{
		/* { "my-action", my_action_activated }, */
	};
##endif

static void
##(PLUGIN_ID.lower)_plugin_init (##(PLUGIN_ID.camel)Plugin *plugin)
{

	plugin->priv = ##(PLUGIN_ID.lower)_plugin_get_instance_private (plugin);

	pluma_debug_message (DEBUG_PLUGINS, "##(PLUGIN_ID.camel)Plugin initializing");
}

static void
##(PLUGIN_ID.lower)_plugin_finalize (GObject *object)
{
	pluma_debug_message (DEBUG_PLUGINS, "##(PLUGIN_ID.camel)Plugin finalizing");

	G_OBJECT_CLASS (##(PLUGIN_ID.lower)_plugin_parent_class)->finalize (object);
}

static void
##(PLUGIN_ID.lower)_plugin_dispose (GObject *object)
{
	##(PLUGIN_ID.camel)Plugin *plugin = ##(PLUGIN_ID.upper)_PLUGIN (object);

	pluma_debug_message (DEBUG_PLUGINS, "##(PLUGIN_ID.camel)Plugin disposing");

	if (plugin->priv->window != NULL)
	{
		g_object_unref (plugin->priv->window);
		plugin->priv->window = NULL;
	}

##ifdef WITH_MENU
	g_clear_object (&plugin->priv->action_group);
##endif

	G_OBJECT_CLASS (##(PLUGIN_ID.lower)_plugin_parent_class)->dispose (object);
}

static void
update_ui (##(PLUGIN_ID.camel)PluginPrivate *data)
{
##ifdef WITH_MENU
	PlumaWindow *window;
	PlumaView *view;
	GAction *action;
##endif

	pluma_debug (DEBUG_PLUGINS);

##ifdef WITH_MENU
	window = PLUMA_WINDOW (data->window);
	view = pluma_window_get_active_view (window);

	pluma_debug_message (DEBUG_PLUGINS, "View: %p", view);

	/* Repeat per action name if you added more than one. */
	action = g_action_map_lookup_action (G_ACTION_MAP (data->action_group), "my-action");
	if (action != NULL)
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
		                             (view != NULL) &&
		                             gtk_text_view_get_editable (GTK_TEXT_VIEW (view)));
##endif
}


static void
##(PLUGIN_ID.lower)_plugin_activate (PlumaWindowActivatable *activatable)
{
##ifdef WITH_MENU
	##(PLUGIN_ID.camel)PluginPrivate *data;
	PlumaWindow *window;
	GMenuItem *item;
##endif

	pluma_debug (DEBUG_PLUGINS);

##ifdef WITH_MENU
	data = ##(PLUGIN_ID.upper)_PLUGIN (activatable)->priv;
	window = PLUMA_WINDOW (data->window);

	/* Registers a "##(PLUGIN_MODULE).<name>" GAction for each entry above,
	 * reachable from menu items and gtk_application_set_accels_for_action()
	 * (use the application from gtk_window_get_application(GTK_WINDOW(window)),
	 * deferring via the window's "notify::application" signal if it is
	 * still NULL at this point -- see pluma-spell-plugin.c for a worked
	 * example of that deferral). */
	data->action_group = pluma_action_migration_create_plugin_group (
		action_entries, G_N_ELEMENTS (action_entries), activatable);
	gtk_widget_insert_action_group (GTK_WIDGET (window), "##(PLUGIN_MODULE)",
	                                G_ACTION_GROUP (data->action_group));

	/* Repeat per action to add more than one menu item. Valid section ids
	 * are declared in pluma-menus.ui: "plugin-tools-section",
	 * "plugin-edit-section", "plugin-search-section". */
	item = g_menu_item_new (_("My Action"), "##(PLUGIN_MODULE).my-action");
	pluma_window_add_menu_item (window, "plugin-tools-section", item);
	g_object_unref (item);

	update_ui (data);
##endif
}

static void
##(PLUGIN_ID.lower)_plugin_deactivate (PlumaWindowActivatable *activatable)
{
##ifdef WITH_MENU
	##(PLUGIN_ID.camel)PluginPrivate *data;
	PlumaWindow *window;
##endif

	pluma_debug (DEBUG_PLUGINS);

##ifdef WITH_MENU
	data = ##(PLUGIN_ID.upper)_PLUGIN (activatable)->priv;
	window = PLUMA_WINDOW (data->window);

	pluma_window_remove_menu_items (window, "plugin-tools-section", "##(PLUGIN_MODULE).my-action");
	gtk_widget_insert_action_group (GTK_WIDGET (window), "##(PLUGIN_MODULE)", NULL);
	g_clear_object (&data->action_group);
##endif
}

static void
##(PLUGIN_ID.lower)_plugin_update_state (PlumaWindowActivatable *activatable)
{
	pluma_debug (DEBUG_PLUGINS);

	update_ui (##(PLUGIN_ID.upper)_PLUGIN (activatable)->priv);
}

##ifdef WITH_CONFIGURE_DIALOG
static GtkWidget *
##(PLUGIN_ID.lower)_plugin_create_configure_widget (PeasGtkConfigurable *configurable)
{
	pluma_debug (DEBUG_PLUGINS);
}
##endif

static void
##(PLUGIN_ID.lower)_plugin_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	##(PLUGIN_ID.camel)Plugin *plugin = ##(PLUGIN_ID.upper)_PLUGIN (object);

	switch (prop_id)
	{
		case PROP_OBJECT:
			plugin->priv->window = PLUMA_WINDOW (g_value_dup_object (value));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
##(PLUGIN_ID.lower)_plugin_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	##(PLUGIN_ID.camel)Plugin *plugin = ##(PLUGIN_ID.upper)_PLUGIN (object);

	switch (prop_id)
	{
		case PROP_OBJECT:
			g_value_set_object (value, plugin->priv->window);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
##(PLUGIN_ID.lower)_plugin_class_init (##(PLUGIN_ID.camel)PluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = ##(PLUGIN_ID.lower)_plugin_finalize;
	object_class->dispose = ##(PLUGIN_ID.lower)_plugin_dispose;
	object_class->set_property = ##(PLUGIN_ID.lower)_plugin_set_property;
	object_class->get_property = ##(PLUGIN_ID.lower)_plugin_get_property;

	g_object_class_override_property (object_class, PROP_OBJECT, "object");
}

static void
##(PLUGIN_ID.lower)_plugin_class_finalize (##(PLUGIN_ID.camel)PluginClass *klass)
{
	/* dummy function - used by G_DEFINE_DYNAMIC_TYPE_EXTENDED */
}

static void
pluma_window_activatable_iface_init (PlumaWindowActivatableInterface *iface)
{
	iface->activate = ##(PLUGIN_ID.lower)_plugin_activate;
	iface->deactivate = ##(PLUGIN_ID.lower)_plugin_deactivate;
	iface->update_state = ##(PLUGIN_ID.lower)_plugin_update_state;
}

##ifdef WITH_CONFIGURE_DIALOG
static void
peas_gtk_configurable_iface_init (PeasGtkConfigurableInterface *iface)
{
	iface->create_configure_widget = ##(PLUGIN_ID.lower)_plugin_create_configure_widget;
}
##endif

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	##(PLUGIN_ID.lower)_plugin_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
                                                    PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                    TYPE_##(PLUGIN_ID.upper)_PLUGIN);

##ifdef WITH_CONFIGURE_DIALOG
	peas_object_module_register_extension_type (module,
                                                    PEAS_GTK_TYPE_CONFIGURABLE,
                                                    TYPE_##(PLUGIN_ID.upper)_PLUGIN);
##endif
}
