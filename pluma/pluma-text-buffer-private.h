/*
 * pluma-text-buffer-private.h
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

/*
 * Internal layout of PlumaTextBuffer, shared only with pluma-text-iter.c.
 * Nothing outside this pair of "engine" files should include this header;
 * pluma-text-buffer.h is the public, opaque API.
 */

#ifndef __PLUMA_TEXT_BUFFER_PRIVATE_H__
#define __PLUMA_TEXT_BUFFER_PRIVATE_H__

#include "pluma-text-buffer.h"

G_BEGIN_DECLS

typedef enum
{
	PLUMA_PIECE_ORIGINAL,
	PLUMA_PIECE_ADD
} PlumaPieceBufferType;

typedef struct _PlumaPieceNode PlumaPieceNode;

struct _PlumaPieceNode
{
	PlumaPieceBufferType buffer_type;
	gsize buf_offset;
	gsize length;
	gsize line_breaks;

	guint32 priority;

	/* Sparse index of this piece's own newlines: checkpoints[i] is the
	 * local byte offset (relative to this piece) of newline number
	 * i * PLUMA_LINE_CHECKPOINT_INTERVAL. Without this, a piece covering
	 * a freshly mmap'd multi-million-line file would need an O(piece
	 * size) memchr scan for every single line lookup, no matter how
	 * balanced the tree above it is -- the tree only bounds the number
	 * of *pieces* to cross, not the size of the piece a lookup lands in. */
	gsize *checkpoints;
	gsize n_checkpoints;

	/* Aggregates over this node and both children, kept up to date by
	 * node_update() after every structural change. */
	gsize subtree_length;
	gsize subtree_line_breaks;

	PlumaPieceNode *left;
	PlumaPieceNode *right;
};

struct _PlumaTextBuffer
{
	/* Read-only mmap of the file this buffer was loaded from, or NULL for
	 * a buffer created empty. Never mutated. */
	gchar *original_data;
	gsize original_size;
	gboolean original_is_mmap;
	gint original_fd;

	/* Append-only store for every byte ever inserted, in insertion order.
	 * Pieces reference it by offset (not pointer), since g_byte_array
	 * may realloc its backing store as it grows. */
	GByteArray *add_data;

	PlumaPieceNode *root;

	/* Accessed with g_atomic_int_*(): a background save (see
	 * pluma-tab.c's save_large_file_tab()) takes its own ref so the
	 * buffer survives even if whatever owns it (a PlumaLargeFileView)
	 * is destroyed while the save is still in flight. */
	gint ref_count;
};

const gchar     *_pluma_piece_node_data     (PlumaTextBuffer *buffer,
                                              PlumaPieceNode  *node);

PlumaPieceNode  *_pluma_text_buffer_locate  (PlumaTextBuffer *buffer,
                                              gsize            offset,
                                              gsize           *piece_start);

G_END_DECLS

#endif /* __PLUMA_TEXT_BUFFER_PRIVATE_H__ */
