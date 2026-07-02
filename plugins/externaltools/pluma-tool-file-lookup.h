#ifndef PLUMA_TOOL_FILE_LOOKUP_H
#define PLUMA_TOOL_FILE_LOOKUP_H

#include <gio/gio.h>

/* Tries several strategies (absolute path, relative to cwd, relative to an
 * open document's directory, suffix of an open document's URI) to resolve
 * a path coming from a tool's output into a real file. Returns a new
 * reference, or NULL if nothing matched. */
GFile *pluma_tool_file_lookup (const gchar *path);

#endif
