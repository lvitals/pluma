#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-action-migration.h"

/* This file is the temporary bridge between the legacy GtkAction/
 * GtkActionGroup/GtkToggleAction API and GAction/GSimpleAction. Its whole
 * purpose is to call the deprecated GTK3 action API, so the deprecation
 * warnings are expected here and silenced locally rather than project-wide.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

typedef struct
{
	GtkAction *legacy_action;
} LegacyBinding;

static void
legacy_binding_free (LegacyBinding *binding)
{
	g_object_unref (binding->legacy_action);
	g_free (binding);
}

static void
modern_action_activated (GSimpleAction *action,
	                     GVariant      *parameter,
	                     gpointer       user_data)
{
	LegacyBinding *binding = user_data;
	gtk_action_activate (binding->legacy_action);
}

static void
modern_action_change_state (GSimpleAction *action,
	                        GVariant      *value,
	                        gpointer       user_data)
{
	LegacyBinding *binding = user_data;

	if (GTK_IS_TOGGLE_ACTION (binding->legacy_action))
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (binding->legacy_action),
		                              g_variant_get_boolean (value));
}

static void
legacy_active_changed (GtkToggleAction *legacy_action,
	                   GParamSpec      *pspec,
	                   GSimpleAction   *modern_action)
{
	g_simple_action_set_state (modern_action,
	                           g_variant_new_boolean (gtk_toggle_action_get_active (legacy_action)));
}

static void
legacy_sensitive_changed (GObject       *source,
	                      GParamSpec    *pspec,
	                      GSimpleAction *modern_action)
{
	LegacyBinding *binding = g_object_get_data (G_OBJECT (modern_action),
	                                            "pluma-legacy-binding");

	if (binding == NULL)
		return;
	g_simple_action_set_enabled (modern_action,
	                             gtk_action_is_sensitive (binding->legacy_action));
}

void
pluma_action_migration_mirror_group (GtkApplication                 *application,
	                                 GActionMap                     *target,
	                                 GtkActionGroup                 *legacy_group,
	                                 const PlumaLegacyActionMapping *mappings,
	                                 gsize                           n_mappings)
{
	gsize i;

	g_return_if_fail (GTK_IS_APPLICATION (application));
	g_return_if_fail (G_IS_ACTION_MAP (target));
	g_return_if_fail (GTK_IS_ACTION_GROUP (legacy_group));

	for (i = 0; i < n_mappings; i++)
	{
		GtkAction *legacy_action;
		GSimpleAction *modern_action;
		LegacyBinding *binding;
		gchar *detailed_name;

		legacy_action = gtk_action_group_get_action (legacy_group, mappings[i].legacy_name);
		if (legacy_action == NULL)
		{
			g_warning ("Cannot mirror missing legacy action %s", mappings[i].legacy_name);
			continue;
		}
		if (g_action_map_lookup_action (target, mappings[i].action_name) != NULL)
			continue;

		if (GTK_IS_TOGGLE_ACTION (legacy_action))
			modern_action = g_simple_action_new_stateful (
				mappings[i].action_name, NULL,
				g_variant_new_boolean (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (legacy_action))));
		else
			modern_action = g_simple_action_new (mappings[i].action_name, NULL);
		binding = g_new0 (LegacyBinding, 1);
		binding->legacy_action = g_object_ref (legacy_action);
		g_simple_action_set_enabled (modern_action, gtk_action_is_sensitive (legacy_action));
		g_signal_connect (modern_action, "activate", G_CALLBACK (modern_action_activated), binding);
		if (GTK_IS_TOGGLE_ACTION (legacy_action))
		{
			g_signal_connect (modern_action, "change-state",
			                  G_CALLBACK (modern_action_change_state), binding);
			g_signal_connect_object (legacy_action, "notify::active",
			                         G_CALLBACK (legacy_active_changed), modern_action, 0);
		}
		g_signal_connect_object (legacy_action, "notify::sensitive",
		                         G_CALLBACK (legacy_sensitive_changed), modern_action, 0);
		g_signal_connect_object (legacy_group, "notify::sensitive",
		                         G_CALLBACK (legacy_sensitive_changed), modern_action, 0);
		g_object_set_data_full (G_OBJECT (modern_action), "pluma-legacy-binding", binding,
		                        (GDestroyNotify) legacy_binding_free);
		g_action_map_add_action (target, G_ACTION (modern_action));

		if (mappings[i].accelerators != NULL)
		{
			detailed_name = g_strconcat ("win.", mappings[i].action_name, NULL);
			gtk_application_set_accels_for_action (application, detailed_name,
			                                       mappings[i].accelerators);
			g_free (detailed_name);
		}
		g_object_unref (modern_action);
	}
}

GSimpleActionGroup *
pluma_action_migration_create_plugin_group (const GActionEntry *entries,
	                                        gsize               n_entries,
	                                        gpointer            user_data)
{
	GSimpleActionGroup *group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, n_entries, user_data);
	return group;
}

void
pluma_action_migration_insert_plugin_group (GtkWidget          *window,
	                                        const gchar        *prefix,
	                                        GSimpleActionGroup *group)
{
	g_return_if_fail (GTK_IS_WIDGET (window));
	g_return_if_fail (prefix != NULL && *prefix != '\0');
	g_return_if_fail (group == NULL || G_IS_SIMPLE_ACTION_GROUP (group));
	gtk_widget_insert_action_group (window, prefix,
	                                group != NULL ? G_ACTION_GROUP (group) : NULL);
}

GMenuModel *
pluma_action_migration_load_menu (const gchar *filename,
	                             const gchar *object_id,
	                             GError     **error)
{
	GtkBuilder *builder = gtk_builder_new ();
	GObject *object;
	GMenuModel *menu = NULL;

	if (!gtk_builder_add_from_file (builder, filename, error))
	{
		g_object_unref (builder);
		return NULL;
	}
	object = gtk_builder_get_object (builder, object_id);
	if (object == NULL || !G_IS_MENU_MODEL (object))
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "Object '%s' in %s is not a GMenuModel", object_id, filename);
	else
		menu = g_object_ref (G_MENU_MODEL (object));
	g_object_unref (builder);
	return menu;
}

#pragma GCC diagnostic pop
