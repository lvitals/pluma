/*
 * pluma-text-buffer.c
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

#ifndef __PLUMA_TEXT_BUFFER_H__
#define __PLUMA_TEXT_BUFFER_H__

#include <glib.h>

G_BEGIN_DECLS

/*
 * PlumaTextBuffer is a standalone (GTK-independent) text storage engine
 * for very large files. It is a piece table: the original file contents
 * are mmap()'d read-only (zero-copy, so opening does not require reading
 * the whole file into the process heap) and every edit is appended to a
 * small in-memory "add buffer". The document is represented as an
 * in-order sequence of pieces (references into one of those two byte
 * arrays), kept in a treap ("implicit treap" / rope): a randomized
 * balanced binary search tree ordered by cumulative byte offset, where
 * split() and merge() give expected O(log n) insert/delete without ever
 * copying the untouched parts of the document.
 *
 * Each node additionally tracks how many newline bytes its own piece
 * contains, and aggregates (subtree byte length, subtree newline count)
 * over its children. That turns "byte offset for line N" and "line
 * number for byte offset" into O(log n) tree descents instead of O(n)
 * scans, without maintaining a separate sparse checkpoint index.
 *
 * This module has no GTK/GtkSourceView dependency on purpose, so it can
 * be unit tested in isolation. It is not yet wired into PlumaDocument;
 * integrating it (e.g. routing very large files through this engine
 * while keeping GtkSourceBuffer for everything else) is a separate,
 * substantially larger follow-up that touches loading, undo/redo,
 * syntax highlighting and plugin-visible APIs.
 */

typedef struct _PlumaTextBuffer PlumaTextBuffer;

PlumaTextBuffer *pluma_text_buffer_new              (void);

PlumaTextBuffer *pluma_text_buffer_new_from_file     (const gchar      *path,
                                                       GError          **error);

void              pluma_text_buffer_free             (PlumaTextBuffer  *buffer);

gsize             pluma_text_buffer_get_length        (PlumaTextBuffer  *buffer);

gsize             pluma_text_buffer_get_line_count     (PlumaTextBuffer  *buffer);

void              pluma_text_buffer_insert            (PlumaTextBuffer  *buffer,
                                                       gsize             offset,
                                                       const gchar      *text,
                                                       gssize            length);

void              pluma_text_buffer_delete            (PlumaTextBuffer  *buffer,
                                                       gsize             offset,
                                                       gsize             length);

gsize             pluma_text_buffer_offset_for_line     (PlumaTextBuffer  *buffer,
                                                       gsize             line);

gsize             pluma_text_buffer_line_for_offset     (PlumaTextBuffer  *buffer,
                                                       gsize             offset);

gchar            *pluma_text_buffer_get_text          (PlumaTextBuffer  *buffer,
                                                       gsize             offset,
                                                       gsize             length);

gboolean          pluma_text_buffer_save              (PlumaTextBuffer  *buffer,
                                                       const gchar      *path,
                                                       GError          **error);

G_END_DECLS

#endif /* __PLUMA_TEXT_BUFFER_H__ */
