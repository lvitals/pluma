#include "config.h"
#include "pluma-snippets-plugin.h"

#include <errno.h>
#include <archive.h>
#include <archive_entry.h>
#include <glib/gi18n-lib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libpeas-gtk/peas-gtk-configurable.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-view.h>
#include <pluma/pluma-window.h>
#include <pluma/pluma-window-activatable.h>

typedef struct
{
    gchar *language;
    gchar *tag;
    gchar *text;
    gchar *description;
    gchar *accelerator;
    gchar **drop_targets;
} Snippet;

typedef struct { guint index; glong start; glong end; gboolean mirror; } PlaceholderSpec;
typedef struct { gchar *text; GArray *placeholders; glong final_cursor; } Expansion;
typedef struct { guint index; GtkTextMark *start; GtkTextMark *end; } Placeholder;
typedef struct _SnippetsProvider SnippetsProvider;
typedef struct _SnippetsProviderClass SnippetsProviderClass;

#define TYPE_SNIPPETS_PROVIDER (snippets_provider_get_type ())
#define SNIPPETS_PROVIDER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_SNIPPETS_PROVIDER, SnippetsProvider))

struct _SnippetsProvider { GObject parent_instance; PlumaSnippetsPlugin *plugin; GtkTextMark *start_mark; };
struct _SnippetsProviderClass { GObjectClass parent_class; };

struct _PlumaSnippetsPlugin
{
    PeasExtensionBase parent_instance;
    PlumaWindow *window;
    PlumaView *view;
    gulong key_handler;
    GHashTable *snippets;
    GPtrArray *placeholders;
    GPtrArray *mirrors;
    gint active_placeholder;
    gulong buffer_changed_handler;
    gulong drop_handler;
    gboolean updating_mirrors;
    GtkTextMark *final_mark;
    GHashTable *drop_environment;
    GtkAccelGroup *accel_group;
    SnippetsProvider *provider;
    gchar *data_dir;
    gchar *lua_program;
    gchar *lua_runtime;
};

static void window_activatable_iface_init (PlumaWindowActivatableInterface *iface);
static void completion_provider_iface_init (GtkSourceCompletionProviderIface *iface);
static void configurable_iface_init (PeasGtkConfigurableInterface *iface);
static void install_accelerators (PlumaSnippetsPlugin *self);

GType snippets_provider_get_type (void);
G_DEFINE_DYNAMIC_TYPE_EXTENDED (SnippetsProvider, snippets_provider, G_TYPE_OBJECT, 0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (GTK_SOURCE_TYPE_COMPLETION_PROVIDER,
                                                               completion_provider_iface_init))

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaSnippetsPlugin, pluma_snippets_plugin,
                                PEAS_TYPE_EXTENSION_BASE, 0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PLUMA_TYPE_WINDOW_ACTIVATABLE,
                                                               window_activatable_iface_init)
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PEAS_GTK_TYPE_CONFIGURABLE,
                                                               configurable_iface_init))

enum { PROP_0, PROP_WINDOW };

static void
snippet_free (Snippet *snippet)
{
    g_free (snippet->text);
    g_free (snippet->language);
    g_free (snippet->tag);
    g_free (snippet->description);
    g_free (snippet->accelerator);
    g_strfreev (snippet->drop_targets);
    g_free (snippet);
}

static gchar *
snippet_key (const gchar *language, const gchar *tag)
{
    return g_strdup_printf ("%s\037%s", language != NULL ? language : "global", tag);
}

static gchar *
language_from_filename (const gchar *filename)
{
    gchar *base = g_path_get_basename (filename);
    gchar *dot = strrchr (base, '.');
    if (dot != NULL)
        *dot = '\0';
    if (g_strcmp0 (base, "snippets") == 0 || g_strcmp0 (base, "global") == 0) {
        g_free (base);
        return g_strdup ("global");
    }
    return base;
}

static gchar *
xml_child_text (xmlNode *node, const gchar *name)
{
    xmlNode *child;
    for (child = node->children; child != NULL; child = child->next) {
        xmlChar *content;
        gchar *result;
        if (child->type != XML_ELEMENT_NODE || xmlStrcmp (child->name, BAD_CAST name) != 0)
            continue;
        content = xmlNodeGetContent (child);
        result = g_strdup ((const gchar *) content);
        xmlFree (content);
        return result;
    }
    return NULL;
}

static void
load_snippet_file (PlumaSnippetsPlugin *self, const gchar *filename)
{
    xmlDoc *document = xmlReadFile (filename, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    xmlNode *node;
    gchar *language;

    if (document == NULL)
        return;
    language = language_from_filename (filename);
    for (node = xmlDocGetRootElement (document)->children; node != NULL; node = node->next) {
        gchar *tag, *key;
        Snippet *snippet;
        if (node->type != XML_ELEMENT_NODE || xmlStrcmp (node->name, BAD_CAST "snippet") != 0)
            continue;
        tag = xml_child_text (node, "tag");
        if (tag == NULL || *tag == '\0') {
            g_free (tag);
            continue;
        }
        snippet = g_new0 (Snippet, 1);
        snippet->language = g_strdup (language);
        snippet->tag = g_strdup (tag);
        snippet->text = xml_child_text (node, "text");
        snippet->description = xml_child_text (node, "description");
        snippet->accelerator = xml_child_text (node, "accelerator");
        { gchar *targets = xml_child_text (node, "drop-targets");
          if (targets != NULL && *targets != '\0') snippet->drop_targets = g_strsplit_set (targets, ",; ", -1);
          g_free (targets); }
        if (snippet->text == NULL) {
            snippet_free (snippet);
            g_free (tag);
            continue;
        }
        key = snippet_key (language, tag);
        g_hash_table_replace (self->snippets, key, snippet);
        g_free (tag);
    }
    g_free (language);
    xmlFreeDoc (document);
}

static void
load_directory (PlumaSnippetsPlugin *self, const gchar *directory)
{
    GDir *dir;
    const gchar *name;
    dir = g_dir_open (directory, 0, NULL);
    if (dir == NULL)
        return;
    while ((name = g_dir_read_name (dir)) != NULL) {
        gchar *path;
        if (!g_str_has_suffix (name, ".xml"))
            continue;
        path = g_build_filename (directory, name, NULL);
        load_snippet_file (self, path);
        g_free (path);
    }
    g_dir_close (dir);
}

enum { MANAGER_COL_LANGUAGE, MANAGER_COL_TAG, MANAGER_COL_DESCRIPTION, MANAGER_COL_KEY, MANAGER_N_COLUMNS };
typedef struct {
    PlumaSnippetsPlugin *plugin;
    GtkListStore *store;
    GtkWidget *tree;
    GtkWidget *tag;
    GtkWidget *description;
    GtkWidget *accelerator;
    GtkWidget *drop_targets;
    GtkWidget *text;
    gchar *selected_key;
} SnippetsManager;

static gboolean
write_language_file (PlumaSnippetsPlugin *self, const gchar *language, GError **error)
{
    gchar *directory = g_build_filename (g_get_user_config_dir (), "pluma", "snippets", NULL);
    gchar *filename = g_strdup_printf ("%s.xml", language);
    gchar *path = g_build_filename (directory, filename, NULL);
    xmlDoc *document = xmlNewDoc (BAD_CAST "1.0");
    xmlNode *root = xmlNewNode (NULL, BAD_CAST "snippets");
    GHashTableIter iter;
    gpointer value;
    gboolean success;

    if (g_strcmp0 (language, "global") != 0)
        xmlNewProp (root, BAD_CAST "language", BAD_CAST language);
    xmlDocSetRootElement (document, root);
    g_hash_table_iter_init (&iter, self->snippets);
    while (g_hash_table_iter_next (&iter, NULL, &value)) {
        Snippet *snippet = value;
        xmlNode *node;
        if (g_strcmp0 (snippet->language, language) != 0)
            continue;
        node = xmlNewChild (root, NULL, BAD_CAST "snippet", NULL);
        xmlNewTextChild (node, NULL, BAD_CAST "text", BAD_CAST snippet->text);
        xmlNewTextChild (node, NULL, BAD_CAST "tag", BAD_CAST snippet->tag);
        xmlNewTextChild (node, NULL, BAD_CAST "description", BAD_CAST snippet->description);
        if (snippet->accelerator != NULL && *snippet->accelerator != '\0')
            xmlNewTextChild (node, NULL, BAD_CAST "accelerator", BAD_CAST snippet->accelerator);
        if (snippet->drop_targets != NULL) {
            gchar *targets = g_strjoinv (",", snippet->drop_targets);
            xmlNewTextChild (node, NULL, BAD_CAST "drop-targets", BAD_CAST targets); g_free (targets);
        }
    }
    if (g_mkdir_with_parents (directory, 0700) != 0) {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                     _("Cannot create snippets directory: %s"), g_strerror (errno));
        success = FALSE;
    } else {
        success = xmlSaveFormatFileEnc (path, document, "UTF-8", 1) >= 0;
        if (!success)
            g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                         _("Cannot save snippets file “%s”"), path);
    }
    xmlFreeDoc (document);
    g_free (path); g_free (filename); g_free (directory);
    return success;
}

