#ifndef PLUMA_TOOL_MANAGER_H
#define PLUMA_TOOL_MANAGER_H

#include <gtk/gtk.h>
#include <pluma/pluma-window.h>
#include "pluma-tool-library.h"

typedef struct _PlumaToolManager PlumaToolManager;

/* Fired whenever the tool list or a tool's metadata changed in a way that
 * should be reflected in the Tools menu. */
typedef void (*PlumaToolManagerUpdatedFunc) (gpointer user_data);

/* `tools` is the shared, mutable tool list owned by the plugin; the
 * manager adds/removes elements from it directly and expects it to stay
 * valid for as long as the manager is alive. initial_width/height (may be
 * 0) restore the dialog's last remembered size. */
PlumaToolManager *pluma_tool_manager_new (const gchar *data_dir,
                                          const gchar *system_dir,
                                          const gchar *user_dir,
                                          GPtrArray   *tools,
                                          gint         initial_width,
                                          gint         initial_height);

/* Only needed to tear the manager down while its dialog may still be open
 * (e.g. plugin deactivation); normally just let it be reused across opens. */
void pluma_tool_manager_free (PlumaToolManager *manager);

void pluma_tool_manager_set_callbacks (PlumaToolManager           *manager,
                                       PlumaToolManagerUpdatedFunc updated,
                                       gpointer                    user_data);

void pluma_tool_manager_run (PlumaToolManager *manager, PlumaWindow *window);

/* Called by the plugin when a tool's shortcut changes from outside the
 * dialog (GtkAccelMap "changed" notification) so the tree reflects it. */
void pluma_tool_manager_tool_changed (PlumaToolManager *manager, PlumaTool *tool);

#endif
