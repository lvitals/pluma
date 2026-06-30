#ifndef PLUMA_GIT_PANEL_H
#define PLUMA_GIT_PANEL_H

#include <gtk/gtk.h>
#include "pluma-window.h"

G_BEGIN_DECLS
#define PLUMA_TYPE_GIT_PANEL (pluma_git_panel_get_type ())
G_DECLARE_FINAL_TYPE (PlumaGitPanel, pluma_git_panel, PLUMA, GIT_PANEL, GtkBox)
GtkWidget *pluma_git_panel_new (PlumaWindow *window);
void       pluma_git_panel_refresh (PlumaGitPanel *panel);
G_END_DECLS
#endif
