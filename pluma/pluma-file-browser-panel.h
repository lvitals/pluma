/*
 * pluma-file-browser-panel.h - File Browser side panel, built into pluma core
 *
 * Copyright (C) 2006 - Jesse van den Kieboom <jesse@icecrew.nl>
 * Copyright (C) 2012-2021 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __PLUMA_FILE_BROWSER_PANEL_H__
#define __PLUMA_FILE_BROWSER_PANEL_H__

#include <gtk/gtk.h>
#include "pluma-window.h"

G_BEGIN_DECLS

/* Builds the File Browser widget, wires it up to @window (settings,
 * message bus, context menu actions) and returns it ready to be added
 * to the window's side panel. */
GtkWidget *pluma_file_browser_panel_new (PlumaWindow *window);

G_END_DECLS

#endif /* __PLUMA_FILE_BROWSER_PANEL_H__ */

// ex:ts=8:noet:
