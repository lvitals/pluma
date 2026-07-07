#include "config.h"
#include "pluma-terminal-plugin.h"

#include <glib/gi18n-lib.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-panel.h>
#include <pluma/pluma-window.h>
#include <pluma/pluma-window-activatable.h>
#include <vte/vte.h>

#define TERMINAL_SCHEMA "org.mate.pluma.plugins.terminal"
#define TARGET_URI_LIST 200

struct _PlumaTerminalPlugin {
    PeasExtensionBase parent_instance;
    PlumaWindow *window;
    GtkWidget *panel;
    VteTerminal *terminal;
    GSettings *profile_settings;
    GSettings *interface_settings;
    gulong profile_changed;
    gulong font_changed;
};

static void window_activatable_iface_init (PlumaWindowActivatableInterface *iface);
G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaTerminalPlugin, pluma_terminal_plugin,
                                PEAS_TYPE_EXTENSION_BASE, 0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                               window_activatable_iface_init))
enum { PROP_0, PROP_WINDOW };

static gboolean
schema_exists (const gchar *schema_id)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
    GSettingsSchema *schema = source != NULL ? g_settings_schema_source_lookup (source, schema_id, TRUE) : NULL;
    if (schema != NULL) { g_settings_schema_unref (schema); return TRUE; }
    return FALSE;
}

static GSettings *
create_profile_settings (void)
{
    if (schema_exists ("org.mate.terminal.profile"))
        return g_settings_new_with_path ("org.mate.terminal.profile", "/org/mate/terminal/profiles/default/");
    return g_settings_new (TERMINAL_SCHEMA);
}

static GdkRGBA
setting_color (GSettings *settings, const gchar *key, const GdkRGBA *fallback)
{
    gchar *text = g_settings_get_string (settings, key);
    GdkRGBA color = *fallback;
    if (*text != '\0') gdk_rgba_parse (&color, text);
    g_free (text);
    return color;
}

static void
reconfigure_terminal (PlumaTerminalPlugin *self)
{
    gchar *font_name;
    PangoFontDescription *font;
    GdkRGBA foreground = { 0.0, 0.0, 0.0, 1.0 }, background = { 1.0, 1.0, 1.0, 1.0 };
    GdkRGBA palette[16];
    gchar *palette_text;
    gchar **colors;
    guint i, n_colors = 0;
    gboolean use_theme_colors;

    if (g_settings_get_boolean (self->profile_settings, "use-system-font"))
        font_name = g_settings_get_string (self->interface_settings, "monospace-font-name");
    else
        font_name = g_settings_get_string (self->profile_settings, "font");
    font = pango_font_description_from_string (font_name);
    vte_terminal_set_font (self->terminal, font);
    pango_font_description_free (font);
    g_free (font_name);

    use_theme_colors = g_settings_get_boolean (self->profile_settings, "use-theme-colors");
    if (use_theme_colors) {
        GtkStyleContext *style = gtk_widget_get_style_context (GTK_WIDGET (self->terminal));
        GtkStateFlags state = gtk_style_context_get_state (style);
        gtk_style_context_get_color (style, state, &foreground);
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_style_context_get_background_color (style, state, &background);
        G_GNUC_END_IGNORE_DEPRECATIONS
    } else {
        foreground = setting_color (self->profile_settings, "foreground-color", &foreground);
        background = setting_color (self->profile_settings, "background-color", &background);
    }
    palette_text = g_settings_get_string (self->profile_settings, "palette");
    colors = g_strsplit (palette_text, ":", 16);
    for (i = 0; colors[i] != NULL && i < 16; i++)
        if (gdk_rgba_parse (&palette[i], colors[i])) n_colors++;
        else break;
    vte_terminal_set_colors (self->terminal, &foreground, &background,
                             n_colors == 16 ? palette : NULL, n_colors == 16 ? 16 : 0);
    g_strfreev (colors); g_free (palette_text);
    vte_terminal_set_cursor_blink_mode (self->terminal, g_settings_get_enum (self->profile_settings, "cursor-blink-mode"));
    vte_terminal_set_cursor_shape (self->terminal, g_settings_get_enum (self->profile_settings, "cursor-shape"));
    vte_terminal_set_audible_bell (self->terminal, !g_settings_get_boolean (self->profile_settings, "silent-bell"));
    vte_terminal_set_scroll_on_keystroke (self->terminal, g_settings_get_boolean (self->profile_settings, "scroll-on-keystroke"));
    vte_terminal_set_scroll_on_output (self->terminal, g_settings_get_boolean (self->profile_settings, "scroll-on-output"));
    vte_terminal_set_scrollback_lines (self->terminal,
        g_settings_get_boolean (self->profile_settings, "scrollback-unlimited") ? -1 :
        g_settings_get_int (self->profile_settings, "scrollback-lines"));
}

static void settings_changed (GSettings *settings, gchar *key, PlumaTerminalPlugin *self) { reconfigure_terminal (self); }

