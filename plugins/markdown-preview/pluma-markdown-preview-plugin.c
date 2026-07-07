#include "config.h"
#include "pluma-markdown-preview-plugin.h"
#include "markdown-ast.h"
#include "markdown-parser.h"
#include "markdown-renderer-gtk.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-panel.h>
#include <pluma/pluma-window.h>
#include <pluma/pluma-window-activatable.h>

#define UPDATE_DELAY_MS 500

struct _PlumaMarkdownPreviewPlugin
{
    PeasExtensionBase parent_instance;
    PlumaWindow *window;
    GtkWidget *preview;      /* GtkScrolledWindow */
    GtkWidget *viewport;     /* GtkViewport */
    GtkWidget *container;    /* GtkBox (vertical) */
    GtkActionGroup *action_group;
    guint ui_id;
    guint update_id;
    PlumaDocument *document;
    gulong changed_id;
    GtkCssProvider *css_provider;
};

static void window_activatable_iface_init (PlumaWindowActivatableInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaMarkdownPreviewPlugin,
                                pluma_markdown_preview_plugin,
                                PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                               window_activatable_iface_init))

enum { PROP_0, PROP_WINDOW };

static gboolean
is_dark_theme (GtkWidget *widget)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    GdkRGBA *fg = NULL;
    gboolean is_dark = FALSE;

    gtk_style_context_get (context, GTK_STATE_FLAG_NORMAL,
                           GTK_STYLE_PROPERTY_COLOR, &fg,
                           NULL);

    if (fg != NULL) {
        double luminance = 0.2126 * fg->red + 0.7152 * fg->green + 0.0722 * fg->blue;
        is_dark = (luminance > 0.5);
        gdk_rgba_free (fg);
    }

    return is_dark;
}

static gboolean
is_markdown (PlumaDocument *document)
{
    GtkSourceLanguage *language;
    GFile *location;
    gchar *basename = NULL;
    gchar *name;
    gboolean result = FALSE;

    if (document == NULL)
        return FALSE;

    language = pluma_document_get_language (document);
    if (language != NULL && g_strcmp0 (gtk_source_language_get_id (language), "markdown") == 0)
        return TRUE;

    location = pluma_document_get_location (document);
    if (location == NULL)
        return FALSE;
    if (g_file_peek_path (location) == NULL)
        basename = g_file_get_basename (location);
    name = g_ascii_strdown (basename != NULL ? basename : g_file_peek_path (location), -1);
    result = g_str_has_suffix (name, ".md") || g_str_has_suffix (name, ".markdown");
    g_free (basename);
    g_free (name);
    return result;
}