static void
manager_populate (SnippetsManager *manager)
{
    GHashTableIter iter;
    gpointer key, value;
    gtk_list_store_clear (manager->store);
    g_hash_table_iter_init (&iter, manager->plugin->snippets);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        Snippet *snippet = value;
        GtkTreeIter row;
        gtk_list_store_append (manager->store, &row);
        gtk_list_store_set (manager->store, &row,
                            MANAGER_COL_LANGUAGE, snippet->language,
                            MANAGER_COL_TAG, snippet->tag,
                            MANAGER_COL_DESCRIPTION, snippet->description,
                            MANAGER_COL_KEY, key, -1);
    }
}

static void
manager_selection_changed (GtkTreeSelection *selection, SnippetsManager *manager)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *key = NULL;
    Snippet *snippet;
    if (!gtk_tree_selection_get_selected (selection, &model, &iter))
        return;
    gtk_tree_model_get (model, &iter, MANAGER_COL_KEY, &key, -1);
    snippet = g_hash_table_lookup (manager->plugin->snippets, key);
    if (snippet != NULL) {
        gtk_entry_set_text (GTK_ENTRY (manager->tag), snippet->tag);
        gtk_entry_set_text (GTK_ENTRY (manager->description), snippet->description != NULL ? snippet->description : "");
        gtk_entry_set_text (GTK_ENTRY (manager->accelerator), snippet->accelerator != NULL ? snippet->accelerator : "");
        { gchar *targets = snippet->drop_targets != NULL ? g_strjoinv (",", snippet->drop_targets) : g_strdup ("");
          gtk_entry_set_text (GTK_ENTRY (manager->drop_targets), targets); g_free (targets); }
        gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (manager->text)), snippet->text, -1);
    }
    g_free (manager->selected_key);
    manager->selected_key = key;
}

static void
show_manager_error (SnippetsManager *manager, const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (manager->plugin->window), GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

static void
manager_save (GtkButton *button, SnippetsManager *manager)
{
    Snippet *snippet;
    GtkTextIter start, end;
    GtkTextBuffer *buffer;
    gchar *new_text, *new_tag, *new_key;
    GError *error = NULL;
    if (manager->selected_key == NULL)
        return;
    snippet = g_hash_table_lookup (manager->plugin->snippets, manager->selected_key);
    if (snippet == NULL)
        return;
    new_tag = g_strdup (gtk_entry_get_text (GTK_ENTRY (manager->tag)));
    if (*new_tag == '\0' || strpbrk (new_tag, " \t\r\n") != NULL) {
        show_manager_error (manager, _("A snippet trigger must not be empty or contain whitespace."));
        g_free (new_tag);
        return;
    }
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (manager->text));
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    new_text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    g_free (snippet->tag); snippet->tag = new_tag;
    g_free (snippet->text); snippet->text = new_text;
    g_free (snippet->description); snippet->description = g_strdup (gtk_entry_get_text (GTK_ENTRY (manager->description)));
    g_free (snippet->accelerator); snippet->accelerator = g_strdup (gtk_entry_get_text (GTK_ENTRY (manager->accelerator)));
    g_strfreev (snippet->drop_targets);
    snippet->drop_targets = g_strsplit_set (gtk_entry_get_text (GTK_ENTRY (manager->drop_targets)), ",; ", -1);
    new_key = snippet_key (snippet->language, snippet->tag);
    if (g_strcmp0 (new_key, manager->selected_key) != 0) {
        g_hash_table_steal (manager->plugin->snippets, manager->selected_key);
        g_hash_table_replace (manager->plugin->snippets, g_strdup (new_key), snippet);
        g_free (manager->selected_key);
        manager->selected_key = g_strdup (new_key);
    }
    if (!write_language_file (manager->plugin, snippet->language, &error)) {
        show_manager_error (manager, error->message);
        g_clear_error (&error);
    }
    g_free (new_key);
    manager_populate (manager);
    install_accelerators (manager->plugin);
}

static void
manager_new (GtkButton *button, SnippetsManager *manager)
{
    PlumaDocument *document = pluma_window_get_active_document (manager->plugin->window);
    GtkSourceLanguage *source_language = document != NULL ? pluma_document_get_language (document) : NULL;
    const gchar *language = source_language != NULL ? gtk_source_language_get_id (source_language) : "global";
    gchar *tag = g_strdup ("new-snippet"), *key = snippet_key (language, tag);
    guint suffix = 2;
    while (g_hash_table_contains (manager->plugin->snippets, key)) {
        g_free (tag); g_free (key); tag = g_strdup_printf ("new-snippet-%u", suffix++); key = snippet_key (language, tag);
    }
    Snippet *snippet = g_new0 (Snippet, 1);
    snippet->language = g_strdup (language); snippet->tag = tag;
    snippet->description = g_strdup (_("New snippet")); snippet->text = g_strdup ("$0");
    snippet->accelerator = g_strdup ("");
    g_hash_table_insert (manager->plugin->snippets, key, snippet);
    write_language_file (manager->plugin, language, NULL);
    manager_populate (manager);
}

static void
manager_delete (GtkButton *button, SnippetsManager *manager)
{
    Snippet *snippet;
    gchar *language;
    if (manager->selected_key == NULL) return;
    snippet = g_hash_table_lookup (manager->plugin->snippets, manager->selected_key);
    if (snippet == NULL) return;
    language = g_strdup (snippet->language);
    g_hash_table_remove (manager->plugin->snippets, manager->selected_key);
    g_clear_pointer (&manager->selected_key, g_free);
    write_language_file (manager->plugin, language, NULL);
    g_free (language); manager_populate (manager); install_accelerators (manager->plugin);
}

