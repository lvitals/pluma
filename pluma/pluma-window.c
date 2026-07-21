/*
 * pluma-window.c
 * This file is part of pluma
 *
 * Copyright (C) 2005 - Paolo Maggi
 * Copyright (C) 2012-2021 MATE Developers
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

/*
 * Modified by the pluma Team, 2005. See the AUTHORS file for a
 * list of people on the pluma Team.
 * See the ChangeLog files for a list of changes.
 *
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include <sys/types.h>
#include <string.h>

#include <gdk/gdk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <libpeas/peas-extension-set.h>

#include "pluma-actions.h"
#include "pluma-action-migration.h"
#include "pluma-window.h"
#include "pluma-window-private.h"
#include "pluma-project-search-panel.h"
#include "pluma-file-browser-panel.h"
#include "pluma-file-browser-messages.h"
#ifdef ENABLE_GIT
#include "pluma-git-panel.h"
#endif
#include "pluma-app.h"
#include "pluma-notebook.h"
#include "pluma-statusbar.h"
#include "pluma-utils.h"
#include "pluma-commands.h"
#include "pluma-debug.h"
#include "pluma-language-manager.h"
#include "pluma-panel.h"
#include "pluma-documents-panel.h"
#include "pluma-plugins-engine.h"
#include "pluma-window-activatable.h"
#include "pluma-enum-types.h"
#include "pluma-dirs.h"
#include "pluma-status-combo-box.h"
#include "pluma-settings.h"

static gboolean
g_settings_has_key (GSettings *settings, const gchar *key)
{
	GSettingsSchema *schema = NULL;
	gboolean has_key = FALSE;

	g_object_get (settings, "settings-schema", &schema, NULL);
	if (schema != NULL)
	{
		has_key = g_settings_schema_has_key (schema, key);
		g_settings_schema_unref (schema);
	}
	return has_key;
}

static gint
pluma_g_settings_get_int_safe (GSettings *settings, const gchar *key, gint default_val)
{
	if (g_settings_has_key (settings, key))
		return g_settings_get_int (settings, key);
	return default_val;
}

static void
pluma_g_settings_set_int_safe (GSettings *settings, const gchar *key, gint val)
{
	if (g_settings_has_key (settings, key))
		g_settings_set_int (settings, key, val);
}

static gboolean
pluma_g_settings_get_boolean_safe (GSettings *settings, const gchar *key, gboolean default_val)
{
	if (g_settings_has_key (settings, key))
		return g_settings_get_boolean (settings, key);
	return default_val;
}

static void
pluma_g_settings_set_boolean_safe (GSettings *settings, const gchar *key, gboolean val)
{
	if (g_settings_has_key (settings, key))
		g_settings_set_boolean (settings, key, val);
}

#define LANGUAGE_NONE (const gchar *)"LangNone"
#define TAB_WIDTH_DATA "PlumaWindowTabWidthData"
#define LANGUAGE_DATA "PlumaWindowLanguageData"
#define FULLSCREEN_ANIMATION_SPEED 4

/* Local variables */
static gboolean cansave = TRUE;

/* Signals */
enum
{
    TAB_ADDED,
    TAB_REMOVED,
    TABS_REORDERED,
    ACTIVE_TAB_CHANGED,
    ACTIVE_TAB_STATE_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum
{
    PROP_0,
    PROP_STATE
};

enum
{
    TARGET_URI_LIST = 100
};

G_DEFINE_TYPE_WITH_PRIVATE (PlumaWindow, pluma_window, GTK_TYPE_APPLICATION_WINDOW)

static void    recent_manager_changed    (GtkRecentManager *manager,
                                          PlumaWindow      *window);

static void
pluma_window_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    PlumaWindow *window = PLUMA_WINDOW (object);

    switch (prop_id)
    {
        case PROP_STATE:
            g_value_set_enum (value,
                              pluma_window_get_state (window));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
save_panes_state (PlumaWindow *window)
{
    gint pane_page;

    pluma_debug (DEBUG_WINDOW);

    if ((window->priv->window_state & GDK_WINDOW_STATE_MAXIMIZED) == 0)
        g_settings_set (window->priv->editor_settings, PLUMA_SETTINGS_WINDOW_SIZE,
                        "(ii)", window->priv->width, window->priv->height);

    g_settings_set_int (window->priv->editor_settings, PLUMA_SETTINGS_WINDOW_STATE,
                        window->priv->window_state);

    if (window->priv->side_panel_size > 0)
        g_settings_set_int (window->priv->editor_settings,
                            PLUMA_SETTINGS_SIDE_PANEL_SIZE,
                            window->priv->side_panel_size);

    pane_page = _pluma_panel_get_active_item_id (PLUMA_PANEL (window->priv->side_panel));
    if (pane_page != 0)
        g_settings_set_int (window->priv->editor_settings,
                            PLUMA_SETTINGS_SIDE_PANEL_ACTIVE_PAGE,
                            pane_page);

    if (window->priv->bottom_panel_size > 0)
        g_settings_set_int (window->priv->editor_settings,
                            PLUMA_SETTINGS_BOTTOM_PANEL_SIZE,
                            window->priv->bottom_panel_size);

    pane_page = _pluma_panel_get_active_item_id (PLUMA_PANEL (window->priv->bottom_panel));
    if (pane_page != 0)
        g_settings_set_int (window->priv->editor_settings,
                            PLUMA_SETTINGS_BOTTOM_PANEL_ACTIVE_PAGE, pane_page);

    if (window->priv->right_panel_size > 0)
        pluma_g_settings_set_int_safe (window->priv->editor_settings,
                                       PLUMA_SETTINGS_RIGHT_PANEL_SIZE,
                                       window->priv->right_panel_size);

    pane_page = _pluma_panel_get_active_item_id (PLUMA_PANEL (window->priv->right_panel));
    if (pane_page != 0)
        pluma_g_settings_set_int_safe (window->priv->editor_settings,
                                       PLUMA_SETTINGS_RIGHT_PANEL_ACTIVE_PAGE, pane_page);
}

static void
pluma_window_dispose (GObject *object)
{
    PlumaWindow *window;

    pluma_debug (DEBUG_WINDOW);

    window = PLUMA_WINDOW (object);

    /* Stop tracking removal of panes otherwise we always
     * end up with thinking we had no pane active, since they
     * should all be removed below */
#if GLIB_CHECK_VERSION(2,62,0)
    g_clear_signal_handler (&window->priv->bottom_panel_item_removed_handler_id,
                            window->priv->bottom_panel);
    g_clear_signal_handler (&window->priv->right_panel_item_removed_handler_id,
                            window->priv->right_panel);
#else
    if (window->priv->bottom_panel_item_removed_handler_id != 0)
    {
        g_signal_handler_disconnect (window->priv->bottom_panel,
                                     window->priv->bottom_panel_item_removed_handler_id);
        window->priv->bottom_panel_item_removed_handler_id = 0;
    }
    if (window->priv->right_panel_item_removed_handler_id != 0)
    {
        g_signal_handler_disconnect (window->priv->right_panel,
                                     window->priv->right_panel_item_removed_handler_id);
        window->priv->right_panel_item_removed_handler_id = 0;
    }
#endif

    /* First of all, force collection so that plugins
     * really drop some of the references.
     */
    peas_engine_garbage_collect (PEAS_ENGINE (pluma_plugins_engine_get_default ()));

    /* save the panes position and make sure to deactivate plugins
     * for this window, but only once */
    if (!window->priv->dispose_has_run)
    {
        save_panes_state (window);

        /* Note that unreffing the extensions will automatically remove
           all extensions which in turn will deactivate the extension */
        g_object_unref (window->priv->extensions);

        peas_engine_garbage_collect (PEAS_ENGINE (pluma_plugins_engine_get_default ()));

        /* Must happen before the message bus is unreffed below: the File
         * Browser panel's own "destroy" (triggered later, as a side
         * effect of chaining up to the parent dispose at the end of this
         * function) runs too late to safely unregister from the bus. */
        if (window->priv->file_browser_panel != NULL)
            pluma_file_browser_messages_unregister (window);

        window->priv->dispose_has_run = TRUE;
    }

    if (window->priv->fullscreen_animation_timeout_id != 0)
    {
        g_source_remove (window->priv->fullscreen_animation_timeout_id);
        window->priv->fullscreen_animation_timeout_id = 0;
    }

    if (window->priv->file_chord_timeout_id != 0)
    {
        g_source_remove (window->priv->file_chord_timeout_id);
        window->priv->file_chord_timeout_id = 0;
    }

    if (window->priv->fullscreen_controls != NULL)
    {
        gtk_widget_destroy (window->priv->fullscreen_controls);

        window->priv->fullscreen_controls = NULL;
    }

    if (window->priv->recents_handler_id != 0)
    {
        GtkRecentManager *recent_manager;

        recent_manager =  gtk_recent_manager_get_default ();
#if GLIB_CHECK_VERSION(2,62,0)
        g_clear_signal_handler (&window->priv->recents_handler_id,
                                recent_manager);
#else
        g_signal_handler_disconnect (recent_manager,
                                     window->priv->recents_handler_id);
        window->priv->recents_handler_id = 0;
#endif
    }

    g_clear_object (&window->priv->modern_menu_builder);
    window->priv->modern_recent_section = NULL;
    window->priv->modern_documents_section = NULL;
    window->priv->modern_tools_section = NULL;
    window->priv->tools_menu_item = NULL;

    if (window->priv->message_bus != NULL)
    {
        g_object_unref (window->priv->message_bus);
        window->priv->message_bus = NULL;
    }

    if (window->priv->window_group != NULL)
    {
        g_object_unref (window->priv->window_group);
        window->priv->window_group = NULL;
    }

    /* We must free the settings after saving the panels */
    g_clear_object (&window->priv->editor_settings);

    /* Now that there have broken some reference loops,
     * force collection again.
     */
    peas_engine_garbage_collect (PEAS_ENGINE (pluma_plugins_engine_get_default ()));

    G_OBJECT_CLASS (pluma_window_parent_class)->dispose (object);
}

static void
pluma_window_finalize (GObject *object)
{
    PlumaWindow *window;

    pluma_debug (DEBUG_WINDOW);

    window = PLUMA_WINDOW (object);

    if (window->priv->default_location != NULL)
        g_object_unref (window->priv->default_location);

    G_OBJECT_CLASS (pluma_window_parent_class)->finalize (object);
}

static gboolean
pluma_window_window_state_event (GtkWidget           *widget,
                                 GdkEventWindowState *event)
{
    PlumaWindow *window = PLUMA_WINDOW (widget);

    window->priv->window_state = event->new_window_state;

    return GTK_WIDGET_CLASS (pluma_window_parent_class)->window_state_event (widget, event);
}

static gboolean
pluma_window_configure_event (GtkWidget         *widget,
                              GdkEventConfigure *event)
{
    PlumaWindow *window = PLUMA_WINDOW (widget);

    gtk_window_get_size(GTK_WINDOW (widget), &window->priv->width, &window->priv->height);

    return GTK_WIDGET_CLASS (pluma_window_parent_class)->configure_event (widget, event);
}

/*
 * GtkWindow catches keybindings for the menu items _before_ passing them to
 * the focused widget. This is unfortunate and means that pressing ctrl+V
 * in an entry on a panel ends up pasting text in the TextView.
 * Here we override GtkWindow's handler to do the same things that it
 * does, but in the opposite order and then we chain up to the grand
 * parent handler, skipping gtk_window_key_press_event.
 */
static gboolean
file_chord_timeout_cb (gpointer user_data)
{
    PlumaWindow *window = PLUMA_WINDOW (user_data);
    GAction *action;

    window->priv->file_chord_timeout_id = 0;
    action = g_action_map_lookup_action (G_ACTION_MAP (window), "incremental-search");
    if (action != NULL)
        g_action_activate (action, NULL);

    return G_SOURCE_REMOVE;
}

static gboolean
pluma_window_key_press_event (GtkWidget   *widget,
                              GdkEventKey *event)
{
    static gpointer grand_parent_class = NULL;
    GtkWindow *window = GTK_WINDOW (widget);
    gboolean handled = FALSE;
    /* FIXME: avoid making a new gsettings variable here */
    GSettings *settings = g_settings_new (PLUMA_SCHEMA_ID);
    GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask ();

    /* F9/<Control>F9/<Shift>F9 are handled explicitly with a direct modifier
     * comparison rather than via gtk_application_set_accels_for_action():
     * GDK's "consumed modifiers" detection for function keys is unreliable
     * across keyboard layouts, and was causing <Shift>F9 to activate the
     * same action as plain F9 while <Control>F9 did nothing. Comparing
     * "modifiers" (already masked the same way as the rest of this
     * function) against an exact value sidesteps that entirely. */
    if (event->keyval == GDK_KEY_F9)
    {
        const gchar *action_name = NULL;

        if (modifiers == GDK_SHIFT_MASK)
            action_name = "show-right-pane";
        else if (modifiers == GDK_CONTROL_MASK)
            action_name = "show-bottom-pane";
        else if (modifiers == 0)
            action_name = "show-side-pane";

        if (action_name != NULL)
        {
            g_action_group_activate_action (G_ACTION_GROUP (window), action_name, NULL);
            g_object_unref (settings);
            return TRUE;
        }
    }

    if (PLUMA_WINDOW (window)->priv->file_chord_timeout_id != 0)
    {
        g_source_remove (PLUMA_WINDOW (window)->priv->file_chord_timeout_id);
        PLUMA_WINDOW (window)->priv->file_chord_timeout_id = 0;

        if (modifiers == GDK_CONTROL_MASK &&
            (event->keyval == GDK_KEY_o || event->keyval == GDK_KEY_O))
        {
            GAction *action = g_action_map_lookup_action (G_ACTION_MAP (window), "open-folder");
            if (action != NULL)
                g_action_activate (action, NULL);
            g_object_unref (settings);
            return TRUE;
        }
    }

    if (modifiers == GDK_CONTROL_MASK &&
        (event->keyval == GDK_KEY_k || event->keyval == GDK_KEY_K))
    {
        PLUMA_WINDOW (window)->priv->file_chord_timeout_id =
            g_timeout_add (1500, file_chord_timeout_cb, window);
        g_object_unref (settings);
        return TRUE;
    }

    if (event->state & GDK_CONTROL_MASK)
    {
        gchar     *font;
        gchar     *tempsize;
        gint       nsize;

        font = g_settings_get_string (settings, PLUMA_SETTINGS_EDITOR_FONT);
        tempsize = g_strdup (font);

        g_strreverse (tempsize);
        g_strcanon (tempsize, "1234567890", '\0');
        g_strreverse (tempsize);

        gchar tempfont [strlen (font) + 1];
        strcpy (tempfont, font);
        tempfont [strlen (font) - strlen (tempsize)] = 0;

        sscanf (tempsize, "%d", &nsize);

        if ((event->keyval == GDK_KEY_plus) || (event->keyval == GDK_KEY_KP_Add))
        {
            nsize = nsize + 1;
            sprintf (tempsize, "%d", nsize);

            if (!g_settings_get_boolean (settings, PLUMA_SETTINGS_USE_DEFAULT_FONT) && (nsize < 73))
            {
                gchar *tmp = g_strconcat (tempfont, tempsize, NULL);
                g_settings_set_string (settings, PLUMA_SETTINGS_EDITOR_FONT, tmp);
                g_free (tmp);
            }
        }
        else if ((event->keyval == GDK_KEY_minus) || (event->keyval == GDK_KEY_KP_Subtract))
        {
            nsize = nsize - 1;
            sprintf (tempsize, "%d", nsize);

            if (!g_settings_get_boolean (settings, PLUMA_SETTINGS_USE_DEFAULT_FONT) && (nsize > 5))
            {
                gchar *tmp = g_strconcat (tempfont, tempsize, NULL);
                g_settings_set_string (settings, PLUMA_SETTINGS_EDITOR_FONT, tmp);
                g_free (tmp);
            }
        }
        else if (event->keyval == GDK_KEY_y)
        {
            g_settings_set_boolean (settings, PLUMA_SETTINGS_DISPLAY_LINE_NUMBERS,
                                    !g_settings_get_boolean (settings, PLUMA_SETTINGS_DISPLAY_LINE_NUMBERS));
        }

        if (g_settings_get_boolean (settings, PLUMA_SETTINGS_CTRL_TABS_SWITCH_TABS))
        {
            GtkNotebook *notebook = GTK_NOTEBOOK (_pluma_window_get_notebook (PLUMA_WINDOW (window)));

            int pages = gtk_notebook_get_n_pages (notebook);
            int page_num = gtk_notebook_get_current_page (notebook);

            if (event->keyval == GDK_KEY_ISO_Left_Tab)
            {
                if (page_num != 0)
                    gtk_notebook_prev_page (notebook);
                else
                    gtk_notebook_set_current_page (notebook, (pages - 1));
                handled = TRUE;
            }

            if (event->keyval == GDK_KEY_Tab)
            {
                if (page_num != (pages -1))
                    gtk_notebook_next_page (notebook);
                else
                    gtk_notebook_set_current_page (notebook, 0);
                handled = TRUE;
            }
        }
        g_free (font);
        g_free (tempsize);
    }

    g_object_unref (settings);

    if (grand_parent_class == NULL)
        grand_parent_class = g_type_class_peek_parent (pluma_window_parent_class);

    /* handle focus widget key events */
    if (!handled)
        handled = gtk_window_propagate_key_event (window, event);

    /* handle mnemonics and accelerators */
    if (!handled)
        handled = gtk_window_activate_key (window, event);

    /* Chain up, invokes binding set */
    if (!handled)
        handled = GTK_WIDGET_CLASS (grand_parent_class)->key_press_event (widget, event);

    return handled;
}

static void
pluma_window_tab_removed (PlumaWindow *window,
                          PlumaTab    *tab)
{
    peas_engine_garbage_collect (PEAS_ENGINE (pluma_plugins_engine_get_default ()));
}

static void
pluma_window_class_init (PlumaWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    klass->tab_removed = pluma_window_tab_removed;

    object_class->dispose = pluma_window_dispose;
    object_class->finalize = pluma_window_finalize;
    object_class->get_property = pluma_window_get_property;

    widget_class->window_state_event = pluma_window_window_state_event;
    widget_class->configure_event = pluma_window_configure_event;
    widget_class->key_press_event = pluma_window_key_press_event;

    signals[TAB_ADDED] =
        g_signal_new ("tab_added",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (PlumaWindowClass, tab_added),
                      NULL, NULL, NULL,
                      G_TYPE_NONE,
                      1,
                      PLUMA_TYPE_TAB);
    signals[TAB_REMOVED] =
        g_signal_new ("tab_removed",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (PlumaWindowClass, tab_removed),
                      NULL, NULL, NULL,
                      G_TYPE_NONE,
                      1,
                      PLUMA_TYPE_TAB);
    signals[TABS_REORDERED] =
        g_signal_new ("tabs_reordered",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (PlumaWindowClass, tabs_reordered),
                      NULL, NULL, NULL,
                      G_TYPE_NONE,
                      0);
    signals[ACTIVE_TAB_CHANGED] =
        g_signal_new ("active_tab_changed",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (PlumaWindowClass, active_tab_changed),
                      NULL, NULL, NULL,
                      G_TYPE_NONE,
                      1,
                      PLUMA_TYPE_TAB);
    signals[ACTIVE_TAB_STATE_CHANGED] =
        g_signal_new ("active_tab_state_changed",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (PlumaWindowClass, active_tab_state_changed),
                      NULL, NULL, NULL,
                      G_TYPE_NONE,
                      0);

    g_object_class_install_property (object_class,
                                     PROP_STATE,
                                     g_param_spec_flags ("state",
                                                         "State",
                                                         "The window's state",
                                                         PLUMA_TYPE_WINDOW_STATE,
                                                         PLUMA_WINDOW_STATE_NORMAL,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

static void
apply_toolbar_style (PlumaWindow *window,
                     GtkWidget   *toolbar)
{
    switch (window->priv->toolbar_style)
    {
        case PLUMA_TOOLBAR_SYSTEM:
            pluma_debug_message (DEBUG_WINDOW, "PLUMA: SYSTEM");
            gtk_toolbar_unset_style (GTK_TOOLBAR (toolbar));
            break;

        case PLUMA_TOOLBAR_ICONS:
            pluma_debug_message (DEBUG_WINDOW, "PLUMA: ICONS");
            gtk_toolbar_set_style (GTK_TOOLBAR (toolbar),
                                   GTK_TOOLBAR_ICONS);
            break;

        case PLUMA_TOOLBAR_ICONS_AND_TEXT:
            pluma_debug_message (DEBUG_WINDOW, "PLUMA: ICONS_AND_TEXT");
            gtk_toolbar_set_style (GTK_TOOLBAR (toolbar),
                                   GTK_TOOLBAR_BOTH);
            break;

        case PLUMA_TOOLBAR_ICONS_BOTH_HORIZ:
            pluma_debug_message (DEBUG_WINDOW, "PLUMA: ICONS_BOTH_HORIZ");
            gtk_toolbar_set_style (GTK_TOOLBAR (toolbar),
                                   GTK_TOOLBAR_BOTH_HORIZ);
            break;
    }
}

/* GtkToolButton has never implemented GtkActionable in GTK3 (unlike
 * GtkButton, GtkMenuItem, GtkModelButton...), so there is no direct way to
 * bind a toolbar button to a win.* GAction the way pluma-menus.ui binds
 * menu items. Wiring "clicked" to g_action_group_activate_action() and
 * binding the button's "sensitive" property to the action's "enabled"
 * property is the standard replacement for the old
 * gtk_activatable_set_related_action() pattern. */
static void
toolbar_button_clicked (GtkToolButton *button,
                        gpointer       user_data)
{
    PlumaWindow *window = PLUMA_WINDOW (user_data);
    const gchar *action_name = g_object_get_data (G_OBJECT (button), "pluma-action-name");

    g_action_group_activate_action (G_ACTION_GROUP (window), action_name, NULL);
}

static GtkToolItem *
create_toolbar_button (PlumaWindow *window,
                       const gchar *action_name,
                       const gchar *icon_name,
                       const gchar *label,
                       const gchar *tooltip,
                       gboolean     is_important)
{
    GtkWidget *icon = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
    GtkToolItem *item = gtk_tool_button_new (icon, label);
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (window), action_name);

    gtk_tool_item_set_is_important (item, is_important);
    if (tooltip != NULL)
        gtk_tool_item_set_tooltip_text (item, tooltip);

    g_object_set_data_full (G_OBJECT (item), "pluma-action-name",
                            g_strdup (action_name), g_free);
    g_signal_connect (item, "clicked", G_CALLBACK (toolbar_button_clicked), window);

    if (action != NULL)
        g_object_bind_property (action, "enabled", item, "sensitive",
                                G_BINDING_SYNC_CREATE);

    return item;
}

/* Toolbar, statusbar and the three side panels are fully native GActions
 * (see create_modern_document_action_mirrors()); these helpers keep their
 * state/enabled flag in sync with the actual widget visibility. The action
 * may not exist yet when these run during window construction (mirroring
 * is deferred until the window is attached to the GtkApplication), so a
 * missing action is a silent no-op: the action is seeded with the correct
 * state from the widget when it is created. */
static void
sync_view_toggle_action_state (PlumaWindow *window,
                               const gchar *action_name,
                               gboolean     active)
{
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (window), action_name);
    GVariant *state;

    if (action == NULL)
        return;

    state = g_action_get_state (G_ACTION (action));
    if (g_variant_get_boolean (state) != active)
        g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (active));
    g_variant_unref (state);
}

static void
sync_view_toggle_action_enabled (PlumaWindow *window,
                                 const gchar *action_name,
                                 gboolean     enabled)
{
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (window), action_name);

    if (action != NULL)
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static gboolean
get_view_toggle_action_state (PlumaWindow *window,
                              const gchar *action_name)
{
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (window), action_name);
    GVariant *state;
    gboolean active;

    if (action == NULL)
        return FALSE;

    state = g_action_get_state (G_ACTION (action));
    active = g_variant_get_boolean (state);
    g_variant_unref (state);
    return active;
}

/* win.* actions that only make sense while at least one tab is open (most
 * of Edit/Search/Documents). Used by set_sensitivity_according_to_window_state()
 * to bulk enable/disable them, replacing the old GtkActionGroup-wide
 * sensitivity toggle. */
static const gchar * const document_action_names[] = {
    "save", "save-as", "save-all", "revert", "print-preview", "print",
    "close-all", "close-tabs-left", "close-tabs-right", "close-other-tabs",
    "previous-document", "next-document", "move-to-new-window",
    "undo", "redo", "cut", "copy", "paste", "delete", "select-all",
    "zoom-in", "zoom-out", "zoom-reset",
    "uppercase", "lowercase", "invert-case", "title-case",
    "toggle-line-comment", "toggle-block-comment",
    "find", "find-in-files", "find-next", "find-previous", "replace",
    "clear-highlight", "goto-line", "incremental-search",
    NULL
};

/* create_window_actions() is deferred until the window is attached to a
 * GtkApplication (see the "notify::application" dance there), so win.*
 * actions do not exist yet during the earliest part of window construction.
 * Every per-action sensitivity update below goes through this helper so a
 * lookup miss during that window is a silent no-op instead of a
 * g_simple_action_set_enabled() CRITICAL on a NULL/non-GSimpleAction. */
static void
set_action_enabled (PlumaWindow *window,
                    const gchar *name,
                    gboolean     enabled)
{
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (window), name);