static void
load_welcome (PlumaMarkdownPreviewPlugin *self)
{
    gtk_container_foreach (GTK_CONTAINER (self->container), (GtkCallback) gtk_widget_destroy, NULL);

    gboolean is_dark = is_dark_theme (self->container);

    GtkWidget *welcome_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign (welcome_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (welcome_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top (welcome_box, 48);

    GdkPixbuf *pixbuf = NULL;
    GtkIconTheme *theme = gtk_icon_theme_get_default ();
    if (gtk_icon_theme_has_icon (theme, "text-x-markdown")) {
        pixbuf = gtk_icon_theme_load_icon (theme, "text-x-markdown", 64, 0, NULL);
    } else if (gtk_icon_theme_has_icon (theme, "accessories-text-editor")) {
        pixbuf = gtk_icon_theme_load_icon (theme, "accessories-text-editor", 64, 0, NULL);
    } else {
        pixbuf = gtk_icon_theme_load_icon (theme, "document-open", 64, 0, NULL);
    }

    if (pixbuf != NULL) {
        GtkWidget *img = gtk_image_new_from_pixbuf (pixbuf);
        gtk_box_pack_start (GTK_BOX (welcome_box), img, FALSE, FALSE, 0);
        g_object_unref (pixbuf);
    }

    GtkWidget *title_lbl = gtk_label_new (NULL);
    gchar *title_markup = g_strdup_printf ("<span size=\"x-large\" weight=\"bold\" foreground=\"%s\">Markdown Preview</span>",
                                           is_dark ? "#8ab4f8" : "#1d3557");
    gtk_label_set_markup (GTK_LABEL (title_lbl), title_markup);
    gtk_box_pack_start (GTK_BOX (welcome_box), title_lbl, FALSE, FALSE, 0);
    g_free (title_markup);

    GtkWidget *desc_lbl = gtk_label_new (_("Open a Markdown file (.md or .markdown) to preview it."));
    gtk_box_pack_start (GTK_BOX (welcome_box), desc_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (self->container), welcome_box, TRUE, TRUE, 0);
    gtk_widget_show_all (self->container);
}

static gboolean
update_preview (gpointer data)
{
    PlumaMarkdownPreviewPlugin *self = data;
    GtkTextIter start, end;
    gchar *text;
    GFile *doc_location = NULL;
    gboolean is_dark = FALSE;

    self->update_id = 0;

    if (self->container == NULL) {
        return G_SOURCE_REMOVE;
    }

    is_dark = is_dark_theme (self->container);

    GtkStyleContext *preview_ctx = gtk_widget_get_style_context (self->preview);
    if (is_dark) {
        gtk_style_context_remove_class (preview_ctx, "markdown-view-light");
        gtk_style_context_add_class (preview_ctx, "markdown-view-dark");
    } else {
        gtk_style_context_remove_class (preview_ctx, "markdown-view-dark");
        gtk_style_context_add_class (preview_ctx, "markdown-view-light");
    }

    if (self->document != NULL) {
        doc_location = pluma_document_get_location (self->document);
    }

    gtk_container_foreach (GTK_CONTAINER (self->container), (GtkCallback) gtk_widget_destroy, NULL);

    if (!is_markdown (self->document)) {
        load_welcome (self);
        return G_SOURCE_REMOVE;
    }

    gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->document), &start, &end);
    text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (self->document), &start, &end, FALSE);

    /* 1. Parse markdown text into AST */
    ASTNode *ast_root = markdown_parser_parse (text);
    g_free (text);

    /* 2. Render AST onto GTK widgets */
    markdown_renderer_gtk_render (GTK_BOX (self->container), ast_root, is_dark, doc_location);

    /* 3. Clean up AST */
    ast_node_free (ast_root);

    gtk_widget_show_all (self->container);

    return G_SOURCE_REMOVE;
}

static void
schedule_update (PlumaMarkdownPreviewPlugin *self)
{
    if (self->update_id != 0)
        g_source_remove (self->update_id);
    self->update_id = g_timeout_add (UPDATE_DELAY_MS, update_preview, self);
}

static void
document_changed (GtkTextBuffer *buffer, PlumaMarkdownPreviewPlugin *self)
{
    schedule_update (self);
}

static void
set_document (PlumaMarkdownPreviewPlugin *self, PlumaDocument *document)
{
    if (self->document == document)
        return;
    if (self->document != NULL && self->changed_id != 0)
        g_signal_handler_disconnect (self->document, self->changed_id);
    g_set_object (&self->document, document);
    self->changed_id = document == NULL ? 0 :
        g_signal_connect (document, "changed", G_CALLBACK (document_changed), self);
}

static void
show_preview (GtkAction *action, PlumaMarkdownPreviewPlugin *self)
{
    PlumaPanel *panel = pluma_window_get_right_panel (self->window);
    gtk_widget_show (GTK_WIDGET (panel));
    pluma_panel_activate_item (panel, self->preview);
    update_preview (self);
}

static void
style_updated_cb (GtkWidget *widget, gpointer data)
{
    PlumaMarkdownPreviewPlugin *self = data;
    update_preview (self);
}

