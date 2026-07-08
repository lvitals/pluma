/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pluma-commands.h
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

#ifndef __PLUMA_COMMANDS_H__
#define __PLUMA_COMMANDS_H__

#include <gtk/gtk.h>
#include <pluma/pluma-window.h>

G_BEGIN_DECLS

/* Do nothing if URI does not exist */
void		 pluma_commands_load_uri		(PlumaWindow         *window,
							 const gchar         *uri,
							 const PlumaEncoding *encoding,
							 gint                 line_pos);

/* Ignore non-existing URIs */
gint		 pluma_commands_load_uris		(PlumaWindow         *window,
							 const GSList        *uris,
							 const PlumaEncoding *encoding,
							 gint                 line_pos);

void		 pluma_commands_save_document		(PlumaWindow         *window,
                                                         PlumaDocument       *document);

void		 pluma_commands_save_all_documents 	(PlumaWindow         *window);

/*
 * Non-exported functions
 */

/* Create titled documens for non-existing URIs */
gint		_pluma_cmd_load_files_from_prompt	(PlumaWindow         *window,
							 GSList              *files,
							 const PlumaEncoding *encoding,
							 gint                 line_pos);

void		_pluma_cmd_file_new			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_open			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_open_folder			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_save			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_save_as			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_save_all			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_revert			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_open_uri			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_print_preview			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_print			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_close			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_close_all			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_close_tabs_left			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_close_tabs_right			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_close_other_tabs			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_file_quit			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);

void		_pluma_cmd_edit_undo			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_redo			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_cut			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_copy			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_paste			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_delete			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_upper_case			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_lower_case			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_invert_case			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_title_case			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_select_all			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_zoom_in			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_zoom_out			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_zoom_reset			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_edit_preferences			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);

/* Native stateful GActions (win.show-toolbar/show-statusbar/show-side-pane/
 * show-bottom-pane/show-right-pane) — wired via the "change-state" signal,
 * not a legacy GtkToggleActionEntry callback. */
void		_pluma_cmd_view_show_toolbar			(GSimpleAction *action,
							 GVariant      *state,
							 gpointer       user_data);
void		_pluma_cmd_view_show_statusbar			(GSimpleAction *action,
							 GVariant      *state,
							 gpointer       user_data);
void		_pluma_cmd_view_show_side_pane			(GSimpleAction *action,
							 GVariant      *state,
							 gpointer       user_data);
void		_pluma_cmd_view_show_bottom_pane			(GSimpleAction *action,
							 GVariant      *state,
							 gpointer       user_data);
void		_pluma_cmd_view_show_right_pane			(GSimpleAction *action,
							 GVariant      *state,
							 gpointer       user_data);
void		_pluma_cmd_view_toggle_fullscreen_mode			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_view_leave_fullscreen_mode			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);

void		_pluma_cmd_search_find			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_search_find_in_files			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_search_find_next			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_search_find_prev			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_search_replace			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_search_clear_highlight			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_search_goto_line			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_search_incremental_search			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);

void		_pluma_cmd_documents_previous_document			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_documents_next_document			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_documents_move_to_new_window			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);

void		_pluma_cmd_help_contents			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);
void		_pluma_cmd_help_about			(GSimpleAction *action,
							 GVariant      *parameter,
							 gpointer       user_data);

void		_pluma_cmd_file_close_tab 		(PlumaTab    *tab,
							 PlumaWindow *window);

void		_pluma_cmd_file_save_documents_list	(PlumaWindow *window,
							 GList       *docs);

G_END_DECLS

#endif /* __PLUMA_COMMANDS_H__ */