    if (action != NULL)
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
set_actions_enabled (PlumaWindow          *window,
                     const gchar * const  *names,
                     gboolean              enabled)
{
    gsize i;

    for (i = 0; names[i] != NULL; i++)
    {
        GAction *action = g_action_map_lookup_action (G_ACTION_MAP (window), names[i]);

        if (action != NULL)
            g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
    }
}

/* GSimpleAction does not toggle its own boolean state automatically on
 * activation (that only happens for specialized action implementations
 * such as GSettings-backed actions) -- it requires an explicit "activate"
 * handler to flip the state. Without this, activating the action from an
 * accelerator (e.g. F9) does nothing, even though clicking the menu item
 * still works because GTK toggles GMenu check-items via change-state
 * directly. Shared by show-toolbar/show-statusbar/show-side-pane/
 * show-bottom-pane/show-right-pane. */
static void
toggle_action_activated (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
    GVariant *state = g_action_get_state (G_ACTION (action));

    g_action_change_state (G_ACTION (action), g_variant_new_boolean (!g_variant_get_boolean (state)));
    g_variant_unref (state);
}

/* Returns TRUE if toolbar is visible */
static gboolean
set_toolbar_style (PlumaWindow *window,
                   PlumaWindow *origin)
{
    gboolean visible;
    PlumaToolbarSetting style;

    if (origin == NULL)
        visible = g_settings_get_boolean (window->priv->editor_settings,
                                          PLUMA_SETTINGS_TOOLBAR_VISIBLE);
    else
        visible = gtk_widget_get_visible (origin->priv->toolbar);

    /* Set visibility */
    if (visible)
        gtk_widget_show (window->priv->toolbar);
    else
        gtk_widget_hide (window->priv->toolbar);

    sync_view_toggle_action_state (window, "show-toolbar", visible);

    /* Set style */
    if (origin == NULL)
    {
        PlumaSettings *settings;

        settings = _pluma_settings_get_singleton ();
        style = pluma_settings_get_toolbar_style (settings);
    }
    else
    {
        style = origin->priv->toolbar_style;
    }

    window->priv->toolbar_style = style;

    apply_toolbar_style (window, window->priv->toolbar);

    return visible;
}

static void
update_next_prev_doc_sensitivity (PlumaWindow *window,
                                  PlumaTab    *tab)
{
    gint         tab_number;
    GtkNotebook *notebook;

    pluma_debug (DEBUG_WINDOW);

    notebook = GTK_NOTEBOOK (_pluma_window_get_notebook (window));

    tab_number = gtk_notebook_page_num (notebook, GTK_WIDGET (tab));
    g_return_if_fail (tab_number >= 0);

        set_action_enabled (window, "previous-document", tab_number != 0);

        set_action_enabled (window, "next-document",
                              tab_number < gtk_notebook_get_n_pages (notebook) - 1);
}

static void
update_next_prev_doc_sensitivity_per_window (PlumaWindow *window)
{
    PlumaTab  *tab;

    pluma_debug (DEBUG_WINDOW);

    tab = pluma_window_get_active_tab (window);
    if (tab != NULL)
    {
        update_next_prev_doc_sensitivity (window, tab);

        return;
    }

        set_action_enabled (window, "previous-document", FALSE);

        set_action_enabled (window, "next-document", FALSE);

}

static void
received_clipboard_contents (GtkClipboard     *clipboard,
                             GtkSelectionData *selection_data,
                             PlumaWindow      *window)
{
    gboolean sens;

    /* getting clipboard contents is async, so we need to
     * get the current tab and its state */

    if (window->priv->active_tab != NULL)
    {
        PlumaTabState state;
        gboolean state_normal;

        state = pluma_tab_get_state (window->priv->active_tab);
        state_normal = (state == PLUMA_TAB_STATE_NORMAL);

        sens = state_normal &&
               gtk_selection_data_targets_include_text (selection_data);
    }
    else
    {
        sens = FALSE;
    }

        set_action_enabled (window, "paste", sens);

    g_object_unref (window);
}

static void
set_paste_sensitivity_according_to_clipboard (PlumaWindow  *window,
                                              GtkClipboard *clipboard)
{
    GdkDisplay *display;

    display = gtk_clipboard_get_display (clipboard);

    if (gdk_display_supports_selection_notification (display))
    {
        gtk_clipboard_request_contents (clipboard,
                                        gdk_atom_intern_static_string ("TARGETS"),
                                        (GtkClipboardReceivedFunc) received_clipboard_contents,
                                        g_object_ref (window));
    }
    else
    {
        GAction *action;

        action = g_action_map_lookup_action (G_ACTION_MAP (window), "paste");

        /* XFIXES extension not availbale, make
         * Paste always sensitive */
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), TRUE);
    }
}

static void
set_sensitivity_according_to_tab (PlumaWindow *window,
                                  PlumaTab    *tab)
{
    PlumaDocument *doc;
    PlumaView     *view;
    GAction     *action;
    gboolean       b;
    gboolean       state_normal;
    gboolean       editable;
    PlumaTabState  state;
    GtkClipboard  *clipboard;
    PlumaLockdownMask lockdown;
    gboolean       enable_syntax_highlighting;

    g_return_if_fail (PLUMA_TAB (tab));

    pluma_debug (DEBUG_WINDOW);

    enable_syntax_highlighting = g_settings_get_boolean (window->priv->editor_settings,
                                                         PLUMA_SETTINGS_SYNTAX_HIGHLIGHTING);

    lockdown = pluma_app_get_lockdown (pluma_app_get_default ());

    state = pluma_tab_get_state (tab);
    state_normal = (state == PLUMA_TAB_STATE_NORMAL);

    view = pluma_tab_get_view (tab);
    editable = gtk_text_view_get_editable (GTK_TEXT_VIEW (view));

    doc = PLUMA_DOCUMENT (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));

    /*
     * Large-file tabs don't have a real GtkSourceBuffer backing them (see
     * pluma-large-file-view.h), so most of the actions below -- which
     * assume syntax highlighting, GtkSourceView-based undo/redo, find &
     * replace, printing, etc. -- simply don't apply. Give them a
     * deliberately reduced profile and skip the rest of this function.
     * "doc" here is the real-but-hidden placeholder document, whose
     * modified flag is kept in sync with the actual PlumaLargeFileView
     * (see large_file_view_modified_notify() in pluma-tab.c), so this
     * check is accurate.
     */
    if (pluma_tab_is_large_file (tab))
    {
        set_action_enabled (window, "save",
                             gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (doc)) &&
                             !(lockdown & PLUMA_LOCKDOWN_SAVE_TO_DISK));
        set_action_enabled (window, "save-as", !(lockdown & PLUMA_LOCKDOWN_SAVE_TO_DISK));
        set_action_enabled (window, "revert", FALSE);
        set_action_enabled (window, "print-preview", FALSE);
        set_action_enabled (window, "print", FALSE);
        set_action_enabled (window, "close", state != PLUMA_TAB_STATE_CLOSING);
        set_action_enabled (window, "undo", FALSE);
        set_action_enabled (window, "redo", FALSE);
        set_action_enabled (window, "cut", FALSE);
        set_action_enabled (window, "copy", FALSE);
        set_action_enabled (window, "paste", FALSE);
        set_action_enabled (window, "delete", FALSE);
        set_action_enabled (window, "find", FALSE);
        set_action_enabled (window, "incremental-search", FALSE);
        set_action_enabled (window, "replace", FALSE);
        set_action_enabled (window, "find-next", FALSE);
        set_action_enabled (window, "find-previous", FALSE);
        set_action_enabled (window, "clear-highlight", FALSE);
        set_action_enabled (window, "goto-line", FALSE);
        set_action_enabled (window, "highlight-mode", FALSE);

        update_next_prev_doc_sensitivity (window, tab);
        return;
    }

    clipboard = gtk_widget_get_clipboard (GTK_WIDGET (window),
                                          GDK_SELECTION_CLIPBOARD);

    action = g_action_map_lookup_action (G_ACTION_MAP (window), "save");

    if (state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) {
        gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (doc), TRUE);
    }

    g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                              (state_normal ||
                              (state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) ||
                              (state == PLUMA_TAB_STATE_SHOWING_PRINT_PREVIEW)) &&
                              !pluma_document_get_readonly (doc) &&
                              !(lockdown & PLUMA_LOCKDOWN_SAVE_TO_DISK) &&
                              (cansave) &&
                              (editable));

        set_action_enabled (window, "save-as",
                              (state_normal ||
                              (state == PLUMA_TAB_STATE_SAVING_ERROR) ||
                              (state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) ||
                              (state == PLUMA_TAB_STATE_SHOWING_PRINT_PREVIEW)) &&
                              !(lockdown & PLUMA_LOCKDOWN_SAVE_TO_DISK));

        set_action_enabled (window, "revert",
                              (state_normal ||
                              (state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION)) &&
                              !pluma_document_is_untitled (doc));

        set_action_enabled (window, "print-preview",
                              state_normal &&
                              !(lockdown & PLUMA_LOCKDOWN_PRINTING));

        set_action_enabled (window, "print",
                              (state_normal ||
                              (state == PLUMA_TAB_STATE_SHOWING_PRINT_PREVIEW)) &&
                              !(lockdown & PLUMA_LOCKDOWN_PRINTING));

        set_action_enabled (window, "close",
                              (state != PLUMA_TAB_STATE_CLOSING) &&
                              (state != PLUMA_TAB_STATE_SAVING) &&
                              (state != PLUMA_TAB_STATE_SHOWING_PRINT_PREVIEW) &&
                              (state != PLUMA_TAB_STATE_PRINTING) &&
                              (state != PLUMA_TAB_STATE_PRINT_PREVIEWING) &&
                              (state != PLUMA_TAB_STATE_SAVING_ERROR));

        set_action_enabled (window, "undo",
                              state_normal &&
                              gtk_source_buffer_can_undo (GTK_SOURCE_BUFFER (doc)));

        set_action_enabled (window, "redo",
                              state_normal &&
                              gtk_source_buffer_can_redo (GTK_SOURCE_BUFFER (doc)));

        set_action_enabled (window, "cut",
                              state_normal &&
                              editable &&
                              gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (doc)));

        set_action_enabled (window, "copy",
                              (state_normal ||
                               state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) &&
                              gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (doc)));

    action = g_action_map_lookup_action (G_ACTION_MAP (window), "paste");
    if (state_normal && editable)
    {
        set_paste_sensitivity_according_to_clipboard (window, clipboard);
    }
    else
    {
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), FALSE);
    }

        set_action_enabled (window, "delete",
                              state_normal &&
                              editable &&
                              gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (doc)));

        set_action_enabled (window, "find",
                              (state_normal ||
                               state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION));

        set_action_enabled (window, "incremental-search",
                              (state_normal ||
                              state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION));

        set_action_enabled (window, "replace",
                              state_normal &&
                              editable);

    b = pluma_document_get_can_search_again (doc);
        set_action_enabled (window, "find-next",
                              (state_normal ||
                              state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) && b);

        set_action_enabled (window, "find-previous",
                              (state_normal ||
                              state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) && b);

        set_action_enabled (window, "clear-highlight",
                              (state_normal ||
                              state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) && b);

        set_action_enabled (window, "goto-line",
                              (state_normal ||
                              state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION));

        set_action_enabled (window, "highlight-mode",
                              (state != PLUMA_TAB_STATE_CLOSING) &&
                              enable_syntax_highlighting);

    update_next_prev_doc_sensitivity (window, tab);

    peas_extension_set_call (window->priv->extensions, "update_state");
}

/* The "win.highlight-mode" GMenu items are radio-style: GTK activates them
 * by calling g_action_change_state() directly (see modern_highlight_mode_changed()),
 * so no per-language callback or GtkAction is needed here any more. */
static void
create_languages_menu (PlumaWindow *window)
{
    GSList *languages, *l;
    GMenu *modern_section;
    GHashTable *section_menus;
    GMenuItem *item;

    pluma_debug (DEBUG_WINDOW);

    if (window->priv->modern_menu_builder == NULL)
        return;

    {
        GObject *object = gtk_builder_get_object (window->priv->modern_menu_builder,
                                                   "highlight-language-section");
        if (!G_IS_MENU (object))
            return;
        modern_section = G_MENU (object);
    }
    g_menu_remove_all (modern_section);

    /* Translators: "Plain Text" means that no highlight mode is selected in
     * the "View->Highlight Mode" submenu and so syntax highlighting is
     * disabled */
    item = g_menu_item_new (_("Plain Text"), NULL);
    g_menu_item_set_action_and_target (item, "win.highlight-mode", "s", LANGUAGE_NONE);
    g_menu_append_item (modern_section, item);
    g_object_unref (item);

    /* Group languages into their section (e.g. "Sources", "Markup") as
     * submenus of modern_section, same grouping the old GtkUIManager-based
     * menu used. */
    section_menus = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    languages = pluma_language_manager_list_languages_sorted (pluma_get_language_manager (), FALSE);

    for (l = languages; l != NULL; l = l->next)
    {
        GtkSourceLanguage *language = l->data;
        const gchar *section = gtk_source_language_get_section (language);
        GMenu *section_menu = g_hash_table_lookup (section_menus, section);
        gchar *escaped_name;

        if (section_menu == NULL)
        {
            gchar *escaped_section = pluma_utils_escape_underscores (section, -1);
            GMenuItem *submenu_item;

            section_menu = g_menu_new ();
            g_hash_table_insert (section_menus, g_strdup (section), g_object_ref (section_menu));

            submenu_item = g_menu_item_new_submenu (escaped_section, G_MENU_MODEL (section_menu));
            g_menu_append_item (modern_section, submenu_item);
            g_object_unref (submenu_item);
            g_free (escaped_section);
        }

        escaped_name = pluma_utils_escape_underscores (gtk_source_language_get_name (language), -1);
        item = g_menu_item_new (escaped_name, NULL);
        g_menu_item_set_action_and_target (item, "win.highlight-mode", "s",
                                           gtk_source_language_get_id (language));
        g_menu_append_item (section_menu, item);
        g_object_unref (item);
        g_free (escaped_name);
    }

    g_slist_free (languages);
    g_hash_table_unref (section_menus);
}

static void
update_languages_menu (PlumaWindow *window)
{
    PlumaDocument *doc;
    GtkSourceLanguage *lang;
    const gchar *lang_id;
    GAction *action;

    doc = pluma_window_get_active_document (window);
    if (doc == NULL)
        return;

    lang = pluma_document_get_language (doc);
    lang_id = lang != NULL ? gtk_source_language_get_id (lang) : LANGUAGE_NONE;

    action = g_action_map_lookup_action (G_ACTION_MAP (window), "highlight-mode");
    if (action != NULL)
        g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_string (lang_id));
}

void
_pluma_recent_add (PlumaWindow *window,
                   const gchar *uri,
                   const gchar *mime)
{
    GtkRecentManager *recent_manager;
    GtkRecentData recent_data;

    static gchar *groups[2] = {
        "pluma",
        NULL
    };

    recent_manager =  gtk_recent_manager_get_default ();

    recent_data.display_name = NULL;
    recent_data.description = NULL;
    recent_data.mime_type = (gchar *) mime;
    recent_data.app_name = (gchar *) g_get_application_name ();
    recent_data.app_exec = g_strjoin (" ", g_get_prgname (), "%u", NULL);
    recent_data.groups = groups;
    recent_data.is_private = FALSE;

    gtk_recent_manager_add_full (recent_manager,
                                 uri,
                                 &recent_data);

    g_free (recent_data.app_exec);
}