static void
activate (PlumaWindowActivatable *activatable)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (activatable);
    PlumaPanel *panel = pluma_window_get_right_panel (self->window);
    GtkUIManager *manager = pluma_window_get_ui_manager (self->window);
    const GtkActionEntry entries[] = {
        { "MarkdownPreview", NULL, N_("Show Markdown Preview"), NULL,
          N_("Preview the current Markdown document"), G_CALLBACK (show_preview) },
        { "MarkdownUpdate", NULL, N_("Update Markdown Preview"), NULL,
          N_("Update the Markdown preview"), G_CALLBACK (show_preview) }
    };

    self->preview = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_name (self->preview, "markdown-preview-panel");

    self->viewport = gtk_viewport_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (self->preview), self->viewport);

    self->container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start (self->container, 16);
    gtk_widget_set_margin_end (self->container, 16);
    gtk_widget_set_margin_top (self->container, 16);
    gtk_widget_set_margin_bottom (self->container, 16);

    gtk_container_add (GTK_CONTAINER (self->viewport), self->container);

    self->css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (self->css_provider,
        "#markdown-preview-panel .markdown-code-wrapper {"
        "  margin-bottom: 12px;"
        "}"
        "#markdown-preview-panel .markdown-code-label {"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  color: #888888;"
        "  margin-bottom: 4px;"
        "  margin-left: 4px;"
        "}"
        "#markdown-preview-panel .markdown-code-block,"
        "#markdown-preview-panel .markdown-code-block textview,"
        "#markdown-preview-panel .markdown-code-block textview text {"
        "  border: none;"
        "  box-shadow: none;"
        "  background-image: none;"
        "  border-radius: 6px;"
        "  font-family: monospace;"
        "}"
        "#markdown-preview-panel .markdown-code-light,"
        "#markdown-preview-panel .markdown-code-light textview,"
        "#markdown-preview-panel .markdown-code-light textview text {"
        "  background-color: #f6f8fa;"
        "  color: #24292e;"
        "}"
        "#markdown-preview-panel .markdown-code-dark,"
        "#markdown-preview-panel .markdown-code-dark textview,"
        "#markdown-preview-panel .markdown-code-dark textview text {"
        "  background-color: #1c2128;"
        "  color: #e1e4e8;"
        "}"
        "#markdown-preview-panel .markdown-table {"
        "  border: none;"
        "}"
        "#markdown-preview-panel .markdown-table-cell-light {"
        "  border: 1px solid #e1e4e8;"
        "  padding: 8px 12px;"
        "}"
        "#markdown-preview-panel .markdown-table-cell-dark {"
        "  border: 1px solid #444c56;"
        "  padding: 8px 12px;"
        "}"
        "#markdown-preview-panel .markdown-table-header-light {"
        "  background-color: #f6f8fa;"
        "  color: #1f2328;"
        "}"
        "#markdown-preview-panel .markdown-table-header-dark {"
        "  background-color: #21262d;"
        "  color: #c9d1d9;"
        "}"
        "#markdown-preview-panel .markdown-blockquote {"
        "  border-left: 4px solid #3465a4;"
        "  padding-left: 12px;"
        "  margin-left: 8px;"
        "  margin-bottom: 8px;"
        "}"
        "#markdown-preview-panel .markdown-blockquote-nested {"
        "  border-left: 4px solid #729fcf;"
        "  padding-left: 12px;"
        "  margin-left: 16px;"
        "  margin-bottom: 6px;"
        "}"
        "#markdown-preview-panel .markdown-blockquote label {"
        "  font-style: italic;"
        "}"
        "#markdown-preview-panel.markdown-view-light .markdown-blockquote label {"
        "  color: #5f6368;"
        "}"
        "#markdown-preview-panel.markdown-view-dark .markdown-blockquote label {"
        "  color: #9aa0a6;"
        "}"
        "#markdown-preview-panel .markdown-heading {"
        "  font-weight: bold;"
        "}"
        "#markdown-preview-panel.markdown-view-light .markdown-heading {"
        "  color: #1a365d;"
        "}"
        "#markdown-preview-panel.markdown-view-dark .markdown-heading {"
        "  color: #8cb4f9;"
        "}"
        "#markdown-preview-panel .markdown-heading-1 {"
        "  font-size: 22px;"
        "  margin-top: 18px;"
        "  margin-bottom: 6px;"
        "}"
        "#markdown-preview-panel .markdown-heading-2 {"
        "  font-size: 18px;"
        "  margin-top: 14px;"
        "  margin-bottom: 4px;"
        "}"
        "#markdown-preview-panel .markdown-heading-3 {"
        "  font-size: 15px;"
        "  margin-top: 10px;"
        "  margin-bottom: 4px;"
        "}"
        "#markdown-preview-panel .markdown-sep-light {"
        "  background-color: #e1e4e8;"
        "  min-height: 1px;"
        "}"
        "#markdown-preview-panel .markdown-sep-dark {"
        "  background-color: #444c56;"
        "  min-height: 1px;"
        "}",
        -1,
        NULL);

    /* Apply CSS styles ONLY inside the preview panel context using screen provider but scoped selector */
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                               GTK_STYLE_PROVIDER (self->css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_signal_connect (self->container, "style-updated", G_CALLBACK (style_updated_cb), self);

    pluma_panel_add_item_with_icon (panel, self->preview, _("Markdown Preview"), "text-x-markdown");
    gtk_widget_show_all (self->preview);
    load_welcome (self);

    self->action_group = gtk_action_group_new ("MarkdownPreviewActions");
    gtk_action_group_set_translation_domain (self->action_group, GETTEXT_PACKAGE);
    gtk_action_group_add_actions (self->action_group, entries, G_N_ELEMENTS (entries), self);
    gtk_ui_manager_insert_action_group (manager, self->action_group, -1);
    self->ui_id = gtk_ui_manager_new_merge_id (manager);
    gtk_ui_manager_add_ui (manager, self->ui_id, "/MenuBar/ToolsMenu/ToolsOps_4",
                           "MarkdownPreview", "MarkdownPreview", GTK_UI_MANAGER_MENUITEM, FALSE);
    gtk_ui_manager_add_ui (manager, self->ui_id, "/MenuBar/ToolsMenu/ToolsOps_4",
                           "MarkdownUpdate", "MarkdownUpdate", GTK_UI_MANAGER_MENUITEM, FALSE);
}

