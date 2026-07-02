#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-externaltools-plugin.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gmodule.h>

#include <pluma/pluma-window-activatable.h>
#include <pluma/pluma-window.h>
#include <pluma/pluma-panel.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-action-migration.h>

#include "pluma-tool-library.h"
#include "pluma-tool-output-panel.h"
#include "pluma-tool-functions.h"
#include "pluma-tool-manager.h"

#define TOOLS_MENU_PATH "/MenuBar/ToolsMenu/ToolsOps_4/ExternalToolsMenu/ExternalToolPlaceholder"

typedef struct {
    PlumaExternalToolsPlugin *plugin;
    PlumaTool *tool;
    gulong handler_id;
} AccelMapBinding;

static void peas_activatable_iface_init (PlumaWindowActivatableInterface *iface);
static void tool_menu_update (PlumaExternalToolsPlugin *plugin);
static void tool_menu_remove (PlumaExternalToolsPlugin *plugin);
static void tool_menu_filter (PlumaExternalToolsPlugin *plugin, PlumaDocument *document);
static void on_manager_updated (gpointer user_data);
static void modern_tool_group_rebuild (PlumaExternalToolsPlugin *plugin);

enum { PROP_0, PROP_WINDOW };

struct _PlumaExternalToolsPluginPrivate {
    PlumaWindow *window;

    gchar *data_dir;
    gchar *system_dir;
    gchar *user_dir;
    GPtrArray *tools;

    PlumaToolOutputPanel *panel;

    GtkActionGroup *static_action_group;
    guint static_merge_id;

    GtkActionGroup *tool_action_group;
    guint tool_merge_id;
    GList *accelmap_handlers; /* AccelMapBinding* */

    GSimpleActionGroup *modern_action_group; /* "plugin-externaltools" prefix */

    PlumaToolManager *manager;
    gint manager_width, manager_height;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaExternalToolsPlugin, pluma_externaltools_plugin,
                                PEAS_TYPE_EXTENSION_BASE, 0,
                                G_ADD_PRIVATE_DYNAMIC (PlumaExternalToolsPlugin)
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                               peas_activatable_iface_init))

/* ---------------------------------------------------------------- */
/* visibility filtering                                              */
/* ---------------------------------------------------------------- */

static gboolean
strv_has (gchar **strv, const gchar *value)
{
    guint i;
    if (strv == NULL)
        return FALSE;
    for (i = 0; strv[i] != NULL; i++)
        if (g_strcmp0 (strv[i], value) == 0)
            return TRUE;
    return FALSE;
}

static gboolean
tool_applies_to_language (PlumaTool *tool, GtkSourceLanguage *language)
{
    if (tool->languages == NULL || tool->languages[0] == NULL)
        return TRUE;

    if (language == NULL)
        return strv_has (tool->languages, "plain");

    return strv_has (tool->languages, gtk_source_language_get_id (language));
}

static gboolean
tool_is_visible (PlumaTool *tool, PlumaDocument *document)
{
    gboolean titled, remote, visible;
    gchar *uri = pluma_document_get_uri (document);

    titled = uri != NULL;
    remote = !pluma_document_is_local (document);
    g_free (uri);

    if (g_strcmp0 (tool->applicability, "local") == 0)
        visible = titled && !remote;
    else if (g_strcmp0 (tool->applicability, "remote") == 0)
        visible = titled && remote;
    else if (g_strcmp0 (tool->applicability, "titled") == 0)
        visible = titled;
    else if (g_strcmp0 (tool->applicability, "untitled") == 0)
        visible = !titled;
    else
        visible = TRUE; /* "all" or unrecognized */

    return visible && tool_applies_to_language (tool, pluma_document_get_language (document));
}

/* ---------------------------------------------------------------- */
/* dynamic Tools submenu                                             */
/* ---------------------------------------------------------------- */

static void
on_tool_action_activate (GtkAction *action, PlumaExternalToolsPlugin *plugin)
{
    PlumaTool *tool = g_object_get_data (G_OBJECT (action), "pluma-tool");

    if (tool != NULL)
        pluma_tool_run_from_menu (plugin->priv->window, plugin->priv->panel, tool);
}

static void
on_accelmap_changed (GtkAccelMap *accel_map, gchar *accel_path, guint key, GdkModifierType mods,
                     AccelMapBinding *binding)
{
    g_free (binding->tool->shortcut);
    binding->tool->shortcut = gtk_accelerator_name (key, mods);
    pluma_tool_save (binding->tool, binding->plugin->priv->user_dir);

    if (binding->plugin->priv->manager != NULL)
        pluma_tool_manager_tool_changed (binding->plugin->priv->manager, binding->tool);

    modern_tool_group_rebuild (binding->plugin);
}