static gboolean
valid_snippets_xml (const gchar *contents, gsize length)
{
    xmlDoc *document = xmlReadMemory (contents, length, "snippets.xml", NULL,
                                     XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    xmlNode *root;
    gboolean valid = FALSE;
    if (document == NULL)
        return FALSE;
    root = xmlDocGetRootElement (document);
    if (root != NULL && xmlStrcmp (root->name, BAD_CAST "snippets") == 0) {
        xmlNode *node;
        valid = TRUE;
        for (node = root->children; node != NULL; node = node->next) {
            gchar *tag, *text;
            if (node->type != XML_ELEMENT_NODE)
                continue;
            if (xmlStrcmp (node->name, BAD_CAST "snippet") != 0) { valid = FALSE; break; }
            tag = xml_child_text (node, "tag"); text = xml_child_text (node, "text");
            if (tag == NULL || *tag == '\0' || text == NULL) valid = FALSE;
            g_free (tag); g_free (text);
            if (!valid) break;
        }
    }
    xmlFreeDoc (document);
    return valid;
}

static gboolean
valid_snippets_document (const gchar *path)
{
    gchar *contents = NULL;
    gsize length = 0;
    gboolean valid = g_file_get_contents (path, &contents, &length, NULL) &&
                     valid_snippets_xml (contents, length);
    g_free (contents);
    return valid;
}

static gboolean
safe_archive_name (const gchar *name)
{
    gchar **parts;
    guint i;
    gboolean safe = name != NULL && !g_path_is_absolute (name) && g_str_has_suffix (name, ".xml");
    if (!safe)
        return FALSE;
    parts = g_strsplit (name, "/", -1);
    for (i = 0; parts[i] != NULL; i++)
        if (*parts[i] == '\0' || g_str_equal (parts[i], ".") || g_str_equal (parts[i], "..")) {
            safe = FALSE;
            break;
        }
    g_strfreev (parts);
    return safe;
}

static gboolean
import_archive (SnippetsManager *manager, const gchar *source, GError **error)
{
    struct archive *archive = archive_read_new ();
    struct archive_entry *entry;
    gchar *directory = g_build_filename (g_get_user_config_dir (), "pluma", "snippets", NULL);
    gboolean imported = FALSE;
    int status;

    archive_read_support_filter_all (archive);
    archive_read_support_format_tar (archive);
    if (archive_read_open_filename (archive, source, 10240) != ARCHIVE_OK) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s", archive_error_string (archive));
        archive_read_free (archive); g_free (directory); return FALSE;
    }
    g_mkdir_with_parents (directory, 0700);
    while ((status = archive_read_next_header (archive, &entry)) == ARCHIVE_OK) {
        const gchar *name = archive_entry_pathname (entry);
        la_int64_t size = archive_entry_size (entry);
        gchar *contents, *base, *destination;
        la_ssize_t count;
        gsize offset = 0;
        if (archive_entry_filetype (entry) != AE_IFREG || !safe_archive_name (name) ||
            size < 0 || size > 10 * 1024 * 1024) {
            archive_read_data_skip (archive);
            continue;
        }
        contents = g_malloc ((gsize) size + 1);
        while (offset < (gsize) size &&
               (count = archive_read_data (archive, contents + offset, (size_t) size - offset)) > 0)
            offset += (gsize) count;
        if (offset != (gsize) size || !valid_snippets_xml (contents, (gsize) size)) {
            g_free (contents);
            continue;
        }
        contents[size] = '\0';
        base = g_path_get_basename (name);
        destination = g_build_filename (directory, base, NULL);
        if (!g_file_set_contents (destination, contents, size, error)) {
            g_free (destination); g_free (base); g_free (contents);
            archive_read_close (archive); archive_read_free (archive); g_free (directory);
            return FALSE;
        }
        load_snippet_file (manager->plugin, destination);
        imported = TRUE;
        g_free (destination); g_free (base); g_free (contents);
    }
    if (status != ARCHIVE_EOF && *error == NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s", archive_error_string (archive));
    archive_read_close (archive); archive_read_free (archive); g_free (directory);
    if (!imported && *error == NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     _("The archive contains no valid snippets XML files."));
    return imported;
}

static void
manager_import (GtkButton *button, SnippetsManager *manager)
{
    GtkWidget *chooser = gtk_file_chooser_dialog_new (_("Import Snippets"), GTK_WINDOW (manager->plugin->window),
                                                      GTK_FILE_CHOOSER_ACTION_OPEN, _("Cancel"), GTK_RESPONSE_CANCEL,
                                                      _("Import"), GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _("Snippets XML files"));
    gtk_file_filter_add_pattern (filter, "*.xml");
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);
    filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _("Snippets archives"));
    gtk_file_filter_add_pattern (filter, "*.tar");
    gtk_file_filter_add_pattern (filter, "*.tar.gz");
    gtk_file_filter_add_pattern (filter, "*.tar.bz2");
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);
    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *source = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
        if (!g_str_has_suffix (source, ".xml")) {
            GError *error = NULL;
            if (!import_archive (manager, source, &error)) {
                show_manager_error (manager, error->message);
                g_clear_error (&error);
            } else {
                manager_populate (manager);
            }
        } else if (!valid_snippets_document (source)) {
            show_manager_error (manager, _("The selected file is not a valid snippets XML document."));
        } else {
            gchar *directory = g_build_filename (g_get_user_config_dir (), "pluma", "snippets", NULL);
            gchar *base = g_path_get_basename (source);
            gchar *destination = g_build_filename (directory, base, NULL);
            GFile *source_file = g_file_new_for_path (source);
            GFile *destination_file = g_file_new_for_path (destination);
            GError *error = NULL;
            g_mkdir_with_parents (directory, 0700);
            if (!g_file_copy (source_file, destination_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
                show_manager_error (manager, error->message); g_clear_error (&error);
            } else {
                load_snippet_file (manager->plugin, destination);
                manager_populate (manager);
            }
            g_object_unref (source_file); g_object_unref (destination_file);
            g_free (destination); g_free (base); g_free (directory);
        }
        g_free (source);
    }
    gtk_widget_destroy (chooser);
}

static void
export_archive (const gchar *source, const gchar *destination, GError **error)
{
    struct archive *archive = archive_write_new ();
    struct archive_entry *entry = archive_entry_new ();
    gchar *contents = NULL;
    gsize length = 0;
    gchar *base = g_path_get_basename (source);
    int status;

    if (g_str_has_suffix (destination, ".tar.gz"))
        archive_write_add_filter_gzip (archive);
    else if (g_str_has_suffix (destination, ".tar.bz2"))
        archive_write_add_filter_bzip2 (archive);
    else
        archive_write_add_filter_none (archive);
    archive_write_set_format_pax_restricted (archive);
    if (!g_file_get_contents (source, &contents, &length, error))
        goto out;
    if (archive_write_open_filename (archive, destination) != ARCHIVE_OK) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", archive_error_string (archive));
        goto out;
    }
    archive_entry_set_pathname (entry, base);
    archive_entry_set_filetype (entry, AE_IFREG);
    archive_entry_set_perm (entry, 0600);
    archive_entry_set_size (entry, length);
    status = archive_write_header (archive, entry);
    if (status != ARCHIVE_OK || archive_write_data (archive, contents, length) != (la_ssize_t) length)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", archive_error_string (archive));
    archive_write_close (archive);
out:
    archive_entry_free (entry); archive_write_free (archive);
    g_free (base); g_free (contents);
}

static void
manager_export (GtkButton *button, SnippetsManager *manager)
{
    Snippet *snippet = manager->selected_key != NULL ?
        g_hash_table_lookup (manager->plugin->snippets, manager->selected_key) : NULL;
    GtkWidget *chooser;
    if (snippet == NULL) {
        show_manager_error (manager, _("Select a snippet language to export."));
        return;
    }
    chooser = gtk_file_chooser_dialog_new (_("Export Snippets"), GTK_WINDOW (manager->plugin->window),
                                           GTK_FILE_CHOOSER_ACTION_SAVE, _("Cancel"), GTK_RESPONSE_CANCEL,
                                           _("Export"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (chooser), TRUE);
    {
        gchar *name = g_strdup_printf ("%s.xml", snippet->language);
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (chooser), name);
        g_free (name);
    }
    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *destination = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
        gchar *source = g_build_filename (g_get_user_config_dir (), "pluma", "snippets", NULL);
        gchar *filename = g_strdup_printf ("%s.xml", snippet->language);
        gchar *source_path = g_build_filename (source, filename, NULL);
        GError *error = NULL;
        if (write_language_file (manager->plugin, snippet->language, &error)) {
            if (g_str_has_suffix (destination, ".tar") || g_str_has_suffix (destination, ".tar.gz") ||
                g_str_has_suffix (destination, ".tar.bz2")) {
                export_archive (source_path, destination, &error);
            } else {
                GFile *from = g_file_new_for_path (source_path);
                GFile *to = g_file_new_for_path (destination);
                if (!g_file_copy (from, to, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error))
                    show_manager_error (manager, error->message);
                g_object_unref (from); g_object_unref (to);
            }
        }
        if (error != NULL)
            show_manager_error (manager, error->message);
        g_clear_error (&error);
        g_free (source_path); g_free (filename); g_free (source); g_free (destination);
    }
    gtk_widget_destroy (chooser);
}

static void manager_free (SnippetsManager *manager) {
    g_clear_object (&manager->store); g_free (manager->selected_key); g_free (manager);
}

static GtkWidget *
create_configure_widget (PeasGtkConfigurable *configurable)
{
    PlumaSnippetsPlugin *self = PLUMA_SNIPPETS_PLUGIN (configurable);
    SnippetsManager *manager = g_new0 (SnippetsManager, 1);
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *left = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *right = gtk_grid_new ();
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    GtkWidget *button = gtk_button_new_with_mnemonic (_("_Save Snippet"));
    GtkWidget *import_button = gtk_button_new_with_mnemonic (_("_Import XML"));
    GtkWidget *export_button = gtk_button_new_with_mnemonic (_("_Export Language"));
    GtkWidget *new_button = gtk_button_new_with_mnemonic (_("_New"));
    GtkWidget *delete_button = gtk_button_new_with_mnemonic (_("_Delete"));
    GtkWidget *buttons = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
    GtkCellRenderer *renderer;
    guint column;
    const gchar *titles[] = { N_("Language"), N_("Trigger"), N_("Description") };

    manager->plugin = self;
    manager->store = gtk_list_store_new (MANAGER_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
                                         G_TYPE_STRING, G_TYPE_STRING);
    manager->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (manager->store));
    for (column = 0; column < 3; column++) {
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (manager->tree), -1,
                                                     _(titles[column]), renderer, "text", column, NULL);
    }
    gtk_container_add (GTK_CONTAINER (scroll), manager->tree);
    gtk_widget_set_size_request (scroll, 360, 320);
    gtk_box_pack_start (GTK_BOX (left), scroll, TRUE, TRUE, 0);
    gtk_container_add (GTK_CONTAINER (buttons), import_button);
    gtk_container_add (GTK_CONTAINER (buttons), export_button);
    gtk_container_add (GTK_CONTAINER (buttons), new_button);
    gtk_container_add (GTK_CONTAINER (buttons), delete_button);
    gtk_box_pack_start (GTK_BOX (left), buttons, FALSE, FALSE, 0);
    gtk_grid_set_row_spacing (GTK_GRID (right), 6);
    gtk_grid_set_column_spacing (GTK_GRID (right), 6);
    manager->tag = gtk_entry_new ();
    manager->description = gtk_entry_new ();
    manager->accelerator = gtk_entry_new ();
    manager->drop_targets = gtk_entry_new ();
    manager->text = gtk_text_view_new ();
    gtk_widget_set_hexpand (manager->text, TRUE); gtk_widget_set_vexpand (manager->text, TRUE);
    gtk_grid_attach (GTK_GRID (right), gtk_label_new (_("Trigger")), 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (right), manager->tag, 1, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (right), gtk_label_new (_("Description")), 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (right), manager->description, 1, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (right), gtk_label_new (_("Accelerator")), 0, 2, 1, 1);
    gtk_grid_attach (GTK_GRID (right), manager->accelerator, 1, 2, 1, 1);
    gtk_grid_attach (GTK_GRID (right), gtk_label_new (_("Drop targets")), 0, 3, 1, 1);
    gtk_grid_attach (GTK_GRID (right), manager->drop_targets, 1, 3, 1, 1);
    gtk_grid_attach (GTK_GRID (right), manager->text, 0, 4, 2, 1);
    gtk_grid_attach (GTK_GRID (right), button, 1, 5, 1, 1);
    gtk_box_pack_start (GTK_BOX (box), left, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (box), right, TRUE, TRUE, 0);
    g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (manager->tree)), "changed",
                      G_CALLBACK (manager_selection_changed), manager);
    g_signal_connect (button, "clicked", G_CALLBACK (manager_save), manager);
    g_signal_connect (import_button, "clicked", G_CALLBACK (manager_import), manager);
    g_signal_connect (export_button, "clicked", G_CALLBACK (manager_export), manager);
    g_signal_connect (new_button, "clicked", G_CALLBACK (manager_new), manager);
    g_signal_connect (delete_button, "clicked", G_CALLBACK (manager_delete), manager);
    g_object_set_data_full (G_OBJECT (box), "snippets-manager", manager, (GDestroyNotify) manager_free);
    manager_populate (manager);
    gtk_widget_show_all (box);
    return box;
}

/* Expand the non-executable subset of the legacy snippet syntax. */
static gint
placeholder_spec_compare (gconstpointer a, gconstpointer b)
{
    const PlaceholderSpec *left = a;
    const PlaceholderSpec *right = b;
    if (left->index == 0) return 1;
    if (right->index == 0) return -1;
    if (left->index == right->index)
        return left->mirror - right->mirror;
    return (left->index > right->index) - (left->index < right->index);
}

static gboolean
has_placeholder (GArray *placeholders, guint index)
{
    guint i;
    for (i = 0; i < placeholders->len; i++)
        if (g_array_index (placeholders, PlaceholderSpec, i).index == index)
            return TRUE;
    return FALSE;
}

static gchar *
fallback_transform (const gchar *operation, const gchar *value)
{
    gchar *result = NULL;
    if (g_str_equal (operation, "upper")) result = g_utf8_strup (value, -1);
    else if (g_str_equal (operation, "lower")) result = g_utf8_strdown (value, -1);
    else if (g_str_equal (operation, "snake") || g_str_equal (operation, "get_type")) {
        GString *out = g_string_new (NULL); const gchar *p;
        for (p = value; *p != '\0'; p++) {
            if (*p == '-') g_string_append_c (out, '_');
            else { if (g_ascii_isupper (*p) && p != value) g_string_append_c (out, '_'); g_string_append_c (out, g_ascii_tolower (*p)); }
        }
        if (g_str_equal (operation, "get_type")) g_string_append (out, "_get_type");
        result = g_string_free (out, FALSE);
    } else if (g_str_equal (operation, "camel")) {
        GString *out = g_string_new (NULL); gboolean upper = TRUE; const gchar *p;
        for (p = value; *p != '\0'; p++) { if (*p == '-' || *p == '_') upper = TRUE; else { g_string_append_c (out, upper ? g_ascii_toupper (*p) : *p); upper = FALSE; } }
        result = g_string_free (out, FALSE);
    } else if (g_str_equal (operation, "type") || g_str_equal (operation, "is")) {
        gchar *snake = fallback_transform ("snake", value); gchar *upper = g_ascii_strup (snake, -1); gchar *separator = strchr (upper, '_');
        if (separator != NULL) { *separator = '\0'; result = g_strdup_printf ("%s_%s_%s", upper, g_str_equal (operation, "type") ? "TYPE" : "IS", separator + 1); }
        else result = g_strdup_printf ("%s_%s", upper, g_str_equal (operation, "type") ? "TYPE" : "IS");
        g_free (upper); g_free (snake);
    } else if (g_str_equal (operation, "gobject_typedefs") || g_str_equal (operation, "ginterface_typedefs")) {
        gchar *camel = fallback_transform ("camel", value);
        result = g_str_equal (operation, "gobject_typedefs")
            ? g_strdup_printf ("typedef struct _%s %s;\ntypedef struct _%sClass %sClass;\ntypedef struct _%sPrivate %sPrivate;", camel, camel, camel, camel, camel, camel)
            : g_strdup_printf ("typedef struct _%s %s;\ntypedef struct _%sIface %sIface;", camel, camel, camel, camel);
        g_free (camel);
    } else if (g_str_equal (operation, "year") || g_str_equal (operation, "date")) {
        GDateTime *now = g_date_time_new_now_local (); result = g_date_time_format (now, g_str_equal (operation, "year") ? "%Y" : "%F"); g_date_time_unref (now);
    } else if (g_str_equal (operation, "user")) result = g_strdup (g_get_real_name ());
    else if (g_str_equal (operation, "email")) result = g_strdup (g_getenv ("EMAIL") != NULL ? g_getenv ("EMAIL") : "");
    return result;
}

static gchar *
run_lua_transform (PlumaSnippetsPlugin *self, const gchar *operation, const gchar *value)
{
    GSubprocess *process;
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    GError *error = NULL;

    if (self->lua_program == NULL || self->lua_runtime == NULL)
        return fallback_transform (operation, value != NULL ? value : "");
    process = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                &error, self->lua_program, self->lua_runtime,
                                operation, value != NULL ? value : "", NULL);
    if (process == NULL) {
        g_warning ("Cannot start restricted snippets Lua runtime: %s", error->message);
        g_clear_error (&error);
        return NULL;
    }
    if (!g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, &error) ||
        !g_subprocess_get_successful (process)) {
        g_warning ("Snippet Lua transformation '%s' failed: %s%s%s", operation,
                   error != NULL ? error->message : "runtime error",
                   stderr_text != NULL ? ": " : "", stderr_text != NULL ? stderr_text : "");
        g_clear_error (&error);
        g_clear_pointer (&stdout_text, g_free);
    }
    g_free (stderr_text);
    g_object_unref (process);
    return stdout_text;
}

static gchar *
environment_value (PlumaSnippetsPlugin *self, GtkTextBuffer *buffer, const gchar *name)
{
    GtkTextIter start, end;
    PlumaDocument *document = PLUMA_DOCUMENT (buffer);
    GFile *location = pluma_document_get_location (document);
    if (g_str_has_prefix (name, "PLUMA_DROP_") && self->drop_environment != NULL) {
        const gchar *value = g_hash_table_lookup (self->drop_environment, name);
        return g_strdup (value != NULL ? value : "");
    }
    if (g_str_equal (name, "PLUMA_SELECTED_TEXT")) {
        if (!gtk_text_buffer_get_selection_bounds (buffer, &start, &end)) return g_strdup ("");
        return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    }
    gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
    if (g_str_equal (name, "PLUMA_CURRENT_LINE_NUMBER"))
        return g_strdup_printf ("%d", gtk_text_iter_get_line (&start) + 1);
    if (g_str_equal (name, "PLUMA_CURRENT_LINE")) {
        gtk_text_iter_set_line_offset (&start, 0); end = start; gtk_text_iter_forward_to_line_end (&end);
        return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    }
    if (g_str_equal (name, "PLUMA_CURRENT_WORD")) {
        end = start; if (!gtk_text_iter_starts_word (&start)) gtk_text_iter_backward_word_start (&start);
        if (!gtk_text_iter_ends_word (&end)) gtk_text_iter_forward_word_end (&end);
        return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    }
    if (location != NULL) {
        if (g_str_equal (name, "PLUMA_CURRENT_DOCUMENT_URI")) return g_file_get_uri (location);
        if (g_str_equal (name, "PLUMA_CURRENT_DOCUMENT_NAME")) return g_file_get_basename (location);
        if (g_str_equal (name, "PLUMA_CURRENT_DOCUMENT_PATH")) return g_file_get_path (location);
        if (g_str_equal (name, "PLUMA_CURRENT_DOCUMENT_SCHEME")) return g_strdup (g_file_get_uri_scheme (location));
        if (g_str_equal (name, "PLUMA_CURRENT_DOCUMENT_DIR")) {
            GFile *parent = g_file_get_parent (location); gchar *path = parent != NULL ? g_file_get_path (parent) : NULL;
            g_clear_object (&parent); return path != NULL ? path : g_strdup ("");
        }
    }
    return g_strdup ("");
}

