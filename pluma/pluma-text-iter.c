/*
 * pluma-text-iter.c
 * This file is part of pluma
 *
 * Copyright (C) 2026 - Pluma contributors
 *
 * pluma is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * pluma is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pluma; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-text-iter.h"
#include "pluma-text-buffer-private.h"

#include <string.h>

void
pluma_text_iter_init (PlumaTextIter   *iter,
                       PlumaTextBuffer *buffer,
                       gsize            offset)
{
	g_return_if_fail (iter != NULL);
	g_return_if_fail (buffer != NULL);
	g_return_if_fail (offset <= pluma_text_buffer_get_length (buffer));

	iter->buffer = buffer;
	iter->offset = offset;
}

gsize
pluma_text_iter_get_offset (PlumaTextIter *iter)
{
	g_return_val_if_fail (iter != NULL, 0);

	return iter->offset;
}

gboolean
pluma_text_iter_is_end (PlumaTextIter *iter)
{
	g_return_val_if_fail (iter != NULL, TRUE);

	return iter->offset >= pluma_text_buffer_get_length (iter->buffer);
}

gsize
pluma_text_iter_read_chunk (PlumaTextIter  *iter,
                             const gchar   **chunk)
{
	gsize piece_start;
	gsize local;
	PlumaPieceNode *node;

	g_return_val_if_fail (iter != NULL, 0);

	if (iter->offset >= pluma_text_buffer_get_length (iter->buffer))
	{
		if (chunk != NULL)
			*chunk = NULL;
		return 0;
	}

	node = _pluma_text_buffer_locate (iter->buffer, iter->offset, &piece_start);
	g_assert (node != NULL);

	local = iter->offset - piece_start;

	if (chunk != NULL)
		*chunk = _pluma_piece_node_data (iter->buffer, node) + local;

	return node->length - local;
}

void
pluma_text_iter_advance (PlumaTextIter *iter,
                          gsize          n)
{
	g_return_if_fail (iter != NULL);
	g_return_if_fail (iter->offset + n <= pluma_text_buffer_get_length (iter->buffer));

	iter->offset += n;
}

gboolean
pluma_text_iter_next_char (PlumaTextIter *iter,
                            gunichar      *codepoint)
{
	guchar staging[4];
	gsize staged = 0;
	gsize scan_offset;
	gsize total_length;
	gsize seq_len;
	gunichar ch;
	gunichar validated;

	g_return_val_if_fail (iter != NULL, FALSE);

	total_length = pluma_text_buffer_get_length (iter->buffer);
	if (iter->offset >= total_length)
		return FALSE;

	/* Gather up to 4 bytes from the current position, hopping across piece
	 * boundaries if necessary, without touching @iter itself: we don't yet
	 * know how many of these bytes the codepoint will actually consume. */
	scan_offset = iter->offset;
	while (staged < 4 && scan_offset < total_length)
	{
		gsize piece_start;
		gsize local;
		gsize avail;
		gsize take;
		gsize i;
		const gchar *data;
		PlumaPieceNode *node = _pluma_text_buffer_locate (iter->buffer, scan_offset, &piece_start);

		g_assert (node != NULL);

		local = scan_offset - piece_start;
		data = _pluma_piece_node_data (iter->buffer, node);
		avail = node->length - local;
		take = MIN (avail, 4 - staged);

		for (i = 0; i < take; i++)
			staging[staged + i] = (guchar) data[local + i];

		staged += take;
		scan_offset += take;
	}

	validated = g_utf8_get_char_validated ((const gchar *) staging, (gssize) staged);

	if (validated == (gunichar) -1 || validated == (gunichar) -2)
	{
		/* Invalid, or a multi-byte sequence truncated by end-of-buffer:
		 * fall back to consuming one raw byte, same as GtkTextBuffer does
		 * on non-UTF-8 content. */
		ch = staging[0];
		seq_len = 1;
	}
	else
	{
		ch = validated;
		seq_len = (gsize) (g_utf8_next_char ((const gchar *) staging) - (const gchar *) staging);
	}

	iter->offset += seq_len;

	if (codepoint != NULL)
		*codepoint = ch;

	return TRUE;
}

gboolean
pluma_text_iter_next_line (PlumaTextIter *iter)
{
	gsize total_length;

	g_return_val_if_fail (iter != NULL, FALSE);

	total_length = pluma_text_buffer_get_length (iter->buffer);

	while (iter->offset < total_length)
	{
		const gchar *chunk;
		const gchar *nl;
		gsize chunk_len = pluma_text_iter_read_chunk (iter, &chunk);

		nl = memchr (chunk, '\n', chunk_len);

		if (nl != NULL)
		{
			iter->offset += (gsize) (nl - chunk) + 1;
			return TRUE;
		}

		iter->offset += chunk_len;
	}

	return FALSE;
}