/* ---------------------------------------------------------------- */
/* "plugin-externaltools" GAction/GMenu mirror                       */
/*                                                                   */
/* Per-tool actions/menu items are rebuilt wholesale (rather than    */
/* toggling visibility like the legacy GtkAction group does) because */
/* GMenuModel has no per-item "visible" state independent of the     */
/* action's enabled flag; the "manage" action/item is static and is  */
/* deliberately skipped by the clear helper so rebuilds don't touch  */
/* it.                                                                */
/* ---------------------------------------------------------------- */

static void
on_modern_tool_action_activate (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    PlumaExternalToolsPlugin *plugin = user_data;
    PlumaTool *tool = g_object_get_data (G_OBJECT (action), "pluma-tool");

    if (tool != NULL)
        pluma_tool_run_from_menu (plugin->priv->window, plugin->priv->panel, tool);
}

static void
modern_tool_group_clear (PlumaExternalToolsPlugin *plugin)
{
    PlumaExternalToolsPluginPrivate *priv = plugin->priv;
    gchar **names;
    guint i;

    if (priv->modern_action_group == NULL)
        return;

    names = g_action_group_list_actions (G_ACTION_GROUP (priv->modern_action_group));
    for (i = 0; names[i] != NULL; i++) {
        gchar *detailed_name;

        if (g_strcmp0 (names[i], "manage") == 0)
            continue;

        detailed_name = g_strconcat ("plugin-externaltools.", names[i], NULL);
        pluma_window_remove_menu_items (priv->window, "plugin-tools-section", detailed_name);
        g_free (detailed_name);
        g_action_map_remove_action (G_ACTION_MAP (priv->modern_action_group), names[i]);
    }
    g_strfreev (names);
}

static void
modern_tool_group_rebuild (PlumaExternalToolsPlugin *plugin)
{
    PlumaExternalToolsPluginPrivate *priv = plugin->priv;
    GtkApplication *application;
    PlumaDocument *document;
    guint i;

    if (priv->modern_action_group == NULL)
        return;

    application = gtk_window_get_application (GTK_WINDOW (priv->window));
    if (application == NULL)
        return;

    modern_tool_group_clear (plugin);

    document = pluma_window_get_active_document (priv->window);

    for (i = 0; i < priv->tools->len; i++) {
        PlumaTool *tool = g_ptr_array_index (priv->tools, i);
        gchar *action_name;
        gchar *detailed_name;
        GSimpleAction *action;
        GMenuItem *item;

        if (document != NULL && !tool_is_visible (tool, document))
            continue;

        action_name = g_strdup_printf ("tool-%p", (void *) tool);
        detailed_name = g_strconcat ("plugin-externaltools.", action_name, NULL);

        action = g_simple_action_new (action_name, NULL);
        g_object_set_data (G_OBJECT (action), "pluma-tool", tool);
        g_signal_connect (action, "activate", G_CALLBACK (on_modern_tool_action_activate), plugin);
        g_action_map_add_action (G_ACTION_MAP (priv->modern_action_group), G_ACTION (action));
        g_object_unref (action);

        if (tool->shortcut != NULL) {
            const gchar *accels[] = { tool->shortcut, NULL };
            gtk_application_set_accels_for_action (application, detailed_name, accels);
        }

        item = g_menu_item_new (tool->name, detailed_name);
        pluma_window_add_menu_item (priv->window, "plugin-tools-section", item);
        g_object_unref (item);

        g_free (detailed_name);
        g_free (action_name);
    }
}

static void
tool_menu_remove (PlumaExternalToolsPlugin *plugin)
{
    PlumaExternalToolsPluginPrivate *priv = plugin->priv;
    GtkUIManager *manager = pluma_window_get_ui_manager (priv->window);
    GList *actions, *l;

    if (priv->tool_merge_id != 0) {
        gtk_ui_manager_remove_ui (manager, priv->tool_merge_id);
        gtk_ui_manager_remove_action_group (manager, priv->tool_action_group);
        priv->tool_merge_id = 0;
    }

    actions = gtk_action_group_list_actions (priv->tool_action_group);
    for (l = actions; l != NULL; l = l->next)
        gtk_action_group_remove_action (priv->tool_action_group, GTK_ACTION (l->data));
    g_list_free (actions);

    {
        GtkAccelMap *accel_map = gtk_accel_map_get ();
        for (l = priv->accelmap_handlers; l != NULL; l = l->next) {
            AccelMapBinding *binding = l->data;
            g_signal_handler_disconnect (accel_map, binding->handler_id);
            g_free (binding);
        }
        g_list_free (priv->accelmap_handlers);
        priv->accelmap_handlers = NULL;
    }

    modern_tool_group_clear (plugin);
}