static gchar *
choose_default (PlumaSnippetsPlugin *self, GtkTextBuffer *buffer, gchar *value)
{
    gsize length = strlen (value);
    gchar *selected = NULL;
    if (length >= 2 && value[0] == '[' && value[length - 1] == ']') {
        gchar **items; guint i;
        value[length - 1] = '\0'; items = g_strsplit (value + 1, ",", -1);
        for (i = 0; items[i] != NULL && selected == NULL; i++) {
            gchar *item = g_strstrip (items[i]);
            if (item[0] == '$') {
                const gchar *name = item + 1;
                selected = environment_value (self, buffer, name);
                if (*selected == '\0') g_clear_pointer (&selected, g_free);
            } else if (*item != '\0') selected = g_strdup (item);
        }
        g_strfreev (items);
    }
    if (selected == NULL) selected = g_strdup (value);
    g_free (value);
    return selected;
}

static void expansion_free (Expansion *expansion);

static Expansion *
expand_text (PlumaSnippetsPlugin *self, GtkTextBuffer *buffer, const gchar *source)
{
    GString *out = g_string_new (NULL);
    GHashTable *values = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
    Expansion *result = g_new0 (Expansion, 1);
    const gchar *p = source;

    result->placeholders = g_array_new (FALSE, FALSE, sizeof (PlaceholderSpec));
    result->final_cursor = -1;

    while (*p != '\0') {
        if (p[0] == '\\' && p[1] == '$') {
            g_string_append_c (out, '$'); p += 2; continue;
        } else if (p[0] == '$' && (g_ascii_isupper (p[1]) ||
                   (p[1] == '{' && g_ascii_isupper (p[2])))) {
            const gchar *start = p + (p[1] == '{' ? 2 : 1), *q = start;
            gchar *name, *value;
            while (g_ascii_isupper (*q) || g_ascii_isdigit (*q) || *q == '_') q++;
            if (p[1] == '{' && *q != '}') { g_string_append_c (out, *p++); continue; }
            name = g_strndup (start, q - start); value = environment_value (self, buffer, name);
            g_string_append (out, value); g_free (value); g_free (name); p = q + (p[1] == '{'); continue;
        } else if (p[0] == '$' && p[1] == '{' && g_ascii_isdigit (p[2])) {
            const gchar *q = p + 2;
            guint index = 0;
            gchar *value = NULL;
            while (g_ascii_isdigit (*q)) {
                index = index * 10 + (*q - '0');
                q++;
            }
            if (*q == ':') {
                const gchar *start = ++q;
                guint depth = 1;
                while (*q != '\0' && depth != 0) {
                    if (*q == '{') depth++;
                    else if (*q == '}') depth--;
                    if (depth != 0) q++;
                }
                if (depth == 0)
                    value = g_strndup (start, q - start);
            } else if (*q == '}') {
                value = g_strdup (g_hash_table_lookup (values, GUINT_TO_POINTER (index)));
            }
            if (value != NULL && *q == '}') {
                value = choose_default (self, buffer, value);
                glong start_offset = g_utf8_strlen (out->str, -1);
                if (strstr (value, "${") != NULL) {
                    Expansion *nested = expand_text (self, buffer, value);
                    guint nested_index;
                    g_free (value); value = g_strdup (nested->text);
                    for (nested_index = 0; nested_index < nested->placeholders->len; nested_index++) {
                        PlaceholderSpec spec = g_array_index (nested->placeholders, PlaceholderSpec, nested_index);
                        spec.start += start_offset; spec.end += start_offset;
                        g_array_append_val (result->placeholders, spec);
                    }
                    expansion_free (nested);
                }
                if (index != 0 && !g_hash_table_contains (values, GUINT_TO_POINTER (index)))
                    g_hash_table_insert (values, GUINT_TO_POINTER (index), g_strdup (value));
                if (index != 0)
                    g_string_append (out, value);
                if (index == 0) {
                    result->final_cursor = start_offset;
                } else if (!has_placeholder (result->placeholders, index)) {
                    PlaceholderSpec spec = { index, start_offset, g_utf8_strlen (out->str, -1), FALSE };
                    g_array_append_val (result->placeholders, spec);
                } else if (index != 0) {
                    PlaceholderSpec spec = { index, start_offset, g_utf8_strlen (out->str, -1), TRUE };
                    g_array_append_val (result->placeholders, spec);
                }
                g_free (value);
                p = q + 1;
                continue;
            }
            g_free (value);
        } else if (p[0] == '$' && g_ascii_isdigit (p[1])) {
            const gchar *q = p + 1;
            guint index = 0;
            const gchar *value;
            while (g_ascii_isdigit (*q)) {
                index = index * 10 + (*q - '0');
                q++;
            }
            value = g_hash_table_lookup (values, GUINT_TO_POINTER (index));
            if (value != NULL) {
                glong start_offset = g_utf8_strlen (out->str, -1);
                g_string_append (out, value);
                if (index != 0) {
                    PlaceholderSpec spec = { index, start_offset, g_utf8_strlen (out->str, -1), TRUE };
                    g_array_append_val (result->placeholders, spec);
                }
            }
            if (index == 0)
                result->final_cursor = g_utf8_strlen (out->str, -1);
            p = q;
            continue;
        } else if (p[0] == '$' && p[1] == '<') {
            const gchar *end = strchr (p + 2, '>');
            if (end != NULL) {
                gchar *expression = g_strndup (p + 2, end - (p + 2));
                gchar **parts = g_strsplit (expression, ":", 3);
                if (g_strcmp0 (parts[0], "lua") == 0 && parts[1] != NULL) {
                    const gchar *input = "";
                    gchar *transformed;
                    if (g_strcmp0 (parts[2], "selected") == 0) {
                        GtkTextIter begin, finish;
                        if (!gtk_text_buffer_get_selection_bounds (buffer, &begin, &finish)) {
                            gtk_text_buffer_get_iter_at_mark (buffer, &begin, gtk_text_buffer_get_insert (buffer));
                            gtk_text_iter_set_line_offset (&begin, 0);
                            finish = begin;
                            gtk_text_iter_forward_to_line_end (&finish);
                        }
                        input = gtk_text_buffer_get_text (buffer, &begin, &finish, FALSE);
                    } else if (parts[2] != NULL) {
                        guint reference = (guint) g_ascii_strtoull (parts[2], NULL, 10);
                        input = g_hash_table_lookup (values, GUINT_TO_POINTER (reference));
                        if (input == NULL) input = parts[2];
                    }
                    transformed = run_lua_transform (self, parts[1], input);
                    if (transformed != NULL) {
                        g_string_append (out, transformed);
                        g_free (transformed);
                    }
                    if (g_strcmp0 (parts[2], "selected") == 0)
                        g_free ((gpointer) input);
                } else {
                    /* Compatibility mapping for legacy Python snippets. The
                     * expression is classified, never evaluated. */
                    const gchar *operation = NULL;
                    guint reference = 1;
                    gchar *transformed;
                    const gchar *input;
                    const gchar *ref = strchr (expression, '[');
                    if (ref == NULL) ref = strchr (expression, '$');
                    if (ref != NULL && g_ascii_isdigit (ref[1]))
                        reference = (guint) g_ascii_strtoull (ref + 1, NULL, 10);
                    if (strstr (expression, "_GET_CLASS") != NULL) operation = "gobject_macros";
                    else if (strstr (expression, "_GET_INTERFACE") != NULL) operation = "ginterface_macros";
                    else if (strstr (expression, "typedef struct") != NULL && strstr (expression, "Private") != NULL) operation = "gobject_typedefs";
                    else if (strstr (expression, "typedef struct") != NULL && strstr (expression, "Iface") != NULL) operation = "ginterface_typedefs";
                    else if (strstr (expression, "return low_str") != NULL) operation = "snake";
                    else if (strstr (expression, "return up_str") != NULL) operation = "upper";
                    else if (strstr (expression, "return camel_str") != NULL) operation = "camel";
                    else if (strstr (expression, "return type_str") != NULL) operation = "type";
                    else if (strstr (expression, "lower() + '_get_type'") != NULL) operation = "get_type";
                    else if (strstr (expression, "return $1.lower()") != NULL) operation = "lower";
                    input = g_hash_table_lookup (values, GUINT_TO_POINTER (reference));
                    transformed = operation != NULL ? run_lua_transform (self, operation, input) : NULL;
                    if (transformed != NULL) { g_string_append (out, transformed); g_free (transformed); }
                }
                g_strfreev (parts);
                g_free (expression);
                /* Legacy Python blocks remain disabled. */
                p = end + 1;
                continue;
            }
        }
        g_string_append_c (out, *p++);
    }
    g_hash_table_unref (values);
    g_array_sort (result->placeholders, placeholder_spec_compare);
    result->text = g_string_free (out, FALSE);
    if (result->final_cursor < 0)
        result->final_cursor = g_utf8_strlen (result->text, -1);
    return result;
}

