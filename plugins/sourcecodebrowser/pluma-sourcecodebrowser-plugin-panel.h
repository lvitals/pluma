/*
 * pluma-sourcecodebrowser-plugin-panel.h
 * This file is part of pluma
 *
 * Copyright (c) 2011, Micah Carrick
 * Copyright (C) 2020-2021 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL_H__
#define __PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN_PANEL              (pluma_sourcecodebrowser_plugin_panel_get_type ())
#define PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN_PANEL, PlumaSourcecodebrowserPluginPanel))
#define PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN_PANEL, PlumaSourcecodebrowserPluginPanelClass))
#define PLUMA_IS_SOURCECODEBROWSER_PLUGIN_PANEL(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN_PANEL))
#define PLUMA_IS_SOURCECODEBROWSER_PLUGIN_PANEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN_PANEL))
#define PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN_PANEL, PlumaSourcecodebrowserPluginPanelClass))

typedef struct _PlumaSourcecodebrowserPluginPanelPrivate PlumaSourcecodebrowserPluginPanelPrivate;

typedef struct _PlumaSourcecodebrowserPluginPanel PlumaSourcecodebrowserPluginPanel;

struct _PlumaSourcecodebrowserPluginPanel
{
	GtkBox box;

	/*< private >*/
	PlumaSourcecodebrowserPluginPanelPrivate *priv;
};

typedef struct _PlumaSourcecodebrowserPluginPanelClass PlumaSourcecodebrowserPluginPanelClass;

struct _PlumaSourcecodebrowserPluginPanelClass
{
	GtkBoxClass parent_class;

	void (* tag_activated) (PlumaSourcecodebrowserPluginPanel *panel,
	                         const gchar                       *uri,
	                         const gchar                        *line);
};

void		 _pluma_sourcecodebrowser_plugin_panel_register_type	(GTypeModule *module);

GType		 pluma_sourcecodebrowser_plugin_panel_get_type		(void) G_GNUC_CONST;

GtkWidget	*pluma_sourcecodebrowser_plugin_panel_new		(const gchar *data_dir);

void		 pluma_sourcecodebrowser_plugin_panel_clear		(PlumaSourcecodebrowserPluginPanel *panel);

void		 pluma_sourcecodebrowser_plugin_panel_parse_file	(PlumaSourcecodebrowserPluginPanel *panel,
									 const gchar                       *path,
									 const gchar                       *uri);

void		 pluma_sourcecodebrowser_plugin_panel_reset_expanded_rows (PlumaSourcecodebrowserPluginPanel *panel);

G_END_DECLS

#endif /* __PLUMA_SOURCECODEBROWSER_PLUGIN_PANEL_H__ */
