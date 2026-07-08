/*
 * pluma-comment.c
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

#include "pluma-comment.h"

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

static void
get_line_comment_tags (GtkSourceLanguage  *language,
                       const gchar       **start_tag,
                       const gchar       **end_tag)
{
	*start_tag = gtk_source_language_get_metadata (language, "line-comment-start");
	*end_tag = NULL;
}

static void
get_block_comment_tags (GtkSourceLanguage  *language,
                        const gchar       **start_tag,
                        const gchar       **end_tag)
{
	*start_tag = gtk_source_language_get_metadata (language, "block-comment-start");
	*end_tag = gtk_source_language_get_metadata (language, "block-comment-end");

	if (*start_tag == NULL || *end_tag == NULL)
	{
		*start_tag = NULL;
		*end_tag = NULL;
	}
}

static gboolean
get_line_or_block_comment_tags (GtkSourceLanguage  *language,
                                const gchar       **start_tag,
                                const gchar       **end_tag)
{
	get_line_comment_tags (language, start_tag, end_tag);

	if (*start_tag == NULL)
		get_block_comment_tags (language, start_tag, end_tag);

	return *start_tag != NULL;
}

static gboolean
find_tag_in_line (GtkTextIter *iter,
                  GtkTextIter *head,
                  const gchar *tag)
{
	while (!gtk_text_iter_ends_line (iter))
	{
		gchar *slice;
		gboolean found;

		slice = gtk_text_iter_get_slice (iter, head);
		found = g_strcmp0 (slice, tag) == 0;
		g_free (slice);

		if (found)
			return TRUE;

		gtk_text_iter_forward_char (iter);
		gtk_text_iter_forward_char (head);
	}

	return FALSE;
}

static gboolean
line_has_tag (GtkTextIter *line_start,
              const gchar *tag)
{
	GtkTextIter iter;
	GtkTextIter head;

	iter = *line_start;
	head = iter;
	gtk_text_iter_forward_chars (&head, g_utf8_strlen (tag, -1));

	return find_tag_in_line (&iter, &head, tag);
}

static gboolean
all_nonempty_lines_have_tag (GtkTextIter *start,
                             GtkTextIter *end,
                             const gchar *tag)
{
	GtkTextIter iter = *start;
	gboolean checked_any = FALSE;

	while (gtk_text_iter_compare (&iter, end) <= 0)
	{
		if (!gtk_text_iter_ends_line (&iter))
		{
			checked_any = TRUE;

			if (!line_has_tag (&iter, tag))
				return FALSE;
		}

		if (!gtk_text_iter_forward_line (&iter))
			break;
	}

	return checked_any;
}

static void
add_comment_characters (GtkTextBuffer *buffer,
                        const gchar   *start_tag,
                        const gchar   *end_tag,
                        GtkTextIter   *start,
                        GtkTextIter   *end)
{
	GtkTextMark *start_mark;
	GtkTextMark *iter_mark;
	GtkTextMark *end_mark;
	GtkTextIter iter;
	GtkTextIter new_start;
	GtkTextIter new_end;
	gint number_lines;
	gint i;

	start_mark = gtk_text_buffer_create_mark (buffer, NULL, start, FALSE);
	iter_mark = gtk_text_buffer_create_mark (buffer, NULL, start, FALSE);
	end_mark = gtk_text_buffer_create_mark (buffer, NULL, end, FALSE);
	number_lines = gtk_text_iter_get_line (end) - gtk_text_iter_get_line (start) + 1;

	gtk_text_buffer_begin_user_action (buffer);

	for (i = 0; i < number_lines; i++)
	{
		gtk_text_buffer_get_iter_at_mark (buffer, &iter, iter_mark);

		if (!gtk_text_iter_ends_line (&iter))
		{
			gtk_text_buffer_insert (buffer, &iter, start_tag, -1);

			if (end_tag != NULL)
			{
				if (i != number_lines - 1)
				{
					gtk_text_buffer_get_iter_at_mark (buffer, &iter, iter_mark);
					gtk_text_iter_forward_to_line_end (&iter);
				}
				else
				{
					gtk_text_buffer_get_iter_at_mark (buffer, &iter, end_mark);
				}

				gtk_text_buffer_insert (buffer, &iter, end_tag, -1);
			}
		}

		gtk_text_buffer_get_iter_at_mark (buffer, &iter, iter_mark);
		gtk_text_iter_forward_line (&iter);
		gtk_text_buffer_move_mark (buffer, iter_mark, &iter);
	}

	gtk_text_buffer_end_user_action (buffer);

	gtk_text_buffer_get_iter_at_mark (buffer, &new_start, start_mark);
	gtk_text_buffer_get_iter_at_mark (buffer, &new_end, end_mark);

	if (!gtk_text_iter_ends_line (&new_start))
		gtk_text_iter_backward_chars (&new_start, g_utf8_strlen (start_tag, -1));

	gtk_text_buffer_select_range (buffer, &new_start, &new_end);

	gtk_text_buffer_delete_mark (buffer, start_mark);
	gtk_text_buffer_delete_mark (buffer, iter_mark);
	gtk_text_buffer_delete_mark (buffer, end_mark);
}

static void
remove_comment_characters (GtkTextBuffer *buffer,
                           const gchar   *start_tag,
                           const gchar   *end_tag,
                           GtkTextIter   *start,
                           GtkTextIter   *end)
{
	GtkTextMark *iter_mark;
	GtkTextIter iter;
	GtkTextIter head;
	gint number_lines;
	gint i;

	iter_mark = gtk_text_buffer_create_mark (buffer, NULL, start, FALSE);
	number_lines = gtk_text_iter_get_line (end) - gtk_text_iter_get_line (start) + 1;

	gtk_text_buffer_begin_user_action (buffer);

	for (i = 0; i < number_lines; i++)
	{
		GtkTextMark *delete_mark;

		gtk_text_buffer_get_iter_at_mark (buffer, &iter, iter_mark);
		head = iter;
		gtk_text_iter_forward_chars (&head, g_utf8_strlen (start_tag, -1));

		if (find_tag_in_line (&iter, &head, start_tag))
		{
			delete_mark = gtk_text_buffer_create_mark (buffer, NULL, &iter, FALSE);
			gtk_text_buffer_delete (buffer, &iter, &head);

			if (end_tag != NULL)
			{
				gtk_text_buffer_get_iter_at_mark (buffer, &iter, delete_mark);
				head = iter;
				gtk_text_iter_forward_chars (&head, g_utf8_strlen (end_tag, -1));

				if (find_tag_in_line (&iter, &head, end_tag))
					gtk_text_buffer_delete (buffer, &iter, &head);
			}

			gtk_text_buffer_delete_mark (buffer, delete_mark);
		}

		gtk_text_buffer_get_iter_at_mark (buffer, &iter, iter_mark);
		gtk_text_iter_forward_line (&iter);
		gtk_text_buffer_move_mark (buffer, iter_mark, &iter);
	}

	gtk_text_buffer_end_user_action (buffer);

	gtk_text_buffer_delete_mark (buffer, iter_mark);
}

static gboolean
get_line_comment_bounds (PlumaDocument *document,
                         GtkTextIter   *start,
                         GtkTextIter   *end,
                         gboolean      *deselect)
{
	GtkTextMark *insert_mark;

	*deselect = FALSE;

	insert_mark = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (document));

	if (gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (document), start, end))
	{
		if (gtk_text_iter_ends_line (start))
			gtk_text_iter_forward_line (start);
		else if (!gtk_text_iter_starts_line (start))
			gtk_text_iter_set_line_offset (start, 0);

		if (gtk_text_iter_starts_line (end))
			gtk_text_iter_backward_char (end);
		else if (!gtk_text_iter_ends_line (end))
			gtk_text_iter_forward_to_line_end (end);
	}
	else
	{
		*deselect = TRUE;
		gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (document), start, insert_mark);
		gtk_text_iter_set_line_offset (start, 0);
		*end = *start;
		gtk_text_iter_forward_to_line_end (end);
	}

	return TRUE;
}

static gboolean
get_block_comment_bounds (PlumaDocument *document,
                          GtkTextIter   *start,
                          GtkTextIter   *end,
                          gboolean      *deselect)
{
	GtkTextMark *insert_mark;

	*deselect = FALSE;

	insert_mark = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (document));

	if (!gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (document), start, end))
	{
		*deselect = TRUE;
		gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (document), start, insert_mark);
		gtk_text_iter_set_line_offset (start, 0);
		*end = *start;
		gtk_text_iter_forward_to_line_end (end);
	}

	return TRUE;
}

static gboolean
iter_starts_with_text (GtkTextIter *iter,
                       const gchar *text)
{
	GtkTextIter end = *iter;
	gchar *slice;
	gboolean matches;

	gtk_text_iter_forward_chars (&end, g_utf8_strlen (text, -1));
	slice = gtk_text_iter_get_slice (iter, &end);
	matches = g_strcmp0 (slice, text) == 0;
	g_free (slice);

	return matches;
}

static gboolean
iter_ends_with_text (GtkTextIter *iter,
                     const gchar *text)
{
	GtkTextIter start = *iter;
	gchar *slice;
	gboolean matches;

	gtk_text_iter_backward_chars (&start, g_utf8_strlen (text, -1));
	slice = gtk_text_iter_get_slice (&start, iter);
	matches = g_strcmp0 (slice, text) == 0;
	g_free (slice);

	return matches;
}

static void
restore_cursor_if_needed (PlumaDocument *document,
                          gboolean       deselect)
{
	GtkTextMark *insert_mark;
	GtkTextIter old_pos;

	if (deselect)
	{
		insert_mark = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (document));
		gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (document), &old_pos, insert_mark);
		gtk_text_buffer_select_range (GTK_TEXT_BUFFER (document), &old_pos, &old_pos);
		gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (document), &old_pos);
	}
}

void
pluma_comment_toggle_line_comment (PlumaDocument *document)
{
	GtkSourceLanguage *language;
	const gchar *start_tag;
	const gchar *end_tag;
	GtkTextIter start;
	GtkTextIter end;
	gboolean deselect;

	g_return_if_fail (PLUMA_IS_DOCUMENT (document));

	language = pluma_document_get_language (document);
	if (language == NULL || !get_line_or_block_comment_tags (language, &start_tag, &end_tag))
		return;

	get_line_comment_bounds (document, &start, &end, &deselect);

	if (all_nonempty_lines_have_tag (&start, &end, start_tag))
		remove_comment_characters (GTK_TEXT_BUFFER (document), start_tag, end_tag, &start, &end);
	else
		add_comment_characters (GTK_TEXT_BUFFER (document), start_tag, end_tag, &start, &end);

	restore_cursor_if_needed (document, deselect);
}

void
pluma_comment_toggle_block_comment (PlumaDocument *document)
{
	GtkSourceLanguage *language;
	const gchar *start_tag;
	const gchar *end_tag;
	GtkTextIter start;
	GtkTextIter end;
	GtkTextIter block_start;
	GtkTextIter block_end;
	GtkTextIter start_tag_end;
	GtkTextIter end_tag_start;
	gboolean deselect;

	g_return_if_fail (PLUMA_IS_DOCUMENT (document));

	language = pluma_document_get_language (document);
	if (language == NULL)
		return;

	get_block_comment_tags (language, &start_tag, &end_tag);
	if (start_tag == NULL)
		return;

	get_block_comment_bounds (document, &start, &end, &deselect);

	block_start = start;
	block_end = end;

	if (iter_ends_with_text (&block_start, start_tag) &&
	    iter_starts_with_text (&block_end, end_tag))
	{
		gtk_text_iter_backward_chars (&block_start, g_utf8_strlen (start_tag, -1));
		gtk_text_iter_forward_chars (&block_end, g_utf8_strlen (end_tag, -1));
	}

	start_tag_end = block_start;
	end_tag_start = block_end;
	gtk_text_iter_forward_chars (&start_tag_end, g_utf8_strlen (start_tag, -1));
	gtk_text_iter_backward_chars (&end_tag_start, g_utf8_strlen (end_tag, -1));

	if (gtk_text_iter_compare (&start_tag_end, &block_end) <= 0 &&
	    gtk_text_iter_compare (&end_tag_start, &block_start) >= 0)
	{
		gchar *prefix;
		gchar *suffix;

		prefix = gtk_text_iter_get_slice (&block_start, &start_tag_end);
		suffix = gtk_text_iter_get_slice (&end_tag_start, &block_end);

		if (g_strcmp0 (prefix, start_tag) == 0 &&
		    g_strcmp0 (suffix, end_tag) == 0)
		{
			GtkTextBuffer *buffer = GTK_TEXT_BUFFER (document);
			GtkTextMark *block_start_mark;
			GtkTextMark *start_tag_end_mark;
			GtkTextMark *end_tag_start_mark;
			GtkTextMark *block_end_mark;
			GtkTextIter delete_start;
			GtkTextIter delete_end;

			block_start_mark = gtk_text_buffer_create_mark (buffer, NULL, &block_start, TRUE);
			start_tag_end_mark = gtk_text_buffer_create_mark (buffer, NULL, &start_tag_end, FALSE);
			end_tag_start_mark = gtk_text_buffer_create_mark (buffer, NULL, &end_tag_start, TRUE);
			block_end_mark = gtk_text_buffer_create_mark (buffer, NULL, &block_end, FALSE);

			gtk_text_buffer_begin_user_action (buffer);

			gtk_text_buffer_get_iter_at_mark (buffer, &delete_start, end_tag_start_mark);
			gtk_text_buffer_get_iter_at_mark (buffer, &delete_end, block_end_mark);
			gtk_text_buffer_delete (buffer, &delete_start, &delete_end);

			gtk_text_buffer_get_iter_at_mark (buffer, &delete_start, block_start_mark);
			gtk_text_buffer_get_iter_at_mark (buffer, &delete_end, start_tag_end_mark);
			gtk_text_buffer_delete (buffer, &delete_start, &delete_end);

			gtk_text_buffer_end_user_action (buffer);

			gtk_text_buffer_delete_mark (buffer, block_start_mark);
			gtk_text_buffer_delete_mark (buffer, start_tag_end_mark);
			gtk_text_buffer_delete_mark (buffer, end_tag_start_mark);
			gtk_text_buffer_delete_mark (buffer, block_end_mark);

			g_free (prefix);
			g_free (suffix);
			restore_cursor_if_needed (document, deselect);
			return;
		}

		g_free (prefix);
		g_free (suffix);
	}

	{
		GtkTextBuffer *buffer = GTK_TEXT_BUFFER (document);
		GtkTextMark *start_mark;
		GtkTextMark *end_mark;
		GtkTextIter insert_iter;
		GtkTextIter select_start;
		GtkTextIter select_end;

		start_mark = gtk_text_buffer_create_mark (buffer, NULL, &start, TRUE);
		end_mark = gtk_text_buffer_create_mark (buffer, NULL, &end, FALSE);

		gtk_text_buffer_begin_user_action (buffer);

		gtk_text_buffer_get_iter_at_mark (buffer, &insert_iter, end_mark);
		gtk_text_buffer_insert (buffer, &insert_iter, end_tag, -1);

		gtk_text_buffer_get_iter_at_mark (buffer, &insert_iter, start_mark);
		gtk_text_buffer_insert (buffer, &insert_iter, start_tag, -1);

		gtk_text_buffer_end_user_action (buffer);

		if (!deselect)
		{
			gtk_text_buffer_get_iter_at_mark (buffer, &select_start, start_mark);
			gtk_text_buffer_get_iter_at_mark (buffer, &select_end, end_mark);
			gtk_text_iter_forward_chars (&select_end, g_utf8_strlen (end_tag, -1));
			gtk_text_buffer_select_range (buffer, &select_start, &select_end);
		}

		gtk_text_buffer_delete_mark (buffer, start_mark);
		gtk_text_buffer_delete_mark (buffer, end_mark);
	}

	restore_cursor_if_needed (document, deselect);
}