void
_pluma_recent_remove (PlumaWindow *window,
                      const gchar *uri)
{
    GtkRecentManager *recent_manager;

    recent_manager =  gtk_recent_manager_get_default ();

    gtk_recent_manager_remove_item (recent_manager, uri, NULL);
}

static void
open_recent_file (const gchar *uri,
                  PlumaWindow *window)
{
    GSList *uris = NULL;

    uris = g_slist_prepend (uris, (gpointer) uri);

    if (pluma_commands_load_uris (window, uris, NULL, 0) != 1)
    {
        _pluma_recent_remove (window, uri);
    }

    g_slist_free (uris);
}

static void
recent_chooser_item_activated (GtkRecentChooser *chooser,
                               PlumaWindow      *window)
{
    gchar *uri;

    uri = gtk_recent_chooser_get_current_uri (chooser);

    open_recent_file (uri, window);

    g_free (uri);
}

static gint
sort_recents_mru (GtkRecentInfo *a, GtkRecentInfo *b)
{
    return (gtk_recent_info_get_modified (b) - gtk_recent_info_get_modified (a));
}

static void    update_recent_files_menu (PlumaWindow *window);

static void
recent_manager_changed (GtkRecentManager *manager,
                        PlumaWindow      *window)
{
    /* regenerate the menu when the model changes */
    update_recent_files_menu (window);
}

/*
 * Rebuild the "win.open-recent"-backed recent-files section of the File
 * menu (pluma-menus.ui's "recent-files-section").
 */
static void
update_recent_files_menu (PlumaWindow *window)
{
    PlumaWindowPrivate *p = window->priv;
    GtkRecentManager *recent_manager;
    guint max_recents;
    GList *l, *items;
    GList *filtered_items = NULL;
    gint i;

    pluma_debug (DEBUG_WINDOW);

    if (p->modern_recent_section == NULL)
        return;

    max_recents = g_settings_get_uint (window->priv->editor_settings, PLUMA_SETTINGS_MAX_RECENTS);

    g_menu_remove_all (p->modern_recent_section);

    recent_manager =  gtk_recent_manager_get_default ();
    items = gtk_recent_manager_get_items (recent_manager);

    /* filter */
    for (l = items; l != NULL; l = l->next)
    {
        GtkRecentInfo *info = l->data;

        if (!gtk_recent_info_has_group (info, "pluma"))
            continue;

        filtered_items = g_list_prepend (filtered_items, info);
    }

    /* sort */
    filtered_items = g_list_sort (filtered_items,
                                  (GCompareFunc) sort_recents_mru);

    i = 0;
    for (l = filtered_items; l != NULL; l = l->next)
    {
        const gchar *display_name;
        gchar *escaped;
        gchar *label;
        GMenuItem *menu_item;
        GtkRecentInfo *info = l->data;

        /* clamp */
        if (i >= max_recents)
            break;

        i++;

        display_name = gtk_recent_info_get_display_name (info);
        escaped = pluma_utils_escape_underscores (display_name, -1);
        if (i >= 10)
            label = g_strdup_printf ("%d.  %s", i, escaped);
        else
            label = g_strdup_printf ("_%d.  %s", i, escaped);
        g_free (escaped);

        menu_item = g_menu_item_new (label, NULL);
        g_menu_item_set_action_and_target (menu_item, "win.open-recent", "s",
                                           gtk_recent_info_get_uri (info));
        g_menu_append_item (p->modern_recent_section, menu_item);
        g_object_unref (menu_item);

        g_free (label);
    }

    g_list_free (filtered_items);

    g_list_free_full (items, (GDestroyNotify) gtk_recent_info_unref);
}

static void
set_non_homogeneus (GtkWidget *widget, gpointer data)
{
    gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (widget), FALSE);
}

static void
toolbar_visibility_changed (GtkWidget   *toolbar,
                            PlumaWindow *window)
{
    gboolean visible;

    visible = gtk_widget_get_visible (toolbar);

    g_settings_set_boolean (window->priv->editor_settings,
                            PLUMA_SETTINGS_TOOLBAR_VISIBLE, visible);

    sync_view_toggle_action_state (window, "show-toolbar", visible);
}

typedef struct
{
    const gchar *action_name;  /* NULL means "insert a separator here" */
    const gchar *icon_name;
    const gchar *label;        /* shown when the toolbar style includes text; NULL for icon-only */
    const gchar *tooltip;
    gboolean     is_important; /* shown with its label even in icon-only mode when space is tight */
} PlumaToolbarItem;

/* Mirrors the old "/ToolBar" (and "/FullscreenToolBar", which is the same
 * buttons plus Leave Fullscreen — see fullscreen_controls_build()) from
 * pluma-ui.xml. The "Open" button is special-cased in
 * setup_toolbar_open_button() since it carries a recent-files dropdown. */
static const PlumaToolbarItem pluma_toolbar_items[] =
{
    { "new",   "document-new-symbolic",  NULL,          N_("Create a new document"), FALSE },
    { "save",  "document-save-symbolic", N_("Save"),    N_("Save the current file"), TRUE },
    { NULL,    NULL,                     NULL,          NULL,                        FALSE },
    { "print", "document-print-symbolic", N_("Print"),  N_("Print the current page"), FALSE },
    { NULL,    NULL,                     NULL,          NULL,                        FALSE },
    { "undo",  "edit-undo-symbolic",     NULL,          N_("Undo the last action"),  TRUE },
    { "redo",  "edit-redo-symbolic",     NULL,          N_("Redo the last undone action"), FALSE },
    { NULL,    NULL,                     NULL,          NULL,                        FALSE },
    { "cut",   "edit-cut-symbolic",      NULL,          N_("Cut the selection"),     FALSE },
    { "copy",  "edit-copy-symbolic",     NULL,          N_("Copy the selection"),    FALSE },
    { "paste", "edit-paste-symbolic",    NULL,          N_("Paste the clipboard"),   FALSE },
    { NULL,    NULL,                     NULL,          NULL,                        FALSE },
    { "find",    "edit-find-symbolic",         N_("Find"),    N_("Search for text"), FALSE },
    { "replace", "edit-find-replace-symbolic", N_("Replace"), N_("Search for and replace text"), FALSE },
};

static GtkWidget *
build_toolbar (PlumaWindow *window)
{
    GtkWidget *toolbar = gtk_toolbar_new ();
    gsize i;

    gtk_style_context_add_class (gtk_widget_get_style_context (toolbar),
                                 GTK_STYLE_CLASS_PRIMARY_TOOLBAR);

    for (i = 0; i < G_N_ELEMENTS (pluma_toolbar_items); i++)
    {
        const PlumaToolbarItem *item = &pluma_toolbar_items[i];
        GtkToolItem *tool_item;

        if (item->action_name == NULL)
            tool_item = gtk_separator_tool_item_new ();
        else
            tool_item = create_toolbar_button (window, item->action_name, item->icon_name,
                                               item->label != NULL ? _(item->label) : NULL,
                                               item->tooltip != NULL ? _(item->tooltip) : NULL,
                                               item->is_important);

        gtk_toolbar_insert (GTK_TOOLBAR (toolbar), tool_item, -1);
    }

    gtk_widget_show_all (toolbar);
    return toolbar;
}

static GtkWidget *
setup_toolbar_open_button (PlumaWindow *window,
                           GtkWidget *toolbar)
{
    GtkRecentManager *recent_manager;
    GtkRecentFilter *filter;
    GtkWidget *toolbar_recent_menu;
    GtkToolItem *open_button;
    GAction *action;
    guint max_recents;

    recent_manager = gtk_recent_manager_get_default ();

    max_recents = g_settings_get_uint (window->priv->editor_settings, PLUMA_SETTINGS_MAX_RECENTS);

    /* recent files menu tool button */
    toolbar_recent_menu = gtk_recent_chooser_menu_new_for_manager (recent_manager);

    gtk_recent_chooser_set_local_only (GTK_RECENT_CHOOSER (toolbar_recent_menu),
                                       FALSE);
    gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (toolbar_recent_menu),
                                      GTK_RECENT_SORT_MRU);
    gtk_recent_chooser_set_limit (GTK_RECENT_CHOOSER (toolbar_recent_menu),
                                  max_recents);

    filter = gtk_recent_filter_new ();
    gtk_recent_filter_add_group (filter, "pluma");
    gtk_recent_chooser_set_filter (GTK_RECENT_CHOOSER (toolbar_recent_menu),
                                   filter);

    g_signal_connect (toolbar_recent_menu,
                      "item_activated",
                      G_CALLBACK (recent_chooser_item_activated),
                      window);

    /* add the custom Open button to the toolbar */
    open_button = gtk_menu_tool_button_new (gtk_image_new_from_icon_name ("document-open",
                                            GTK_ICON_SIZE_MENU),
                                            _("Open a file"));

    gtk_menu_tool_button_set_menu (GTK_MENU_TOOL_BUTTON (open_button),
                                   toolbar_recent_menu);

    gtk_menu_tool_button_set_arrow_tooltip_text (GTK_MENU_TOOL_BUTTON (open_button),
                                                 _("Open a recently used file"));

    gtk_tool_item_set_is_important (open_button, TRUE);
    g_object_set_data_full (G_OBJECT (open_button), "pluma-action-name",
                            g_strdup ("open"), g_free);
    g_signal_connect (open_button, "clicked",
                      G_CALLBACK (toolbar_button_clicked), window);

    action = g_action_map_lookup_action (G_ACTION_MAP (window), "open");
    if (action != NULL)
        g_object_bind_property (action, "enabled", open_button, "sensitive",
                                G_BINDING_SYNC_CREATE);

    gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
                        open_button,
                        1);

    return toolbar_recent_menu;
}

typedef struct
{
    GtkLabel parent_instance;
} PlumaChordAccelLabel;

typedef struct
{
    GtkLabelClass parent_class;
} PlumaChordAccelLabelClass;

/* Purely private to this file (used only to render the "Ctrl+K, Ctrl+O"
 * chord hint below); G_DEFINE_TYPE's generated _get_type() has external
 * linkage but nothing outside this file needs to call it, so declare a
 * prototype here rather than exposing it through a header. */
GType pluma_chord_accel_label_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (PlumaChordAccelLabel, pluma_chord_accel_label, GTK_TYPE_LABEL)

static void
pluma_chord_accel_label_class_init (PlumaChordAccelLabelClass *klass)
{
    gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "accelerator");
}

static void
pluma_chord_accel_label_init (PlumaChordAccelLabel *label)
{
}

/* Depth-first search for a GtkMenuItem bound (directly, or via a submenu)
 * to @action_name (a "win.<name>"-style detailed action name) within the
 * GtkMenuBar built by gtk_menu_bar_new_from_model(). Used to reach into the
 * modern menu for widgets that pluma-menus.ui cannot express on its own,
 * such as the chord-accelerator hint below. Returns NULL if not found. */
static GtkWidget *
find_menu_item_for_action (GtkWidget *widget, const gchar *action_name)
{
    if (GTK_IS_ACTIONABLE (widget) &&
        g_strcmp0 (gtk_actionable_get_action_name (GTK_ACTIONABLE (widget)), action_name) == 0)
        return widget;

    if (GTK_IS_MENU_ITEM (widget))
    {
        GtkWidget *submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (widget));

        return submenu != NULL ? find_menu_item_for_action (submenu, action_name) : NULL;
    }
    else if (GTK_IS_CONTAINER (widget))
    {
        GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
        GList *l;
        GtkWidget *found = NULL;

        for (l = children; l != NULL && found == NULL; l = l->next)
            found = find_menu_item_for_action (l->data, action_name);
        g_list_free (children);
        return found;
    }

    return NULL;
}

static void
set_chord_menu_label (GtkWidget *item)
{
    GtkWidget *box;
    GtkWidget *label;
    GtkWidget *shortcut;
    GtkWidget *child;

    if (item == NULL)
        return;

    child = gtk_bin_get_child (GTK_BIN (item));
    if (child != NULL)
        gtk_container_remove (GTK_CONTAINER (item), child);

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 24);
    label = gtk_label_new_with_mnemonic (_("Open _Folder..."));
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), item);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    shortcut = g_object_new (pluma_chord_accel_label_get_type (),
                             "label", "Ctrl+K, Ctrl+O",
                             NULL);
    gtk_widget_set_halign (shortcut, GTK_ALIGN_END);

    gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (box), shortcut, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (item), box);
    gtk_widget_show_all (box);
}

static void
modern_open_recent_activated (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
    open_recent_file (g_variant_get_string (parameter, NULL), PLUMA_WINDOW (user_data));
}

/* Stateful (not just activatable) so the Documents menu can show a radio
 * mark next to the current tab, the same way "highlight-mode" does for
 * languages. notebook_switch_page() keeps the state in sync when the active
 * tab changes through means other than this menu (tab click, shortcuts). */
static void
modern_switch_document_activated (GSimpleAction *action,
                                  GVariant      *value,
                                  gpointer       user_data)
{
    PlumaWindow *window = PLUMA_WINDOW (user_data);
    gint page = g_variant_get_int32 (value);

    if (page >= 0 && page < gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook)))
        gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook), page);
    g_simple_action_set_state (action, value);
}

static void
modern_highlight_mode_changed (GSimpleAction *action,
                               GVariant      *value,
                               gpointer       user_data)
{
    PlumaWindow *window = PLUMA_WINDOW (user_data);
    PlumaDocument *document = pluma_window_get_active_document (window);
    const gchar *language_id = g_variant_get_string (value, NULL);
    GtkSourceLanguage *language = NULL;

    if (document == NULL)
        return;
    if (g_strcmp0 (language_id, LANGUAGE_NONE) != 0)
    {
        language = gtk_source_language_manager_get_language (pluma_get_language_manager (),
                                                              language_id);
        if (language == NULL)
            return;
    }
    pluma_document_set_language (document, language);
    g_simple_action_set_state (action, value);
}

static void create_window_actions (PlumaWindow *window);

static void
on_window_application_notify (GObject *object, GParamSpec *pspec, gpointer user_data)
{
    PlumaWindow *window = PLUMA_WINDOW (object);

    if (gtk_window_get_application (GTK_WINDOW (window)) != NULL)
    {
        create_window_actions (window);
        g_signal_handlers_disconnect_by_func (window, on_window_application_notify, user_data);
    }
}

static void
create_window_actions (PlumaWindow *window)
{
	GtkApplication *application = gtk_window_get_application (GTK_WINDOW (window));
	GSimpleAction *parameterized_action;
	GSimpleAction *toggle_action;
	gsize i;

	if (application == NULL)
		return;

	g_action_map_add_action_entries (G_ACTION_MAP (window),
	                                 pluma_window_action_entries,
	                                 G_N_ELEMENTS (pluma_window_action_entries),
	                                 window);

	for (i = 0; i < G_N_ELEMENTS (pluma_window_action_accels); i++)
	{
		gchar *detailed_name = g_strconcat ("win.", pluma_window_action_accels[i].action_name, NULL);

		gtk_application_set_accels_for_action (application, detailed_name,
		                                       pluma_window_action_accels[i].accelerators);
		g_free (detailed_name);
	}

	/* Toolbar/statusbar/side/bottom/right-pane visibility: native stateful
	 * GActions, not mirrored from any legacy GtkToggleAction. Initial state
	 * is read straight from the widgets, since by the time this runs
	 * (deferred to notify::application) construction has already set their
	 * real visibility from GSettings; sync_view_toggle_action_state() and
	 * friends (pluma-window.c) keep them in sync afterwards, and
	 * _pluma_cmd_view_show_* (pluma-commands-view.c) implement the actions
	 * themselves via the "change-state" signal. */
	toggle_action = g_simple_action_new_stateful ("show-toolbar", NULL,
		g_variant_new_boolean (gtk_widget_get_visible (window->priv->toolbar)));
	g_signal_connect (toggle_action, "activate",
	                  G_CALLBACK (toggle_action_activated), NULL);
	g_signal_connect (toggle_action, "change-state",
	                  G_CALLBACK (_pluma_cmd_view_show_toolbar), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (toggle_action));
	g_object_unref (toggle_action);

	toggle_action = g_simple_action_new_stateful ("show-statusbar", NULL,
		g_variant_new_boolean (gtk_widget_get_visible (window->priv->statusbar)));
	g_signal_connect (toggle_action, "activate",
	                  G_CALLBACK (toggle_action_activated), NULL);
	g_signal_connect (toggle_action, "change-state",
	                  G_CALLBACK (_pluma_cmd_view_show_statusbar), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (toggle_action));
	g_object_unref (toggle_action);

	/* No gtk_application_set_accels_for_action() for the F9 family: see the
	 * comment in pluma_window_key_press_event() -- their modifiers are
	 * matched explicitly there instead. */
	toggle_action = g_simple_action_new_stateful ("show-side-pane", NULL,
		g_variant_new_boolean (gtk_widget_get_visible (window->priv->side_panel)));
	g_signal_connect (toggle_action, "activate",
	                  G_CALLBACK (toggle_action_activated), NULL);
	g_signal_connect (toggle_action, "change-state",
	                  G_CALLBACK (_pluma_cmd_view_show_side_pane), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (toggle_action));
	g_object_unref (toggle_action);

	toggle_action = g_simple_action_new_stateful ("show-bottom-pane", NULL,
		g_variant_new_boolean (gtk_widget_get_visible (window->priv->bottom_panel)));
	g_simple_action_set_enabled (toggle_action,
		pluma_panel_get_n_items (PLUMA_PANEL (window->priv->bottom_panel)) > 0);
	g_signal_connect (toggle_action, "activate",
	                  G_CALLBACK (toggle_action_activated), NULL);
	g_signal_connect (toggle_action, "change-state",
	                  G_CALLBACK (_pluma_cmd_view_show_bottom_pane), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (toggle_action));
	g_object_unref (toggle_action);

	toggle_action = g_simple_action_new_stateful ("show-right-pane", NULL,
		g_variant_new_boolean (gtk_widget_get_visible (window->priv->right_panel)));
	g_signal_connect (toggle_action, "activate",
	                  G_CALLBACK (toggle_action_activated), NULL);
	g_signal_connect (toggle_action, "change-state",
	                  G_CALLBACK (_pluma_cmd_view_show_right_pane), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (toggle_action));
	g_object_unref (toggle_action);

	parameterized_action = g_simple_action_new ("open-recent", G_VARIANT_TYPE_STRING);
	g_signal_connect (parameterized_action, "activate",
	                  G_CALLBACK (modern_open_recent_activated), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (parameterized_action));
	g_object_unref (parameterized_action);

	parameterized_action = g_simple_action_new_stateful ("switch-document",
	                                                     G_VARIANT_TYPE_INT32,
	                                                     g_variant_new_int32 (0));
	g_signal_connect (parameterized_action, "change-state",
	                  G_CALLBACK (modern_switch_document_activated), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (parameterized_action));
	g_object_unref (parameterized_action);

	/* Alt+1..Alt+0 switch directly to the first ten tabs, regardless of
	 * which document is currently open at that position (matches the old
	 * per-position GtkRadioAction accelerators in update_documents_list_menu()). */
	for (i = 0; i < 10; i++)
	{
		gchar *detailed_name = g_strdup_printf ("win.switch-document(%" G_GSIZE_FORMAT ")", i);
		gchar *accel = g_strdup_printf ("<Alt>%" G_GSIZE_FORMAT, (i + 1) % 10);
		const gchar *accels[] = { accel, NULL };

		gtk_application_set_accels_for_action (application, detailed_name, accels);
		g_free (detailed_name);
		g_free (accel);
	}

	parameterized_action = g_simple_action_new_stateful ("highlight-mode",
	                                                     G_VARIANT_TYPE_STRING,
	                                                     g_variant_new_string (LANGUAGE_NONE));
	g_signal_connect (parameterized_action, "change-state",
	                  G_CALLBACK (modern_highlight_mode_changed), window);
	g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (parameterized_action));
	g_object_unref (parameterized_action);
}

/* Finds the index, among @root's top-level items, of the submenu that
 * (directly or via a nested section) links to @section. Used to locate
 * the "Tools" top-level menu item without hard-coding its position in
 * pluma-menus.ui. Returns -1 if not found. */
