/* pluma-project-search-panel.h */
#ifndef PLUMA_PROJECT_SEARCH_PANEL_H
#define PLUMA_PROJECT_SEARCH_PANEL_H

#include <gtk/gtk.h>
#include "pluma-window.h"

G_BEGIN_DECLS

#define PLUMA_TYPE_PROJECT_SEARCH_PANEL (pluma_project_search_panel_get_type ())
G_DECLARE_FINAL_TYPE (PlumaProjectSearchPanel, pluma_project_search_panel, PLUMA, PROJECT_SEARCH_PANEL, GtkBox)

GtkWidget *pluma_project_search_panel_new   (PlumaWindow *window);
void       pluma_project_search_panel_focus (PlumaProjectSearchPanel *panel);

G_END_DECLS
#endif
