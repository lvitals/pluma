#ifndef PLUMA_TOOL_FUNCTIONS_H
#define PLUMA_TOOL_FUNCTIONS_H

#include <pluma/pluma-window.h>
#include "pluma-tool-library.h"
#include "pluma-tool-output-panel.h"

/* Runs the tool immediately (building its environment from the window's
 * active document and wiring input/output as configured on the tool). */
void pluma_tool_run (PlumaWindow *window, PlumaToolOutputPanel *panel, PlumaTool *tool);

/* Entry point for menu activation: saves documents first if the tool's
 * Save-files setting requires it, then calls pluma_tool_run(). */
void pluma_tool_run_from_menu (PlumaWindow *window, PlumaToolOutputPanel *panel, PlumaTool *tool);

#endif