static gint
find_menubar_index_for_section (GMenuModel *root, GMenu *section)
{
    gint n, i;

    if (root == NULL || section == NULL)
        return -1;

    n = g_menu_model_get_n_items (root);
    for (i = 0; i < n; i++)
    {
        GMenuModel *submenu;
        gint m, j;

        submenu = g_menu_model_get_item_link (root, i, G_MENU_LINK_SUBMENU);
        if (submenu == NULL)
            continue;

        m = g_menu_model_get_n_items (submenu);
        for (j = 0; j < m; j++)
        {
            GMenuModel *inner_section;

            inner_section = g_menu_model_get_item_link (submenu, j, G_MENU_LINK_SECTION);
            if (inner_section == G_MENU_MODEL (section))
            {
                g_object_unref (inner_section);
                g_object_unref (submenu);
                return i;
            }
            g_clear_object (&inner_section);
        }
        g_object_unref (submenu);
    }

    return -1;
}

static void
update_tools_menu_visibility (PlumaWindow *window)
{
    gboolean has_items;

    if (window->priv->tools_menu_item == NULL ||
        window->priv->modern_tools_section == NULL)
        return;

    has_items = g_menu_model_get_n_items (G_MENU_MODEL (window->priv->modern_tools_section)) > 0;
    gtk_widget_set_visible (window->priv->tools_menu_item, has_items);
}

static void
modern_tools_section_items_changed (GMenuModel *model,
                                    gint        position,
                                    gint        removed,
                                    gint        added,
                                    PlumaWindow *window)
{
    update_tools_menu_visibility (window);
}

static void
create_menu_bar_and_toolbar (PlumaWindow *window,
                             GtkWidget   *main_box)
{
    GtkRecentManager *recent_manager;
    GError *error = NULL;
    GObject *menu_model;

    pluma_debug (DEBUG_WINDOW);

    /* win.* actions (pluma-actions.h) back both the GMenuModel menubar
     * below and the manually-built toolbar. gtk_window_get_application()
     * is still NULL here: this runs during PlumaWindow construction
     * (pluma_app_create_window()), and the window is only attached to the
     * GtkApplication afterwards, via gtk_application_add_window() in
     * pluma_application_activate(). Create the actions once that actually
     * happens instead of silently no-op'ing forever. */
    if (gtk_window_get_application (GTK_WINDOW (window)) != NULL)
        create_window_actions (window);
    else
        g_signal_connect (window, "notify::application",
                          G_CALLBACK (on_window_application_notify), NULL);

    window->priv->modern_menu_builder = gtk_builder_new ();
    if (!gtk_builder_add_from_file (window->priv->modern_menu_builder,
                                    PLUMA_DATADIR "/ui/pluma-menus.ui",
                                    &error))
    {
        /* pluma-menus.ui is not optional any more: it is the only menu
         * definition left, so a missing/broken file is a packaging error,
         * not something to silently fall back from. */
        g_error ("Could not load %s: %s", PLUMA_DATADIR "/ui/pluma-menus.ui", error->message);
    }

    {
        GObject *recent = gtk_builder_get_object (window->priv->modern_menu_builder,
                                                   "recent-files-section");
        GObject *documents = gtk_builder_get_object (window->priv->modern_menu_builder,
                                                      "documents-list-section");
        GObject *tools = gtk_builder_get_object (window->priv->modern_menu_builder,
                                                  "plugin-tools-section");

        if (G_IS_MENU (recent))
            window->priv->modern_recent_section = G_MENU (recent);
        if (G_IS_MENU (documents))
            window->priv->modern_documents_section = G_MENU (documents);
        if (G_IS_MENU (tools))
            window->priv->modern_tools_section = G_MENU (tools);
    }

    recent_manager = gtk_recent_manager_get_default ();
    window->priv->recents_handler_id = g_signal_connect (recent_manager,
                                                         "changed",
                                                         G_CALLBACK (recent_manager_changed),
                                                         window);
    update_recent_files_menu (window);
    create_languages_menu (window);

    menu_model = gtk_builder_get_object (window->priv->modern_menu_builder, "pluma-menubar");
    if (!G_IS_MENU_MODEL (menu_model))
        g_error ("pluma-menus.ui has no top-level \"pluma-menubar\" menu");

    window->priv->menubar = gtk_menu_bar_new_from_model (G_MENU_MODEL (menu_model));

    /* Unlike the old gtk_ui_manager_get_widget(), gtk_menu_bar_new_from_model()
     * does not pre-show the widgets it creates, and main_box below is only
     * shown with a plain gtk_widget_show (not _show_all). */
    gtk_widget_show_all (window->priv->menubar);

    set_chord_menu_label (find_menu_item_for_action (window->priv->menubar, "win.open-folder"));

    /* The Tools top-level menu only ever holds plugin-contributed items
     * (see "plugin-tools-section" in pluma-menus.ui). GMenuModel has no
     * built-in way to hide a submenu when its contents are empty, so track
     * the section ourselves and hide the menubar entry while no plugin has
     * added anything to it. */
    if (window->priv->modern_tools_section != NULL)
    {
        gint tools_index = find_menubar_index_for_section (G_MENU_MODEL (menu_model),
                                                            window->priv->modern_tools_section);

        if (tools_index >= 0)
        {
            GList *children = gtk_container_get_children (GTK_CONTAINER (window->priv->menubar));

            window->priv->tools_menu_item = g_list_nth_data (children, tools_index);
            g_list_free (children);

            g_signal_connect (window->priv->modern_tools_section, "items-changed",
                              G_CALLBACK (modern_tools_section_items_changed), window);
            update_tools_menu_visibility (window);
        }
    }

    gtk_box_pack_start (GTK_BOX (main_box),
                        window->priv->menubar,
                        FALSE,
                        FALSE,
                        0);

    window->priv->toolbar = build_toolbar (window);
    gtk_box_pack_start (GTK_BOX (main_box),
                        window->priv->toolbar,
                        FALSE,
                        FALSE,
                        0);

    set_toolbar_style (window, NULL);

    window->priv->toolbar_recent_menu = setup_toolbar_open_button (window, window->priv->toolbar);

    gtk_container_foreach (GTK_CONTAINER (window->priv->toolbar),
                           (GtkCallback)set_non_homogeneus,
                           NULL);

    g_signal_connect_after (window->priv->toolbar, "show",
                            G_CALLBACK (toolbar_visibility_changed),
                            window);
    g_signal_connect_after (window->priv->toolbar, "hide",
                            G_CALLBACK (toolbar_visibility_changed),
                            window);
}

/*
 * Rebuild the "win.switch-document"-backed tab list of the Documents menu
 * (pluma-menus.ui's "documents-list-section"). Keyboard access to the
 * first ten tabs (Alt+1..Alt+0) is wired once, in create_window_actions(),
 * as detailed-name accelerators ("win.switch-document(0)", etc.) rather
 * than here, since it depends on tab position, not on this list being
 * rebuilt.
 */
static void
update_documents_list_menu (PlumaWindow *window)
{
    PlumaWindowPrivate *p = window->priv;
    gint n, i;

    pluma_debug (DEBUG_WINDOW);

    if (p->modern_documents_section == NULL)
        return;

    g_menu_remove_all (p->modern_documents_section);

    n = gtk_notebook_get_n_pages (GTK_NOTEBOOK (p->notebook));

    for (i = 0; i < n; i++)
    {
        GtkWidget *tab = gtk_notebook_get_nth_page (GTK_NOTEBOOK (p->notebook), i);
        gchar *tab_name = _pluma_tab_get_name (PLUMA_TAB (tab));
        gchar *name = pluma_utils_escape_underscores (tab_name, -1);
        GMenuItem *menu_item = g_menu_item_new (name, NULL);

        g_menu_item_set_action_and_target (menu_item, "win.switch-document", "i", i);
        g_menu_append_item (p->modern_documents_section, menu_item);
        g_object_unref (menu_item);

        g_free (tab_name);
        g_free (name);
    }
}

/* Returns TRUE if status bar is visible */
static gboolean
set_statusbar_style (PlumaWindow *window,
                     PlumaWindow *origin)
{
    gboolean visible;

    if (origin == NULL)
        visible = g_settings_get_boolean (window->priv->editor_settings,
                                          PLUMA_SETTINGS_STATUSBAR_VISIBLE);
    else
        visible = gtk_widget_get_visible (origin->priv->statusbar);

    if (visible)
        gtk_widget_show (window->priv->statusbar);
    else
        gtk_widget_hide (window->priv->statusbar);

    sync_view_toggle_action_state (window, "show-statusbar", visible);

    return visible;
}

static void
statusbar_visibility_changed (GtkWidget   *statusbar,
                              PlumaWindow *window)
{
    gboolean visible;

    visible = gtk_widget_get_visible (statusbar);

    g_settings_set_boolean (window->priv->editor_settings,
                            PLUMA_SETTINGS_STATUSBAR_VISIBLE, visible);

    sync_view_toggle_action_state (window, "show-statusbar", visible);
}

static void
tab_width_combo_changed (PlumaStatusComboBox *combo,
                         GtkMenuItem     *item,
                         PlumaWindow     *window)
{
    PlumaView *view;
    guint width_data = 0;

    view = pluma_window_get_active_view (window);

    if (!view)
        return;

    width_data = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), TAB_WIDTH_DATA));

    if (width_data == 0)
        return;

    g_signal_handler_block (view, window->priv->tab_width_id);
    gtk_source_view_set_tab_width (GTK_SOURCE_VIEW (view), width_data);
    g_signal_handler_unblock (view, window->priv->tab_width_id);
}

static void
use_spaces_toggled (GtkCheckMenuItem *item,
                    PlumaWindow      *window)
{
    PlumaView *view;

    view = pluma_window_get_active_view (window);

    g_signal_handler_block (view, window->priv->spaces_instead_of_tabs_id);
    gtk_source_view_set_insert_spaces_instead_of_tabs (GTK_SOURCE_VIEW (view),
                                                       gtk_check_menu_item_get_active (item));
    g_signal_handler_unblock (view, window->priv->spaces_instead_of_tabs_id);
}

static void
language_combo_changed (PlumaStatusComboBox *combo,
                        GtkMenuItem     *item,
                        PlumaWindow     *window)
{
    PlumaDocument *doc;
    GtkSourceLanguage *language;

    doc = pluma_window_get_active_document (window);

    if (!doc)
        return;

    language = GTK_SOURCE_LANGUAGE (g_object_get_data (G_OBJECT (item), LANGUAGE_DATA));

    g_signal_handler_block (doc, window->priv->language_changed_id);
    pluma_document_set_language (doc, language);
    g_signal_handler_unblock (doc, window->priv->language_changed_id);
}

typedef struct
{
    const gchar *label;
    guint width;
} TabWidthDefinition;

static void
fill_tab_width_combo (PlumaWindow *window)
{
    static TabWidthDefinition defs[] = {
        {"2", 2},
        {"4", 4},
        {"8", 8},
        {"", 0}, /* custom size */
        {NULL, 0}
    };

    PlumaStatusComboBox *combo = PLUMA_STATUS_COMBO_BOX (window->priv->tab_width_combo);
    guint i = 0;
    GtkWidget *item;

    while (defs[i].label != NULL)
    {
        item = gtk_menu_item_new_with_label (defs[i].label);
        g_object_set_data (G_OBJECT (item), TAB_WIDTH_DATA, GINT_TO_POINTER (defs[i].width));

        pluma_status_combo_box_add_item (combo,
                                         GTK_MENU_ITEM (item),
                                         defs[i].label);

        if (defs[i].width != 0)
            gtk_widget_show (item);

        ++i;
    }

    item = gtk_separator_menu_item_new ();
    pluma_status_combo_box_add_item (combo, GTK_MENU_ITEM (item), NULL);
    gtk_widget_show (item);

    item = gtk_check_menu_item_new_with_label (_("Use Spaces"));
    pluma_status_combo_box_add_item (combo, GTK_MENU_ITEM (item), NULL);
    gtk_widget_show (item);

    g_signal_connect (item,
                      "toggled",
                      G_CALLBACK (use_spaces_toggled),
                      window);
}

static void
fill_language_combo (PlumaWindow *window)
{
    GtkSourceLanguageManager *manager;
    GSList *languages;
    GSList *item;
    GtkWidget *menu_item;
    const gchar *name;

    manager = pluma_get_language_manager ();
    languages = pluma_language_manager_list_languages_sorted (manager, FALSE);

    name = _("Plain Text");
    menu_item = gtk_menu_item_new_with_label (name);
    gtk_widget_show (menu_item);

    g_object_set_data (G_OBJECT (menu_item), LANGUAGE_DATA, NULL);
    pluma_status_combo_box_add_item (PLUMA_STATUS_COMBO_BOX (window->priv->language_combo),
                                     GTK_MENU_ITEM (menu_item),
                                     name);

    for (item = languages; item; item = item->next)
    {
        GtkSourceLanguage *lang = GTK_SOURCE_LANGUAGE (item->data);

        name = gtk_source_language_get_name (lang);
        menu_item = gtk_menu_item_new_with_label (name);
        gtk_widget_show (menu_item);

        g_object_set_data_full (G_OBJECT (menu_item),
                                LANGUAGE_DATA,
                                g_object_ref (lang),
                                (GDestroyNotify)g_object_unref);

        pluma_status_combo_box_add_item (PLUMA_STATUS_COMBO_BOX (window->priv->language_combo),
                                         GTK_MENU_ITEM (menu_item),
                                         name);
    }

    g_slist_free (languages);
}

static void
create_statusbar (PlumaWindow *window,
                  GtkWidget   *main_box)
{
    pluma_debug (DEBUG_WINDOW);

    window->priv->statusbar = pluma_statusbar_new ();

    window->priv->generic_message_cid = gtk_statusbar_get_context_id
        (GTK_STATUSBAR (window->priv->statusbar), "generic_message");
    window->priv->tip_message_cid = gtk_statusbar_get_context_id
        (GTK_STATUSBAR (window->priv->statusbar), "tip_message");

    gtk_box_pack_end (GTK_BOX (main_box),
                      window->priv->statusbar,
                      FALSE,
                      TRUE,
                      0);

    window->priv->tab_width_combo = pluma_status_combo_box_new (_("Tab Width"));
    gtk_widget_show (window->priv->tab_width_combo);
    gtk_box_pack_end (GTK_BOX (window->priv->statusbar),
                      window->priv->tab_width_combo,
                      FALSE,
                      TRUE,
                      0);

    fill_tab_width_combo (window);

    g_signal_connect (window->priv->tab_width_combo, "changed",
                      G_CALLBACK (tab_width_combo_changed),
                      window);

    window->priv->language_combo = pluma_status_combo_box_new (NULL);
    gtk_widget_show (window->priv->language_combo);
    gtk_box_pack_end (GTK_BOX (window->priv->statusbar),
                      window->priv->language_combo,
                      FALSE,
                      TRUE,
                      0);

    fill_language_combo (window);

    g_signal_connect (window->priv->language_combo, "changed",
                      G_CALLBACK (language_combo_changed),
                      window);

    g_signal_connect_after (window->priv->statusbar, "show",
                            G_CALLBACK (statusbar_visibility_changed),
                            window);
    g_signal_connect_after (window->priv->statusbar, "hide",
                            G_CALLBACK (statusbar_visibility_changed),
                            window);

    set_statusbar_style (window, NULL);
}

static PlumaWindow *
clone_window (PlumaWindow *origin)
{
    PlumaWindow *window;
    GdkScreen *screen;
    PlumaApp  *app;
    gint panel_page;

    pluma_debug (DEBUG_WINDOW);

    app = pluma_app_get_default ();

    screen = gtk_window_get_screen (GTK_WINDOW (origin));
    window = pluma_app_create_window (app, screen);

    gtk_window_set_default_size (GTK_WINDOW (window),
                                 origin->priv->width,
                                 origin->priv->height);

    if ((origin->priv->window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0)
        gtk_window_maximize (GTK_WINDOW (window));
    else
        gtk_window_unmaximize (GTK_WINDOW (window));

    if ((origin->priv->window_state & GDK_WINDOW_STATE_STICKY ) != 0)
        gtk_window_stick (GTK_WINDOW (window));
    else
        gtk_window_unstick (GTK_WINDOW (window));

    /* set the panes size, the paned position will be set when
     * they are mapped */
    window->priv->side_panel_size = origin->priv->side_panel_size;
    window->priv->bottom_panel_size = origin->priv->bottom_panel_size;
    window->priv->right_panel_size = origin->priv->right_panel_size;

    panel_page = _pluma_panel_get_active_item_id (PLUMA_PANEL (origin->priv->side_panel));
    _pluma_panel_set_active_item_by_id (PLUMA_PANEL (window->priv->side_panel), panel_page);

    panel_page = _pluma_panel_get_active_item_id (PLUMA_PANEL (origin->priv->bottom_panel));
    _pluma_panel_set_active_item_by_id (PLUMA_PANEL (window->priv->bottom_panel), panel_page);

    panel_page = _pluma_panel_get_active_item_id (PLUMA_PANEL (origin->priv->right_panel));
    _pluma_panel_set_active_item_by_id (PLUMA_PANEL (window->priv->right_panel), panel_page);

    if (gtk_widget_get_visible (origin->priv->side_panel))
        gtk_widget_show (window->priv->side_panel);
    else
        gtk_widget_hide (window->priv->side_panel);

    if (gtk_widget_get_visible (origin->priv->bottom_panel))
        gtk_widget_show (window->priv->bottom_panel);
    else
        gtk_widget_hide (window->priv->bottom_panel);

    if (gtk_widget_get_visible (origin->priv->right_panel))
        gtk_widget_show (window->priv->right_panel);
    else
        gtk_widget_hide (window->priv->right_panel);

    set_statusbar_style (window, origin);
    set_toolbar_style (window, origin);

    return window;
}

static void
update_cursor_position_statusbar (GtkTextBuffer *buffer,
                                  PlumaWindow   *window)
{
    gint row, col;
    GtkTextIter iter;
    PlumaView *view;

    pluma_debug (DEBUG_WINDOW);

     if (buffer != GTK_TEXT_BUFFER (pluma_window_get_active_document (window)))
         return;

     view = pluma_window_get_active_view (window);

    gtk_text_buffer_get_iter_at_mark (buffer,
                                      &iter,
                                      gtk_text_buffer_get_insert (buffer));

    row = gtk_text_iter_get_line (&iter);

    col = gtk_source_view_get_visual_column (GTK_SOURCE_VIEW(view), &iter);

    pluma_statusbar_set_cursor_position (PLUMA_STATUSBAR (window->priv->statusbar),
                                         row + 1,
                                         col + 1);
}

static void
update_overwrite_mode_statusbar (GtkTextView *view,
                                 PlumaWindow *window)
{
    if (view != GTK_TEXT_VIEW (pluma_window_get_active_view (window)))
        return;

    /* Note that we have to use !gtk_text_view_get_overwrite since we
       are in the in the signal handler of "toggle overwrite" that is
       G_SIGNAL_RUN_LAST
    */
    pluma_statusbar_set_overwrite (PLUMA_STATUSBAR (window->priv->statusbar),
                                   !gtk_text_view_get_overwrite (view));
}

#define MAX_TITLE_LENGTH 100

static void
set_title (PlumaWindow *window)
{
    PlumaDocument *doc = NULL;
    gchar *name;
    gchar *dirname = NULL;
    gchar *title = NULL;
    gint len;

    if (window->priv->active_tab == NULL)
    {
        gtk_window_set_title (GTK_WINDOW (window), "Pluma");
        return;
    }

    doc = pluma_tab_get_document (window->priv->active_tab);
    g_return_if_fail (doc != NULL);

    name = pluma_document_get_short_name_for_display (doc);

    len = g_utf8_strlen (name, -1);

    /* if the name is awfully long, truncate it and be done with it,
     * otherwise also show the directory (ellipsized if needed)
     */
    if (len > MAX_TITLE_LENGTH)
    {
        gchar *tmp;

        tmp = pluma_utils_str_middle_truncate (name, MAX_TITLE_LENGTH);
        g_free (name);
        name = tmp;
    }
    else
    {
        GFile *file;

        file = pluma_document_get_location (doc);
        if (file != NULL)
        {
            gchar *str;

            str = pluma_utils_location_get_dirname_for_display (file);
            g_object_unref (file);

            /* use the remaining space for the dir, but use a min of 20 chars
             * so that we do not end up with a dirname like "(a...b)".
             * This means that in the worst case when the filename is long 99
             * we have a title long 99 + 20, but I think it's a rare enough
             * case to be acceptable. It's justa darn title afterall :)
             */
            dirname = pluma_utils_str_middle_truncate (str, MAX (20, MAX_TITLE_LENGTH - len));
            g_free (str);
        }
    }

    if (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (doc)))
    {
        gchar *tmp_name;

        tmp_name = g_strdup_printf ("*%s", name);
        g_free (name);

        name = tmp_name;
        cansave = TRUE;
    }
    else
        cansave = FALSE;

    if (pluma_document_get_readonly (doc))
    {
        if (dirname != NULL)
            title = g_strdup_printf ("%s [%s] (%s) - Pluma",
                                     name,
                                     _("Read-Only"),
                                     dirname);
        else
            title = g_strdup_printf ("%s [%s] - Pluma",
                                     name,
                                     _("Read-Only"));
    }
    else
    {
        if (dirname != NULL)
            title = g_strdup_printf ("%s (%s) - Pluma",
                                     name,
                                     dirname);
        else
            title = g_strdup_printf ("%s - Pluma",
                                     name);
    }

        set_action_enabled (window, "save", cansave);

    gtk_window_set_title (GTK_WINDOW (window), title);

    g_free (dirname);
    g_free (name);
    g_free (title);
}

