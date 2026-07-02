#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-tool-manager.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gtksourceview/gtksource.h>
#include <pluma/pluma-utils.h>
#include <pluma/pluma-help.h>
#include <pluma/pluma-document.h>
#include <pluma/pluma-view.h>

/* Tree model columns. COL_POINTER holds a PlumaTool* for tool rows, a
 * GtkSourceLanguage* for language rows, or NULL otherwise. COL_KEY is the
 * canonical string key used in language_rows/for group lookups. */
enum { COL_POINTER, COL_KIND, COL_KEY, N_COLUMNS };

typedef enum {
    ROW_ALL_LANGUAGES,
    ROW_PLAIN_TEXT,
    ROW_LANGUAGE,
    ROW_TOOL
} RowKind;

#define ALL_LANGUAGES_KEY ""
#define PLAIN_TEXT_KEY "plain"

enum { LANG_COL_NAME, LANG_COL_ID, LANG_COL_ENABLED, LANG_N_COLUMNS };

struct _PlumaToolManager {
    gchar *data_dir;
    gchar *system_dir;
    gchar *user_dir;
    GPtrArray *tools; /* not owned */

    GtkWidget *dialog;
    GtkTreeView *view;
    GtkWidget *new_button, *remove_button, *revert_button;
    GtkWidget *tool_table;
    GtkComboBox *input_combo, *output_combo, *save_files_combo, *applicability_combo;
    GtkEntry *accelerator_entry;
    PlumaView *commands_view;
    GtkWidget *languages_button;
    GtkLabel *languages_label;

    GtkTreeStore *model;
    gulong selection_changed_id;

    PlumaTool *current_tool;

    GHashTable *language_rows; /* gchar* key -> GtkTreeRowReference* */
    GHashTable *tool_rows;     /* PlumaTool* -> GList* of GtkTreeRowReference* */
    GHashTable *accelerators;  /* gchar* shortcut -> GList* of PlumaTool* (not owned) */

    gint width, height;

    PlumaToolManagerUpdatedFunc updated_cb;
    gpointer user_data;
};

typedef struct {
    PlumaToolManager *manager;
    GtkWidget *popover;
    GtkListStore *model;
} LanguagesPopup;

static void add_tool (PlumaToolManager *manager, PlumaTool *tool);
static void save_current_tool (PlumaToolManager *manager);
static void do_update (PlumaToolManager *manager);
static void fill_languages_button (PlumaToolManager *manager);
static void update_remove_revert (PlumaToolManager *manager);

/* ---------------------------------------------------------------- */
/* accelerator bookkeeping                                          */
/* ---------------------------------------------------------------- */

static void
add_accelerator (PlumaToolManager *manager, PlumaTool *tool)
{
    GList *list;

    if (tool->shortcut == NULL)
        return;

    list = g_hash_table_lookup (manager->accelerators, tool->shortcut);
    if (g_list_find (list, tool) != NULL)
        return;

    list = g_list_append (list, tool);
    g_hash_table_insert (manager->accelerators, g_strdup (tool->shortcut), list);
}

static void
remove_accelerator (PlumaToolManager *manager, PlumaTool *tool, const gchar *shortcut)
{
    const gchar *key = shortcut != NULL ? shortcut : tool->shortcut;
    GList *list;

    if (key == NULL)
        return;

    list = g_hash_table_lookup (manager->accelerators, key);
    if (list == NULL)
        return;

    list = g_list_remove (list, tool);
    if (list == NULL)
        g_hash_table_remove (manager->accelerators, key);
    else
        g_hash_table_insert (manager->accelerators, g_strdup (key), list);
}

static GList *
accelerator_collision (PlumaToolManager *manager, const gchar *name, PlumaTool *node)
{
    GList *others = g_hash_table_lookup (manager->accelerators, name);
    GList *ret = NULL, *l;

    for (l = others; l != NULL; l = l->next) {
        PlumaTool *other = l->data;
        gboolean collide = FALSE;

        if (other == node)
            continue;

        if (other->languages == NULL || node->languages == NULL) {
            collide = TRUE;
        } else {
            guint i;
            for (i = 0; other->languages[i] != NULL && !collide; i++) {
                guint j;
                for (j = 0; node->languages[j] != NULL; j++)
                    if (g_strcmp0 (other->languages[i], node->languages[j]) == 0) {
                        collide = TRUE;
                        break;
                    }
            }
        }
        if (collide)
            ret = g_list_prepend (ret, other);
    }
    return g_list_reverse (ret);
}

static gboolean
set_accelerator (PlumaToolManager *manager, guint keyval, GdkModifierType mods)
{
    gchar *name;
    GList *collisions;

    remove_accelerator (manager, manager->current_tool, NULL);
    name = gtk_accelerator_name (keyval, mods);

    if (name == NULL || *name == '\0') {
        g_free (manager->current_tool->shortcut);
        manager->current_tool->shortcut = NULL;
        g_free (name);
        save_current_tool (manager);
        return TRUE;
    }

    collisions = accelerator_collision (manager, name, manager->current_tool);
    if (collisions != NULL) {
        GString *names = g_string_new (NULL);
        GList *l;
        GtkWidget *dialog;

        for (l = collisions; l != NULL; l = l->next) {
            if (names->len > 0)
                g_string_append (names, ", ");
            g_string_append (names, ((PlumaTool *) l->data)->name);
        }

        dialog = gtk_message_dialog_new (GTK_WINDOW (manager->dialog), GTK_DIALOG_MODAL,
                                         GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                         _("This accelerator is already bound to %s"), names->str);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);

        g_string_free (names, TRUE);
        g_list_free (collisions);
        g_free (name);

        add_accelerator (manager, manager->current_tool);
        return FALSE;
    }
    g_list_free (collisions);

    g_free (manager->current_tool->shortcut);
    manager->current_tool->shortcut = name;
    add_accelerator (manager, manager->current_tool);
    save_current_tool (manager);
    return TRUE;
}

/* ---------------------------------------------------------------- */
/* tree model                                                        */
/* ---------------------------------------------------------------- */