static void expansion_free (Expansion *expansion) {
    g_free (expansion->text); g_array_unref (expansion->placeholders); g_free (expansion);
}

static void
placeholder_free (Placeholder *placeholder)
{
    GtkTextBuffer *buffer = gtk_text_mark_get_buffer (placeholder->start);
    if (buffer != NULL) {
        gtk_text_buffer_delete_mark (buffer, placeholder->start);
        gtk_text_buffer_delete_mark (buffer, placeholder->end);
    }
    g_free (placeholder);
}

static void
clear_placeholders (PlumaSnippetsPlugin *self)
{
    if (self->final_mark != NULL) {
        GtkTextBuffer *buffer = gtk_text_mark_get_buffer (self->final_mark);
        if (buffer != NULL) gtk_text_buffer_delete_mark (buffer, self->final_mark);
        self->final_mark = NULL;
    }
    g_ptr_array_set_size (self->placeholders, 0);
    g_ptr_array_set_size (self->mirrors, 0);
    self->active_placeholder = -1;
}

static gboolean
select_placeholder (PlumaSnippetsPlugin *self, gint position)
{
    Placeholder *placeholder;
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    if (position < 0 || position >= (gint) self->placeholders->len)
        return FALSE;
    placeholder = g_ptr_array_index (self->placeholders, position);
    buffer = gtk_text_mark_get_buffer (placeholder->start);
    if (buffer == NULL)
        return FALSE;
    gtk_text_buffer_get_iter_at_mark (buffer, &start, placeholder->start);
    gtk_text_buffer_get_iter_at_mark (buffer, &end, placeholder->end);
    gtk_text_buffer_select_range (buffer, &start, &end);
    gtk_text_view_scroll_mark_onscreen (GTK_TEXT_VIEW (self->view), placeholder->start);
    self->active_placeholder = position;
    return TRUE;
}

static void
update_mirrors (GtkTextBuffer *buffer, PlumaSnippetsPlugin *self)
{
    Placeholder *primary;
    GtkTextIter start, end;
    gchar *text;
    guint i;
    if (self->updating_mirrors || self->active_placeholder < 0 ||
        self->active_placeholder >= (gint) self->placeholders->len)
        return;
    primary = g_ptr_array_index (self->placeholders, self->active_placeholder);
    gtk_text_buffer_get_iter_at_mark (buffer, &start, primary->start);
    gtk_text_buffer_get_iter_at_mark (buffer, &end, primary->end);
    text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    self->updating_mirrors = TRUE;
    for (i = 0; i < self->mirrors->len; i++) {
        Placeholder *mirror = g_ptr_array_index (self->mirrors, i);
        if (mirror->index != primary->index) continue;
        gtk_text_buffer_get_iter_at_mark (buffer, &start, mirror->start);
        gtk_text_buffer_get_iter_at_mark (buffer, &end, mirror->end);
        gtk_text_buffer_delete (buffer, &start, &end);
        gtk_text_buffer_insert (buffer, &start, text, -1);
    }
    self->updating_mirrors = FALSE;
    g_free (text);
}

static Snippet *
lookup_snippet (PlumaSnippetsPlugin *self, PlumaDocument *document, const gchar *tag)
{
    GtkSourceLanguage *language = pluma_document_get_language (document);
    Snippet *snippet = NULL;
    gchar *key;
    if (language != NULL) {
        key = snippet_key (gtk_source_language_get_id (language), tag);
        snippet = g_hash_table_lookup (self->snippets, key);
        g_free (key);
    }
    if (snippet == NULL) {
        key = snippet_key ("global", tag);
        snippet = g_hash_table_lookup (self->snippets, key);
        g_free (key);
    }
    return snippet;
}

static void
insert_snippet (PlumaSnippetsPlugin *self, GtkTextBuffer *buffer,
                GtkTextIter *replace_start, GtkTextIter *replace_end, Snippet *snippet)
{
    Expansion *expanded = expand_text (self, buffer, snippet->text);
    GtkTextIter insertion = *replace_start;
    GtkTextMark *origin;
    guint i;

    clear_placeholders (self);
    gtk_text_buffer_begin_user_action (buffer);
    if (!gtk_text_iter_equal (replace_start, replace_end))
        gtk_text_buffer_delete (buffer, replace_start, replace_end);
    origin = gtk_text_buffer_create_mark (buffer, NULL, &insertion, TRUE);
    gtk_text_buffer_insert (buffer, &insertion, expanded->text, -1);
    {
        GtkTextIter final;
        gtk_text_buffer_get_iter_at_mark (buffer, &final, origin);
        gtk_text_iter_forward_chars (&final, expanded->final_cursor);
        self->final_mark = gtk_text_buffer_create_mark (buffer, NULL, &final, FALSE);
    }
    for (i = 0; i < expanded->placeholders->len; i++) {
        PlaceholderSpec spec = g_array_index (expanded->placeholders, PlaceholderSpec, i);
        Placeholder *placeholder = g_new0 (Placeholder, 1);
        GtkTextIter begin, end;
        gtk_text_buffer_get_iter_at_mark (buffer, &begin, origin);
        end = begin;
        gtk_text_iter_forward_chars (&begin, spec.start);
        gtk_text_iter_forward_chars (&end, spec.end);
        placeholder->index = spec.index;
        placeholder->start = gtk_text_buffer_create_mark (buffer, NULL, &begin, TRUE);
        placeholder->end = gtk_text_buffer_create_mark (buffer, NULL, &end, FALSE);
        g_ptr_array_add (spec.mirror ? self->mirrors : self->placeholders, placeholder);
    }
    if (self->placeholders->len == 0) {
        GtkTextIter final;
        gtk_text_buffer_get_iter_at_mark (buffer, &final, self->final_mark);
        gtk_text_buffer_place_cursor (buffer, &final);
    }
    gtk_text_buffer_delete_mark (buffer, origin);
    gtk_text_buffer_end_user_action (buffer);
    expansion_free (expanded);
    if (self->placeholders->len > 0)
        select_placeholder (self, 0);
}

static gchar *
completion_word (SnippetsProvider *provider, GtkSourceCompletionContext *context, GtkTextIter *cursor)
{
    GtkTextIter start;
    if (!gtk_source_completion_context_get_iter (context, cursor))
        return NULL;
    start = *cursor;
    while (!gtk_text_iter_starts_line (&start)) {
        GtkTextIter previous = start;
        gunichar c;
        gtk_text_iter_backward_char (&previous);
        c = gtk_text_iter_get_char (&previous);
        if (!(g_unichar_isalnum (c) || c == '_' || c == '-'))
            break;
        start = previous;
    }
    if (provider->start_mark == NULL)
        provider->start_mark = gtk_text_buffer_create_mark (gtk_text_iter_get_buffer (&start), NULL, &start, TRUE);
    else
        gtk_text_buffer_move_mark (gtk_text_iter_get_buffer (&start), provider->start_mark, &start);
    return gtk_text_buffer_get_text (gtk_text_iter_get_buffer (&start), &start, cursor, FALSE);
}

static gchar *provider_get_name (GtkSourceCompletionProvider *provider) { return g_strdup (_("Snippets")); }
static const gchar *provider_get_icon_name (GtkSourceCompletionProvider *provider) { return "format-justify-left"; }
static GtkSourceCompletionActivation provider_get_activation (GtkSourceCompletionProvider *provider) {
    return GTK_SOURCE_COMPLETION_ACTIVATION_USER_REQUESTED;
}

static gint proposal_compare (gconstpointer a, gconstpointer b) {
    gchar *left = gtk_source_completion_proposal_get_text (GTK_SOURCE_COMPLETION_PROPOSAL ((gpointer) a));
    gchar *right = gtk_source_completion_proposal_get_text (GTK_SOURCE_COMPLETION_PROPOSAL ((gpointer) b));
    gint result = g_strcmp0 (left, right);
    g_free (left); g_free (right); return result;
}

