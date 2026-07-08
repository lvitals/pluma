/*
 * pluma-bracket-completion.c
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

#include "pluma-bracket-completion.h"

#include <gdk/gdkkeysyms.h>
#include <gtksourceview/gtksource.h>

#include "pluma-document.h"
#include "pluma-settings.h"
#include "pluma-view.h"

typedef struct
{
	gunichar open;
	gunichar close;
} BracketPair;

struct _PlumaBracketCompletion
{
	PlumaView     *view;
	GSettings     *settings;
	GtkTextBuffer *buffer;
	GPtrArray     *pairs;
	GtkTextMark   *mark_begin;
	GtkTextMark   *mark_end;
	GtkTextMark   *last_mark;
	GArray        *stack;
	gboolean       relocate_marks;
	gulong         delete_range_id;
};

static const BracketPair default_pairs[] =
{
	{ '(', ')' },
	{ '[', ']' },
	{ '{', '}' },
	{ '"', '"' },
	{ '\'', '\'' },
	{ 0, 0 }
};

static void
clear_stack (PlumaBracketCompletion *completion)
{
	g_array_set_size (completion->stack, 0);
}

static void
push_stack (PlumaBracketCompletion *completion,
            gunichar                c)
{
	g_array_append_val (completion->stack, c);
}

static gboolean
stack_top_is (PlumaBracketCompletion *completion,
              gunichar                c)
{
	gunichar top;

	if (completion->stack->len == 0)
		return FALSE;

	top = g_array_index (completion->stack, gunichar, completion->stack->len - 1);
	return top == c;
}

static void
pop_stack (PlumaBracketCompletion *completion)
{
	if (completion->stack->len > 0)
		g_array_set_size (completion->stack, completion->stack->len - 1);
}

static void
clear_pairs (PlumaBracketCompletion *completion)
{
	g_ptr_array_set_size (completion->pairs, 0);
}

static void
add_pair (PlumaBracketCompletion *completion,
          gunichar                open,
          gunichar                close)
{
	BracketPair *pair;

	if (open == 0 || close == 0)
		return;

	pair = g_new0 (BracketPair, 1);
	pair->open = open;
	pair->close = close;
	g_ptr_array_add (completion->pairs, pair);
}

static void
add_default_pairs (PlumaBracketCompletion *completion)
{
	guint i;

	for (i = 0; default_pairs[i].open != 0; i++)
		add_pair (completion, default_pairs[i].open, default_pairs[i].close);
}

static void
add_metadata_pairs (PlumaBracketCompletion *completion,
                    GtkSourceLanguage      *language)
{
	const gchar *metadata;
	gchar **tokens;
	guint i;

	metadata = gtk_source_language_get_metadata (language, "pluma-bracket-pairs");
	if (metadata == NULL || *metadata == '\0')
		return;

	tokens = g_strsplit_set (metadata, " \t\r\n,;", -1);

	for (i = 0; tokens[i] != NULL; i++)
	{
		const gchar *p;
		const gchar *next;
		gunichar open;
		gunichar close;

		if (*tokens[i] == '\0')
			continue;

		p = tokens[i];
		open = g_utf8_get_char_validated (p, -1);
		if (open == (gunichar) -1 || open == (gunichar) -2)
			continue;

		next = g_utf8_next_char (p);
		close = g_utf8_get_char_validated (next, -1);
		if (close == (gunichar) -1 || close == (gunichar) -2)
			continue;

		if (*g_utf8_next_char (next) != '\0')
			continue;

		add_pair (completion, open, close);
	}

	g_strfreev (tokens);
}

static void
update_pairs (PlumaBracketCompletion *completion)
{
	GtkSourceLanguage *language = NULL;

	clear_pairs (completion);
	add_default_pairs (completion);

	if (PLUMA_IS_DOCUMENT (completion->buffer))
		language = pluma_document_get_language (PLUMA_DOCUMENT (completion->buffer));

	if (language != NULL)
		add_metadata_pairs (completion, language);
}

static gboolean
get_close_pair (PlumaBracketCompletion *completion,
                gunichar                open,
                gunichar               *close)
{
	guint i;

	for (i = 0; i < completion->pairs->len; i++)
	{
		BracketPair *pair = g_ptr_array_index (completion->pairs, i);

		if (pair->open == open)
		{
			*close = pair->close;
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
get_open_pair (PlumaBracketCompletion *completion,
               gunichar                close,
               gunichar               *open)
{
	guint i;

	for (i = 0; i < completion->pairs->len; i++)
	{
		BracketPair *pair = g_ptr_array_index (completion->pairs, i);

		if (pair->open != pair->close && pair->close == close)
		{
			*open = pair->open;
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
is_active (PlumaBracketCompletion *completion)
{
	return completion->buffer != NULL &&
	       g_settings_get_boolean (completion->settings, PLUMA_SETTINGS_BRACKET_COMPLETION) &&
	       gtk_text_view_get_editable (GTK_TEXT_VIEW (completion->view)) &&
	       PLUMA_IS_DOCUMENT (completion->buffer) &&
	       pluma_document_get_language (PLUMA_DOCUMENT (completion->buffer)) != NULL;
}

static gchar *
get_line_indentation (GtkTextBuffer *buffer,
                      GtkTextIter   *cur)
{
	GtkTextIter start;
	GtkTextIter end;

	gtk_text_buffer_get_iter_at_line (buffer, &start, gtk_text_iter_get_line (cur));
	end = start;

	while (gtk_text_iter_compare (&end, cur) < 0)
	{
		gunichar c = gtk_text_iter_get_char (&end);

		if (!g_unichar_isspace (c) || c == '\n' || c == '\r')
			break;

		if (!gtk_text_iter_forward_char (&end))
			break;
	}

	return gtk_text_iter_get_slice (&start, &end);
}

static void
delete_range_cb (GtkTextBuffer          *buffer,
                 GtkTextIter            *start,
                 GtkTextIter            *end,
                 PlumaBracketCompletion *completion)
{
	clear_stack (completion);
}

static void
clear_last_mark (PlumaBracketCompletion *completion)
{
	GtkTextIter iter;

	if (completion->buffer == NULL || completion->last_mark == NULL)
		return;

	gtk_text_buffer_get_iter_at_mark (completion->buffer,
	                                  &iter,
	                                  gtk_text_buffer_get_insert (completion->buffer));
	gtk_text_buffer_move_mark (completion->buffer, completion->last_mark, &iter);
}

static gboolean
cursor_is_before_completion (GtkTextIter *cursor,
                             GtkTextIter *last)
{
	GtkTextIter before_last = *last;

	return gtk_text_iter_backward_char (&before_last) &&
	       gtk_text_iter_equal (&before_last, cursor);
}

PlumaBracketCompletion *
pluma_bracket_completion_new (PlumaView *view,
                              GSettings *settings)
{
	PlumaBracketCompletion *completion;

	g_return_val_if_fail (PLUMA_IS_VIEW (view), NULL);
	g_return_val_if_fail (G_IS_SETTINGS (settings), NULL);

	completion = g_new0 (PlumaBracketCompletion, 1);
	completion->view = view;
	completion->settings = g_object_ref (settings);
	completion->pairs = g_ptr_array_new_with_free_func (g_free);
	completion->stack = g_array_new (FALSE, FALSE, sizeof (gunichar));
	completion->relocate_marks = TRUE;

	return completion;
}

void
pluma_bracket_completion_free (PlumaBracketCompletion *completion)
{
	if (completion == NULL)
		return;

	pluma_bracket_completion_detach (completion);
	g_clear_object (&completion->settings);
	g_ptr_array_free (completion->pairs, TRUE);
	g_array_free (completion->stack, TRUE);
	g_free (completion);
}

void
pluma_bracket_completion_attach (PlumaBracketCompletion *completion,
                                 GtkTextBuffer          *buffer)
{
	GtkTextIter iter;

	g_return_if_fail (completion != NULL);
	g_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));

	pluma_bracket_completion_detach (completion);

	completion->buffer = g_object_ref (buffer);
	clear_stack (completion);

	gtk_text_buffer_get_iter_at_mark (buffer,
	                                  &iter,
	                                  gtk_text_buffer_get_insert (buffer));
	completion->mark_begin = gtk_text_buffer_create_mark (buffer, NULL, &iter, TRUE);
	completion->mark_end = gtk_text_buffer_create_mark (buffer, NULL, &iter, FALSE);
	completion->last_mark = gtk_text_buffer_create_mark (buffer, NULL, &iter, FALSE);
	completion->relocate_marks = TRUE;
	completion->delete_range_id = g_signal_connect (buffer,
	                                                "delete-range",
	                                                G_CALLBACK (delete_range_cb),
	                                                completion);

	update_pairs (completion);
}

void
pluma_bracket_completion_detach (PlumaBracketCompletion *completion)
{
	if (completion == NULL || completion->buffer == NULL)
		return;

	if (completion->delete_range_id != 0)
	{
		g_signal_handler_disconnect (completion->buffer, completion->delete_range_id);
		completion->delete_range_id = 0;
	}

	if (completion->mark_begin != NULL)
	{
		gtk_text_buffer_delete_mark (completion->buffer, completion->mark_begin);
		completion->mark_begin = NULL;
	}

	if (completion->mark_end != NULL)
	{
		gtk_text_buffer_delete_mark (completion->buffer, completion->mark_end);
		completion->mark_end = NULL;
	}

	if (completion->last_mark != NULL)
	{
		gtk_text_buffer_delete_mark (completion->buffer, completion->last_mark);
		completion->last_mark = NULL;
	}

	g_clear_object (&completion->buffer);
	clear_stack (completion);
	completion->relocate_marks = TRUE;
	clear_pairs (completion);
}

gboolean
pluma_bracket_completion_key_press (PlumaBracketCompletion *completion,
                                    GdkEventKey            *event)
{
	GtkTextIter cur;
	GtkTextIter end;
	gchar *indent;
	gchar *insert;

	g_return_val_if_fail (completion != NULL, FALSE);

	if ((event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) != 0 || !is_active (completion))
		return FALSE;

	if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right)
	{
		clear_stack (completion);
		clear_last_mark (completion);
		return FALSE;
	}

	if (event->keyval == GDK_KEY_BackSpace)
	{
		gtk_text_buffer_get_iter_at_mark (completion->buffer,
		                                  &cur,
		                                  gtk_text_buffer_get_insert (completion->buffer));
		gtk_text_buffer_get_iter_at_mark (completion->buffer, &end, completion->last_mark);

		if (!cursor_is_before_completion (&cur, &end) ||
		    !gtk_text_iter_backward_char (&cur))
		{
			clear_last_mark (completion);
			return FALSE;
		}

		clear_stack (completion);

		gtk_text_buffer_begin_user_action (completion->buffer);
		gtk_text_buffer_delete (completion->buffer, &cur, &end);
		gtk_text_buffer_end_user_action (completion->buffer);
		clear_last_mark (completion);

		return TRUE;
	}

	if ((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) &&
	    gtk_source_view_get_auto_indent (GTK_SOURCE_VIEW (completion->view)))
	{
		gtk_text_buffer_get_iter_at_mark (completion->buffer,
		                                  &cur,
		                                  gtk_text_buffer_get_insert (completion->buffer));
		gtk_text_buffer_get_iter_at_mark (completion->buffer, &end, completion->last_mark);

		if (!cursor_is_before_completion (&cur, &end))
		{
			clear_last_mark (completion);
			return FALSE;
		}

		indent = get_line_indentation (completion->buffer, &cur);
		insert = g_strdup_printf ("\n%s", indent);

		gtk_text_buffer_begin_user_action (completion->buffer);
		gtk_text_buffer_insert (completion->buffer, &cur, insert, -1);
		gtk_text_buffer_insert (completion->buffer, &cur, insert, -1);
		gtk_text_buffer_end_user_action (completion->buffer);

		gtk_text_iter_backward_chars (&cur, g_utf8_strlen (insert, -1));
		gtk_text_buffer_place_cursor (completion->buffer, &cur);
		gtk_text_view_scroll_mark_onscreen (GTK_TEXT_VIEW (completion->view),
		                                    gtk_text_buffer_get_insert (completion->buffer));

		clear_last_mark (completion);
		g_free (insert);
		g_free (indent);

		return TRUE;
	}

	clear_last_mark (completion);
	return FALSE;
}

void
pluma_bracket_completion_event_after (PlumaBracketCompletion *completion,
                                      GdkEvent               *event)
{
	GtkTextIter insert_iter;
	GtkTextIter start;
	GtkTextIter end;
	GtkTextIter range_begin;
	GtkTextIter range_end;
	GdkEventKey *key_event;
	GtkTextMark *skip_mark;
	gunichar typed;
	gunichar open_bracket;
	gunichar close_bracket;
	gchar close_utf8[7] = { 0 };
	gint close_len;

	g_return_if_fail (completion != NULL);

	if (event->type != GDK_KEY_PRESS || !is_active (completion))
		return;

	key_event = (GdkEventKey *) event;
	if ((key_event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) != 0)
		return;

	typed = gdk_keyval_to_unicode (key_event->keyval);
	if (typed == 0)
		return;

	gtk_text_buffer_get_iter_at_mark (completion->buffer,
	                                  &insert_iter,
	                                  gtk_text_buffer_get_insert (completion->buffer));
	gtk_text_buffer_get_iter_at_mark (completion->buffer, &range_begin, completion->mark_begin);
	gtk_text_buffer_get_iter_at_mark (completion->buffer, &range_end, completion->mark_end);

	if (!gtk_text_iter_equal (&range_begin, &range_end) &&
	    !gtk_text_iter_in_range (&insert_iter, &range_begin, &range_end))
	{
		clear_stack (completion);
		completion->relocate_marks = TRUE;
	}

	if (completion->relocate_marks)
	{
		gtk_text_buffer_move_mark (completion->buffer, completion->mark_begin, &insert_iter);
		gtk_text_buffer_move_mark (completion->buffer, completion->mark_end, &insert_iter);
		completion->relocate_marks = FALSE;
	}

	if (get_open_pair (completion, typed, &open_bracket))
	{
		start = insert_iter;
		end = insert_iter;

		if (!gtk_text_iter_backward_char (&start) ||
		    gtk_text_iter_get_char (&start) != typed ||
		    gtk_text_iter_get_char (&end) != typed ||
		    !stack_top_is (completion, open_bracket))
			return;

		pop_stack (completion);

		skip_mark = gtk_text_buffer_create_mark (completion->buffer, NULL, &end, FALSE);

		if (completion->delete_range_id != 0)
			g_signal_handler_block (completion->buffer, completion->delete_range_id);
		gtk_text_buffer_delete (completion->buffer, &start, &insert_iter);
		if (completion->delete_range_id != 0)
			g_signal_handler_unblock (completion->buffer, completion->delete_range_id);

		gtk_text_buffer_get_iter_at_mark (completion->buffer, &end, skip_mark);
		gtk_text_iter_forward_char (&end);
		gtk_text_buffer_place_cursor (completion->buffer, &end);
		gtk_text_buffer_delete_mark (completion->buffer, skip_mark);
		clear_last_mark (completion);
		return;
	}

	if (!get_close_pair (completion, typed, &close_bracket))
		return;

	push_stack (completion, typed);

	close_len = g_unichar_to_utf8 (close_bracket, close_utf8);
	close_utf8[close_len] = '\0';

	gtk_text_buffer_begin_user_action (completion->buffer);
	gtk_text_buffer_insert (completion->buffer, &insert_iter, close_utf8, -1);
	gtk_text_buffer_end_user_action (completion->buffer);

	gtk_text_buffer_move_mark (completion->buffer, completion->last_mark, &insert_iter);
	gtk_text_iter_backward_chars (&insert_iter, g_utf8_strlen (close_utf8, -1));
	gtk_text_buffer_place_cursor (completion->buffer, &insert_iter);
}