static void
add_tool_to_language (PlumaToolManager *manager, PlumaTool *tool, RowKind kind,
                      GtkSourceLanguage *lang, const gchar *key)
{
    GtkTreeRowReference *parent_ref = g_hash_table_lookup (manager->language_rows, key);
    GtkTreeIter parent_iter, child_iter;
    GtkTreePath *path;
    GList *rows;

    if (parent_ref == NULL) {
        gtk_tree_store_append (manager->model, &parent_iter, NULL);
        gtk_tree_store_set (manager->model, &parent_iter,
                            COL_POINTER, kind == ROW_LANGUAGE ? (gpointer) lang : NULL,
                            COL_KIND, (gint) kind,
                            COL_KEY, key,
                            -1);
        path = gtk_tree_model_get_path (GTK_TREE_MODEL (manager->model), &parent_iter);
        parent_ref = gtk_tree_row_reference_new (GTK_TREE_MODEL (manager->model), path);
        gtk_tree_path_free (path);
        g_hash_table_insert (manager->language_rows, g_strdup (key), parent_ref);
    } else {
        path = gtk_tree_row_reference_get_path (parent_ref);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (manager->model), &parent_iter, path);
        gtk_tree_path_free (path);
    }

    gtk_tree_store_append (manager->model, &child_iter, &parent_iter);
    gtk_tree_store_set (manager->model, &child_iter, COL_POINTER, tool, COL_KIND, (gint) ROW_TOOL, COL_KEY, NULL, -1);

    path = gtk_tree_model_get_path (GTK_TREE_MODEL (manager->model), &child_iter);
    rows = g_hash_table_lookup (manager->tool_rows, tool);
    rows = g_list_append (rows, gtk_tree_row_reference_new (GTK_TREE_MODEL (manager->model), path));
    gtk_tree_path_free (path);
    g_hash_table_insert (manager->tool_rows, tool, rows);
}

static void
add_tool (PlumaToolManager *manager, PlumaTool *tool)
{
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
    gboolean added = FALSE;

    if (tool->languages != NULL) {
        guint i;
        for (i = 0; tool->languages[i] != NULL; i++) {
            const gchar *lang_id = tool->languages[i];
            GtkSourceLanguage *lang = gtk_source_language_manager_get_language (lm, lang_id);

            if (lang != NULL) {
                add_tool_to_language (manager, tool, ROW_LANGUAGE, lang, lang_id);
                added = TRUE;
            } else if (g_strcmp0 (lang_id, PLAIN_TEXT_KEY) == 0) {
                add_tool_to_language (manager, tool, ROW_PLAIN_TEXT, NULL, PLAIN_TEXT_KEY);
                added = TRUE;
            }
        }
    }

    if (!added)
        add_tool_to_language (manager, tool, ROW_ALL_LANGUAGES, NULL, ALL_LANGUAGES_KEY);

    add_accelerator (manager, tool);
}

static void
remove_tool_rows (PlumaToolManager *manager, PlumaTool *tool)
{
    GList *refs = g_hash_table_lookup (manager->tool_rows, tool);
    GList *l;

    for (l = refs; l != NULL; l = l->next) {
        GtkTreeRowReference *ref = l->data;
        GtkTreePath *path;
        GtkTreeIter iter, parent_iter;
        gboolean has_parent;

        if (!gtk_tree_row_reference_valid (ref)) {
            gtk_tree_row_reference_free (ref);
            continue;
        }

        path = gtk_tree_row_reference_get_path (ref);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (manager->model), &iter, path);
        gtk_tree_path_free (path);

        has_parent = gtk_tree_model_iter_parent (GTK_TREE_MODEL (manager->model), &parent_iter, &iter);

        gtk_tree_store_remove (manager->model, &iter);
        gtk_tree_row_reference_free (ref);

        if (has_parent && !gtk_tree_model_iter_has_child (GTK_TREE_MODEL (manager->model), &parent_iter)) {
            gchar *key = NULL;
            gtk_tree_model_get (GTK_TREE_MODEL (manager->model), &parent_iter, COL_KEY, &key, -1);
            gtk_tree_store_remove (manager->model, &parent_iter);
            if (key != NULL) {
                g_hash_table_remove (manager->language_rows, key);
                g_free (key);
            }
        }
    }
    g_list_free (refs);
    g_hash_table_remove (manager->tool_rows, tool);
}

static gint
sort_tools (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
    gpointer pa, pb;
    gint ka, kb;
    GtkTreeIter parent;
    gchar *na, *nb;
    gint result;

    gtk_tree_model_get (model, a, COL_POINTER, &pa, COL_KIND, &ka, -1);
    gtk_tree_model_get (model, b, COL_POINTER, &pb, COL_KIND, &kb, -1);

    if (!gtk_tree_model_iter_parent (model, &parent, a)) {
        if (ka == ROW_ALL_LANGUAGES) return -1;
        if (kb == ROW_ALL_LANGUAGES) return 1;
        na = g_utf8_casefold (ka == ROW_PLAIN_TEXT ? _("Plain Text") : gtk_source_language_get_name (pa), -1);
        nb = g_utf8_casefold (kb == ROW_PLAIN_TEXT ? _("Plain Text") : gtk_source_language_get_name (pb), -1);
    } else {
        na = g_utf8_casefold (((PlumaTool *) pa)->name, -1);
        nb = g_utf8_casefold (((PlumaTool *) pb)->name, -1);
    }

    result = g_strcmp0 (na, nb);
    g_free (na);
    g_free (nb);
    return result;
}

static void
get_cell_data_cb (GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model,
                  GtkTreeIter *iter, gpointer user_data)
{
    gpointer ptr;
    gint kind;
    gchar *markup;
    gboolean editable;

    gtk_tree_model_get (model, iter, COL_POINTER, &ptr, COL_KIND, &kind, -1);

    if (kind == ROW_TOOL) {
        PlumaTool *tool = ptr;
        gchar *escaped = g_markup_escape_text (tool->name, -1);

        if (tool->shortcut != NULL) {
            gchar *escaped_accel = g_markup_escape_text (tool->shortcut, -1);
            markup = g_strdup_printf ("%s (<b>%s</b>)", escaped, escaped_accel);
            g_free (escaped_accel);
            g_free (escaped);
        } else {
            markup = escaped;
        }
        editable = TRUE;
    } else {
        const gchar *label;

        if (kind == ROW_ALL_LANGUAGES)
            label = _("All Languages");
        else if (kind == ROW_PLAIN_TEXT)
            label = _("Plain Text");
        else
            label = gtk_source_language_get_name (GTK_SOURCE_LANGUAGE (ptr));

        markup = g_markup_escape_text (label, -1);
        editable = FALSE;
    }

    g_object_set (cell, "markup", markup, "editable", editable, NULL);
    g_free (markup);
}

