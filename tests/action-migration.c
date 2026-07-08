#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include "pluma-action-migration.h"

/* This test exercises pluma_action_migration_mirror_group() against real
 * GtkAction/GtkToggleAction/GtkActionGroup instances on purpose, so the
 * resulting deprecation warnings are expected here and silenced locally
 * rather than project-wide. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static void
legacy_activated (GtkAction *action, guint *count)
{
	(*count)++;
}

static void
plugin_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	guint *count = user_data;
	(*count)++;
}

static void
test_legacy_mirror (void)
{
	GtkApplication *application;
	GtkActionGroup *legacy_group;
	GtkAction *legacy_action;
	GSimpleActionGroup *modern_group;
	GAction *modern_action;
	PlumaLegacyActionMapping mapping = { "LegacySave", "save", NULL };
	guint count = 0;

	application = gtk_application_new ("org.mate.pluma.ActionMigrationTest",
	                                  G_APPLICATION_NON_UNIQUE);
	legacy_group = gtk_action_group_new ("legacy");
	legacy_action = gtk_action_new ("LegacySave", "Save", NULL, NULL);
	g_signal_connect (legacy_action, "activate", G_CALLBACK (legacy_activated), &count);
	gtk_action_group_add_action (legacy_group, legacy_action);
	modern_group = g_simple_action_group_new ();

	pluma_action_migration_mirror_group (application, G_ACTION_MAP (modern_group),
	                                     legacy_group, &mapping, 1);
	modern_action = g_action_map_lookup_action (G_ACTION_MAP (modern_group), "save");
	g_assert_nonnull (modern_action);
	g_assert_true (g_action_get_enabled (modern_action));
	g_action_activate (modern_action, NULL);
	g_assert_cmpuint (count, ==, 1);
	gtk_action_set_sensitive (legacy_action, FALSE);
	g_assert_false (g_action_get_enabled (modern_action));
	gtk_action_set_sensitive (legacy_action, TRUE);
	g_assert_true (g_action_get_enabled (modern_action));
	gtk_action_group_set_sensitive (legacy_group, FALSE);
	g_assert_false (g_action_get_enabled (modern_action));

	g_object_unref (modern_group);
	g_object_unref (legacy_action);
	g_object_unref (legacy_group);
	g_object_unref (application);
}

static void
test_menu_loader (void)
{
	const gchar *contents =
		"<interface><menu id='test-menu'><item>"
		"<attribute name='label'>Save</attribute>"
		"<attribute name='action'>win.save</attribute>"
		"</item></menu></interface>";
	gchar *filename = NULL;
	GError *error = NULL;
	GMenuModel *menu;
	gint fd;

	fd = g_file_open_tmp ("pluma-menu-test-XXXXXX.ui", &filename, &error);
	g_assert_no_error (error);
	g_assert_cmpint (fd, >=, 0);
	close (fd);
	g_assert_true (g_file_set_contents (filename, contents, -1, &error));
	g_assert_no_error (error);
	menu = pluma_action_migration_load_menu (filename, "test-menu", &error);
	g_assert_no_error (error);
	g_assert_nonnull (menu);
	g_assert_cmpint (g_menu_model_get_n_items (menu), ==, 1);
	g_object_unref (menu);
	g_remove (filename);
	g_free (filename);
}

static void
test_toggle_mirror (void)
{
	GtkApplication *application;
	GtkActionGroup *legacy_group;
	GtkToggleAction *legacy_action;
	GSimpleActionGroup *modern_group;
	GAction *modern_action;
	GVariant *state;
	PlumaLegacyActionMapping mapping = { "LegacyPanel", "show-panel", NULL };

	application = gtk_application_new ("org.mate.pluma.ActionToggleTest",
	                                  G_APPLICATION_NON_UNIQUE);
	legacy_group = gtk_action_group_new ("legacy-toggle");
	legacy_action = gtk_toggle_action_new ("LegacyPanel", "Panel", NULL, NULL);
	gtk_action_group_add_action (legacy_group, GTK_ACTION (legacy_action));
	modern_group = g_simple_action_group_new ();
	pluma_action_migration_mirror_group (application, G_ACTION_MAP (modern_group),
	                                     legacy_group, &mapping, 1);
	modern_action = g_action_map_lookup_action (G_ACTION_MAP (modern_group), "show-panel");

	state = g_action_get_state (modern_action);
	g_assert_false (g_variant_get_boolean (state));
	g_variant_unref (state);
	g_action_activate (modern_action, NULL);
	g_assert_true (gtk_toggle_action_get_active (legacy_action));
	state = g_action_get_state (modern_action);
	g_assert_true (g_variant_get_boolean (state));
	g_variant_unref (state);
	g_action_change_state (modern_action, g_variant_new_boolean (FALSE));
	g_assert_false (gtk_toggle_action_get_active (legacy_action));

	g_object_unref (modern_group);
	g_object_unref (legacy_action);
	g_object_unref (legacy_group);
	g_object_unref (application);
}

static void
test_plugin_group (void)
{
	static const GActionEntry entries[] = {
		{ "run", plugin_activated, NULL, NULL, NULL, { 0, 0, 0 } }
	};
	GSimpleActionGroup *group;
	guint count = 0;

	group = pluma_action_migration_create_plugin_group (entries, G_N_ELEMENTS (entries), &count);
	g_action_group_activate_action (G_ACTION_GROUP (group), "run", NULL);
	g_assert_cmpuint (count, ==, 1);
	g_object_unref (group);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/actions/legacy-mirror", test_legacy_mirror);
	g_test_add_func ("/actions/toggle-mirror", test_toggle_mirror);
	g_test_add_func ("/actions/plugin-group", test_plugin_group);
	g_test_add_func ("/actions/menu-loader", test_menu_loader);
	return g_test_run ();
}

#pragma GCC diagnostic pop