static void
tool_menu_update (PlumaExternalToolsPlugin *plugin)
{
    PlumaExternalToolsPluginPrivate *priv = plugin->priv;
    GtkUIManager *manager = pluma_window_get_ui_manager (priv->window);
    guint i;

    tool_menu_remove (plugin);

    priv->tool_merge_id = gtk_ui_manager_new_merge_id (manager);

    for (i = 0; i < priv->tools->len; i++) {
        PlumaTool *tool = g_ptr_array_index (priv->tools, i);
        gchar *action_name = g_strdup_printf ("ExternalToolTool%p", (void *) tool);
        GtkAction *action = gtk_action_new (action_name, tool->name, tool->comment, NULL);

        g_object_set_data (G_OBJECT (action), "pluma-tool", tool);
        g_signal_connect (action, "activate", G_CALLBACK (on_tool_action_activate), plugin);

        if (tool->shortcut != NULL) {
            guint key;
            GdkModifierType mods;
            gchar *accel_path = g_strdup_printf ("<Actions>/ExternalToolsPluginToolActions/%s", action_name);
            gchar *detailed_signal = g_strdup_printf ("changed::%s", accel_path);
            AccelMapBinding *binding = g_new0 (AccelMapBinding, 1);

            gtk_accelerator_parse (tool->shortcut, &key, &mods);
            gtk_accel_map_add_entry (accel_path, key, mods);

            binding->plugin = plugin;
            binding->tool = tool;
            binding->handler_id = g_signal_connect (gtk_accel_map_get (), detailed_signal,
                                                    G_CALLBACK (on_accelmap_changed), binding);
            priv->accelmap_handlers = g_list_prepend (priv->accelmap_handlers, binding);

            g_free (detailed_signal);
            g_free (accel_path);

            gtk_action_group_add_action_with_accel (priv->tool_action_group, action, tool->shortcut);
        } else {
            gtk_action_group_add_action_with_accel (priv->tool_action_group, action, NULL);
        }

        gtk_ui_manager_add_ui (manager, priv->tool_merge_id, TOOLS_MENU_PATH,
                               action_name, action_name, GTK_UI_MANAGER_MENUITEM, FALSE);

        g_object_unref (action);
        g_free (action_name);
    }

    gtk_ui_manager_insert_action_group (manager, priv->tool_action_group, -1);
    tool_menu_filter (plugin, pluma_window_get_active_document (priv->window));
}

static void
tool_menu_filter (PlumaExternalToolsPlugin *plugin, PlumaDocument *document)
{
    GList *actions, *l;

    modern_tool_group_rebuild (plugin);

    if (document == NULL)
        return;

    actions = gtk_action_group_list_actions (plugin->priv->tool_action_group);
    for (l = actions; l != NULL; l = l->next) {
        GtkAction *action = GTK_ACTION (l->data);
        PlumaTool *tool = g_object_get_data (G_OBJECT (action), "pluma-tool");

        if (tool != NULL)
            gtk_action_set_visible (action, tool_is_visible (tool, document));
    }
    g_list_free (actions);
}

/* ---------------------------------------------------------------- */
/* manager dialog                                                    */
/* ---------------------------------------------------------------- */

static void
on_manager_updated (gpointer user_data)
{
    tool_menu_update (PLUMA_EXTERNALTOOLS_PLUGIN (user_data));
}

static void
open_manager_cb (GtkAction *action, PlumaExternalToolsPlugin *plugin)
{
    PlumaExternalToolsPluginPrivate *priv = plugin->priv;

    if (priv->manager == NULL) {
        priv->manager = pluma_tool_manager_new (priv->data_dir, priv->system_dir, priv->user_dir,
                                                priv->tools, priv->manager_width, priv->manager_height);
        if (priv->manager == NULL)
            return;
        pluma_tool_manager_set_callbacks (priv->manager, on_manager_updated, plugin);
    }

    pluma_tool_manager_run (priv->manager, priv->window);
}

