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
#include <gio/gio.h>

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
 * be unit tested in isolation. It backs PlumaLargeFileView (see
 * pluma-large-file-view.h) for files too big for a normal GtkSourceView
 * tab; PlumaDocument/GtkSourceBuffer are untouched otherwise.
 *
 * Thread safety: a PlumaTextBuffer has no internal locking. Concurrent
 * *read-only* access from multiple threads is fine (this is exactly what
 * pluma-tab.c's asynchronous save relies on: a worker thread streams the
 * buffer to disk via pluma_text_buffer_ref() while the main thread may
 * still be redrawing it). Concurrently *mutating* it (insert/delete)
 * while another thread reads or writes it is not supported and will
 * corrupt the piece tree; callers that allow editing during a background
 * read must keep the two mutually exclusive themselves (pluma-tab.c does
 * this by making the view insensitive for the duration of a save).
 */

typedef struct _PlumaTextBuffer PlumaTextBuffer;

PlumaTextBuffer *pluma_text_buffer_new              (void);

/* @cancellable (nullable) is checked roughly once per 64KB while the
 * file is scanned to build the initial line index, so cancelling it
 * promptly aborts an in-flight open of a very large file. */
PlumaTextBuffer *pluma_text_buffer_new_from_file     (const gchar      *path,
                                                       GCancellable     *cancellable,
                                                       GError          **error);

/* Drops one reference; the buffer (mmap, add-buffer, piece tree) is
 * actually freed once the ref count reaches zero. Equivalent to
 * pluma_text_buffer_unref() -- kept as the primary name since callers
 * that never share a buffer (the common case) can just treat this as
 * "free" and ignore ref-counting entirely. */
void              pluma_text_buffer_free             (PlumaTextBuffer  *buffer);

/* For code that needs the buffer to outlive its current owner -- e.g. a
 * background save reading it after the widget that displays it could be
 * destroyed. Pairs with pluma_text_buffer_free()/_unref(). */
PlumaTextBuffer  *pluma_text_buffer_ref              (PlumaTextBuffer  *buffer);
void              pluma_text_buffer_unref            (PlumaTextBuffer  *buffer);

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