static void
provider_populate (GtkSourceCompletionProvider *base, GtkSourceCompletionContext *context)
{
    SnippetsProvider *provider = SNIPPETS_PROVIDER (base);
    PlumaSnippetsPlugin *self = provider->plugin;
    GtkTextIter cursor;
    gchar *word = completion_word (provider, context, &cursor);
    PlumaDocument *document = PLUMA_DOCUMENT (gtk_text_iter_get_buffer (&cursor));
    GtkSourceLanguage *language = pluma_document_get_language (document);
    const gchar *language_id = language != NULL ? gtk_source_language_get_id (language) : "global";
    GHashTableIter hash_iter;
    gpointer key_ptr, value_ptr;
    GList *proposals = NULL;

    g_hash_table_iter_init (&hash_iter, self->snippets);
    while (g_hash_table_iter_next (&hash_iter, &key_ptr, &value_ptr)) {
        const gchar *key = key_ptr;
        const gchar *separator = strchr (key, '\037');
        const gchar *tag = separator != NULL ? separator + 1 : key;
        gsize language_len = separator != NULL ? (gsize) (separator - key) : 0;
        Snippet *snippet = value_ptr;
        GtkSourceCompletionItem *item;
        if (word != NULL && *word != '\0' && !g_str_has_prefix (tag, word))
            continue;
        if (!((strlen (language_id) == language_len && strncmp (key, language_id, language_len) == 0) ||
              (language_len == 6 && strncmp (key, "global", 6) == 0)))
            continue;
        item = g_object_new (GTK_SOURCE_TYPE_COMPLETION_ITEM,
                             "label", tag, "text", tag,
                             "info", snippet->text, NULL);
        g_object_set_data (G_OBJECT (item), "snippet", snippet);
        proposals = g_list_prepend (proposals, item);
    }
    proposals = g_list_sort (proposals, proposal_compare);
    gtk_source_completion_context_add_proposals (context, base, proposals, TRUE);
    g_list_free_full (proposals, g_object_unref);
    g_free (word);
}

static gboolean
provider_get_start_iter (GtkSourceCompletionProvider *base, GtkSourceCompletionContext *context,
                         GtkSourceCompletionProposal *proposal, GtkTextIter *iter)
{
    SnippetsProvider *provider = SNIPPETS_PROVIDER (base);
    GtkTextBuffer *buffer;
    if (provider->start_mark == NULL || gtk_text_mark_get_deleted (provider->start_mark))
        return FALSE;
    buffer = gtk_text_mark_get_buffer (provider->start_mark);
    gtk_text_buffer_get_iter_at_mark (buffer, iter, provider->start_mark);
    return TRUE;
}

static gboolean
provider_activate (GtkSourceCompletionProvider *base, GtkSourceCompletionProposal *proposal, GtkTextIter *iter)
{
    SnippetsProvider *provider = SNIPPETS_PROVIDER (base);
    Snippet *snippet = g_object_get_data (G_OBJECT (proposal), "snippet");
    GtkTextBuffer *buffer = gtk_text_iter_get_buffer (iter);
    GtkTextIter end;
    if (snippet == NULL)
        return FALSE;
    gtk_text_buffer_get_iter_at_mark (buffer, &end, gtk_text_buffer_get_insert (buffer));
    insert_snippet (provider->plugin, buffer, iter, &end, snippet);
    return TRUE;
}

static void snippets_provider_dispose (GObject *object) {
    SnippetsProvider *self = SNIPPETS_PROVIDER (object);
    if (self->start_mark != NULL && !gtk_text_mark_get_deleted (self->start_mark))
        gtk_text_buffer_delete_mark (gtk_text_mark_get_buffer (self->start_mark), self->start_mark);
    self->start_mark = NULL; self->plugin = NULL;
    G_OBJECT_CLASS (snippets_provider_parent_class)->dispose (object);
}
static void snippets_provider_init (SnippetsProvider *self) {}
static void snippets_provider_class_finalize (SnippetsProviderClass *klass) {}
static void snippets_provider_class_init (SnippetsProviderClass *klass) {
    G_OBJECT_CLASS (klass)->dispose = snippets_provider_dispose;
}
static void completion_provider_iface_init (GtkSourceCompletionProviderIface *iface) {
    iface->get_name = provider_get_name;
    iface->get_icon_name = provider_get_icon_name;
    iface->get_activation = provider_get_activation;
    iface->populate = provider_populate;
    iface->get_start_iter = provider_get_start_iter;
    iface->activate_proposal = provider_activate;
}

static gboolean
accelerator_activate (GtkAccelGroup *group, GObject *object, guint keyval,
                      GdkModifierType modifiers, gpointer data)
{
    PlumaSnippetsPlugin *self = data;
    PlumaDocument *document = pluma_window_get_active_document (self->window);
    GtkSourceLanguage *language;
    const gchar *language_id = "global";
    gchar *accelerator;
    GHashTableIter iter;
    gpointer key_ptr, value_ptr;

    if (document == NULL || self->view == NULL)
        return FALSE;
    language = pluma_document_get_language (document);
    if (language != NULL)
        language_id = gtk_source_language_get_id (language);
    accelerator = gtk_accelerator_name (keyval, modifiers);
    g_hash_table_iter_init (&iter, self->snippets);
    while (g_hash_table_iter_next (&iter, &key_ptr, &value_ptr)) {
        const gchar *key = key_ptr;
        Snippet *snippet = value_ptr;
        const gchar *separator = strchr (key, '\037');
        gsize language_len = separator != NULL ? (gsize) (separator - key) : 0;
        if (g_strcmp0 (snippet->accelerator, accelerator) != 0)
            continue;
        if (!((strlen (language_id) == language_len && strncmp (key, language_id, language_len) == 0) ||
              (language_len == 6 && strncmp (key, "global", 6) == 0)))
            continue;
        {
            GtkTextBuffer *buffer = GTK_TEXT_BUFFER (document);
            GtkTextIter cursor;
            gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
            insert_snippet (self, buffer, &cursor, &cursor, snippet);
        }
        g_free (accelerator);
        return TRUE;
    }
    g_free (accelerator);
    return FALSE;
}

static void
install_accelerators (PlumaSnippetsPlugin *self)
{
    GHashTable *installed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GHashTableIter iter;
    gpointer value;
    if (self->accel_group != NULL) {
        gtk_window_remove_accel_group (GTK_WINDOW (self->window), self->accel_group);
        g_clear_object (&self->accel_group);
    }
    self->accel_group = gtk_accel_group_new ();
    g_hash_table_iter_init (&iter, self->snippets);
    while (g_hash_table_iter_next (&iter, NULL, &value)) {
        Snippet *snippet = value;
        guint keyval;
        GdkModifierType modifiers;
        if (snippet->accelerator == NULL || *snippet->accelerator == '\0' ||
            g_hash_table_contains (installed, snippet->accelerator))
            continue;
        gtk_accelerator_parse (snippet->accelerator, &keyval, &modifiers);
        if (!gtk_accelerator_valid (keyval, modifiers))
            continue;
        gtk_accel_group_connect (self->accel_group, keyval, modifiers, GTK_ACCEL_VISIBLE,
                                 g_cclosure_new (G_CALLBACK (accelerator_activate), self, NULL));
        g_hash_table_add (installed, g_strdup (snippet->accelerator));
    }
    gtk_window_add_accel_group (GTK_WINDOW (self->window), self->accel_group);
    g_hash_table_unref (installed);
}

static gboolean
key_press (GtkWidget *widget, GdkEventKey *event, PlumaSnippetsPlugin *self)
{
    GtkTextBuffer *buffer;
    GtkTextIter cursor, start;
    gchar *tag;
    Snippet *snippet;

    if (event->keyval != GDK_KEY_Tab && event->keyval != GDK_KEY_ISO_Left_Tab)
        return FALSE;
    if (self->placeholders->len > 0) {
        gboolean backwards = event->keyval == GDK_KEY_ISO_Left_Tab || (event->state & GDK_SHIFT_MASK) != 0;
        gint next = self->active_placeholder + (backwards ? -1 : 1);
        if (select_placeholder (self, next))
            return TRUE;
        if (!backwards && self->final_mark != NULL) {
            GtkTextIter final;
            gtk_text_buffer_get_iter_at_mark (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view)), &final, self->final_mark);
            gtk_text_buffer_place_cursor (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view)), &final);
        }
        clear_placeholders (self);
        return TRUE;
    }
    if ((event->state & gtk_accelerator_get_default_mod_mask ()) != 0)
        return FALSE;
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
    gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
    start = cursor;
    while (!gtk_text_iter_starts_line (&start)) {
        GtkTextIter previous = start;
        gunichar c;
        if (!gtk_text_iter_backward_char (&previous))
            break;
        c = gtk_text_iter_get_char (&previous);
        if (g_unichar_isspace (c))
            break;
        start = previous;
    }
    if (gtk_text_iter_equal (&start, &cursor))
        return FALSE;
    tag = gtk_text_buffer_get_text (buffer, &start, &cursor, FALSE);
    snippet = lookup_snippet (self, PLUMA_DOCUMENT (buffer), tag);
    g_free (tag);
    if (snippet == NULL)
        return FALSE;
    insert_snippet (self, buffer, &start, &cursor, snippet);
    return TRUE;
}

