/*
 * pluma-view-commands.c
 * This file is part of pluma
 *
 * Copyright (C) 1998, 1999 Alex Roberts, Evan Lawrence
 * Copyright (C) 2000, 2001 Chema Celorio, Paolo Maggi
 * Copyright (C) 2002-2005 Paolo Maggi
 * Copyright (C) 2012-2021 MATE Developers
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

/*
 * Modified by the pluma Team, 1998-2005. See the AUTHORS file for a
 * list of people on the pluma Team.
 * See the ChangeLog files for a list of changes.
 *
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>

#include "pluma-commands.h"
#include "pluma-debug.h"
#include "pluma-window.h"
#include "pluma-window-private.h"


void
_pluma_cmd_view_show_toolbar (GSimpleAction *action,
                             GVariant      *state,
                             gpointer       user_data)
{
	PlumaWindow *window = PLUMA_WINDOW (user_data);
	gboolean visible;

	pluma_debug (DEBUG_COMMANDS);

	visible = g_variant_get_boolean (state);

	if (visible)
		gtk_widget_show (window->priv->toolbar);
	else
		gtk_widget_hide (window->priv->toolbar);

	g_simple_action_set_state (action, state);
}

void
_pluma_cmd_view_show_statusbar (GSimpleAction *action,
                               GVariant      *state,
                               gpointer       user_data)
{
	PlumaWindow *window = PLUMA_WINDOW (user_data);
	gboolean visible;

	pluma_debug (DEBUG_COMMANDS);

	visible = g_variant_get_boolean (state);

	if (visible)
		gtk_widget_show (window->priv->statusbar);
	else
		gtk_widget_hide (window->priv->statusbar);

	g_simple_action_set_state (action, state);
}

void
_pluma_cmd_view_show_side_pane (GSimpleAction *action,
                               GVariant      *state,
                               gpointer       user_data)
{
	PlumaWindow *window = PLUMA_WINDOW (user_data);
	gboolean visible;
	PlumaPanel *panel;

	pluma_debug (DEBUG_COMMANDS);

	visible = g_variant_get_boolean (state);

	panel = pluma_window_get_side_panel (window);

	if (visible)
	{
		gtk_widget_show (GTK_WIDGET (panel));
		gtk_widget_grab_focus (GTK_WIDGET (panel));
	}
	else
	{
		gtk_widget_hide (GTK_WIDGET (panel));
	}

	g_simple_action_set_state (action, state);
}

void
_pluma_cmd_view_show_bottom_pane (GSimpleAction *action,
                                 GVariant      *state,
                                 gpointer       user_data)
{
	PlumaWindow *window = PLUMA_WINDOW (user_data);
	gboolean visible;
	PlumaPanel *panel;

	pluma_debug (DEBUG_COMMANDS);

	visible = g_variant_get_boolean (state);

	panel = pluma_window_get_bottom_panel (window);

	if (visible)
	{
		gtk_widget_show (GTK_WIDGET (panel));
		gtk_widget_grab_focus (GTK_WIDGET (panel));
	}
	else
	{
		gtk_widget_hide (GTK_WIDGET (panel));
	}

	g_simple_action_set_state (action, state);
}

void
_pluma_cmd_view_show_right_pane (GSimpleAction *action,
                                GVariant      *state,
                                gpointer       user_data)
{
	PlumaWindow *window = PLUMA_WINDOW (user_data);
	gboolean visible;
	PlumaPanel *panel;

	pluma_debug (DEBUG_COMMANDS);

	visible = g_variant_get_boolean (state);

	panel = pluma_window_get_right_panel (window);

	if (visible)
	{
		gtk_widget_show (GTK_WIDGET (panel));
		gtk_widget_grab_focus (GTK_WIDGET (panel));
	}
	else
	{
		gtk_widget_hide (GTK_WIDGET (panel));
	}

	g_simple_action_set_state (action, state);
}

void
_pluma_cmd_view_toggle_fullscreen_mode (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
	PlumaWindow *window = PLUMA_WINDOW (user_data);
	pluma_debug (DEBUG_COMMANDS);

	if (_pluma_window_is_fullscreen (window))
		_pluma_window_unfullscreen (window);
	else
		_pluma_window_fullscreen (window);
}

void
_pluma_cmd_view_leave_fullscreen_mode (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
	PlumaWindow *window = PLUMA_WINDOW (user_data);

	pluma_debug (DEBUG_COMMANDS);

	/* _pluma_window_unfullscreen() keeps the "fullscreen" GAction's state
	 * in sync (see pluma-window.c); no need to touch it here. */
	_pluma_window_unfullscreen (window);
}
