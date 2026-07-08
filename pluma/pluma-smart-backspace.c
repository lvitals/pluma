/*
 * pluma-smart-backspace.c
 * This file is part of pluma
 *
 * Copyright (C) 2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-smart-backspace.h"

#include <gdk/gdkkeysyms.h>
#include <gtksourceview/gtksource.h>

#include "pluma-settings.h"
#include "pluma-view.h"

gboolean
pluma_smart_backspace_key_press (PlumaView   *view,
                                 GSettings   *settings,
                                 GdkEventKey *event)
{
	GtkTextBuffer *buffer;
	GtkTextIter cur;
	GtkTextIter start;
	GtkTextIter prev;
	guint state;
	gint indent_width;
	gint max_move;
	gint moved = 0;
	gint offset;

	g_return_val_if_fail (PLUMA_IS_VIEW (view), FALSE);
	g_return_val_if_fail (G_IS_SETTINGS (settings), FALSE);

	if (!g_settings_get_boolean (settings, PLUMA_SETTINGS_SMART_INDENTATION_BACKSPACE) ||
	    !gtk_text_view_get_editable (GTK_TEXT_VIEW (view)) ||
	    !gtk_source_view_get_insert_spaces_instead_of_tabs (GTK_SOURCE_VIEW (view)))
		return FALSE;

	state = event->state & gtk_accelerator_get_default_mod_mask ();
	if (event->keyval != GDK_KEY_BackSpace ||
	    (state != 0 && state != GDK_SHIFT_MASK))
		return FALSE;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	if (gtk_text_buffer_get_has_selection (buffer))
		return FALSE;

	gtk_text_buffer_get_iter_at_mark (buffer, &cur, gtk_text_buffer_get_insert (buffer));
	offset = gtk_text_iter_get_line_offset (&cur);

	if (offset == 0)
		return FALSE;

	start = cur;
	prev = cur;

	if (!gtk_text_iter_backward_char (&prev))
		return FALSE;

	indent_width = gtk_source_view_get_indent_width (GTK_SOURCE_VIEW (view));
	if (indent_width < 0)
		indent_width = gtk_source_view_get_tab_width (GTK_SOURCE_VIEW (view));

	if (indent_width <= 0)
		return FALSE;

	max_move = offset % indent_width;
	if (max_move == 0)
		max_move = indent_width;

	while (moved < max_move && gtk_text_iter_get_char (&prev) == ' ')
	{
		gtk_text_iter_backward_char (&start);
		moved++;

		if (!gtk_text_iter_backward_char (&prev))
			break;
	}

	if (moved == 0)
		return FALSE;

	gtk_text_buffer_begin_user_action (buffer);
	gtk_text_buffer_delete (buffer, &start, &cur);
	gtk_text_buffer_end_user_action (buffer);

	return TRUE;
}
