/*
 * pluma-text-iter.h
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

#ifndef __PLUMA_TEXT_ITER_H__
#define __PLUMA_TEXT_ITER_H__

#include <glib.h>
#include "pluma-text-buffer.h"

G_BEGIN_DECLS

/*
 * A value-typed, stack-allocated cursor over a PlumaTextBuffer. It never
 * allocates and never copies piece data: pluma_text_iter_read_chunk()
 * hands back a direct pointer into the mmap'd original buffer or the
 * add buffer, valid until the next edit.
 *
 * Like a GtkTextIter, a PlumaTextIter is invalidated by any insert or
 * delete on the buffer it points into.
 */
typedef struct
{
	/*< private >*/
	PlumaTextBuffer *buffer;
	gsize offset;
} PlumaTextIter;

void      pluma_text_iter_init        (PlumaTextIter    *iter,
                                        PlumaTextBuffer  *buffer,
                                        gsize             offset);

gsize     pluma_text_iter_get_offset  (PlumaTextIter    *iter);

gboolean  pluma_text_iter_is_end      (PlumaTextIter    *iter);

/* Returns a pointer to, and the length of, the run of bytes starting at
 * the iterator's current position that are contiguous in memory (i.e.
 * belong to the same piece). Does not advance the iterator. Returns 0
 * and sets *chunk to NULL at end of buffer. */
gsize     pluma_text_iter_read_chunk  (PlumaTextIter    *iter,
                                        const gchar     **chunk);

void      pluma_text_iter_advance     (PlumaTextIter    *iter,
                                        gsize             n);

/* Decodes and consumes one Unicode codepoint, even if its UTF-8 encoding
 * straddles a piece boundary. Returns FALSE at end of buffer. Invalid or
 * truncated UTF-8 is consumed one raw byte at a time. */
gboolean  pluma_text_iter_next_char   (PlumaTextIter    *iter,
                                        gunichar         *codepoint);

/* Advances to just past the next '\n'. Returns FALSE (and moves to the
 * end of the buffer) if there is no further newline. */
gboolean  pluma_text_iter_next_line   (PlumaTextIter    *iter);

G_END_DECLS

#endif /* __PLUMA_TEXT_ITER_H__ */