static PlumaTool *
get_selected_tool (PlumaToolManager *manager, GtkTreeIter *out_iter)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection (manager->view);
    GtkTreeIter iter;
    gpointer ptr;
    gint kind;

    if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
        return NULL;

    gtk_tree_model_get (GTK_TREE_MODEL (manager->model), &iter, COL_POINTER, &ptr, COL_KIND, &kind, -1);
    if (kind != ROW_TOOL)
        return NULL;

    if (out_iter != NULL)
        *out_iter = iter;
    return ptr;
}

static gchar *
row_language_key (PlumaToolManager *manager, GtkTreeIter *iter)
{
    gchar *key = NULL;
    gtk_tree_model_get (GTK_TREE_MODEL (manager->model), iter, COL_KEY, &key, -1);
    return key;
}

/* ---------------------------------------------------------------- */
/* field <-> tool synchronization                                   */
/* ---------------------------------------------------------------- */

static gchar *
combo_value (GtkComboBox *combo)
{
    GtkTreeIter iter;
    gchar *value = NULL;

    if (gtk_combo_box_get_active_iter (combo, &iter))
        gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter, 1, &value, -1);
    return value;
}

static gboolean
set_active_by_name (GtkComboBox *combo, const gchar *option_name)
{
    GtkTreeModel *model = gtk_combo_box_get_model (combo);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first (model, &iter);

    while (valid) {
        gchar *value = NULL;
        gboolean match;

        gtk_tree_model_get (model, &iter, 1, &value, -1);
        match = g_strcmp0 (value, option_name) == 0;
        g_free (value);

        if (match) {
            gtk_combo_box_set_active_iter (combo, &iter);
            return TRUE;
        }
        valid = gtk_tree_model_iter_next (model, &iter);
    }
    return FALSE;
}

static void
save_current_tool (PlumaToolManager *manager)
{
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    gchar *script;
    gchar *value;

    if (manager->current_tool == NULL)
        return;

    if (manager->current_tool->filename == NULL)
        pluma_tool_autoset_filename (manager->current_tool, manager->user_dir);

    value = combo_value (manager->input_combo);
    if (value != NULL) { g_free (manager->current_tool->input); manager->current_tool->input = value; }
    value = combo_value (manager->output_combo);
    if (value != NULL) { g_free (manager->current_tool->output); manager->current_tool->output = value; }
    value = combo_value (manager->applicability_combo);
    if (value != NULL) { g_free (manager->current_tool->applicability); manager->current_tool->applicability = value; }
    value = combo_value (manager->save_files_combo);
    if (value != NULL) { g_free (manager->current_tool->save_files); manager->current_tool->save_files = value; }

    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (manager->commands_view));
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    script = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

    pluma_tool_save_with_script (manager->current_tool, manager->user_dir, script);
    g_free (script);

    update_remove_revert (manager);
}

static void
clear_fields (PlumaToolManager *manager)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (manager->commands_view));

    gtk_entry_set_text (manager->accelerator_entry, "");

    gtk_text_buffer_begin_user_action (buffer);
    gtk_text_buffer_set_text (buffer, "", -1);
    gtk_text_buffer_end_user_action (buffer);

    gtk_combo_box_set_active (manager->input_combo, 0);
    gtk_combo_box_set_active (manager->output_combo, 0);
    gtk_combo_box_set_active (manager->applicability_combo, 0);
    gtk_combo_box_set_active (manager->save_files_combo, 0);

    gtk_label_set_text (manager->languages_label, _("All Languages"));
}

static void
fill_languages_button (PlumaToolManager *manager)
{
    if (manager->current_tool == NULL || manager->current_tool->languages == NULL ||
        manager->current_tool->languages[0] == NULL) {
        gtk_label_set_text (manager->languages_label, _("All Languages"));
        return;
    }

    {
        GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
        GString *names = g_string_new (NULL);
        guint i;

        for (i = 0; manager->current_tool->languages[i] != NULL; i++) {
            const gchar *id = manager->current_tool->languages[i];
            const gchar *name = NULL;

            if (g_strcmp0 (id, PLAIN_TEXT_KEY) == 0) {
                name = _("Plain Text");
            } else {
                GtkSourceLanguage *lang = gtk_source_language_manager_get_language (lm, id);
                if (lang != NULL)
                    name = gtk_source_language_get_name (lang);
            }

            if (name != NULL) {
                if (names->len > 0)
                    g_string_append (names, ", ");
                g_string_append (names, name);
            }
        }

        gtk_label_set_text (manager->languages_label, names->str);
        g_string_free (names, TRUE);
    }
}

static void
fill_fields (PlumaToolManager *manager)
{
    PlumaTool *tool = manager->current_tool;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (manager->commands_view));
    gchar *script;
    gchar *content_type;
    GtkSourceLanguageManager *lmanager;
    GtkSourceLanguage *language;

    gtk_entry_set_text (manager->accelerator_entry, tool->shortcut != NULL ? tool->shortcut : "");

    script = pluma_tool_get_script (tool);
    gtk_text_buffer_begin_user_action (buffer);
    gtk_text_buffer_set_text (buffer, script, -1);
    gtk_text_buffer_end_user_action (buffer);

    content_type = g_content_type_guess (NULL, (const guchar *) script, strlen (script), NULL);
    lmanager = gtk_source_language_manager_get_default ();
    language = gtk_source_language_manager_guess_language (lmanager, NULL, content_type);
    if (language != NULL) {
        pluma_document_set_language (PLUMA_DOCUMENT (buffer), language);
        gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (buffer), TRUE);
    } else {
        gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (buffer), FALSE);
    }
    g_free (content_type);
    g_free (script);

    set_active_by_name (manager->input_combo, tool->input);
    set_active_by_name (manager->output_combo, tool->output);
    set_active_by_name (manager->applicability_combo, tool->applicability);
    set_active_by_name (manager->save_files_combo, tool->save_files);

    fill_languages_button (manager);
}