static void
modern_open_manager_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    open_manager_cb (NULL, PLUMA_EXTERNALTOOLS_PLUGIN (user_data));
}

/* ---------------------------------------------------------------- */
/* PlumaWindowActivatable                                            */
/* ---------------------------------------------------------------- */

static const GtkActionEntry static_entries[] = {
    { "ExternalToolManager", NULL, N_("Manage _External Tools..."), NULL,
      N_("Opens the External Tools Manager"), G_CALLBACK (open_manager_cb) },
    { "ExternalTools", NULL, N_("External _Tools"), NULL, N_("External tools"), NULL }
};

static const gchar ui_string[] =
    "<ui>"
    "  <menubar name=\"MenuBar\">"
    "    <menu name=\"ToolsMenu\" action=\"Tools\">"
    "      <placeholder name=\"ToolsOps_4\">"
    "        <separator/>"
    "        <menu name=\"ExternalToolsMenu\" action=\"ExternalTools\">"
    "          <placeholder name=\"ExternalToolPlaceholder\"/>"
    "        </menu>"
    "        <separator/>"
    "      </placeholder>"
    "      <placeholder name=\"ToolsOps_5\">"
    "        <menuitem name=\"ExternalToolManager\" action=\"ExternalToolManager\"/>"
    "      </placeholder>"
    "    </menu>"
    "  </menubar>"
    "</ui>";

static void
pluma_externaltools_plugin_activate (PlumaWindowActivatable *activatable)
{
    PlumaExternalToolsPlugin *plugin = PLUMA_EXTERNALTOOLS_PLUGIN (activatable);
    PlumaExternalToolsPluginPrivate *priv = plugin->priv;
    GtkUIManager *manager;
    GError *error = NULL;

    priv->data_dir = peas_extension_base_get_data_dir (PEAS_EXTENSION_BASE (plugin));
    priv->system_dir = g_build_filename (priv->data_dir, "tools", NULL);
    priv->user_dir = g_build_filename (g_get_user_config_dir (), "pluma", "tools", NULL);
    g_mkdir_with_parents (priv->user_dir, 0700);

    priv->tools = pluma_tool_library_load (priv->system_dir, priv->user_dir);

    manager = pluma_window_get_ui_manager (priv->window);

    priv->static_action_group = gtk_action_group_new ("ExternalToolsPluginActions");
    gtk_action_group_set_translation_domain (priv->static_action_group, GETTEXT_PACKAGE);
    gtk_action_group_add_actions (priv->static_action_group, static_entries,
                                  G_N_ELEMENTS (static_entries), plugin);
    gtk_ui_manager_insert_action_group (manager, priv->static_action_group, -1);

    priv->static_merge_id = gtk_ui_manager_add_ui_from_string (manager, ui_string, -1, &error);
    if (priv->static_merge_id == 0) {
        g_warning ("External Tools: failed to merge menu UI: %s", error->message);
        g_error_free (error);
    }

    priv->panel = pluma_tool_output_panel_new (priv->data_dir, priv->window);
    if (priv->panel != NULL)
        pluma_panel_add_item_with_icon (pluma_window_get_bottom_panel (priv->window),
                                        pluma_tool_output_panel_get_widget (priv->panel),
                                        _("Shell Output"), "system-run");

    priv->tool_action_group = gtk_action_group_new ("ExternalToolsPluginToolActions");

    {
        GtkApplication *application = gtk_window_get_application (GTK_WINDOW (priv->window));

        if (application != NULL) {
            GSimpleAction *manage_action;
            GMenuItem *item;

            priv->modern_action_group = g_simple_action_group_new ();
            gtk_widget_insert_action_group (GTK_WIDGET (priv->window), "plugin-externaltools",
                                            G_ACTION_GROUP (priv->modern_action_group));

            manage_action = g_simple_action_new ("manage", NULL);
            g_signal_connect (manage_action, "activate", G_CALLBACK (modern_open_manager_activated), plugin);
            g_action_map_add_action (G_ACTION_MAP (priv->modern_action_group), G_ACTION (manage_action));
            g_object_unref (manage_action);

            item = g_menu_item_new (_("Manage _External Tools..."), "plugin-externaltools.manage");
            pluma_window_add_menu_item (priv->window, "plugin-tools-section", item);
            g_object_unref (item);
        }
    }

    tool_menu_update (plugin);

    gtk_ui_manager_ensure_update (manager);
}