#undef MAX_TITLE_LENGTH

static void
set_tab_width_item_blocked (PlumaWindow *window,
                            GtkMenuItem *item)
{
    g_signal_handlers_block_by_func (window->priv->tab_width_combo,
                                     tab_width_combo_changed,
                                     window);

    pluma_status_combo_box_set_item (PLUMA_STATUS_COMBO_BOX (window->priv->tab_width_combo),
                                     item);

    g_signal_handlers_unblock_by_func (window->priv->tab_width_combo,
                                       tab_width_combo_changed,
                                       window);
}

static void
spaces_instead_of_tabs_changed (GObject     *object,
                                GParamSpec  *pspec,
                                PlumaWindow *window)
{
    PlumaView *view = PLUMA_VIEW (object);
    gboolean active = gtk_source_view_get_insert_spaces_instead_of_tabs (GTK_SOURCE_VIEW (view));
    GList *children = pluma_status_combo_box_get_items (PLUMA_STATUS_COMBO_BOX (window->priv->tab_width_combo));
    GtkCheckMenuItem *item;

    item = GTK_CHECK_MENU_ITEM (g_list_last (children)->data);

    gtk_check_menu_item_set_active (item, active);

    g_list_free (children);
}

static void
tab_width_changed (GObject     *object,
                   GParamSpec  *pspec,
                   PlumaWindow *window)
{
    GList *items;
    GList *item;
    PlumaStatusComboBox *combo = PLUMA_STATUS_COMBO_BOX (window->priv->tab_width_combo);
    guint new_tab_width;
    gboolean found = FALSE;

    items = pluma_status_combo_box_get_items (combo);

    new_tab_width = gtk_source_view_get_tab_width (GTK_SOURCE_VIEW (object));

    for (item = items; item; item = item->next)
    {
        guint tab_width = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item->data), TAB_WIDTH_DATA));

        if (tab_width == new_tab_width)
        {
            set_tab_width_item_blocked (window, GTK_MENU_ITEM (item->data));
            found = TRUE;
        }

        if (GTK_IS_SEPARATOR_MENU_ITEM (item->next->data))
        {
            if (!found)
            {
                /* Set for the last item the custom thing */
                gchar *text;

                text = g_strdup_printf ("%u", new_tab_width);
                pluma_status_combo_box_set_item_text (combo,
                                                      GTK_MENU_ITEM (item->data),
                                                      text);

                gtk_label_set_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item->data))),
                                    text);

                set_tab_width_item_blocked (window, GTK_MENU_ITEM (item->data));
                gtk_widget_show (GTK_WIDGET (item->data));
            }
            else
            {
                gtk_widget_hide (GTK_WIDGET (item->data));
            }

            break;
        }
    }

    g_list_free (items);
}

static void
language_changed (GObject     *object,
                  GParamSpec  *pspec,
                  PlumaWindow *window)
{
    GList *items;
    GList *item;
    PlumaStatusComboBox *combo = PLUMA_STATUS_COMBO_BOX (window->priv->language_combo);
    GtkSourceLanguage *new_language;
    const gchar *new_id;

    items = pluma_status_combo_box_get_items (combo);

    new_language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (object));

    if (new_language)
        new_id = gtk_source_language_get_id (new_language);
    else
        new_id = NULL;

    for (item = items; item; item = item->next)
    {
        GtkSourceLanguage *lang = g_object_get_data (G_OBJECT (item->data), LANGUAGE_DATA);

        if ((new_id == NULL && lang == NULL) ||
            (new_id != NULL && lang != NULL && strcmp (gtk_source_language_get_id (lang), new_id) == 0))
        {
            g_signal_handlers_block_by_func (window->priv->language_combo,
                                             language_combo_changed,
                                             window);

            pluma_status_combo_box_set_item (PLUMA_STATUS_COMBO_BOX (window->priv->language_combo),
                                             GTK_MENU_ITEM (item->data));

            g_signal_handlers_unblock_by_func (window->priv->language_combo,
                                               language_combo_changed,
                                               window);
        }
    }

    g_list_free (items);
}

static void
notebook_switch_page (GtkNotebook     *book,
                      GtkWidget       *pg,
                      gint             page_num,
                      PlumaWindow     *window)
{
    PlumaView *view;
    PlumaTab *tab;
    GAction *action;

    /* CHECK: I don't know why but it seems notebook_switch_page is called
    two times every time the user change the active tab */

    tab = PLUMA_TAB (gtk_notebook_get_nth_page (book, page_num));
    if (tab == window->priv->active_tab)
        return;

    if (window->priv->active_tab)
    {
#if GLIB_CHECK_VERSION(2,62,0)
        PlumaView *active_tab_view;

        active_tab_view = pluma_tab_get_view (window->priv->active_tab);
        g_clear_signal_handler (&window->priv->tab_width_id,
                                active_tab_view);
        g_clear_signal_handler (&window->priv->spaces_instead_of_tabs_id,
                                active_tab_view);
#else
        if (window->priv->tab_width_id)
        {
            g_signal_handler_disconnect (pluma_tab_get_view (window->priv->active_tab),
                                         window->priv->tab_width_id);

            window->priv->tab_width_id = 0;
        }

        if (window->priv->spaces_instead_of_tabs_id)
        {
            g_signal_handler_disconnect (pluma_tab_get_view (window->priv->active_tab),
                                         window->priv->spaces_instead_of_tabs_id);

            window->priv->spaces_instead_of_tabs_id = 0;
        }
#endif
    }

    /* set the active tab */
    window->priv->active_tab = tab;

    set_title (window);
    set_sensitivity_according_to_tab (window, tab);

    /* activate the right item in the documents menu */
    action = g_action_map_lookup_action (G_ACTION_MAP (window), "switch-document");
    if (action != NULL)
        g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_int32 (page_num));

    /* update the syntax menu */
    update_languages_menu (window);

    view = pluma_tab_get_view (tab);

    /* sync the statusbar */
    update_cursor_position_statusbar (GTK_TEXT_BUFFER (pluma_tab_get_document (tab)),
                                      window);
    pluma_statusbar_set_overwrite (PLUMA_STATUSBAR (window->priv->statusbar),
                                   gtk_text_view_get_overwrite (GTK_TEXT_VIEW (view)));

    gtk_widget_show (window->priv->tab_width_combo);
    gtk_widget_show (window->priv->language_combo);

    window->priv->tab_width_id = g_signal_connect (view,
                                                   "notify::tab-width",
                                                   G_CALLBACK (tab_width_changed),
                                                   window);
    window->priv->spaces_instead_of_tabs_id = g_signal_connect (view,
                                                                "notify::insert-spaces-instead-of-tabs",
                                                                G_CALLBACK (spaces_instead_of_tabs_changed),
                                                                window);

    window->priv->language_changed_id = g_signal_connect (pluma_tab_get_document (tab),
                                                          "notify::language",
                                                          G_CALLBACK (language_changed),
                                                          window);

    /* call it for the first time */
    tab_width_changed (G_OBJECT (view), NULL, window);
    spaces_instead_of_tabs_changed (G_OBJECT (view), NULL, window);
    language_changed (G_OBJECT (pluma_tab_get_document (tab)), NULL, window);

    g_signal_emit (G_OBJECT (window),
                   signals[ACTIVE_TAB_CHANGED],
                   0,
                   window->priv->active_tab);
}

static void
set_sensitivity_according_to_window_state (PlumaWindow *window)
{
    PlumaLockdownMask lockdown;
    gboolean saving_session;
    gboolean tabs_enabled;

    lockdown = pluma_app_get_lockdown (pluma_app_get_default ());

    /* We disable File->SaveAll/CloseAll while printing to avoid to have two
       operations (save and print/print preview) that uses the message area at
       the same time (may be we can remove this limitation in the future) */
    /* We disable File->CloseAll if state is saving since saving cannot be
       cancelled (may be we can remove this limitation in the future).
       Quit is an app-scoped action (app.quit, pluma-application.c) rather
       than per-window now, so it is not greyed out here any more;
       _pluma_cmd_file_quit() still refuses to run while saving/printing/
       saving-session, so this only drops a UX nicety, not a safety check. */
        set_action_enabled (window, "close-all",
                              !(window->priv->state & PLUMA_WINDOW_STATE_SAVING) &&
                              !(window->priv->state & PLUMA_WINDOW_STATE_PRINTING));

        set_action_enabled (window, "save-all",
                              !(window->priv->state & PLUMA_WINDOW_STATE_PRINTING) &&
                              !(lockdown & PLUMA_LOCKDOWN_SAVE_TO_DISK));

        set_action_enabled (window, "new",
                              !(window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION));

        set_action_enabled (window, "open",
                              !(window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION));

        set_action_enabled (window, "open-recent",
                              !(window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION));

    pluma_notebook_set_close_buttons_sensitive (PLUMA_NOTEBOOK (window->priv->notebook),
                                                !(window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION));

    pluma_notebook_set_tab_drag_and_drop_enabled (PLUMA_NOTEBOOK (window->priv->notebook),
                                                  !(window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION));

    /* TODO: If we really care, Find could be active when in
     * SAVING_SESSION state */
    saving_session = (window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION) != 0;
    tabs_enabled = saving_session ? FALSE : (window->priv->num_tabs > 0);

    set_actions_enabled (window, document_action_names, tabs_enabled);

        set_action_enabled (window, "close", tabs_enabled);
}

static void
update_tab_autosave (GtkWidget *widget,
                     gpointer   data)
{
    PlumaTab *tab = PLUMA_TAB (widget);
    gboolean *enabled = (gboolean *) data;

    pluma_tab_set_auto_save_enabled (tab, *enabled);
}

void
_pluma_window_set_lockdown (PlumaWindow       *window,
                            PlumaLockdownMask  lockdown)
{
    PlumaTab *tab;
    gboolean autosave;

    /* start/stop autosave in each existing tab */
    autosave = g_settings_get_boolean (window->priv->editor_settings, PLUMA_SETTINGS_AUTO_SAVE);
    gtk_container_foreach (GTK_CONTAINER (window->priv->notebook),
                           update_tab_autosave,
                           &autosave);

    /* update menues wrt the current active tab */
    tab = pluma_window_get_active_tab (window);

    set_sensitivity_according_to_tab (window, tab);

        set_action_enabled (window, "save-all",
                              !(window->priv->state & PLUMA_WINDOW_STATE_PRINTING) &&
                              !(lockdown & PLUMA_LOCKDOWN_SAVE_TO_DISK));

}

static void
analyze_tab_state (PlumaTab    *tab,
                   PlumaWindow *window)
{
    PlumaTabState ts;

    ts = pluma_tab_get_state (tab);

    switch (ts)
    {
        case PLUMA_TAB_STATE_LOADING:
        case PLUMA_TAB_STATE_REVERTING:
            window->priv->state |= PLUMA_WINDOW_STATE_LOADING;
            break;

        case PLUMA_TAB_STATE_SAVING:
            window->priv->state |= PLUMA_WINDOW_STATE_SAVING;
            break;

        case PLUMA_TAB_STATE_PRINTING:
        case PLUMA_TAB_STATE_PRINT_PREVIEWING:
            window->priv->state |= PLUMA_WINDOW_STATE_PRINTING;
            break;

        case PLUMA_TAB_STATE_LOADING_ERROR:
        case PLUMA_TAB_STATE_REVERTING_ERROR:
        case PLUMA_TAB_STATE_SAVING_ERROR:
        case PLUMA_TAB_STATE_GENERIC_ERROR:
            window->priv->state |= PLUMA_WINDOW_STATE_ERROR;
            ++window->priv->num_tabs_with_error;
        default:
            /* NOP */
            break;
    }
}

static void
update_window_state (PlumaWindow *window)
{
    PlumaWindowState old_ws;
    gint old_num_of_errors;

    pluma_debug_message (DEBUG_WINDOW, "Old state: %x", window->priv->state);

    old_ws = window->priv->state;
    old_num_of_errors = window->priv->num_tabs_with_error;

    window->priv->state = old_ws & PLUMA_WINDOW_STATE_SAVING_SESSION;

    window->priv->num_tabs_with_error = 0;

    gtk_container_foreach (GTK_CONTAINER (window->priv->notebook),
                           (GtkCallback)analyze_tab_state,
                           window);

    pluma_debug_message (DEBUG_WINDOW, "New state: %x", window->priv->state);

    if (old_ws != window->priv->state)
    {
        set_sensitivity_according_to_window_state (window);

        pluma_statusbar_set_window_state (PLUMA_STATUSBAR (window->priv->statusbar),
                                          window->priv->state,
                                          window->priv->num_tabs_with_error);

        g_object_notify (G_OBJECT (window), "state");
    }
    else if (old_num_of_errors != window->priv->num_tabs_with_error)
    {
        pluma_statusbar_set_window_state (PLUMA_STATUSBAR (window->priv->statusbar),
                                          window->priv->state,
                                          window->priv->num_tabs_with_error);
    }
}

static void
sync_state (PlumaTab    *tab,
            GParamSpec  *pspec,
            PlumaWindow *window)
{
    pluma_debug (DEBUG_WINDOW);

    update_window_state (window);

    if (tab != window->priv->active_tab)
        return;

    set_sensitivity_according_to_tab (window, tab);

    g_signal_emit (G_OBJECT (window), signals[ACTIVE_TAB_STATE_CHANGED], 0);
}

static void
sync_name (PlumaTab    *tab,
           GParamSpec  *pspec,
           PlumaWindow *window)
{
    PlumaDocument *doc;

    if (tab == window->priv->active_tab)
    {
        set_title (window);

        doc = pluma_tab_get_document (tab);
                set_action_enabled (window, "revert", !pluma_document_is_untitled (doc));
    }

    /* The documents-list GMenu items carry a static label set when they were
     * created; rebuild the section so a rename (e.g. after Save As) is
     * reflected there too. */
    update_documents_list_menu (window);

    peas_extension_set_call (window->priv->extensions, "update_state");
}

static PlumaWindow *
get_drop_window (GtkWidget *widget)
{
    GtkWidget *target_window;

    target_window = gtk_widget_get_toplevel (widget);
    g_return_val_if_fail (PLUMA_IS_WINDOW (target_window), NULL);

    if ((PLUMA_WINDOW(target_window)->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION) != 0)
        return NULL;

    return PLUMA_WINDOW (target_window);
}

static void
load_uris_from_drop (PlumaWindow  *window,
                     gchar       **uri_list)
{
    GSList *uris = NULL;
    gint i;

    if (uri_list == NULL)
        return;

    for (i = 0; uri_list[i] != NULL; ++i)
    {
        uris = g_slist_prepend (uris, uri_list[i]);
    }

    uris = g_slist_reverse (uris);
    pluma_commands_load_uris (window,
                              uris,
                              NULL,
                              0);

    g_slist_free (uris);
}

/* Handle drops on the PlumaWindow */
static void
drag_data_received_cb (GtkWidget    *widget,
                       GdkDragContext   *context,
                       gint          x,
                       gint          y,
                       GtkSelectionData *selection_data,
                       guint         info,
                       guint         timestamp,
                       gpointer      data)
{
    PlumaWindow *window;
    gchar **uri_list;

    window = get_drop_window (widget);

    if (window == NULL)
        return;

    if (info == TARGET_URI_LIST)
    {
        uri_list = pluma_utils_drop_get_uris(selection_data);
        load_uris_from_drop (window, uri_list);
        g_strfreev (uri_list);
    }
}

/* Handle drops on the PlumaView */
static void
drop_uris_cb (GtkWidget    *widget,
              gchar       **uri_list)
{
    PlumaWindow *window;

    window = get_drop_window (widget);

    if (window == NULL)
        return;

    load_uris_from_drop (window, uri_list);
}

static void
fullscreen_controls_show (PlumaWindow *window)
{
    GdkScreen *screen;
    GdkDisplay *display;
    GdkRectangle fs_rect;
    gint w, h;

    screen = gtk_window_get_screen (GTK_WINDOW (window));
    display = gdk_screen_get_display (screen);

    gdk_monitor_get_geometry (gdk_display_get_monitor_at_window (display,
                              gtk_widget_get_window (GTK_WIDGET (window))),
                              &fs_rect);

    gtk_window_get_size (GTK_WINDOW (window->priv->fullscreen_controls), &w, &h);

    gtk_window_resize (GTK_WINDOW (window->priv->fullscreen_controls),
                       fs_rect.width, h);

    gtk_window_move (GTK_WINDOW (window->priv->fullscreen_controls),
                     fs_rect.x, fs_rect.y - h + 1);

    gtk_widget_show_all (window->priv->fullscreen_controls);
}

static gboolean
run_fullscreen_animation (gpointer data)
{
    PlumaWindow *window = PLUMA_WINDOW (data);
    GdkScreen *screen;
    GdkDisplay *display;
    GdkRectangle fs_rect;
    gint x, y;

    screen = gtk_window_get_screen (GTK_WINDOW (window));
    display = gdk_screen_get_display (screen);

    gdk_monitor_get_geometry (gdk_display_get_monitor_at_window (display,
                              gtk_widget_get_window (GTK_WIDGET (window))),
                              &fs_rect);

    gtk_window_get_position (GTK_WINDOW (window->priv->fullscreen_controls),
                             &x,
                             &y);

    if (window->priv->fullscreen_animation_enter)
    {
        if (y == fs_rect.y)
        {
            window->priv->fullscreen_animation_timeout_id = 0;
            return FALSE;
        }
        else
        {
            gtk_window_move (GTK_WINDOW (window->priv->fullscreen_controls),
                             x, y + 1);
            return TRUE;
        }
    }
    else
    {
        gint w, h;

        gtk_window_get_size (GTK_WINDOW (window->priv->fullscreen_controls),
                             &w, &h);

        if (y == fs_rect.y - h + 1)
        {
            window->priv->fullscreen_animation_timeout_id = 0;
            return FALSE;
        }
        else
        {
            gtk_window_move (GTK_WINDOW (window->priv->fullscreen_controls),
                             x, y - 1);
            return TRUE;
        }
    }
}

static void
show_hide_fullscreen_toolbar (PlumaWindow *window,
                              gboolean     show,
                              gint     height)
{
    GtkSettings *settings;
    gboolean enable_animations;

    settings = gtk_widget_get_settings (GTK_WIDGET (window));
    g_object_get (G_OBJECT (settings),
                  "gtk-enable-animations",
                  &enable_animations,
                  NULL);

    if (enable_animations)
    {
        window->priv->fullscreen_animation_enter = show;

        if (window->priv->fullscreen_animation_timeout_id == 0)
        {
            window->priv->fullscreen_animation_timeout_id =
                g_timeout_add (FULLSCREEN_ANIMATION_SPEED,
                               (GSourceFunc) run_fullscreen_animation,
                               window);
        }
    }
    else
    {
        GdkRectangle fs_rect;
        GdkScreen *screen;
        GdkDisplay *display;

        screen = gtk_window_get_screen (GTK_WINDOW (window));
        display = gdk_screen_get_display (screen);

        gdk_monitor_get_geometry (gdk_display_get_monitor_at_window (display,
                                  gtk_widget_get_window (GTK_WIDGET (window))),
                                  &fs_rect);

        if (show)
            gtk_window_move (GTK_WINDOW (window->priv->fullscreen_controls),
                             fs_rect.x, fs_rect.y);
        else
            gtk_window_move (GTK_WINDOW (window->priv->fullscreen_controls),
                             fs_rect.x, fs_rect.y - height + 1);
    }

}

static gboolean
on_fullscreen_controls_enter_notify_event (GtkWidget    *widget,
                                           GdkEventCrossing *event,
                                           PlumaWindow      *window)
{
    show_hide_fullscreen_toolbar (window, TRUE, 0);

    return FALSE;
}

