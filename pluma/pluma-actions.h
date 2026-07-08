/*
 * pluma-actions.h
 * This file is part of pluma
 *
 * Copyright (C) 2005 - Paolo Maggi
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

#ifndef __PLUMA_ACTIONS_H__
#define __PLUMA_ACTIONS_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>

#include "pluma-commands.h"

G_BEGIN_DECLS

/* win.* actions for the main window. These back both the GMenuModel menubar
 * (pluma-menus.ui) and the manually-built toolbar; the action names here
 * must match the "win.<name>" attributes used there exactly.
 *
 * Actions that are seeded from live widget/window state at construction
 * time (show-toolbar, show-statusbar, show-side-pane, show-bottom-pane,
 * show-right-pane), or whose handler is a static function private to
 * pluma-window.c (open-recent, switch-document, highlight-mode), are not
 * listed here: they are created individually in create_window_actions()
 * (pluma-window.c).
 */
static const GActionEntry pluma_window_action_entries[] =
{
	/* File menu */
	{ "new",               _pluma_cmd_file_new },
	{ "open",              _pluma_cmd_file_open },
	{ "open-folder",       _pluma_cmd_file_open_folder },
	{ "save",              _pluma_cmd_file_save },
	{ "save-as",           _pluma_cmd_file_save_as },
	{ "save-all",          _pluma_cmd_file_save_all },
	{ "revert",            _pluma_cmd_file_revert },
	{ "print-preview",     _pluma_cmd_file_print_preview },
	{ "print",             _pluma_cmd_file_print },
	{ "close",             _pluma_cmd_file_close },
	{ "close-all",         _pluma_cmd_file_close_all },
	{ "close-tabs-left",   _pluma_cmd_file_close_tabs_left },
	{ "close-tabs-right",  _pluma_cmd_file_close_tabs_right },
	{ "close-other-tabs",  _pluma_cmd_file_close_other_tabs },

	/* Edit menu */
	{ "undo",              _pluma_cmd_edit_undo },
	{ "redo",              _pluma_cmd_edit_redo },
	{ "cut",               _pluma_cmd_edit_cut },
	{ "copy",              _pluma_cmd_edit_copy },
	{ "paste",             _pluma_cmd_edit_paste },
	{ "delete",            _pluma_cmd_edit_delete },
	{ "select-all",        _pluma_cmd_edit_select_all },
	{ "zoom-in",           _pluma_cmd_edit_zoom_in },
	{ "zoom-out",          _pluma_cmd_edit_zoom_out },
	{ "zoom-reset",        _pluma_cmd_edit_zoom_reset },
	{ "uppercase",         _pluma_cmd_edit_upper_case },
	{ "lowercase",         _pluma_cmd_edit_lower_case },
	{ "invert-case",       _pluma_cmd_edit_invert_case },
	{ "title-case",        _pluma_cmd_edit_title_case },
	{ "toggle-line-comment", _pluma_cmd_edit_toggle_line_comment },
	{ "toggle-block-comment", _pluma_cmd_edit_toggle_block_comment },
	{ "preferences",       _pluma_cmd_edit_preferences },

	/* View menu */
	{ "fullscreen",        NULL, NULL, "false", _pluma_cmd_view_toggle_fullscreen_mode },
	{ "leave-fullscreen",  _pluma_cmd_view_leave_fullscreen_mode },

	/* Search menu */
	{ "find",                 _pluma_cmd_search_find },
	{ "find-in-files",        _pluma_cmd_search_find_in_files },
	{ "find-next",            _pluma_cmd_search_find_next },
	{ "find-previous",        _pluma_cmd_search_find_prev },
	{ "replace",              _pluma_cmd_search_replace },
	{ "clear-highlight",      _pluma_cmd_search_clear_highlight },
	{ "goto-line",            _pluma_cmd_search_goto_line },
	{ "incremental-search",   _pluma_cmd_search_incremental_search },

	/* Documents menu */
	{ "previous-document",    _pluma_cmd_documents_previous_document },
	{ "next-document",        _pluma_cmd_documents_next_document },
	{ "move-to-new-window",   _pluma_cmd_documents_move_to_new_window },

	/* Help menu */
	{ "help",              _pluma_cmd_help_contents },
	{ "about",             _pluma_cmd_help_about },
};

typedef struct
{
	const gchar *action_name;
	const gchar * const *accelerators;
} PlumaActionAccel;