static void style_updated (GtkWidget *widget, PlumaTerminalPlugin *self) {
    if (g_settings_get_boolean (self->profile_settings, "use-theme-colors"))
        reconfigure_terminal (self);
}

static void
spawn_terminal (PlumaTerminalPlugin *self)
{
    gchar *shell = vte_get_user_shell ();
    gchar *argv[] = { shell != NULL ? shell : (gchar *) "/bin/sh", NULL };
    vte_terminal_spawn_async (self->terminal, VTE_PTY_DEFAULT, NULL, argv, NULL,
                              G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, NULL, NULL);
    g_free (shell);
}

static void child_exited (VteTerminal *terminal, gint status, PlumaTerminalPlugin *self) { spawn_terminal (self); }

static void copy_terminal (GtkMenuItem *item, PlumaTerminalPlugin *self) {
    vte_terminal_copy_clipboard_format (self->terminal, VTE_FORMAT_TEXT); gtk_widget_grab_focus (GTK_WIDGET (self->terminal));
}
static void paste_terminal (GtkMenuItem *item, PlumaTerminalPlugin *self) {
    vte_terminal_paste_clipboard (self->terminal); gtk_widget_grab_focus (GTK_WIDGET (self->terminal));
}

static gchar *
active_document_directory (PlumaTerminalPlugin *self)
{
    PlumaDocument *document = pluma_window_get_active_document (self->window);
    GFile *location, *parent;
    gchar *path;
    if (document == NULL || (location = pluma_document_get_location (document)) == NULL) return NULL;
    if (!g_file_is_native (location)) { g_object_unref (location); return NULL; }
    parent = g_file_get_parent (location); path = parent != NULL ? g_file_get_path (parent) : NULL;
    g_clear_object (&parent); g_object_unref (location); return path;
}

static void change_directory (GtkMenuItem *item, PlumaTerminalPlugin *self) {
    gchar *path = active_document_directory (self);
    if (path != NULL) { gchar *quoted = g_shell_quote (path); gchar *command = g_strdup_printf ("cd %s\n", quoted);
        vte_terminal_feed_child (self->terminal, command, -1); g_free (command); g_free (quoted); g_free (path); }
    gtk_widget_grab_focus (GTK_WIDGET (self->terminal));
}

static GtkWidget *
create_popup (PlumaTerminalPlugin *self)
{
    GtkWidget *menu = gtk_menu_new (), *item;
    item = gtk_menu_item_new_with_mnemonic (_("_Copy")); gtk_widget_set_sensitive (item, vte_terminal_get_has_selection (self->terminal));
    g_signal_connect (item, "activate", G_CALLBACK (copy_terminal), self); gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    item = gtk_menu_item_new_with_mnemonic (_("_Paste")); g_signal_connect (item, "activate", G_CALLBACK (paste_terminal), self);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item); gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());
    item = gtk_menu_item_new_with_mnemonic (_("C_hange Directory"));
    { gchar *path = active_document_directory (self); gtk_widget_set_sensitive (item, path != NULL); g_free (path); }
    g_signal_connect (item, "activate", G_CALLBACK (change_directory), self); gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    gtk_widget_show_all (menu); return menu;
}

static gboolean button_press (GtkWidget *widget, GdkEventButton *event, PlumaTerminalPlugin *self) {
    if (event->button != GDK_BUTTON_SECONDARY) return FALSE;
    gtk_menu_popup_at_pointer (GTK_MENU (create_popup (self)), (GdkEvent *) event); return TRUE;
}
static gboolean popup_menu (GtkWidget *widget, PlumaTerminalPlugin *self) {
    GtkWidget *menu = create_popup (self); gtk_menu_popup_at_widget (GTK_MENU (menu), widget, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST, NULL); return TRUE;
}

static gboolean key_press (GtkWidget *widget, GdkEventKey *event, PlumaTerminalPlugin *self) {
    GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask ();
    if (event->keyval == GDK_KEY_c && modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) { copy_terminal (NULL, self); return TRUE; }
    if (event->keyval == GDK_KEY_v && modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) { paste_terminal (NULL, self); return TRUE; }
    if ((event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab) && (modifiers & GDK_CONTROL_MASK)) {
        gtk_widget_child_focus (gtk_widget_get_toplevel (widget), (modifiers & GDK_SHIFT_MASK) ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD); return TRUE;
    }
    return FALSE;
}

static void drag_received (GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time, PlumaTerminalPlugin *self) {
    gchar **uris = gtk_selection_data_get_uris (data); GString *text = g_string_new (NULL); guint i;
    for (i = 0; uris != NULL && uris[i] != NULL; i++) { GFile *file = g_file_new_for_uri (uris[i]); gchar *path = g_file_get_path (file);
        if (path != NULL) { gchar *quoted = g_shell_quote (path); if (text->len > 0) g_string_append_c (text, ' '); g_string_append (text, quoted); g_free (quoted); g_free (path); } g_object_unref (file); }
    if (text->len > 0) vte_terminal_feed_child (self->terminal, text->str, text->len);
    gtk_drag_finish (context, text->len > 0, FALSE, time); g_string_free (text, TRUE); g_strfreev (uris);
}

