#ifndef PLUMA_TOOL_LIBRARY_H
#define PLUMA_TOOL_LIBRARY_H

#include <glib.h>

typedef struct {
    gchar *path;          /* full path to the tool script on disk */
    gchar *filename;      /* basename of path */
    gchar *name;
    gchar *comment;
    gchar *input;
    gchar *output;
    gchar *save_files;
    gchar *shortcut;
    gchar *applicability;
    gchar **languages;
    gboolean is_local;     /* a copy exists in the user directory */
    gboolean is_global;    /* a copy exists in a system directory */
} PlumaTool;

/* system_dir/user_dir passed explicitly since callers (manager, revert,
 * autoset-filename) all need to know both locations. */
typedef struct {
    gchar *system_dir;
    gchar *user_dir;
} PlumaToolLocations;

PlumaTool  *pluma_tool_load             (const gchar *path, GError **error);
GPtrArray  *pluma_tool_library_load     (const gchar *system_dir, const gchar *user_dir);
void        pluma_tool_free             (PlumaTool *tool);

gchar      *pluma_tool_get_script       (PlumaTool *tool);
gboolean    pluma_tool_has_hash_bang    (PlumaTool *tool);
gboolean    pluma_tool_save_with_script (PlumaTool *tool, const gchar *user_dir, const gchar *script);
gboolean    pluma_tool_save             (PlumaTool *tool, const gchar *user_dir);

PlumaTool  *pluma_tool_new               (void);
void        pluma_tool_autoset_filename  (PlumaTool *tool, const gchar *user_dir);
gboolean    pluma_tool_delete            (PlumaTool *tool, const gchar *user_dir);
/* Removes the local override and reloads from system_dir. Returns FALSE
 * (and leaves the tool untouched) if there is nothing to revert to. */
gboolean    pluma_tool_revert            (PlumaTool *tool, const gchar *user_dir, const gchar *system_dir);

#endif