static gboolean
on_fullscreen_controls_leave_notify_event (GtkWidget    *widget,
                                           GdkEventCrossing *event,
                                           PlumaWindow      *window)
{
    GdkDevice *device;
    gint w, h;
    gint x, y;

    device = gdk_event_get_device ((GdkEvent *)event);

    gtk_window_get_size (GTK_WINDOW (window->priv->fullscreen_controls), &w, &h);
    gdk_device_get_position (device, NULL, &x, &y);

    /* gtk seems to emit leave notify when clicking on tool items,
     * work around it by checking the coordinates
     */
    if (y >= h)
    {
        show_hide_fullscreen_toolbar (window, FALSE, h);
    }

    return FALSE;
}

static void
fullscreen_controls_build (PlumaWindow *window)
{
    PlumaWindowPrivate *priv = window->priv;
    GtkWidget *toolbar;
    GtkToolItem *separator;
    GtkToolItem *leave_button;

    if (priv->fullscreen_controls != NULL)
        return;

    priv->fullscreen_controls = gtk_window_new (GTK_WINDOW_POPUP);

    gtk_window_set_transient_for (GTK_WINDOW (priv->fullscreen_controls),
                                  GTK_WINDOW (&window->window));

    /* popup toolbar: same buttons as the main toolbar (build_toolbar()),
     * plus an expanding separator and a "Leave Fullscreen" button at the
     * far end, matching the old "/FullscreenToolBar" layout. */
    toolbar = build_toolbar (window);
    setup_toolbar_open_button (window, toolbar);

    separator = gtk_separator_tool_item_new ();
    gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (separator), FALSE);
    gtk_tool_item_set_expand (separator, TRUE);
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), separator, -1);

    leave_button = create_toolbar_button (window, "leave-fullscreen", "view-restore-symbolic",
                                          NULL, _("Leave fullscreen mode"), TRUE);
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), leave_button, -1);
    gtk_widget_show_all (toolbar);

    gtk_container_add (GTK_CONTAINER (priv->fullscreen_controls), toolbar);

    gtk_container_foreach (GTK_CONTAINER (toolbar),
                           (GtkCallback)set_non_homogeneus,
                           NULL);

    /* Set the toolbar style */
    gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);

    g_signal_connect (priv->fullscreen_controls, "enter-notify-event",
                      G_CALLBACK (on_fullscreen_controls_enter_notify_event),
                      window);
    g_signal_connect (priv->fullscreen_controls, "leave-notify-event",
                      G_CALLBACK (on_fullscreen_controls_leave_notify_event),
                      window);
}

static void
can_search_again (PlumaDocument *doc,
                  GParamSpec    *pspec,
                  PlumaWindow   *window)
{
    gboolean sensitive;

    if (doc != pluma_window_get_active_document (window))
        return;

    sensitive = pluma_document_get_can_search_again (doc);

        set_action_enabled (window, "find-next", sensitive);

        set_action_enabled (window, "find-previous", sensitive);

        set_action_enabled (window, "clear-highlight", sensitive);
}

static void
can_undo (PlumaDocument *doc,
          GParamSpec    *pspec,
          PlumaWindow   *window)
{
    gboolean sensitive;

    sensitive = gtk_source_buffer_can_undo (GTK_SOURCE_BUFFER (doc));

    if (doc != pluma_window_get_active_document (window))
        return;

    set_action_enabled (window, "undo", sensitive);
}

static void
can_redo (PlumaDocument *doc,
          GParamSpec    *pspec,
          PlumaWindow   *window)
{
    gboolean sensitive;

    sensitive = gtk_source_buffer_can_redo (GTK_SOURCE_BUFFER (doc));

    if (doc != pluma_window_get_active_document (window))
        return;

    set_action_enabled (window, "redo", sensitive);
}

static void
selection_changed (PlumaDocument *doc,
                   GParamSpec    *pspec,
                   PlumaWindow   *window)
{
    PlumaTab *tab;
    PlumaView *view;
    PlumaTabState state;
    gboolean state_normal;
    gboolean editable;

    pluma_debug (DEBUG_WINDOW);

    if (doc != pluma_window_get_active_document (window))
        return;

    tab = pluma_tab_get_from_document (doc);
    state = pluma_tab_get_state (tab);
    state_normal = (state == PLUMA_TAB_STATE_NORMAL);

    view = pluma_tab_get_view (tab);
    editable = gtk_text_view_get_editable (GTK_TEXT_VIEW (view));

        set_action_enabled (window, "cut",
                              state_normal &&
                              editable &&
                              gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (doc)));

        set_action_enabled (window, "copy",
                              (state_normal ||
                              state == PLUMA_TAB_STATE_EXTERNALLY_MODIFIED_NOTIFICATION) &&
                              gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (doc)));

        set_action_enabled (window, "delete",
                              state_normal &&
                              editable &&
                              gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (doc)));

    peas_extension_set_call (window->priv->extensions, "update_state");
}

static void
sync_languages_menu (PlumaDocument *doc,
                     GParamSpec    *pspec,
                     PlumaWindow   *window)
{
    update_languages_menu (window);
    peas_extension_set_call (window->priv->extensions, "update_state");
}

static void
readonly_changed (PlumaDocument *doc,
                  GParamSpec    *pspec,
                  PlumaWindow   *window)
{
    set_sensitivity_according_to_tab (window, window->priv->active_tab);

    sync_name (window->priv->active_tab, NULL, window);

    peas_extension_set_call (window->priv->extensions, "update_state");
}

static void
editable_changed (PlumaView  *view,
                  GParamSpec  *arg1,
                  PlumaWindow *window)
{
    peas_extension_set_call (window->priv->extensions, "update_state");
}

static void
update_sensitivity_according_to_open_tabs (PlumaWindow *window)
{

    /* Set sensitivity */
    set_actions_enabled (window, document_action_names, window->priv->num_tabs != 0);

        set_action_enabled (window, "move-to-new-window", window->priv->num_tabs > 1);

        set_action_enabled (window, "close", window->priv->num_tabs != 0);
}

static void
notebook_tab_added (PlumaNotebook *notebook,
                    PlumaTab      *tab,
                    PlumaWindow   *window)
{
    PlumaView *view;
    PlumaDocument *doc;

    pluma_debug (DEBUG_WINDOW);

    g_return_if_fail ((window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION) == 0);

    ++window->priv->num_tabs;

    update_sensitivity_according_to_open_tabs (window);

    view = pluma_tab_get_view (tab);
    doc = pluma_tab_get_document (tab);

    /* IMPORTANT: remember to disconnect the signal in notebook_tab_removed
     * if a new signal is connected here */

    g_signal_connect (tab,
                      "notify::name",
                      G_CALLBACK (sync_name),
                      window);
    g_signal_connect (tab,
                      "notify::state",
                      G_CALLBACK (sync_state),
                      window);

    g_signal_connect (doc,
                      "cursor-moved",
                      G_CALLBACK (update_cursor_position_statusbar),
                      window);
    g_signal_connect (doc,
                      "notify::can-search-again",
                      G_CALLBACK (can_search_again),
                      window);
    g_signal_connect (doc,
                      "notify::can-undo",
                      G_CALLBACK (can_undo),
                      window);
    g_signal_connect (doc,
                      "notify::can-redo",
                      G_CALLBACK (can_redo),
                      window);
    g_signal_connect (doc,
                      "notify::has-selection",
                      G_CALLBACK (selection_changed),
                      window);
    g_signal_connect (doc,
                      "notify::language",
                      G_CALLBACK (sync_languages_menu),
                      window);
    g_signal_connect (doc,
                      "notify::read-only",
                      G_CALLBACK (readonly_changed),
                      window);
    g_signal_connect (view,
                      "toggle_overwrite",
                      G_CALLBACK (update_overwrite_mode_statusbar),
                      window);
    g_signal_connect (view,
                      "notify::editable",
                      G_CALLBACK (editable_changed),
                      window);

    update_documents_list_menu (window);

    g_signal_connect (view,
                      "drop_uris",
                      G_CALLBACK (drop_uris_cb),
                      NULL);

    update_window_state (window);

    g_signal_emit (G_OBJECT (window), signals[TAB_ADDED], 0, tab);
}

static void
notebook_tab_removed (PlumaNotebook *notebook,
                      PlumaTab      *tab,
                      PlumaWindow   *window)
{
    PlumaView     *view;
    PlumaDocument *doc;

    pluma_debug (DEBUG_WINDOW);

    g_return_if_fail ((window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION) == 0);

    --window->priv->num_tabs;

    view = pluma_tab_get_view (tab);
    doc = pluma_tab_get_document (tab);

    g_signal_handlers_disconnect_by_func (tab,
                                          G_CALLBACK (sync_name),
                                          window);
    g_signal_handlers_disconnect_by_func (tab,
                                          G_CALLBACK (sync_state),
                                          window);
    g_signal_handlers_disconnect_by_func (doc,
                                          G_CALLBACK (update_cursor_position_statusbar),
                                          window);
    g_signal_handlers_disconnect_by_func (doc,
                                          G_CALLBACK (can_search_again),
                                          window);
    g_signal_handlers_disconnect_by_func (doc,
                                          G_CALLBACK (can_undo),
                                          window);
    g_signal_handlers_disconnect_by_func (doc,
                                          G_CALLBACK (can_redo),
                                          window);
    g_signal_handlers_disconnect_by_func (doc,
                                          G_CALLBACK (selection_changed),
                                          window);
    g_signal_handlers_disconnect_by_func (doc,
                                          G_CALLBACK (sync_languages_menu),
                                          window);
    g_signal_handlers_disconnect_by_func (doc,
                                          G_CALLBACK (readonly_changed),
                                          window);
    g_signal_handlers_disconnect_by_func (view,
                                          G_CALLBACK (update_overwrite_mode_statusbar),
                                          window);
    g_signal_handlers_disconnect_by_func (view,
                                          G_CALLBACK (editable_changed),
                                          window);
    g_signal_handlers_disconnect_by_func (view,
                                          G_CALLBACK (drop_uris_cb),
                                          NULL);

#if GLIB_CHECK_VERSION(2,62,0)
    if (tab == pluma_window_get_active_tab (window))
    {
       g_clear_signal_handler (&window->priv->tab_width_id, view);
       g_clear_signal_handler (&window->priv->spaces_instead_of_tabs_id, view);
       g_clear_signal_handler (&window->priv->language_changed_id, doc);
    }
#else
    if (window->priv->tab_width_id && tab == pluma_window_get_active_tab (window))
    {
        g_signal_handler_disconnect (view, window->priv->tab_width_id);
        window->priv->tab_width_id = 0;
    }

    if (window->priv->spaces_instead_of_tabs_id && tab == pluma_window_get_active_tab (window))
    {
        g_signal_handler_disconnect (view, window->priv->spaces_instead_of_tabs_id);
        window->priv->spaces_instead_of_tabs_id = 0;
    }

    if (window->priv->language_changed_id && tab == pluma_window_get_active_tab (window))
    {
        g_signal_handler_disconnect (doc, window->priv->language_changed_id);
        window->priv->language_changed_id = 0;
    }
#endif

    g_return_if_fail (window->priv->num_tabs >= 0);
    if (window->priv->num_tabs == 0)
    {
        window->priv->active_tab = NULL;

        set_title (window);

        /* Remove line and col info */
        pluma_statusbar_set_cursor_position (PLUMA_STATUSBAR (window->priv->statusbar),
                                             -1,
                                             -1);

        pluma_statusbar_clear_overwrite (PLUMA_STATUSBAR (window->priv->statusbar));

        /* hide the combos */
        gtk_widget_hide (window->priv->tab_width_combo);
        gtk_widget_hide (window->priv->language_combo);
    }

    if (!window->priv->removing_tabs)
    {
        update_documents_list_menu (window);
        update_next_prev_doc_sensitivity_per_window (window);
    }
    else
    {
        if (window->priv->num_tabs == 0)
        {
            update_documents_list_menu (window);
            update_next_prev_doc_sensitivity_per_window (window);
        }
    }

    update_sensitivity_according_to_open_tabs (window);

    if (window->priv->num_tabs == 0)
    {
        peas_extension_set_call (window->priv->extensions, "update_state");
    }

    update_window_state (window);

    g_signal_emit (G_OBJECT (window), signals[TAB_REMOVED], 0, tab);
}

static void
notebook_tabs_reordered (PlumaNotebook *notebook,
                         PlumaWindow   *window)
{
    update_documents_list_menu (window);
    update_next_prev_doc_sensitivity_per_window (window);

    g_signal_emit (G_OBJECT (window), signals[TABS_REORDERED], 0);
}

static void
notebook_tab_detached (PlumaNotebook *notebook,
                       PlumaTab      *tab,
                       PlumaWindow   *window)
{
    PlumaWindow *new_window;

    new_window = clone_window (window);

    pluma_notebook_move_tab (notebook,
                             PLUMA_NOTEBOOK (_pluma_window_get_notebook (new_window)),
                             tab, 0);

    gtk_window_set_position (GTK_WINDOW (new_window), GTK_WIN_POS_MOUSE);

    gtk_widget_show (GTK_WIDGET (new_window));
}

static void
notebook_tab_close_request (PlumaNotebook *notebook,
                            PlumaTab      *tab,
                            GtkWindow     *window)
{
    /* Note: we are destroying the tab before the default handler
     * seems to be ok, but we need to keep an eye on this. */
    _pluma_cmd_file_close_tab (tab, PLUMA_WINDOW (window));
}

static gboolean
show_notebook_popup_menu (GtkNotebook    *notebook,
                          PlumaWindow    *window,
                          GdkEventButton *event)
{
    GtkWidget *menu;
    GtkWidget *tab;
    GtkWidget *tab_label;
    gint page;
    gint pages;
    GMenu *model;
    GMenu *section;
    GMenu *close_multiple;
    GMenuItem *close_multiple_item;

    tab = GTK_WIDGET (pluma_window_get_active_tab (window));
    g_return_val_if_fail (tab != NULL, FALSE);

    page = gtk_notebook_page_num (notebook, tab);
    pages = gtk_notebook_get_n_pages (notebook);

        set_action_enabled (window, "close-tabs-left", page > 0);

        set_action_enabled (window, "close-tabs-right", page >= 0 && page < pages - 1);

        set_action_enabled (window, "close-other-tabs", pages > 1);

    model = g_menu_new ();

    section = g_menu_new ();
    g_menu_append (section, _("_Move to New Window"), "win.move-to-new-window");
    g_menu_append_section (model, NULL, G_MENU_MODEL (section));
    g_object_unref (section);

    section = g_menu_new ();
    g_menu_append (section, _("_Save"), "win.save");
    g_menu_append (section, _("Save _As…"), "win.save-as");
    g_menu_append_section (model, NULL, G_MENU_MODEL (section));
    g_object_unref (section);

    section = g_menu_new ();
    g_menu_append (section, _("_Print…"), "win.print");
    g_menu_append_section (model, NULL, G_MENU_MODEL (section));
    g_object_unref (section);

    close_multiple = g_menu_new ();
    g_menu_append (close_multiple, _("Close Tabs to the _Left"), "win.close-tabs-left");
    g_menu_append (close_multiple, _("Close Tabs to the _Right"), "win.close-tabs-right");
    g_menu_append (close_multiple, _("Close _Other Tabs"), "win.close-other-tabs");
    close_multiple_item = g_menu_item_new_submenu (_("Close Multiple Ta_bs"), G_MENU_MODEL (close_multiple));
    g_object_unref (close_multiple);
    section = g_menu_new ();
    g_menu_append_item (section, close_multiple_item);
    g_object_unref (close_multiple_item);
    g_menu_append_section (model, NULL, G_MENU_MODEL (section));
    g_object_unref (section);

    section = g_menu_new ();
    g_menu_append (section, _("_Close"), "win.close");
    g_menu_append_section (model, NULL, G_MENU_MODEL (section));
    g_object_unref (section);

    menu = gtk_menu_new_from_model (G_MENU_MODEL (model));
    g_object_unref (model);
    gtk_menu_attach_to_widget (GTK_MENU (menu), GTK_WIDGET (window), NULL);
    g_signal_connect_swapped (menu, "selection-done", G_CALLBACK (gtk_widget_destroy), menu);

    tab_label = gtk_notebook_get_tab_label (notebook, tab);

    gtk_menu_popup_at_widget (GTK_MENU (menu),
                              tab_label,
                              GDK_GRAVITY_SOUTH_WEST,
                              GDK_GRAVITY_NORTH_WEST,
                              (const GdkEvent*) event);

    gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);

    return TRUE;
}

static gboolean
notebook_button_press_event (GtkNotebook    *notebook,
                             GdkEventButton *event,
                             PlumaWindow    *window)
{
    if (event->type == GDK_BUTTON_PRESS)
    {
        if (event->button == 3)
            return show_notebook_popup_menu (notebook, window, event);

        else if (event->button == 2)
        {
            PlumaTab *tab;
            tab = pluma_window_get_active_tab (window);
            notebook_tab_close_request (PLUMA_NOTEBOOK (notebook), tab, GTK_WINDOW (window));
        }
    }
    else if ((event->type == GDK_2BUTTON_PRESS) && (event->button == 1))
    {
        pluma_window_create_tab (window, TRUE);
    }

    return FALSE;
}

static gboolean
notebook_scroll_event (GtkNotebook    *notebook,
                       GdkEventScroll *event,
                       PlumaWindow    *window)
{
    if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_LEFT)
    {
        gtk_notebook_prev_page (notebook);
    }
    else if (event->direction == GDK_SCROLL_DOWN || event->direction == GDK_SCROLL_RIGHT)
    {
        gtk_notebook_next_page (notebook);
    }

    return FALSE;
}

static gboolean
notebook_popup_menu (GtkNotebook *notebook,
                     PlumaWindow *window)
{
    /* Only respond if the notebook is the actual focus */
    if (PLUMA_IS_NOTEBOOK (gtk_window_get_focus (GTK_WINDOW (window))))
    {
        return show_notebook_popup_menu (notebook, window, NULL);
    }

    return FALSE;
}

static void
side_panel_size_allocate (GtkWidget     *widget,
                          GtkAllocation *allocation,
                          PlumaWindow   *window)
{
    window->priv->side_panel_size = allocation->width;
}

static void
bottom_panel_size_allocate (GtkWidget     *widget,
                            GtkAllocation *allocation,
                            PlumaWindow   *window)
{
    window->priv->bottom_panel_size = allocation->height;
}

static void
right_panel_size_allocate (GtkWidget     *widget,
                           GtkAllocation *allocation,
                           PlumaWindow   *window)
{
    window->priv->right_panel_size = allocation->width;
}

static void
hpaned_restore_position (GtkWidget   *widget,
                         PlumaWindow *window)
{
    gint pos;

    pluma_debug_message (DEBUG_WINDOW,
                         "Restoring hpaned position: side panel size %d",
                         window->priv->side_panel_size);

    pos = MAX (100, window->priv->side_panel_size);
    gtk_paned_set_position (GTK_PANED (window->priv->hpaned), pos);

    /* start monitoring the size */
    g_signal_connect (window->priv->side_panel,
                      "size-allocate",
                      G_CALLBACK (side_panel_size_allocate),
                      window);

    /* run this only once */
    g_signal_handlers_disconnect_by_func (widget, hpaned_restore_position, window);
}

static void
vpaned_restore_position (GtkWidget   *widget,
                         PlumaWindow *window)
{
    GtkAllocation allocation;
    gint pos;

    gtk_widget_get_allocation (widget, &allocation);

    pluma_debug_message (DEBUG_WINDOW,
                         "Restoring vpaned position: bottom panel size %d",
                         window->priv->bottom_panel_size);

    pos = allocation.height - MAX (50, window->priv->bottom_panel_size);
    gtk_paned_set_position (GTK_PANED (window->priv->vpaned), pos);

    /* start monitoring the size */
    g_signal_connect (window->priv->bottom_panel,
                      "size-allocate",
                      G_CALLBACK (bottom_panel_size_allocate),
                      window);

    /* run this only once */
    g_signal_handlers_disconnect_by_func (widget, vpaned_restore_position, window);
}

static void
hpaned_inner_restore_position (GtkWidget   *widget,
                                PlumaWindow *window)
{
    GtkAllocation allocation;
    gint pos;

    gtk_widget_get_allocation (widget, &allocation);

    pluma_debug_message (DEBUG_WINDOW,
                         "Restoring hpaned_inner position: right panel size %d",
                         window->priv->right_panel_size);

    pos = allocation.width - MAX (50, window->priv->right_panel_size);
    gtk_paned_set_position (GTK_PANED (window->priv->hpaned_inner), pos);

    /* start monitoring the size */
    g_signal_connect (window->priv->right_panel,
                      "size-allocate",
                      G_CALLBACK (right_panel_size_allocate),
                      window);

    /* run this only once */
    g_signal_handlers_disconnect_by_func (widget, hpaned_inner_restore_position, window);
}