static void
create_terminal_panel (PlumaTerminalPlugin *self)
{
    GtkTargetList *targets;
    GtkWidget *scrollbar;
    self->panel = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    self->terminal = VTE_TERMINAL (vte_terminal_new ());
    vte_terminal_set_size (self->terminal, vte_terminal_get_column_count (self->terminal), 5);
    gtk_widget_set_size_request (GTK_WIDGET (self->terminal), 200, 50);
    gtk_box_pack_start (GTK_BOX (self->panel), GTK_WIDGET (self->terminal), TRUE, TRUE, 0);
    scrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (self->terminal)));
    gtk_box_pack_start (GTK_BOX (self->panel), scrollbar, FALSE, FALSE, 0);
    targets = gtk_target_list_new (NULL, 0); gtk_target_list_add_uri_targets (targets, TARGET_URI_LIST);
    gtk_drag_dest_set (GTK_WIDGET (self->terminal), GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_set_target_list (GTK_WIDGET (self->terminal), targets); gtk_target_list_unref (targets);
    g_signal_connect (self->terminal, "child-exited", G_CALLBACK (child_exited), self);
    g_signal_connect (self->terminal, "key-press-event", G_CALLBACK (key_press), self);
    g_signal_connect (self->terminal, "button-press-event", G_CALLBACK (button_press), self);
    g_signal_connect (self->terminal, "popup-menu", G_CALLBACK (popup_menu), self);
    g_signal_connect (self->terminal, "drag-data-received", G_CALLBACK (drag_received), self);
    g_signal_connect (self->terminal, "style-updated", G_CALLBACK (style_updated), self);
    gtk_widget_show_all (self->panel);
}

static void activate (PlumaWindowActivatable *activatable) {
    PlumaTerminalPlugin *self = PLUMA_TERMINAL_PLUGIN (activatable);
    self->profile_settings = create_profile_settings (); self->interface_settings = g_settings_new ("org.mate.interface");
    create_terminal_panel (self); reconfigure_terminal (self); spawn_terminal (self);
    self->profile_changed = g_signal_connect (self->profile_settings, "changed", G_CALLBACK (settings_changed), self);
    self->font_changed = g_signal_connect (self->interface_settings, "changed::monospace-font-name", G_CALLBACK (settings_changed), self);
    pluma_panel_add_item_with_icon (pluma_window_get_bottom_panel (self->window), self->panel, _("Terminal"), "utilities-terminal-symbolic");
}
static void deactivate (PlumaWindowActivatable *activatable) {
    PlumaTerminalPlugin *self = PLUMA_TERMINAL_PLUGIN (activatable);
    pluma_panel_remove_item (pluma_window_get_bottom_panel (self->window), self->panel); self->panel = NULL; self->terminal = NULL;
    g_clear_object (&self->profile_settings); g_clear_object (&self->interface_settings);
}
static void update_state (PlumaWindowActivatable *activatable) {}
static void set_property (GObject *o, guint id, const GValue *v, GParamSpec *p) { if (id == PROP_WINDOW) PLUMA_TERMINAL_PLUGIN(o)->window=g_value_dup_object(v); else G_OBJECT_WARN_INVALID_PROPERTY_ID(o,id,p); }
static void get_property (GObject *o, guint id, GValue *v, GParamSpec *p) { if (id == PROP_WINDOW) g_value_set_object(v,PLUMA_TERMINAL_PLUGIN(o)->window); else G_OBJECT_WARN_INVALID_PROPERTY_ID(o,id,p); }
static void dispose (GObject *o) { PlumaTerminalPlugin *self=PLUMA_TERMINAL_PLUGIN(o);g_clear_object(&self->profile_settings);g_clear_object(&self->interface_settings);g_clear_object(&self->window);G_OBJECT_CLASS(pluma_terminal_plugin_parent_class)->dispose(o); }
static void pluma_terminal_plugin_init (PlumaTerminalPlugin *self) {}
static void pluma_terminal_plugin_class_finalize (PlumaTerminalPluginClass *klass) {}
static void pluma_terminal_plugin_class_init (PlumaTerminalPluginClass *klass) { GObjectClass *c=G_OBJECT_CLASS(klass);c->set_property=set_property;c->get_property=get_property;c->dispose=dispose;g_object_class_override_property(c,PROP_WINDOW,"window"); }
static void window_activatable_iface_init (PlumaWindowActivatableInterface *iface) { iface->activate=activate;iface->deactivate=deactivate;iface->update_state=update_state; }
G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module) { pluma_terminal_plugin_register_type(G_TYPE_MODULE(module));peas_object_module_register_extension_type(module,PLUMA_TYPE_WINDOW_ACTIVATABLE,PLUMA_TYPE_TERMINAL_PLUGIN); }