static void
update_remove_revert (PlumaToolManager *manager)
{
    PlumaTool *tool = get_selected_tool (manager, NULL);
    gboolean removable = (tool != NULL) && tool->is_local;

    gtk_widget_set_sensitive (manager->remove_button, removable);
    gtk_widget_set_sensitive (manager->revert_button, removable);

    if (tool != NULL && tool->is_global) {
        gtk_widget_hide (manager->remove_button);
        gtk_widget_show (manager->revert_button);
    } else {
        gtk_widget_show (manager->remove_button);
        gtk_widget_hide (manager->revert_button);
    }
}

static void
do_update (PlumaToolManager *manager)
{
    update_remove_revert (manager);
    manager->current_tool = get_selected_tool (manager, NULL);

    if (manager->current_tool != NULL) {
        fill_fields (manager);
        gtk_widget_set_sensitive (manager->tool_table, TRUE);
    } else {
        clear_fields (manager);
        gtk_widget_set_sensitive (manager->tool_table, FALSE);
    }
}

static gchar *
selected_language_key (PlumaToolManager *manager)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection (manager->view);
    GtkTreeIter iter, parent;
    gpointer ptr;
    gint kind;

    if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
        return NULL;

    gtk_tree_model_get (GTK_TREE_MODEL (manager->model), &iter, COL_POINTER, &ptr, COL_KIND, &kind, -1);
    if (kind == ROW_TOOL) {
        if (!gtk_tree_model_iter_parent (GTK_TREE_MODEL (manager->model), &parent, &iter))
            return NULL;
        return row_language_key (manager, &parent);
    }
    return row_language_key (manager, &iter);
}

/* ---------------------------------------------------------------- */
/* signal handlers                                                   */
/* ---------------------------------------------------------------- */

static void
on_new_tool_button_clicked (GtkButton *button, PlumaToolManager *manager)
{
    PlumaTool *tool;
    GList *rows;
    GtkTreeRowReference *ref;
    GtkTreePath *path;
    gchar *lang_key;

    save_current_tool (manager);

    g_signal_handler_block (gtk_tree_view_get_selection (manager->view), manager->selection_changed_id);

    tool = pluma_tool_new ();
    tool->name = g_strdup (_("New tool"));
    g_ptr_array_add (manager->tools, tool);

    lang_key = selected_language_key (manager);
    if (lang_key != NULL && *lang_key != '\0') {
        tool->languages = g_new0 (gchar *, 2);
        tool->languages[0] = g_strdup (lang_key);
    }
    g_free (lang_key);

    add_tool (manager, tool);

    rows = g_hash_table_lookup (manager->tool_rows, tool);
    ref = g_list_last (rows)->data;
    path = gtk_tree_row_reference_get_path (ref);
    gtk_tree_view_expand_to_path (manager->view, path);
    gtk_tree_view_set_cursor (manager->view, path, gtk_tree_view_get_column (manager->view, 0), TRUE);
    gtk_tree_path_free (path);

    manager->current_tool = tool;
    fill_fields (manager);
    gtk_widget_set_sensitive (manager->tool_table, TRUE);

    g_signal_handler_unblock (gtk_tree_view_get_selection (manager->view), manager->selection_changed_id);
}

static void
tool_changed (PlumaToolManager *manager, PlumaTool *tool, gboolean refresh)
{
    GList *rows = g_hash_table_lookup (manager->tool_rows, tool);
    GList *l;

    for (l = rows; l != NULL; l = l->next) {
        GtkTreeRowReference *ref = l->data;
        GtkTreePath *path;
        GtkTreeIter iter;

        if (!gtk_tree_row_reference_valid (ref))
            continue;

        path = gtk_tree_row_reference_get_path (ref);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (manager->model), &iter, path);
        gtk_tree_model_row_changed (GTK_TREE_MODEL (manager->model), path, &iter);
        gtk_tree_path_free (path);
    }

    if (refresh && tool == manager->current_tool)
        fill_fields (manager);

    update_remove_revert (manager);
}

static void
on_remove_tool_button_clicked (GtkButton *button, PlumaToolManager *manager)
{
    GtkTreeIter iter;
    PlumaTool *tool = get_selected_tool (manager, &iter);

    if (tool == NULL)
        return;

    if (tool->is_global) {
        gchar *shortcut = g_strdup (tool->shortcut);

        if (pluma_tool_revert (tool, manager->user_dir, manager->system_dir)) {
            remove_accelerator (manager, tool, shortcut);
            add_accelerator (manager, tool);

            gtk_widget_set_sensitive (manager->revert_button, FALSE);
            fill_fields (manager);
            tool_changed (manager, tool, FALSE);
        }
        g_free (shortcut);
    } else {
        GtkTreeIter parent_iter;
        gchar *language_key;
        GList *rows;
        gboolean has_parent = gtk_tree_model_iter_parent (GTK_TREE_MODEL (manager->model), &parent_iter, &iter);

        language_key = has_parent ? row_language_key (manager, &parent_iter) : NULL;

        gtk_tree_store_remove (manager->model, &iter);

        if (language_key != NULL && tool->languages != NULL) {
            guint i;
            GPtrArray *kept = g_ptr_array_new ();
            for (i = 0; tool->languages[i] != NULL; i++) {
                if (g_strcmp0 (tool->languages[i], language_key) == 0)
                    g_free (tool->languages[i]);
                else
                    g_ptr_array_add (kept, tool->languages[i]);
            }
            g_free (tool->languages);
            g_ptr_array_add (kept, NULL);
            tool->languages = (gchar **) g_ptr_array_free (kept, FALSE);
        }

        rows = g_hash_table_lookup (manager->tool_rows, tool);
        {
            GList *valid_rows = NULL, *l;
            for (l = rows; l != NULL; l = l->next) {
                GtkTreeRowReference *ref = l->data;
                if (gtk_tree_row_reference_valid (ref))
                    valid_rows = g_list_append (valid_rows, ref);
                else
                    gtk_tree_row_reference_free (ref);
            }
            g_list_free (rows);
            rows = valid_rows;
        }

        if (rows == NULL) {
            g_hash_table_remove (manager->tool_rows, tool);

            if (pluma_tool_delete (tool, manager->user_dir)) {
                remove_accelerator (manager, tool, NULL);
                manager->current_tool = NULL;
                g_ptr_array_remove (manager->tools, tool);
                pluma_tool_free (tool);
            }
            gtk_widget_grab_focus (GTK_WIDGET (manager->view));
        } else {
            g_hash_table_insert (manager->tool_rows, tool, rows);
        }

        if (language_key != NULL) {
            GtkTreeRowReference *lang_ref = g_hash_table_lookup (manager->language_rows, language_key);
            if (lang_ref != NULL && gtk_tree_row_reference_valid (lang_ref)) {
                GtkTreePath *lang_path = gtk_tree_row_reference_get_path (lang_ref);
                GtkTreeIter lang_iter;
                gtk_tree_model_get_iter (GTK_TREE_MODEL (manager->model), &lang_iter, lang_path);
                if (!gtk_tree_model_iter_has_child (GTK_TREE_MODEL (manager->model), &lang_iter)) {
                    gtk_tree_store_remove (manager->model, &lang_iter);
                    g_hash_table_remove (manager->language_rows, language_key);
                }
                gtk_tree_path_free (lang_path);
            }
            g_free (language_key);
        }
    }
}