static void
deactivate (PlumaWindowActivatable *activatable)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (activatable);
    GtkUIManager *manager = pluma_window_get_ui_manager (self->window);
    if (self->update_id != 0) {
        g_source_remove (self->update_id);
        self->update_id = 0;
    }
    set_document (self, NULL);

    if (self->css_provider != NULL) {
        gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
                                                      GTK_STYLE_PROVIDER (self->css_provider));
        g_clear_object (&self->css_provider);
    }

    gtk_ui_manager_remove_ui (manager, self->ui_id);
    gtk_ui_manager_remove_action_group (manager, self->action_group);
    g_clear_object (&self->action_group);
    pluma_panel_remove_item (pluma_window_get_right_panel (self->window), self->preview);
    self->preview = NULL;
    self->viewport = NULL;
    self->container = NULL;
}

static void
update_state (PlumaWindowActivatable *activatable)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (activatable);
    set_document (self, pluma_window_get_active_document (self->window));
    schedule_update (self);
}

static void
pluma_markdown_preview_plugin_set_property (GObject *object, guint id,
                                            const GValue *value, GParamSpec *pspec)
{
    if (id == PROP_WINDOW)
        PLUMA_MARKDOWN_PREVIEW_PLUGIN (object)->window = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}

static void
pluma_markdown_preview_plugin_get_property (GObject *object, guint id,
                                            GValue *value, GParamSpec *pspec)
{
    if (id == PROP_WINDOW)
        g_value_set_object (value, PLUMA_MARKDOWN_PREVIEW_PLUGIN (object)->window);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}

static void
pluma_markdown_preview_plugin_dispose (GObject *object)
{
    PlumaMarkdownPreviewPlugin *self = PLUMA_MARKDOWN_PREVIEW_PLUGIN (object);
    g_clear_object (&self->document);
    g_clear_object (&self->window);
    G_OBJECT_CLASS (pluma_markdown_preview_plugin_parent_class)->dispose (object);
}

static void pluma_markdown_preview_plugin_init (PlumaMarkdownPreviewPlugin *self) {}
static void pluma_markdown_preview_plugin_class_finalize (PlumaMarkdownPreviewPluginClass *klass) {}

static void
pluma_markdown_preview_plugin_class_init (PlumaMarkdownPreviewPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->set_property = pluma_markdown_preview_plugin_set_property;
    object_class->get_property = pluma_markdown_preview_plugin_get_property;
    object_class->dispose = pluma_markdown_preview_plugin_dispose;
    g_object_class_override_property (object_class, PROP_WINDOW, "window");
}

static void
window_activatable_iface_init (PlumaWindowActivatableInterface *iface)
{
    iface->activate = activate;
    iface->deactivate = deactivate;
    iface->update_state = update_state;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
    pluma_markdown_preview_plugin_register_type (G_TYPE_MODULE (module));
    peas_object_module_register_extension_type (module, PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                PLUMA_TYPE_MARKDOWN_PREVIEW_PLUGIN);
}