static void
pluma_externaltools_plugin_deactivate (PlumaWindowActivatable *activatable)
{
    PlumaExternalToolsPlugin *plugin = PLUMA_EXTERNALTOOLS_PLUGIN (activatable);
    PlumaExternalToolsPluginPrivate *priv = plugin->priv;
    GtkUIManager *manager = pluma_window_get_ui_manager (priv->window);

    tool_menu_remove (plugin);
    g_clear_object (&priv->tool_action_group);

    if (priv->modern_action_group != NULL) {
        pluma_window_remove_menu_items (priv->window, "plugin-tools-section",
                                        "plugin-externaltools.manage");
        gtk_widget_insert_action_group (GTK_WIDGET (priv->window), "plugin-externaltools", NULL);
        g_clear_object (&priv->modern_action_group);
    }

    if (priv->static_merge_id != 0)
        gtk_ui_manager_remove_ui (manager, priv->static_merge_id);
    if (priv->static_action_group != NULL) {
        gtk_ui_manager_remove_action_group (manager, priv->static_action_group);
        g_clear_object (&priv->static_action_group);
    }
    gtk_ui_manager_ensure_update (manager);

    if (priv->panel != NULL) {
        pluma_panel_remove_item (pluma_window_get_bottom_panel (priv->window),
                                 pluma_tool_output_panel_get_widget (priv->panel));
        pluma_tool_output_panel_free (priv->panel);
        priv->panel = NULL;
    }

    if (priv->manager != NULL) {
        pluma_tool_manager_free (priv->manager);
        priv->manager = NULL;
    }

    if (priv->tools != NULL) {
        g_ptr_array_free (priv->tools, TRUE);
        priv->tools = NULL;
    }

    g_free (priv->data_dir); priv->data_dir = NULL;
    g_free (priv->system_dir); priv->system_dir = NULL;
    g_free (priv->user_dir); priv->user_dir = NULL;
}

static void
pluma_externaltools_plugin_update_state (PlumaWindowActivatable *activatable)
{
    PlumaExternalToolsPlugin *plugin = PLUMA_EXTERNALTOOLS_PLUGIN (activatable);

    tool_menu_filter (plugin, pluma_window_get_active_document (plugin->priv->window));
    gtk_ui_manager_ensure_update (pluma_window_get_ui_manager (plugin->priv->window));
}

static void
pluma_externaltools_plugin_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    PlumaExternalToolsPlugin *plugin = PLUMA_EXTERNALTOOLS_PLUGIN (object);

    switch (prop_id) {
        case PROP_WINDOW:
            plugin->priv->window = PLUMA_WINDOW (g_value_dup_object (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
pluma_externaltools_plugin_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    PlumaExternalToolsPlugin *plugin = PLUMA_EXTERNALTOOLS_PLUGIN (object);

    switch (prop_id) {
        case PROP_WINDOW:
            g_value_set_object (value, plugin->priv->window);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
pluma_externaltools_plugin_init (PlumaExternalToolsPlugin *plugin)
{
    plugin->priv = pluma_externaltools_plugin_get_instance_private (plugin);
}

static void
pluma_externaltools_plugin_dispose (GObject *object)
{
    PlumaExternalToolsPlugin *plugin = PLUMA_EXTERNALTOOLS_PLUGIN (object);

    g_clear_object (&plugin->priv->window);

    G_OBJECT_CLASS (pluma_externaltools_plugin_parent_class)->dispose (object);
}

static void
pluma_externaltools_plugin_class_init (PlumaExternalToolsPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = pluma_externaltools_plugin_dispose;
    object_class->set_property = pluma_externaltools_plugin_set_property;
    object_class->get_property = pluma_externaltools_plugin_get_property;

    g_object_class_override_property (object_class, PROP_WINDOW, "window");
}

static void
pluma_externaltools_plugin_class_finalize (PlumaExternalToolsPluginClass *klass)
{
    /* dummy function - used by G_DEFINE_DYNAMIC_TYPE_EXTENDED */
}

static void
peas_activatable_iface_init (PlumaWindowActivatableInterface *iface)
{
    iface->activate = pluma_externaltools_plugin_activate;
    iface->deactivate = pluma_externaltools_plugin_deactivate;
    iface->update_state = pluma_externaltools_plugin_update_state;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
    pluma_externaltools_plugin_register_type (G_TYPE_MODULE (module));
    peas_object_module_register_extension_type (module, PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                PLUMA_TYPE_EXTERNALTOOLS_PLUGIN);
}