static void
on_view_label_cell_edited (GtkCellRendererText *cell, gchar *path_str, gchar *new_text, PlumaToolManager *manager)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    PlumaTool *tool;

    if (new_text == NULL || *new_text == '\0')
        return;

    path = gtk_tree_path_new_from_string (path_str);
    gtk_tree_model_get_iter (GTK_TREE_MODEL (manager->model), &iter, path);
    gtk_tree_path_free (path);

    gtk_tree_model_get (GTK_TREE_MODEL (manager->model), &iter, COL_POINTER, &tool, -1);

    g_free (tool->name);
    tool->name = g_strdup (new_text);

    save_current_tool (manager);
    tool_changed (manager, tool, FALSE);
}

static void
on_view_label_cell_editing_started (GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *path_str,
                                    PlumaToolManager *manager)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    PlumaTool *tool;

    path = gtk_tree_path_new_from_string (path_str);
    gtk_tree_model_get_iter (GTK_TREE_MODEL (manager->model), &iter, path);
    gtk_tree_path_free (path);

    gtk_tree_model_get (GTK_TREE_MODEL (manager->model), &iter, COL_POINTER, &tool, -1);

    if (GTK_IS_ENTRY (editable)) {
        gtk_entry_set_text (GTK_ENTRY (editable), tool->name);
        gtk_widget_grab_focus (GTK_WIDGET (editable));
    }
}

static void
on_view_selection_changed (GtkTreeSelection *selection, PlumaToolManager *manager)
{
    save_current_tool (manager);
    do_update (manager);
}

static gboolean
is_function_key (const gchar *keyname)
{
    gint n;
    if (keyname == NULL || keyname[0] != 'F')
        return FALSE;
    return sscanf (keyname + 1, "%d", &n) == 1 && n >= 1 && n <= 12;
}

