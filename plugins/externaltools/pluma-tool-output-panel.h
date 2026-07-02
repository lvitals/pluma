#ifndef PLUMA_TOOL_OUTPUT_PANEL_H
#define PLUMA_TOOL_OUTPUT_PANEL_H

#include <gtk/gtk.h>
#include <pluma/pluma-window.h>
#include "pluma-tool-capture.h"

typedef struct _PlumaToolOutputPanel PlumaToolOutputPanel;

typedef enum {
    PLUMA_TOOL_OUTPUT_TAG_NONE,
    PLUMA_TOOL_OUTPUT_TAG_ERROR,
    PLUMA_TOOL_OUTPUT_TAG_ITALIC,
    PLUMA_TOOL_OUTPUT_TAG_BOLD
} PlumaToolOutputTag;

/* data_dir is the plugin's own data directory (peas_extension_base_get_data_dir),
 * which contains a "ui/outputpanel.ui" file. Returns NULL on failure. */
PlumaToolOutputPanel *pluma_tool_output_panel_new  (const gchar *data_dir, PlumaWindow *window);
PlumaToolOutputPanel *pluma_tool_output_panel_ref  (PlumaToolOutputPanel *panel);
void                   pluma_tool_output_panel_unref (PlumaToolOutputPanel *panel);
void                   pluma_tool_output_panel_free (PlumaToolOutputPanel *panel);

GtkWidget *pluma_tool_output_panel_get_widget (PlumaToolOutputPanel *panel);

/* Associates the panel with the capture currently running, so the Stop
 * button can act on it. Does not take ownership. */
void pluma_tool_output_panel_set_process (PlumaToolOutputPanel *panel, PlumaToolCapture *process);
void pluma_tool_output_panel_set_running (PlumaToolOutputPanel *panel, gboolean running);

void     pluma_tool_output_panel_clear   (PlumaToolOutputPanel *panel);
void     pluma_tool_output_panel_write   (PlumaToolOutputPanel *panel, const gchar *text, PlumaToolOutputTag tag);
void     pluma_tool_output_panel_show    (PlumaToolOutputPanel *panel);
gboolean pluma_tool_output_panel_visible (PlumaToolOutputPanel *panel);

#endif