static void
side_panel_visibility_changed (GtkWidget   *side_panel,
                               PlumaWindow *window)
{
    gboolean   visible;

    visible = gtk_widget_get_visible (side_panel);

    if (!g_settings_get_boolean (window->priv->editor_settings, "show-tabs-with-side-pane"))
    {
        if (visible)
            gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->priv->notebook), FALSE);
        else
            gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->priv->notebook),
                                        g_settings_get_boolean (window->priv->editor_settings, "show-single-tab") ||
                                        (gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook)) > 1));
    }
    else
        gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->priv->notebook),
                                    g_settings_get_boolean (window->priv->editor_settings, "show-single-tab") ||
                                    (gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook)) > 1));

    g_settings_set_boolean (window->priv->editor_settings,
                            PLUMA_SETTINGS_SIDE_PANE_VISIBLE,
                            visible);

    sync_view_toggle_action_state (window, "show-side-pane", visible);

    /* focus the document */
    if (!visible && window->priv->active_tab != NULL)
        gtk_widget_grab_focus (GTK_WIDGET (pluma_tab_get_view (PLUMA_TAB (window->priv->active_tab))));
}

static void
create_side_panel (PlumaWindow *window)
{
    GtkWidget *documents_panel;

    pluma_debug (DEBUG_WINDOW);

    window->priv->side_panel = pluma_panel_new (GTK_ORIENTATION_VERTICAL);
    g_object_set_data (G_OBJECT (window->priv->side_panel), "panel-id", "side");

    gtk_paned_pack1 (GTK_PANED (window->priv->hpaned),
                     window->priv->side_panel,
                     FALSE,
                     FALSE);

    g_signal_connect_after (window->priv->side_panel,
                            "show",
                            G_CALLBACK (side_panel_visibility_changed),
                            window);
    g_signal_connect_after (window->priv->side_panel,
                            "hide",
                            G_CALLBACK (side_panel_visibility_changed),
                            window);

    documents_panel = pluma_documents_panel_new (window);
    pluma_panel_add_item_with_icon (PLUMA_PANEL (window->priv->side_panel),
                                    documents_panel,
                                    _("Documents"),
                                    "text-x-generic-symbolic");

    window->priv->project_search_panel = pluma_project_search_panel_new (window);
    pluma_panel_add_item_with_icon (PLUMA_PANEL (window->priv->side_panel),
                                    window->priv->project_search_panel,
                                    _("Search"),
                                    "system-search-symbolic");

    window->priv->file_browser_panel = pluma_file_browser_panel_new (window);
    pluma_panel_add_item_with_icon (PLUMA_PANEL (window->priv->side_panel),
                                    window->priv->file_browser_panel,
                                    _("File Browser"),
                                    "folder-symbolic");

#ifdef ENABLE_GIT
    window->priv->git_panel = pluma_git_panel_new (window);
    pluma_panel_add_item_with_icon (PLUMA_PANEL (window->priv->side_panel),
                                    window->priv->git_panel,
                                    _("Source Control"),
                                    "network-wired-symbolic");
#endif
}

static void
bottom_panel_visibility_changed (PlumaPanel  *bottom_panel,
                                 PlumaWindow *window)
{
    gboolean visible;

    visible = gtk_widget_get_visible (GTK_WIDGET (bottom_panel));

    g_settings_set_boolean (window->priv->editor_settings,
                            PLUMA_SETTINGS_BOTTOM_PANE_VISIBLE,
                            visible);

    sync_view_toggle_action_state (window, "show-bottom-pane", visible);

    /* focus the document */
    if (!visible && window->priv->active_tab != NULL)
        gtk_widget_grab_focus (GTK_WIDGET (pluma_tab_get_view (PLUMA_TAB (window->priv->active_tab))));
}

static void
bottom_panel_item_removed (PlumaPanel  *panel,
                           GtkWidget   *item,
                           PlumaWindow *window)
{
    if (pluma_panel_get_n_items (panel) == 0)
    {
        gtk_widget_hide (GTK_WIDGET (panel));

        sync_view_toggle_action_enabled (window, "show-bottom-pane", FALSE);
    }
}

static void
bottom_panel_item_added (PlumaPanel  *panel,
             GtkWidget   *item,
             PlumaWindow *window)
{
    /* if it's the first item added, set the menu item
     * sensitive and if needed show the panel */
    if (pluma_panel_get_n_items (panel) == 1)
    {
        gboolean show;

        sync_view_toggle_action_enabled (window, "show-bottom-pane", TRUE);

        show = get_view_toggle_action_state (window, "show-bottom-pane");
        if (show)
            gtk_widget_show (GTK_WIDGET (panel));
    }
}

static void
right_panel_item_removed (PlumaPanel  *panel,
                          GtkWidget   *item,
                          PlumaWindow *window)
{
    if (pluma_panel_get_n_items (panel) == 0)
    {
        gtk_widget_hide (GTK_WIDGET (panel));

        sync_view_toggle_action_enabled (window, "show-right-pane", FALSE);
    }
}

static void
right_panel_item_added (PlumaPanel  *panel,
                        GtkWidget   *item,
                        PlumaWindow *window)
{
    /* if it's the first item added, set the menu item
     * sensitive and if needed show the panel */
    if (pluma_panel_get_n_items (panel) == 1)
    {
        gboolean show;

        sync_view_toggle_action_enabled (window, "show-right-pane", TRUE);

        show = get_view_toggle_action_state (window, "show-right-pane");
        if (show)
            gtk_widget_show (GTK_WIDGET (panel));
    }
}

static void
create_bottom_panel (PlumaWindow *window)
{
    pluma_debug (DEBUG_WINDOW);

    window->priv->bottom_panel = pluma_panel_new (GTK_ORIENTATION_HORIZONTAL);
    g_object_set_data (G_OBJECT (window->priv->bottom_panel), "panel-id", "bottom");

    gtk_paned_pack2 (GTK_PANED (window->priv->vpaned),
                     window->priv->bottom_panel,
                     FALSE,
                     FALSE);

    g_signal_connect_after (window->priv->bottom_panel,
                            "show",
                            G_CALLBACK (bottom_panel_visibility_changed),
                            window);
    g_signal_connect_after (window->priv->bottom_panel,
                            "hide",
                            G_CALLBACK (bottom_panel_visibility_changed),
                            window);
}

static void
right_panel_visibility_changed (PlumaPanel  *right_panel,
                                 PlumaWindow *window)
{
    gboolean visible;

    visible = gtk_widget_get_visible (GTK_WIDGET (right_panel));

    pluma_g_settings_set_boolean_safe (window->priv->editor_settings,
                                       PLUMA_SETTINGS_RIGHT_PANE_VISIBLE,
                                       visible);

    sync_view_toggle_action_state (window, "show-right-pane", visible);

    /* focus the document */
    if (!visible && window->priv->active_tab != NULL)
        gtk_widget_grab_focus (GTK_WIDGET (pluma_tab_get_view (PLUMA_TAB (window->priv->active_tab))));
}

static void
create_right_panel (PlumaWindow *window)
{
    pluma_debug (DEBUG_WINDOW);

    window->priv->right_panel = pluma_panel_new (GTK_ORIENTATION_VERTICAL);
    g_object_set_data (G_OBJECT (window->priv->right_panel), "panel-id", "right");

    gtk_paned_pack2 (GTK_PANED (window->priv->hpaned_inner),
                     window->priv->right_panel,
                     FALSE,
                     FALSE);

    g_signal_connect_after (window->priv->right_panel,
                            "show",
                            G_CALLBACK (right_panel_visibility_changed),
                            window);
    g_signal_connect_after (window->priv->right_panel,
                            "hide",
                            G_CALLBACK (right_panel_visibility_changed),
                            window);
}

static void
init_panels_visibility (PlumaWindow *window)
{
    gint active_page;
    gboolean side_pane_visible;
    gboolean bottom_pane_visible;
    gboolean right_pane_visible;

    pluma_debug (DEBUG_WINDOW);

    /* side pane */
    active_page = g_settings_get_int (window->priv->editor_settings,
                                      PLUMA_SETTINGS_SIDE_PANEL_ACTIVE_PAGE);
    _pluma_panel_set_active_item_by_id (PLUMA_PANEL (window->priv->side_panel),
                                        active_page);

    side_pane_visible = g_settings_get_boolean (window->priv->editor_settings,
                                                PLUMA_SETTINGS_SIDE_PANE_VISIBLE);
    bottom_pane_visible = g_settings_get_boolean (window->priv->editor_settings,
                                                  PLUMA_SETTINGS_BOTTOM_PANE_VISIBLE);
    right_pane_visible = pluma_g_settings_get_boolean_safe (window->priv->editor_settings,
                                                            PLUMA_SETTINGS_RIGHT_PANE_VISIBLE,
                                                            FALSE);

    if (side_pane_visible)

    {
        gtk_widget_show (window->priv->side_panel);
    }

    /* bottom pane, it can be empty */
    if (pluma_panel_get_n_items (PLUMA_PANEL (window->priv->bottom_panel)) > 0)
    {
        active_page = g_settings_get_int (window->priv->editor_settings,
                                          PLUMA_SETTINGS_BOTTOM_PANEL_ACTIVE_PAGE);
        _pluma_panel_set_active_item_by_id (PLUMA_PANEL (window->priv->bottom_panel),
                                            active_page);

        if (bottom_pane_visible)
        {
            gtk_widget_show (window->priv->bottom_panel);
        }
    }
    else
    {
        sync_view_toggle_action_enabled (window, "show-bottom-pane", FALSE);
    }

    /* right pane */
    if (pluma_panel_get_n_items (PLUMA_PANEL (window->priv->right_panel)) > 0)
    {
        active_page = pluma_g_settings_get_int_safe (window->priv->editor_settings,
                                                     PLUMA_SETTINGS_RIGHT_PANEL_ACTIVE_PAGE,
                                                     0);
        _pluma_panel_set_active_item_by_id (PLUMA_PANEL (window->priv->right_panel),
                                            active_page);

        if (right_pane_visible)
        {
            gtk_widget_show (window->priv->right_panel);
        }
    }
    else
    {
        sync_view_toggle_action_enabled (window, "show-right-pane", FALSE);
    }

    /* start track sensitivity after the initial state is set */
    window->priv->bottom_panel_item_removed_handler_id =
        g_signal_connect (window->priv->bottom_panel,
                          "item_removed",
                          G_CALLBACK (bottom_panel_item_removed),
                          window);

    g_signal_connect (window->priv->bottom_panel,
                      "item_added",
                      G_CALLBACK (bottom_panel_item_added),
                      window);

    window->priv->right_panel_item_removed_handler_id =
        g_signal_connect (window->priv->right_panel,
                          "item_removed",
                          G_CALLBACK (right_panel_item_removed),
                          window);

    g_signal_connect (window->priv->right_panel,
                      "item_added",
                      G_CALLBACK (right_panel_item_added),
                      window);
}

static void
clipboard_owner_change (GtkClipboard    *clipboard,
                        GdkEventOwnerChange *event,
                        PlumaWindow     *window)
{
    set_paste_sensitivity_according_to_clipboard (window, clipboard);
}

static void
window_realized (GtkWidget *window,
                 gpointer  *data)
{
    GtkClipboard *clipboard;

    clipboard = gtk_widget_get_clipboard (window, GDK_SELECTION_CLIPBOARD);

    g_signal_connect (clipboard,
                      "owner_change",
                      G_CALLBACK (clipboard_owner_change),
                      window);
}

static void
window_unrealized (GtkWidget *window,
                   gpointer  *data)
{
    GtkClipboard *clipboard;

    clipboard = gtk_widget_get_clipboard (window, GDK_SELECTION_CLIPBOARD);

    g_signal_handlers_disconnect_by_func (clipboard,
                                          G_CALLBACK (clipboard_owner_change),
                                          window);
}

static void
check_window_is_active (PlumaWindow *window,
                        GParamSpec *property,
                        gpointer useless)
{
    if (window->priv->window_state & GDK_WINDOW_STATE_FULLSCREEN)
    {
        if (gtk_window_is_active (GTK_WINDOW (window)))
        {
            gtk_widget_show (window->priv->fullscreen_controls);
        }
        else
        {
            gtk_widget_hide (window->priv->fullscreen_controls);
        }
    }
}

static void
connect_notebook_signals (PlumaWindow *window,
                          GtkWidget   *notebook)
{
    g_signal_connect (notebook,
                      "switch-page",
                      G_CALLBACK (notebook_switch_page),
                      window);
    g_signal_connect (notebook,
                      "tab-added",
                      G_CALLBACK (notebook_tab_added),
                      window);
    g_signal_connect (notebook,
                      "tab-removed",
                      G_CALLBACK (notebook_tab_removed),
                      window);
    g_signal_connect (notebook,
                      "tabs-reordered",
                      G_CALLBACK (notebook_tabs_reordered),
                      window);
    g_signal_connect (notebook,
                      "tab-detached",
                      G_CALLBACK (notebook_tab_detached),
                      window);
    g_signal_connect (notebook,
                      "tab-close-request",
                      G_CALLBACK (notebook_tab_close_request),
                      window);
    g_signal_connect (notebook,
                      "button-press-event",
                      G_CALLBACK (notebook_button_press_event),
                      window);
    g_signal_connect (notebook,
                      "popup-menu",
                      G_CALLBACK (notebook_popup_menu),
                      window);
    g_signal_connect (notebook,
                      "scroll-event",
                      G_CALLBACK (notebook_scroll_event),
                      window);
}

static void
add_notebook (PlumaWindow *window,
          GtkWidget   *notebook)
{
    gtk_paned_pack1 (GTK_PANED (window->priv->vpaned),
                     notebook,
                     TRUE,
                     TRUE);

    gtk_widget_show (notebook);

    gtk_widget_add_events (notebook, GDK_SCROLL_MASK);
    connect_notebook_signals (window, notebook);
}

static void
on_extension_added (PeasExtensionSet *extensions,
                    PeasPluginInfo   *info,
                    PeasExtension    *exten,
                    PlumaWindow      *window)
{
    peas_extension_call (exten, "activate", window);
}

static void
on_extension_removed (PeasExtensionSet *extensions,
                      PeasPluginInfo   *info,
                      PeasExtension    *exten,
                      PlumaWindow      *window)
{
    peas_extension_call (exten, "deactivate", window);
}

static void
pluma_window_init (PlumaWindow *window)
{
    GtkWidget *main_box;
    GtkTargetList *tl;

    pluma_debug (DEBUG_WINDOW);

    window->priv = pluma_window_get_instance_private (window);
    window->priv->active_tab = NULL;
    window->priv->num_tabs = 0;
    window->priv->removing_tabs = FALSE;
    window->priv->state = PLUMA_WINDOW_STATE_NORMAL;
    window->priv->dispose_has_run = FALSE;
    window->priv->fullscreen_controls = NULL;
    window->priv->fullscreen_animation_timeout_id = 0;
    window->priv->editor_settings = g_settings_new (PLUMA_SCHEMA_ID);

    window->priv->message_bus = pluma_message_bus_new ();

    window->priv->window_group = gtk_window_group_new ();
    gtk_window_group_add_window (window->priv->window_group, GTK_WINDOW (window));

    GtkStyleContext *context;

    context = gtk_widget_get_style_context (GTK_WIDGET (window));
    gtk_style_context_add_class (context, "pluma-window");

    main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (window), main_box);
    gtk_widget_show (main_box);

    /* Add menu bar and toolbar bar */
    create_menu_bar_and_toolbar (window, main_box);

    /* Add status bar */
    create_statusbar (window, main_box);

    /* Add the main area */
    pluma_debug_message (DEBUG_WINDOW, "Add main area");
    window->priv->hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start (GTK_BOX (main_box),
                        window->priv->hpaned,
                        TRUE,
                        TRUE,
                        0);

    /* Create nested horizontal paned for center+right layout */
    window->priv->hpaned_inner = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack2 (GTK_PANED (window->priv->hpaned),
                     window->priv->hpaned_inner,
                     TRUE,
                     FALSE);

    /* Create vertical paned for notebook+bottom panel */
    window->priv->vpaned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack1 (GTK_PANED (window->priv->hpaned_inner),
                     window->priv->vpaned,
                     TRUE,
                     TRUE);

    pluma_debug_message (DEBUG_WINDOW, "Create pluma notebook");
    window->priv->notebook = pluma_notebook_new ();
    add_notebook (window, window->priv->notebook);

    /* side and bottom panels */
    create_side_panel (window);
    create_bottom_panel (window);
    create_right_panel (window);

    /* panes' state must be restored after panels have been mapped,
     * since the bottom pane position depends on the size of the vpaned. */
    window->priv->side_panel_size = g_settings_get_int (window->priv->editor_settings,
                                                        PLUMA_SETTINGS_SIDE_PANEL_SIZE);
    window->priv->bottom_panel_size = g_settings_get_int (window->priv->editor_settings,
                                                          PLUMA_SETTINGS_BOTTOM_PANEL_SIZE);
    window->priv->right_panel_size = pluma_g_settings_get_int_safe (window->priv->editor_settings,
                                                                    PLUMA_SETTINGS_RIGHT_PANEL_SIZE,
                                                                    200);

    g_signal_connect_after (window->priv->hpaned,
                            "map",
                            G_CALLBACK (hpaned_restore_position),
                            window);
    g_signal_connect_after (window->priv->vpaned,
                            "map",
                            G_CALLBACK (vpaned_restore_position),
                            window);
    g_signal_connect_after (window->priv->hpaned_inner,
                            "map",
                            G_CALLBACK (hpaned_inner_restore_position),
                            window);

    gtk_widget_show (window->priv->hpaned);
    gtk_widget_show (window->priv->hpaned_inner);
    gtk_widget_show (window->priv->vpaned);

    /* Drag and drop support, set targets to NULL because we add the
       default uri_targets below */
    gtk_drag_dest_set (GTK_WIDGET (window),
                       GTK_DEST_DEFAULT_MOTION |
                       GTK_DEST_DEFAULT_HIGHLIGHT |
                       GTK_DEST_DEFAULT_DROP,
                       NULL,
                       0,
                       GDK_ACTION_COPY);

    /* Add uri targets */
    tl = gtk_drag_dest_get_target_list (GTK_WIDGET (window));

    if (tl == NULL)
    {
        tl = gtk_target_list_new (NULL, 0);
        gtk_drag_dest_set_target_list (GTK_WIDGET (window), tl);
        gtk_target_list_unref (tl);
    }

    gtk_target_list_add_uri_targets (tl, TARGET_URI_LIST);

    /* connect instead of override, so that we can
     * share the cb code with the view */
    g_signal_connect (window,
                      "drag_data_received",
                      G_CALLBACK (drag_data_received_cb),
                      NULL);

    /* we can get the clipboard only after the widget
     * is realized */
    g_signal_connect (window,
                      "realize",
                      G_CALLBACK (window_realized),
                      NULL);
    g_signal_connect (window,
                      "unrealize",
                      G_CALLBACK (window_unrealized),
                      NULL);

    /* Check if the window is active for fullscreen */
    g_signal_connect (window,
                      "notify::is-active",
                      G_CALLBACK (check_window_is_active),
                      NULL);

    pluma_debug_message (DEBUG_WINDOW, "Update plugins ui");

    window->priv->extensions = peas_extension_set_new (PEAS_ENGINE (pluma_plugins_engine_get_default ()),
                                                       PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                       "window",
                                                       window,
                                                       NULL);

    g_signal_connect (window->priv->extensions, "extension-added",
                      G_CALLBACK (on_extension_added),
                      window);
    g_signal_connect (window->priv->extensions, "extension-removed",
                      G_CALLBACK (on_extension_removed),
                      window);

    peas_extension_set_call (window->priv->extensions, "activate");

     /* set visibility of panes.
      * This needs to be done after plugins activatation */
    init_panels_visibility (window);

    update_sensitivity_according_to_open_tabs (window);

    pluma_debug_message (DEBUG_WINDOW, "END");
}

/**
 * pluma_window_get_active_view:
 * @window: a #PlumaWindow
 *
 * Gets the active #PlumaView.
 *
 * Returns: (transfer none): the active #PlumaView
 */