static gboolean
on_accelerator_key_press (GtkWidget *entry, GdkEventKey *event, PlumaToolManager *manager)
{
    GdkModifierType mask = event->state & gtk_accelerator_get_default_mod_mask ();
    const gchar *keyname = gdk_keyval_name (event->keyval);

    if (manager->current_tool == NULL)
        return FALSE;

    if (g_strcmp0 (keyname, "Escape") == 0) {
        gtk_entry_set_text (GTK_ENTRY (entry), manager->current_tool->shortcut != NULL ? manager->current_tool->shortcut : "");
        gtk_widget_grab_focus (GTK_WIDGET (manager->commands_view));
        return TRUE;
    }
    if (g_strcmp0 (keyname, "Delete") == 0 || g_strcmp0 (keyname, "BackSpace") == 0) {
        gtk_entry_set_text (GTK_ENTRY (entry), "");
        remove_accelerator (manager, manager->current_tool, NULL);
        g_free (manager->current_tool->shortcut);
        manager->current_tool->shortcut = NULL;
        save_current_tool (manager);
        gtk_widget_grab_focus (GTK_WIDGET (manager->commands_view));
        return TRUE;
    }
    if (is_function_key (keyname)) {
        if (set_accelerator (manager, event->keyval, mask)) {
            gtk_entry_set_text (GTK_ENTRY (entry), manager->current_tool->shortcut != NULL ? manager->current_tool->shortcut : "");
            gtk_widget_grab_focus (GTK_WIDGET (manager->commands_view));
        }
        return TRUE;
    }
    if (gdk_keyval_to_unicode (event->keyval) != 0) {
        if (mask != 0 && set_accelerator (manager, event->keyval, mask)) {
            gtk_entry_set_text (GTK_ENTRY (entry), manager->current_tool->shortcut != NULL ? manager->current_tool->shortcut : "");
            gtk_widget_grab_focus (GTK_WIDGET (manager->commands_view));
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean
on_accelerator_focus_in (GtkWidget *entry, GdkEventFocus *event, PlumaToolManager *manager)
{
    if (manager->current_tool == NULL)
        return FALSE;

    gtk_entry_set_text (GTK_ENTRY (entry), manager->current_tool->shortcut != NULL
                        ? _("Type a new accelerator, or press Backspace to clear")
                        : _("Type a new accelerator"));
    return FALSE;
}

static gboolean
on_accelerator_focus_out (GtkWidget *entry, GdkEventFocus *event, PlumaToolManager *manager)
{
    if (manager->current_tool != NULL) {
        gtk_entry_set_text (GTK_ENTRY (entry), manager->current_tool->shortcut != NULL ? manager->current_tool->shortcut : "");
        tool_changed (manager, manager->current_tool, FALSE);
    }
    return FALSE;
}

static void
finalize_editing (PlumaToolManager *manager)
{
    save_current_tool (manager);
    if (manager->updated_cb != NULL)
        manager->updated_cb (manager->user_data);
}

static void
on_tool_manager_dialog_response (GtkDialog *dialog, gint response, PlumaToolManager *manager)
{
    if (response == GTK_RESPONSE_HELP) {
        pluma_help_display (GTK_WINDOW (manager->dialog), NULL, "pluma-external-tools-plugin");
        return;
    }

    finalize_editing (manager);

    gtk_widget_hide (manager->dialog);
}

static gboolean
on_tool_manager_dialog_delete_event (GtkWidget *dialog,
                                     GdkEvent  *event,
                                     PlumaToolManager *manager)
{
    /* GtkDialog's default delete handler destroys its child hierarchy.  The
     * manager is persistent, so keep that hierarchy alive for the next open. */
    finalize_editing (manager);
    gtk_widget_hide (dialog);
    return TRUE;
}

static gboolean
on_tool_manager_dialog_configure_event (GtkWidget *dialog, GdkEventConfigure *event, PlumaToolManager *manager)
{
    if (gtk_widget_get_realized (dialog)) {
        GtkAllocation alloc;
        gtk_widget_get_allocation (dialog, &alloc);
        manager->width = alloc.width;
        manager->height = alloc.height;
    }
    return FALSE;
}

static gboolean
on_tool_manager_dialog_focus_out (GtkWidget *dialog, GdkEventFocus *event, PlumaToolManager *manager)
{
    finalize_editing (manager);
    return FALSE;
}

/* ---------------------------------------------------------------- */
/* languages popover                                                 */
/* ---------------------------------------------------------------- */

static gint
compare_language_names (gconstpointer a, gconstpointer b)
{
    return g_utf8_collate (gtk_source_language_get_name ((GtkSourceLanguage *) a),
                           gtk_source_language_get_name ((GtkSourceLanguage *) b));
}

static gboolean
strv_contains (gchar **strv, const gchar *value)
{
    guint i;
    if (strv == NULL)
        return FALSE;
    for (i = 0; strv[i] != NULL; i++)
        if (g_strcmp0 (strv[i], value) == 0)
            return TRUE;
    return FALSE;
}

static void
languages_popup_init_languages (GtkListStore *model, gchar **current_languages)
{
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
    const gchar * const *ids = gtk_source_language_manager_get_language_ids (lm);
    GList *langs = NULL, *l;
    GtkTreeIter iter;
    guint i;

    for (i = 0; ids != NULL && ids[i] != NULL; i++) {
        GtkSourceLanguage *lang = gtk_source_language_manager_get_language (lm, ids[i]);
        if (lang != NULL)
            langs = g_list_prepend (langs, lang);
    }
    langs = g_list_sort (langs, compare_language_names);

    gtk_list_store_append (model, &iter);
    gtk_list_store_set (model, &iter, LANG_COL_NAME, _("All languages"), LANG_COL_ID, NULL,
                        LANG_COL_ENABLED, current_languages == NULL || current_languages[0] == NULL, -1);

    gtk_list_store_append (model, &iter);
    gtk_list_store_set (model, &iter, LANG_COL_NAME, "-", LANG_COL_ID, NULL, LANG_COL_ENABLED, FALSE, -1);

    gtk_list_store_append (model, &iter);
    gtk_list_store_set (model, &iter, LANG_COL_NAME, _("Plain Text"), LANG_COL_ID, PLAIN_TEXT_KEY,
                        LANG_COL_ENABLED, strv_contains (current_languages, PLAIN_TEXT_KEY), -1);

    gtk_list_store_append (model, &iter);
    gtk_list_store_set (model, &iter, LANG_COL_NAME, "-", LANG_COL_ID, NULL, LANG_COL_ENABLED, FALSE, -1);

    for (l = langs; l != NULL; l = l->next) {
        GtkSourceLanguage *lang = l->data;
        const gchar *id = gtk_source_language_get_id (lang);

        gtk_list_store_append (model, &iter);
        gtk_list_store_set (model, &iter, LANG_COL_NAME, gtk_source_language_get_name (lang),
                            LANG_COL_ID, id, LANG_COL_ENABLED, strv_contains (current_languages, id), -1);
    }
    g_list_free (langs);
}

static gboolean
lang_row_is_separator (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gchar *name;
    gboolean is_sep;
    gtk_tree_model_get (model, iter, LANG_COL_NAME, &name, -1);
    is_sep = g_strcmp0 (name, "-") == 0;
    g_free (name);
    return is_sep;
}

static void
lang_toggled (GtkCellRendererToggle *renderer, gchar *path_str, LanguagesPopup *popup)
{
    GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
    GtkTreeIter iter;
    gboolean enabled;
    gboolean is_first = gtk_tree_path_get_indices (path)[0] == 0;

    gtk_tree_model_get_iter (GTK_TREE_MODEL (popup->model), &iter, path);
    gtk_tree_model_get (GTK_TREE_MODEL (popup->model), &iter, LANG_COL_ENABLED, &enabled, -1);
    gtk_list_store_set (popup->model, &iter, LANG_COL_ENABLED, !enabled, -1);
    gtk_tree_path_free (path);

    if (is_first && !enabled) {
        GtkTreeIter it2;
        gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (popup->model), &it2);
        gboolean first = TRUE;
        while (valid) {
            if (!first)
                gtk_list_store_set (popup->model, &it2, LANG_COL_ENABLED, FALSE, -1);
            first = FALSE;
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (popup->model), &it2);
        }
    } else if (!is_first) {
        GtkTreeIter first_iter;
        gtk_tree_model_get_iter_first (GTK_TREE_MODEL (popup->model), &first_iter);
        gtk_list_store_set (popup->model, &first_iter, LANG_COL_ENABLED, FALSE, -1);
    }
}

static gchar **
languages_popup_get_selected (GtkListStore *model)
{
    GtkTreeIter iter;
    gboolean all_selected = FALSE;
    GPtrArray *ids = g_ptr_array_new ();
    gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter);
    gint idx = 0;

    while (valid) {
        gchar *id = NULL;
        gboolean enabled = FALSE;

        gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, LANG_COL_ID, &id, LANG_COL_ENABLED, &enabled, -1);

        if (idx == 0 && enabled)
            all_selected = TRUE;
        else if (enabled && id != NULL)
            g_ptr_array_add (ids, id);
        else
            g_free (id);

        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter);
        idx++;
    }

    if (all_selected) {
        g_ptr_array_set_free_func (ids, g_free);
        g_ptr_array_free (ids, TRUE);
        return NULL;
    }

    g_ptr_array_add (ids, NULL);
    return (gchar **) g_ptr_array_free (ids, FALSE);
}