static const gchar * const new_accels[]              = { "<Control>n", NULL };
static const gchar * const open_accels[]              = { "<Control>o", NULL };
static const gchar * const save_accels[]              = { "<Control>s", NULL };
static const gchar * const save_as_accels[]           = { "<Control><Shift>s", NULL };
static const gchar * const save_all_accels[]          = { "<Control><Shift>l", NULL };
static const gchar * const print_preview_accels[]     = { "<Control><Shift>p", NULL };
static const gchar * const print_accels[]              = { "<Control>p", NULL };
static const gchar * const close_accels[]             = { "<Control>w", NULL };
static const gchar * const close_all_accels[]         = { "<Control><Shift>w", NULL };
static const gchar * const undo_accels[]              = { "<Control>z", NULL };
static const gchar * const redo_accels[]              = { "<Control><Shift>z", NULL };
static const gchar * const cut_accels[]               = { "<Control>x", NULL };
static const gchar * const copy_accels[]              = { "<Control>c", NULL };
static const gchar * const paste_accels[]             = { "<Control>v", NULL };
static const gchar * const select_all_accels[]        = { "<Control>a", NULL };
static const gchar * const zoom_in_accels[]           = { "<Control>plus", NULL };
static const gchar * const zoom_out_accels[]          = { "<Control>minus", NULL };
static const gchar * const zoom_reset_accels[]        = { "<Control>equal", NULL };
static const gchar * const toggle_line_comment_accels[] = { "<Control>m", NULL };
static const gchar * const toggle_block_comment_accels[] = { "<Control><Shift>m", NULL };
static const gchar * const find_accels[]              = { "<Control>f", NULL };
static const gchar * const find_in_files_accels[]     = { "<Control><Shift>f", NULL };
static const gchar * const find_next_accels[]         = { "<Control>g", NULL };
static const gchar * const find_previous_accels[]     = { "<Control><Shift>g", NULL };
static const gchar * const replace_accels[]           = { "<Control>h", NULL };
static const gchar * const clear_highlight_accels[]   = { "<Control><Shift>k", NULL };
static const gchar * const goto_line_accels[]         = { "<Control>i", NULL };
static const gchar * const incremental_search_accels[] = { "<Control>k", NULL };
static const gchar * const previous_document_accels[] = { "<Control><Alt>Page_Up", NULL };
static const gchar * const next_document_accels[]     = { "<Control><Alt>Page_Down", NULL };
static const gchar * const help_accels[]              = { "F1", NULL };
static const gchar * const fullscreen_accels[]        = { "F11", NULL };

/* Side/bottom/right pane accelerators (F9 family) are intentionally not
 * listed here: GDK's "consumed modifiers" detection for function keys is
 * unreliable across keyboard layouts, so pluma_window_key_press_event()
 * matches those modifiers explicitly instead of going through
 * gtk_application_set_accels_for_action(). */
static const PlumaActionAccel pluma_window_action_accels[] =
{
	{ "new",               new_accels },
	{ "open",              open_accels },
	{ "save",              save_accels },
	{ "save-as",           save_as_accels },
	{ "save-all",          save_all_accels },
	{ "print-preview",     print_preview_accels },
	{ "print",             print_accels },
	{ "close",             close_accels },
	{ "close-all",         close_all_accels },
	{ "undo",              undo_accels },
	{ "redo",              redo_accels },
	{ "cut",               cut_accels },
	{ "copy",              copy_accels },
	{ "paste",             paste_accels },
	{ "select-all",        select_all_accels },
	{ "zoom-in",           zoom_in_accels },
	{ "zoom-out",          zoom_out_accels },
	{ "zoom-reset",        zoom_reset_accels },
	{ "toggle-line-comment", toggle_line_comment_accels },
	{ "toggle-block-comment", toggle_block_comment_accels },
	{ "find",              find_accels },
	{ "find-in-files",     find_in_files_accels },
	{ "find-next",         find_next_accels },
	{ "find-previous",     find_previous_accels },
	{ "replace",           replace_accels },
	{ "clear-highlight",   clear_highlight_accels },
	{ "goto-line",         goto_line_accels },
	{ "incremental-search", incremental_search_accels },
	{ "previous-document", previous_document_accels },
	{ "next-document",     next_document_accels },
	{ "help",              help_accels },
	{ "fullscreen",        fullscreen_accels },
};

G_END_DECLS

#endif  /* __PLUMA_ACTIONS_H__ */