PlumaView *
pluma_window_get_active_view (PlumaWindow *window)
{
    PlumaView *view;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    if (window->priv->active_tab == NULL)
        return NULL;

    view = pluma_tab_get_view (PLUMA_TAB (window->priv->active_tab));

    return view;
}

/**
 * pluma_window_get_active_document:
 * @window: a #PlumaWindow
 *
 * Gets the active #PlumaDocument.
 *
 * Returns: (transfer none): the active #PlumaDocument
 */
PlumaDocument *
pluma_window_get_active_document (PlumaWindow *window)
{
    PlumaView *view;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    view = pluma_window_get_active_view (window);
    if (view == NULL)
        return NULL;

    return PLUMA_DOCUMENT (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));
}

GtkWidget *
_pluma_window_get_notebook (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return window->priv->notebook;
}

/**
 * pluma_window_create_tab:
 * @window: a #PlumaWindow
 * @jump_to: %TRUE to set the new #PlumaTab as active
 *
 * Creates a new #PlumaTab and adds the new tab to the #PlumaNotebook.
 * In case @jump_to is %TRUE the #PlumaNotebook switches to that new #PlumaTab.
 *
 * Returns: (transfer none): a new #PlumaTab
 */
PlumaTab *
pluma_window_create_tab (PlumaWindow *window,
                         gboolean     jump_to)
{
    PlumaTab *tab;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    tab = PLUMA_TAB (_pluma_tab_new ());
    gtk_widget_show (GTK_WIDGET (tab));

    pluma_notebook_add_tab (PLUMA_NOTEBOOK (window->priv->notebook),
                            tab,
                            -1,
                            jump_to);

    if (!gtk_widget_get_visible (GTK_WIDGET (window)))
    {
        gtk_window_present (GTK_WINDOW (window));
    }

    return tab;
}

/**
 * pluma_window_create_tab_from_uri:
 * @window: a #PlumaWindow
 * @uri: the uri of the document
 * @encoding: a #PlumaEncoding
 * @line_pos: the line position to visualize
 * @create: %TRUE to create a new document in case @uri does exist
 * @jump_to: %TRUE to set the new #PlumaTab as active
 *
 * Creates a new #PlumaTab loading the document specified by @uri.
 * In case @jump_to is %TRUE the #PlumaNotebook swithes to that new #PlumaTab.
 * Whether @create is %TRUE, creates a new empty document if location does
 * not refer to an existing file
 *
 * Returns: (transfer none): a new #PlumaTab
 */
PlumaTab *
pluma_window_create_tab_from_uri (PlumaWindow         *window,
                                  const gchar         *uri,
                                  const PlumaEncoding *encoding,
                                  gint                 line_pos,
                                  gboolean             create,
                                  gboolean             jump_to)
{
    GtkWidget *tab;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);
    g_return_val_if_fail (uri != NULL, NULL);

    tab = _pluma_tab_new_from_uri (uri,
                                   encoding,
                                   line_pos,
                                   create);
    if (tab == NULL)
        return NULL;

    gtk_widget_show (tab);

    pluma_notebook_add_tab (PLUMA_NOTEBOOK (window->priv->notebook),
                            PLUMA_TAB (tab),
                            -1,
                            jump_to);

    if (!gtk_widget_get_visible (GTK_WIDGET (window)))
    {
        gtk_window_present (GTK_WINDOW (window));
    }

    return PLUMA_TAB (tab);
}

/**
 * pluma_window_create_tab_from_large_file:
 * @window: a #PlumaWindow
 * @file: the file to open in large-file mode
 * @jump_to: %TRUE to set the new #PlumaTab as active
 *
 * The tab is added to the notebook immediately, showing a loading
 * indicator while @file is read on a worker thread (see
 * _pluma_tab_load_large_file()); if loading fails, the tab shows an
 * inline error message instead of content, same as a normal document
 * load failure.
 *
 * Returns: (transfer none): the new #PlumaTab
 */
PlumaTab *
pluma_window_create_tab_from_large_file (PlumaWindow *window,
                                         GFile       *file,
                                         gboolean     jump_to)
{
    GtkWidget *tab;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);
    g_return_val_if_fail (G_IS_FILE (file), NULL);

    tab = _pluma_tab_new ();
    gtk_widget_show (tab);

    pluma_notebook_add_tab (PLUMA_NOTEBOOK (window->priv->notebook),
                            PLUMA_TAB (tab),
                            -1,
                            jump_to);

    if (!gtk_widget_get_visible (GTK_WIDGET (window)))
    {
        gtk_window_present (GTK_WINDOW (window));
    }

    _pluma_tab_load_large_file (PLUMA_TAB (tab), file);

    return PLUMA_TAB (tab);
}

/**
 * pluma_window_get_active_tab:
 * @window: a PlumaWindow
 *
 * Gets the active #PlumaTab in the @window.
 *
 * Returns: (transfer none): the active #PlumaTab in the @window.
 */
PlumaTab *
pluma_window_get_active_tab (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return (window->priv->active_tab == NULL) ?
                NULL : PLUMA_TAB (window->priv->active_tab);
}

static void
add_document (PlumaTab *tab, GList **res)
{
    PlumaDocument *doc;

    doc = pluma_tab_get_document (tab);

    *res = g_list_prepend (*res, doc);
}

/**
 * pluma_window_get_documents:
 * @window: a #PlumaWindow
 *
 * Gets a newly allocated list with all the documents in the window.
 * This list must be freed.
 *
 * Returns: (element-type Pluma.Document) (transfer container): a newly
 * allocated list with all the documents in the window
 */
GList *
pluma_window_get_documents (PlumaWindow *window)
{
    GList *res = NULL;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    gtk_container_foreach (GTK_CONTAINER (window->priv->notebook),
                           (GtkCallback)add_document,
                           &res);

    res = g_list_reverse (res);

    return res;
}

static void
add_view (PlumaTab *tab, GList **res)
{
    PlumaView *view;

    view = pluma_tab_get_view (tab);

    *res = g_list_prepend (*res, view);
}

/**
 * pluma_window_get_views:
 * @window: a #PlumaWindow
 *
 * Gets a list with all the views in the window. This list must be freed.
 *
 * Returns: (element-type Pluma.View) (transfer container): a newly allocated
 * list with all the views in the window
 */
GList *
pluma_window_get_views (PlumaWindow *window)
{
    GList *res = NULL;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    gtk_container_foreach (GTK_CONTAINER (window->priv->notebook),
                           (GtkCallback)add_view,
                           &res);

    res = g_list_reverse (res);

    return res;
}

/**
 * pluma_window_close_tab:
 * @window: a #PlumaWindow
 * @tab: the #PlumaTab to close
 *
 * Closes the @tab.
 */
void
pluma_window_close_tab (PlumaWindow *window,
            PlumaTab    *tab)
{
    g_return_if_fail (PLUMA_IS_WINDOW (window));
    g_return_if_fail (PLUMA_IS_TAB (tab));
    g_return_if_fail ((pluma_tab_get_state (tab) != PLUMA_TAB_STATE_SAVING) &&
                      (pluma_tab_get_state (tab) != PLUMA_TAB_STATE_SHOWING_PRINT_PREVIEW));

    pluma_notebook_remove_tab (PLUMA_NOTEBOOK (window->priv->notebook), tab);
}

/**
 * pluma_window_close_all_tabs:
 * @window: a #PlumaWindow
 *
 * Closes all opened tabs.
 */
void
pluma_window_close_all_tabs (PlumaWindow *window)
{
    g_return_if_fail (PLUMA_IS_WINDOW (window));
    g_return_if_fail (!(window->priv->state & PLUMA_WINDOW_STATE_SAVING) &&
                      !(window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION));

    window->priv->removing_tabs = TRUE;

    pluma_notebook_remove_all_tabs (PLUMA_NOTEBOOK (window->priv->notebook));

    window->priv->removing_tabs = FALSE;
}

/**
 * pluma_window_close_tabs:
 * @window: a #PlumaWindow
 * @tabs: (element-type Pluma.Tab): a list of #PlumaTab
 *
 * Closes all tabs specified by @tabs.
 */
void
pluma_window_close_tabs (PlumaWindow *window,
             const GList *tabs)
{
    g_return_if_fail (PLUMA_IS_WINDOW (window));
    g_return_if_fail (!(window->priv->state & PLUMA_WINDOW_STATE_SAVING) &&
                      !(window->priv->state & PLUMA_WINDOW_STATE_SAVING_SESSION));

    if (tabs == NULL)
        return;

    window->priv->removing_tabs = TRUE;

    while (tabs != NULL)
    {
        if (tabs->next == NULL)
            window->priv->removing_tabs = FALSE;

        pluma_notebook_remove_tab (PLUMA_NOTEBOOK (window->priv->notebook),
                                   PLUMA_TAB (tabs->data));

        tabs = g_list_next (tabs);
    }

    g_return_if_fail (window->priv->removing_tabs == FALSE);
}

PlumaWindow *
_pluma_window_move_tab_to_new_window (PlumaWindow *window,
                                      PlumaTab    *tab)
{
    PlumaWindow *new_window;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);
    g_return_val_if_fail (PLUMA_IS_TAB (tab), NULL);
    g_return_val_if_fail (gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook)) > 1,
                          NULL);

    new_window = clone_window (window);

    pluma_notebook_move_tab (PLUMA_NOTEBOOK (window->priv->notebook),
                             PLUMA_NOTEBOOK (new_window->priv->notebook),
                             tab,
                             -1);

    gtk_widget_show (GTK_WIDGET (new_window));

    return new_window;
}

/**
 * pluma_window_set_active_tab:
 * @window: a #PlumaWindow
 * @tab: a #PlumaTab
 *
 * Switches to the tab that matches with @tab.
 */
void
pluma_window_set_active_tab (PlumaWindow *window,
                             PlumaTab    *tab)
{
    gint page_num;

    g_return_if_fail (PLUMA_IS_WINDOW (window));
    g_return_if_fail (PLUMA_IS_TAB (tab));

    page_num = gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook),
                                      GTK_WIDGET (tab));
    if (page_num == -1)
        return;

    gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook),
                                   page_num);
}

/**
 * pluma_window_get_group:
 * @window: a #PlumaWindow
 *
 * Gets the #GtkWindowGroup in which @window resides.
 *
 * Returns: (transfer none): the #GtkWindowGroup
 */
GtkWindowGroup *
pluma_window_get_group (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return window->priv->window_group;
}

gboolean
_pluma_window_is_removing_tabs (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), FALSE);

    return window->priv->removing_tabs;
}

gboolean
pluma_window_add_menu_item (PlumaWindow *window,
                            const gchar *section_id,
                            GMenuItem   *item)
{
    GObject *section;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), FALSE);
    g_return_val_if_fail (section_id != NULL, FALSE);
    g_return_val_if_fail (G_IS_MENU_ITEM (item), FALSE);

    if (window->priv->modern_menu_builder == NULL)
        return FALSE;
    section = gtk_builder_get_object (window->priv->modern_menu_builder, section_id);
    if (!G_IS_MENU (section))
        return FALSE;
    g_menu_append_item (G_MENU (section), item);
    return TRUE;
}

void
pluma_window_remove_menu_items (PlumaWindow *window,
                                const gchar *section_id,
                                const gchar *action_name)
{
    GObject *section;
    GMenuModel *model;
    gint i;

    g_return_if_fail (PLUMA_IS_WINDOW (window));
    g_return_if_fail (section_id != NULL);
    g_return_if_fail (action_name != NULL);

    if (window->priv->modern_menu_builder == NULL)
        return;
    section = gtk_builder_get_object (window->priv->modern_menu_builder, section_id);
    if (!G_IS_MENU (section))
        return;

    model = G_MENU_MODEL (section);
    for (i = g_menu_model_get_n_items (model) - 1; i >= 0; i--)
    {
        gchar *item_action = NULL;
        if (g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_ACTION,
                                             "s", &item_action) &&
            g_strcmp0 (item_action, action_name) == 0)
            g_menu_remove (G_MENU (section), i);
        g_free (item_action);
    }
}

/**
 * pluma_window_get_side_panel:
 * @window: a #PlumaWindow
 *
 * Gets the side #PlumaPanel of the @window.
 *
 * Returns: (transfer none): the side #PlumaPanel.
 */
PlumaPanel *
pluma_window_get_side_panel (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return PLUMA_PANEL (window->priv->side_panel);
}

/**
 * pluma_window_get_bottom_panel:
 * @window: a #PlumaWindow
 *
 * Gets the bottom #PlumaPanel of the @window.
 *
 * Returns: (transfer none): the bottom #PlumaPanel.
 */
PlumaPanel *
pluma_window_get_bottom_panel (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return PLUMA_PANEL (window->priv->bottom_panel);
}

/**
 * pluma_window_get_right_panel:
 * @window: a #PlumaWindow
 *
 * Gets the right #PlumaPanel of the @window.
 *
 * Returns: (transfer none): the right #PlumaPanel.
 */
PlumaPanel *
pluma_window_get_right_panel (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return PLUMA_PANEL (window->priv->right_panel);
}

/**
 * pluma_window_get_statusbar:
 * @window: a #PlumaWindow
 *
 * Gets the #PlumaStatusbar of the @window.
 *
 * Returns: (transfer none): the #PlumaStatusbar of the @window.
 */
GtkWidget *
pluma_window_get_statusbar (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), 0);

    return window->priv->statusbar;
}

/**
 * pluma_window_get_state:
 * @window: a #PlumaWindow
 *
 * Retrieves the state of the @window.
 *
 * Returns: the current #PlumaWindowState of the @window.
 */
PlumaWindowState
pluma_window_get_state (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), PLUMA_WINDOW_STATE_NORMAL);

    return window->priv->state;
}

GFile *
_pluma_window_get_default_location (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return window->priv->default_location != NULL ?
           g_object_ref (window->priv->default_location) : NULL;
}

void
_pluma_window_set_default_location (PlumaWindow *window,
                                    GFile       *location)
{
    GFile *dir;

    g_return_if_fail (PLUMA_IS_WINDOW (window));
    g_return_if_fail (G_IS_FILE (location));

    if (g_file_query_file_type (location, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY)
        dir = g_object_ref (location);
    else
        dir = g_file_get_parent (location);
    g_return_if_fail (dir != NULL);

    if (window->priv->default_location != NULL)
        g_object_unref (window->priv->default_location);

    window->priv->default_location = dir;
}

void
_pluma_window_refresh_git_panel (PlumaWindow *window)
{
    g_return_if_fail (PLUMA_IS_WINDOW (window));

#ifdef ENABLE_GIT
    if (window->priv->git_panel != NULL)
        pluma_git_panel_refresh (PLUMA_GIT_PANEL (window->priv->git_panel));
#endif
}

/**
 * pluma_window_get_unsaved_documents:
 * @window: a #PlumaWindow
 *
 * Gets the list of documents that need to be saved before closing the window.
 *
 * Returns: (element-type Pluma.Document) (transfer container): a list of
 * #PlumaDocument that need to be saved before closing the window
 */
GList *
pluma_window_get_unsaved_documents (PlumaWindow *window)
{
    GList *unsaved_docs = NULL;
    GList *tabs;
    GList *l;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    tabs = gtk_container_get_children (GTK_CONTAINER (window->priv->notebook));

    l = tabs;
    while (l != NULL)
    {
        PlumaTab *tab;

        tab = PLUMA_TAB (l->data);

        if (!_pluma_tab_can_close (tab))
        {
            PlumaDocument *doc;

            doc = pluma_tab_get_document (tab);
            unsaved_docs = g_list_prepend (unsaved_docs, doc);
        }

        l = g_list_next (l);
    }

    g_list_free (tabs);

    return g_list_reverse (unsaved_docs);
}

void
_pluma_window_set_saving_session_state (PlumaWindow *window,
                                        gboolean     saving_session)
{
    PlumaWindowState old_state;

    g_return_if_fail (PLUMA_IS_WINDOW (window));

    old_state = window->priv->state;

    if (saving_session)
        window->priv->state |= PLUMA_WINDOW_STATE_SAVING_SESSION;
    else
        window->priv->state &= ~PLUMA_WINDOW_STATE_SAVING_SESSION;

    if (old_state != window->priv->state)
    {
        set_sensitivity_according_to_window_state (window);

        g_object_notify (G_OBJECT (window), "state");
    }
}

static void
hide_notebook_tabs_on_fullscreen (GtkNotebook    *notebook,
                                  GParamSpec    *pspec,
                                  PlumaWindow    *window)
{
    gtk_notebook_set_show_tabs (notebook, FALSE);
}

void
_pluma_window_fullscreen (PlumaWindow *window)
{
    g_return_if_fail (PLUMA_IS_WINDOW (window));

    if (_pluma_window_is_fullscreen (window))
        return;

    /* Go to fullscreen mode and hide bars */
    gtk_window_fullscreen (GTK_WINDOW (&window->window));
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->priv->notebook), FALSE);
    g_signal_connect (window->priv->notebook, "notify::show-tabs",
                      G_CALLBACK (hide_notebook_tabs_on_fullscreen), window);

    gtk_widget_hide (window->priv->menubar);

    g_signal_handlers_block_by_func (window->priv->toolbar,
                                     toolbar_visibility_changed,
                                     window);
    gtk_widget_hide (window->priv->toolbar);

    g_signal_handlers_block_by_func (window->priv->statusbar,
                                     statusbar_visibility_changed,
                                     window);
    gtk_widget_hide (window->priv->statusbar);

    fullscreen_controls_build (window);
    fullscreen_controls_show (window);

    sync_view_toggle_action_state (window, "fullscreen", TRUE);
}

void
_pluma_window_unfullscreen (PlumaWindow *window)
{
    g_return_if_fail (PLUMA_IS_WINDOW (window));

    if (!_pluma_window_is_fullscreen (window))
        return;

    /* Unfullscreen and show bars */
    gtk_window_unfullscreen (GTK_WINDOW (&window->window));
    g_signal_handlers_disconnect_by_func (window->priv->notebook,
                                          hide_notebook_tabs_on_fullscreen,
                                          window);
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->priv->notebook), TRUE);
    gtk_widget_show (window->priv->menubar);

    if (get_view_toggle_action_state (window, "show-toolbar"))
        gtk_widget_show (window->priv->toolbar);
    g_signal_handlers_unblock_by_func (window->priv->toolbar,
                                       toolbar_visibility_changed,
                                       window);

    if (get_view_toggle_action_state (window, "show-statusbar"))
        gtk_widget_show (window->priv->statusbar);
    g_signal_handlers_unblock_by_func (window->priv->statusbar,
                                       statusbar_visibility_changed,
                                       window);

    gtk_widget_hide (window->priv->fullscreen_controls);

    sync_view_toggle_action_state (window, "fullscreen", FALSE);
}

gboolean
_pluma_window_is_fullscreen (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), FALSE);

    return window->priv->window_state & GDK_WINDOW_STATE_FULLSCREEN;
}

/**
 * pluma_window_get_tab_from_location:
 * @window: a #PlumaWindow
 * @location: a #GFile
 *
 * Gets the #PlumaTab that matches with the given @location.
 *
 * Returns: (transfer none): the #PlumaTab that matches with the given @location.
 */
PlumaTab *
pluma_window_get_tab_from_location (PlumaWindow *window,
                                    GFile       *location)
{
    GList *tabs;
    GList *l;
    PlumaTab *ret = NULL;

    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);
    g_return_val_if_fail (G_IS_FILE (location), NULL);

    tabs = gtk_container_get_children (GTK_CONTAINER (window->priv->notebook));

    for (l = tabs; l != NULL; l = g_list_next (l))
    {
        PlumaDocument *d;
        PlumaTab *t;
        GFile *f;

        t = PLUMA_TAB (l->data);
        d = pluma_tab_get_document (t);

        f = pluma_document_get_location (d);

        if ((f != NULL))
        {
            gboolean found = g_file_equal (location, f);

            g_object_unref (f);

            if (found)
            {
                ret = t;
                break;
            }
        }
    }

    g_list_free (tabs);

    return ret;
}

/**
 * pluma_window_get_message_bus:
 * @window: a #PlumaWindow
 *
 * Gets the #PlumaMessageBus associated with @window. The returned reference
 * is owned by the window and should not be unreffed.
 *
 * Return value: (transfer none): the #PlumaMessageBus associated with @window
 */
PlumaMessageBus    *
pluma_window_get_message_bus (PlumaWindow *window)
{
    g_return_val_if_fail (PLUMA_IS_WINDOW (window), NULL);

    return window->priv->message_bus;
}