static void
update_languages (PlumaToolManager *manager, gchar **new_ids)
{
    PlumaTool *tool = manager->current_tool;
    GList *rows;

    if (tool == NULL)
        return;

    g_strfreev (tool->languages);
    tool->languages = g_strdupv (new_ids);
    fill_languages_button (manager);
    save_current_tool (manager);

    g_signal_handler_block (gtk_tree_view_get_selection (manager->view), manager->selection_changed_id);

    remove_tool_rows (manager, tool);
    add_tool (manager, tool);

    rows = g_hash_table_lookup (manager->tool_rows, tool);
    if (rows != NULL) {
        GtkTreeRowReference *ref = rows->data;
        GtkTreePath *path = gtk_tree_row_reference_get_path (ref);

        gtk_tree_view_expand_to_path (manager->view, path);
        gtk_tree_selection_select_path (gtk_tree_view_get_selection (manager->view), path);
        gtk_tree_path_free (path);
    }

    g_signal_handler_unblock (gtk_tree_view_get_selection (manager->view), manager->selection_changed_id);
}

static void
on_languages_popup_closed (GtkPopover *popover, gpointer user_data)
{
    LanguagesPopup *popup = user_data;
    gchar **ids = languages_popup_get_selected (popup->model);

    update_languages (popup->manager, ids);
    g_strfreev (ids);

    gtk_widget_destroy (popup->popover);
    g_free (popup);
}

static void
on_languages_button_clicked (GtkButton *button, PlumaToolManager *manager)
{
    LanguagesPopup *popup;
    GtkWidget *sw, *view;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    if (manager->current_tool == NULL)
        return;

    popup = g_new0 (LanguagesPopup, 1);
    popup->manager = manager;
    popup->model = gtk_list_store_new (LANG_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    languages_popup_init_languages (popup->model, manager->current_tool->languages);

    popup->popover = gtk_popover_new (GTK_WIDGET (button));
    gtk_widget_set_can_focus (popup->popover, TRUE);

    sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_size_request (sw, -1, 200);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_ETCHED_IN);

    view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (popup->model));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
    gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (view), lang_row_is_separator, NULL, NULL);

    column = gtk_tree_view_column_new ();
    renderer = gtk_cell_renderer_toggle_new ();
    gtk_tree_view_column_pack_start (column, renderer, FALSE);
    gtk_tree_view_column_add_attribute (column, renderer, "active", LANG_COL_ENABLED);
    g_signal_connect (renderer, "toggled", G_CALLBACK (lang_toggled), popup);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (column, renderer, TRUE);
    gtk_tree_view_column_add_attribute (column, renderer, "text", LANG_COL_NAME);

    gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);
    gtk_tree_selection_select_path (gtk_tree_view_get_selection (GTK_TREE_VIEW (view)), gtk_tree_path_new_first ());

    gtk_container_add (GTK_CONTAINER (sw), view);
    gtk_container_add (GTK_CONTAINER (popup->popover), sw);
    gtk_widget_show_all (sw);

    g_signal_connect (popup->popover, "closed", G_CALLBACK (on_languages_popup_closed), popup);

    gtk_popover_popup (GTK_POPOVER (popup->popover));
}

/* ---------------------------------------------------------------- */
/* construction                                                      */
/* ---------------------------------------------------------------- */

static gboolean
build (PlumaToolManager *manager)
{
    gchar *ui_file = g_build_filename (manager->data_dir, "ui", "tools.ui", NULL);
    /* GtkBuilder does not automatically instantiate top-level objects that
     * are referenced by a requested root.  The dialog references these list
     * stores and the PlumaView references commands_buffer. */
    gchar *root_objects[] = {
        "commands_buffer",
        "model_applicability",
        "model_input",
        "model_output",
        "model_save_files",
        "tool-manager-dialog",
        NULL
    };
    GtkWidget *error_widget = NULL;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    guint i;

    /* tools.ui instantiates <object class="PlumaDocument"> / "PlumaView">;
     * make sure those GTypes are registered before GtkBuilder parses it. */
    g_type_ensure (PLUMA_TYPE_DOCUMENT);
    g_type_ensure (PLUMA_TYPE_VIEW);

    if (!pluma_utils_get_ui_objects (ui_file, root_objects, &error_widget,
                                     "tool-manager-dialog", &manager->dialog,
                                     "view", &manager->view,
                                     "new-tool-button", &manager->new_button,
                                     "remove-tool-button", &manager->remove_button,
                                     "revert-tool-button", &manager->revert_button,
                                     "tool-table", &manager->tool_table,
                                     "input", &manager->input_combo,
                                     "output", &manager->output_combo,
                                     "save-files", &manager->save_files_combo,
                                     "applicability", &manager->applicability_combo,
                                     "accelerator", &manager->accelerator_entry,
                                     "commands", &manager->commands_view,
                                     "languages_button", &manager->languages_button,
                                     "languages_label", &manager->languages_label,
                                     NULL)) {
        g_warning ("External Tools: could not load tools.ui from %s", ui_file);
        if (error_widget != NULL)
            gtk_widget_destroy (error_widget);
        g_free (ui_file);
        return FALSE;
    }
    g_free (ui_file);

    if (manager->width > 0 && manager->height > 0)
        gtk_window_set_default_size (GTK_WINDOW (manager->dialog), manager->width, manager->height);

    manager->model = gtk_tree_store_new (N_COLUMNS, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_STRING);
    gtk_tree_view_set_model (manager->view, GTK_TREE_MODEL (manager->model));

    for (i = 0; i < manager->tools->len; i++)
        add_tool (manager, g_ptr_array_index (manager->tools, i));

    gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (manager->model), sort_tools, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (manager->model),
                                         GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);

    column = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_title (column, _("Tools"));
    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (column, renderer, FALSE);
    g_object_set (renderer, "editable", TRUE, NULL);
    gtk_tree_view_append_column (manager->view, column);
    gtk_tree_view_column_set_cell_data_func (column, renderer, get_cell_data_cb, NULL, NULL);

    g_signal_connect (renderer, "edited", G_CALLBACK (on_view_label_cell_edited), manager);
    g_signal_connect (renderer, "editing-started", G_CALLBACK (on_view_label_cell_editing_started), manager);

    manager->selection_changed_id = g_signal_connect (gtk_tree_view_get_selection (manager->view), "changed",
                                                       G_CALLBACK (on_view_selection_changed), manager);

    gtk_combo_box_set_active (manager->input_combo, 0);
    gtk_combo_box_set_active (manager->output_combo, 0);
    gtk_combo_box_set_active (manager->applicability_combo, 0);
    gtk_combo_box_set_active (manager->save_files_combo, 0);

    g_signal_connect (manager->new_button, "clicked", G_CALLBACK (on_new_tool_button_clicked), manager);
    g_signal_connect (manager->remove_button, "clicked", G_CALLBACK (on_remove_tool_button_clicked), manager);
    g_signal_connect (manager->revert_button, "clicked", G_CALLBACK (on_remove_tool_button_clicked), manager);
    g_signal_connect (manager->dialog, "response", G_CALLBACK (on_tool_manager_dialog_response), manager);
    g_signal_connect (manager->dialog, "delete-event", G_CALLBACK (on_tool_manager_dialog_delete_event), manager);
    g_signal_connect (manager->dialog, "configure-event", G_CALLBACK (on_tool_manager_dialog_configure_event), manager);
    g_signal_connect (manager->dialog, "focus-out-event", G_CALLBACK (on_tool_manager_dialog_focus_out), manager);
    g_signal_connect (manager->accelerator_entry, "key-press-event", G_CALLBACK (on_accelerator_key_press), manager);
    g_signal_connect (manager->accelerator_entry, "focus-in-event", G_CALLBACK (on_accelerator_focus_in), manager);
    g_signal_connect (manager->accelerator_entry, "focus-out-event", G_CALLBACK (on_accelerator_focus_out), manager);
    g_signal_connect (manager->languages_button, "clicked", G_CALLBACK (on_languages_button_clicked), manager);

    do_update (manager);
    return TRUE;
}

