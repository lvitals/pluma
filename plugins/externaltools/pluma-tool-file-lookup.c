#include "pluma-tool-file-lookup.h"

#include <pluma/pluma-app.h>
#include <pluma/pluma-document.h>

static GFile *
lookup_absolute (const gchar *path)
{
    if (!g_path_is_absolute (path) || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
        return NULL;
    return g_file_new_for_path (path);
}

static GFile *
lookup_cwd (const gchar *path)
{
    gchar *cwd = g_get_current_dir ();
    gchar *real_path = g_build_filename (cwd, path, NULL);
    GFile *file = NULL;

    if (g_file_test (real_path, G_FILE_TEST_IS_REGULAR))
        file = g_file_new_for_path (real_path);

    g_free (real_path);
    g_free (cwd);
    return file;
}

static GFile *
lookup_open_document_relative (const gchar *path)
{
    GList *docs, *l;
    GFile *result = NULL;

    if (g_path_is_absolute (path))
        return NULL;

    docs = pluma_app_get_documents (pluma_app_get_default ());
    for (l = docs; l != NULL && result == NULL; l = l->next) {
        PlumaDocument *doc = PLUMA_DOCUMENT (l->data);
        GFile *location;

        if (!pluma_document_is_local (doc))
            continue;

        location = pluma_document_get_location (doc);
        if (location == NULL)
            continue;

        {
            GFile *parent = g_file_get_parent (location);
            if (parent != NULL) {
                GFile *candidate = g_file_resolve_relative_path (parent, path);
                gchar *candidate_path = g_file_get_path (candidate);

                if (candidate_path != NULL && g_file_test (candidate_path, G_FILE_TEST_IS_REGULAR))
                    result = g_object_ref (candidate);

                g_free (candidate_path);
                g_object_unref (candidate);
                g_object_unref (parent);
            }
        }
        g_object_unref (location);
    }
    g_list_free (docs);
    return result;
}

static GFile *
lookup_open_document_suffix (const gchar *path)
{
    GList *docs, *l;
    GFile *result = NULL;

    if (g_path_is_absolute (path))
        return NULL;

    docs = pluma_app_get_documents (pluma_app_get_default ());
    for (l = docs; l != NULL && result == NULL; l = l->next) {
        PlumaDocument *doc = PLUMA_DOCUMENT (l->data);
        GFile *location;

        if (!pluma_document_is_local (doc))
            continue;

        location = pluma_document_get_location (doc);
        if (location == NULL)
            continue;

        {
            gchar *uri = g_file_get_uri (location);
            if (uri != NULL && g_str_has_suffix (uri, path))
                result = g_object_ref (location);
            g_free (uri);
        }
        g_object_unref (location);
    }
    g_list_free (docs);
    return result;
}

GFile *
pluma_tool_file_lookup (const gchar *path)
{
    GFile *file;

    if (path == NULL || *path == '\0')
        return NULL;

    if ((file = lookup_absolute (path)) != NULL) return file;
    if ((file = lookup_cwd (path)) != NULL) return file;
    if ((file = lookup_open_document_relative (path)) != NULL) return file;
    if ((file = lookup_open_document_suffix (path)) != NULL) return file;
    return NULL;
}