static void
drag_data_received (GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                    GtkSelectionData *data, guint info, guint time, PlumaSnippetsPlugin *self)
{
    gchar **uris = gtk_selection_data_get_uris (data);
    GFile *file;
    GFileInfo *file_info;
    const gchar *content_type;
    GHashTableIter iter;
    gpointer value;
    Snippet *matched = NULL;
    guint i;
    if (uris == NULL || uris[0] == NULL) { g_strfreev (uris); return; }
    file = g_file_new_for_uri (uris[0]);
    file_info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, 0, NULL, NULL);
    content_type = file_info != NULL ? g_file_info_get_content_type (file_info) : NULL;
    g_hash_table_iter_init (&iter, self->snippets);
    while (content_type != NULL && g_hash_table_iter_next (&iter, NULL, &value)) {
        Snippet *snippet = value;
        for (i = 0; snippet->drop_targets != NULL && snippet->drop_targets[i] != NULL; i++)
            if (g_content_type_is_a (content_type, snippet->drop_targets[i])) { matched = snippet; break; }
        if (matched != NULL) break;
    }
    if (matched != NULL) {
        gchar *path = g_file_get_path (file), *name = g_file_get_basename (file);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget)); GtkTextIter cursor;
        g_hash_table_replace (self->drop_environment, g_strdup ("PLUMA_DROP_DOCUMENT_URI"), g_strdup (uris[0]));
        g_hash_table_replace (self->drop_environment, g_strdup ("PLUMA_DROP_DOCUMENT_PATH"), path != NULL ? path : g_strdup (""));
        g_hash_table_replace (self->drop_environment, g_strdup ("PLUMA_DROP_DOCUMENT_NAME"), name);
        g_hash_table_replace (self->drop_environment, g_strdup ("PLUMA_DROP_DOCUMENT_TYPE"), g_strdup (content_type));
        gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
        insert_snippet (self, buffer, &cursor, &cursor, matched);
        g_hash_table_remove_all (self->drop_environment);
        gtk_drag_finish (context, TRUE, FALSE, time);
    }
    g_clear_object (&file_info); g_object_unref (file); g_strfreev (uris);
}

static void
set_view (PlumaSnippetsPlugin *self, PlumaView *view)
{
    if (self->view == view)
        return;
    if (self->view != NULL && self->key_handler != 0)
        g_signal_handler_disconnect (self->view, self->key_handler);
    if (self->view != NULL && self->buffer_changed_handler != 0)
        g_signal_handler_disconnect (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view)), self->buffer_changed_handler);
    if (self->view != NULL && self->drop_handler != 0)
        g_signal_handler_disconnect (self->view, self->drop_handler);
    if (self->view != NULL && self->provider != NULL)
        gtk_source_completion_remove_provider (gtk_source_view_get_completion (GTK_SOURCE_VIEW (self->view)),
                                               GTK_SOURCE_COMPLETION_PROVIDER (self->provider), NULL);
    clear_placeholders (self);
    g_set_object (&self->view, view);
    self->key_handler = view == NULL ? 0 :
        g_signal_connect (view, "key-press-event", G_CALLBACK (key_press), self);
    self->buffer_changed_handler = view == NULL ? 0 :
        g_signal_connect (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)), "changed", G_CALLBACK (update_mirrors), self);
    self->drop_handler = view == NULL ? 0 :
        g_signal_connect (view, "drag-data-received", G_CALLBACK (drag_data_received), self);
    if (view != NULL && self->provider != NULL)
        gtk_source_completion_add_provider (gtk_source_view_get_completion (GTK_SOURCE_VIEW (view)),
                                            GTK_SOURCE_COMPLETION_PROVIDER (self->provider), NULL);
}

static void
activate (PlumaWindowActivatable *activatable)
{
    PlumaSnippetsPlugin *self = PLUMA_SNIPPETS_PLUGIN (activatable);
    gchar *data_dir = peas_extension_base_get_data_dir (PEAS_EXTENSION_BASE (self));
    gchar *system_dir = g_build_filename (data_dir, "snippets", NULL);
    gchar *user_dir = g_build_filename (g_get_user_config_dir (), "pluma", "snippets", NULL);
    load_directory (self, system_dir);
    load_directory (self, user_dir); /* user definitions override system definitions */
    self->provider = g_object_new (TYPE_SNIPPETS_PROVIDER, NULL);
    self->provider->plugin = self;
    self->data_dir = g_strdup (system_dir);
    self->lua_program = g_find_program_in_path ("lua5.1");
    if (self->lua_program == NULL)
        self->lua_program = g_find_program_in_path ("lua51");
    self->lua_runtime = g_build_filename (system_dir, "snippets-runtime.lua", NULL);
    if (!g_file_test (self->lua_runtime, G_FILE_TEST_IS_REGULAR))
        g_clear_pointer (&self->lua_runtime, g_free);
    install_accelerators (self);
    g_free (user_dir);
    g_free (system_dir);
    g_free (data_dir);
    set_view (self, pluma_window_get_active_view (self->window));
}

static void deactivate (PlumaWindowActivatable *a) {
    PlumaSnippetsPlugin *self = PLUMA_SNIPPETS_PLUGIN (a);
    set_view (self, NULL);
    if (self->accel_group != NULL) {
        gtk_window_remove_accel_group (GTK_WINDOW (self->window), self->accel_group);
        g_clear_object (&self->accel_group);
    }
}
static void update_state (PlumaWindowActivatable *a) {
    PlumaSnippetsPlugin *self = PLUMA_SNIPPETS_PLUGIN (a);
    set_view (self, pluma_window_get_active_view (self->window));
}

static void
set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec)
{
    if (id == PROP_WINDOW) PLUMA_SNIPPETS_PLUGIN (object)->window = g_value_dup_object (value);
    else G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}

static void
get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
    if (id == PROP_WINDOW) g_value_set_object (value, PLUMA_SNIPPETS_PLUGIN (object)->window);
    else G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}

static void
dispose (GObject *object)
{
    PlumaSnippetsPlugin *self = PLUMA_SNIPPETS_PLUGIN (object);
    set_view (self, NULL);
    g_clear_object (&self->window);
    g_clear_pointer (&self->snippets, g_hash_table_unref);
    g_clear_pointer (&self->placeholders, g_ptr_array_unref);
    g_clear_pointer (&self->mirrors, g_ptr_array_unref);
    g_clear_pointer (&self->drop_environment, g_hash_table_unref);
    g_clear_object (&self->accel_group);
    g_clear_object (&self->provider);
    g_clear_pointer (&self->data_dir, g_free);
    g_clear_pointer (&self->lua_program, g_free);
    g_clear_pointer (&self->lua_runtime, g_free);
    G_OBJECT_CLASS (pluma_snippets_plugin_parent_class)->dispose (object);
}

static void pluma_snippets_plugin_init (PlumaSnippetsPlugin *self) {
    self->snippets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) snippet_free);
    self->placeholders = g_ptr_array_new_with_free_func ((GDestroyNotify) placeholder_free);
    self->mirrors = g_ptr_array_new_with_free_func ((GDestroyNotify) placeholder_free);
    self->drop_environment = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    self->active_placeholder = -1;
}
static void pluma_snippets_plugin_class_finalize (PlumaSnippetsPluginClass *klass) {}
static void pluma_snippets_plugin_class_init (PlumaSnippetsPluginClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;
    g_object_class_override_property (object_class, PROP_WINDOW, "window");
}
static void window_activatable_iface_init (PlumaWindowActivatableInterface *iface) {
    iface->activate = activate; iface->deactivate = deactivate; iface->update_state = update_state;
}
static void configurable_iface_init (PeasGtkConfigurableInterface *iface) {
    iface->create_configure_widget = create_configure_widget;
}
G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module) {
    snippets_provider_register_type (G_TYPE_MODULE (module));
    pluma_snippets_plugin_register_type (G_TYPE_MODULE (module));
    peas_object_module_register_extension_type (module, PLUMA_TYPE_WINDOW_ACTIVATABLE, PLUMA_TYPE_SNIPPETS_PLUGIN);
}