PlumaToolManager *
pluma_tool_manager_new (const gchar *data_dir, const gchar *system_dir, const gchar *user_dir,
                        GPtrArray *tools, gint initial_width, gint initial_height)
{
    PlumaToolManager *manager = g_new0 (PlumaToolManager, 1);

    manager->data_dir = g_strdup (data_dir);
    manager->system_dir = g_strdup (system_dir);
    manager->user_dir = g_strdup (user_dir);
    manager->tools = tools;
    manager->width = initial_width;
    manager->height = initial_height;

    manager->language_rows = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                    (GDestroyNotify) gtk_tree_row_reference_free);
    manager->tool_rows = g_hash_table_new (g_direct_hash, g_direct_equal);
    manager->accelerators = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    if (!build (manager)) {
        pluma_tool_manager_free (manager);
        return NULL;
    }

    return manager;
}

void
pluma_tool_manager_set_callbacks (PlumaToolManager *manager, PlumaToolManagerUpdatedFunc updated,
                                  gpointer user_data)
{
    manager->updated_cb = updated;
    manager->user_data = user_data;
}

static void
free_tool_rows_value (gpointer value)
{
    g_list_free_full ((GList *) value, (GDestroyNotify) gtk_tree_row_reference_free);
}

void
pluma_tool_manager_free (PlumaToolManager *manager)
{
    GHashTableIter iter;
    gpointer key, value;

    if (manager == NULL)
        return;

    if (manager->dialog != NULL) {
        gtk_widget_destroy (manager->dialog);
        g_object_unref (manager->dialog);
        manager->dialog = NULL;
    }

    g_hash_table_iter_init (&iter, manager->tool_rows);
    while (g_hash_table_iter_next (&iter, &key, &value))
        free_tool_rows_value (value);

    g_hash_table_unref (manager->language_rows);
    g_hash_table_unref (manager->tool_rows);
    g_hash_table_unref (manager->accelerators);
    g_clear_object (&manager->model);

    g_free (manager->data_dir);
    g_free (manager->system_dir);
    g_free (manager->user_dir);
    g_free (manager);
}

void
pluma_tool_manager_run (PlumaToolManager *manager, PlumaWindow *window)
{
    PlumaDocument *doc;
    gchar *key = NULL;

    if (manager->dialog == NULL && !build (manager))
        return;

    if (!GTK_IS_WINDOW (manager->dialog) ||
        !GTK_IS_TREE_VIEW (manager->view) ||
        manager->model == NULL)
        return;

    if (gtk_tree_view_get_model (manager->view) != GTK_TREE_MODEL (manager->model))
        gtk_tree_view_set_model (manager->view, GTK_TREE_MODEL (manager->model));

    doc = pluma_window_get_active_document (window);
    if (doc != NULL) {
        GtkSourceLanguage *lang = pluma_document_get_language (doc);
        if (lang != NULL && g_hash_table_contains (manager->language_rows, gtk_source_language_get_id (lang)))
            key = g_strdup (gtk_source_language_get_id (lang));
        else if (lang == NULL && g_hash_table_contains (manager->language_rows, PLAIN_TEXT_KEY))
            key = g_strdup (PLAIN_TEXT_KEY);
    }
    if (key == NULL)
        key = g_strdup (ALL_LANGUAGES_KEY);

    {
        GtkTreeRowReference *ref = g_hash_table_lookup (manager->language_rows, key);
        if (ref != NULL && gtk_tree_row_reference_valid (ref)) {
            GtkTreePath *path = gtk_tree_row_reference_get_path (ref);
            gtk_tree_view_expand_row (manager->view, path, FALSE);
            gtk_tree_selection_select_path (gtk_tree_view_get_selection (manager->view), path);
            gtk_tree_path_free (path);
        }
    }
    g_free (key);

    gtk_window_set_transient_for (GTK_WINDOW (manager->dialog), GTK_WINDOW (window));
    gtk_window_group_add_window (pluma_window_get_group (window), GTK_WINDOW (manager->dialog));
    gtk_window_present (GTK_WINDOW (manager->dialog));
}

void
pluma_tool_manager_tool_changed (PlumaToolManager *manager, PlumaTool *tool)
{
    if (manager->dialog == NULL)
        return;
    tool_changed (manager, tool, TRUE);
}
