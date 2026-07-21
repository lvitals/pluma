/*
 * pluma-large-file-view.h
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

#ifndef __PLUMA_LARGE_FILE_VIEW_H__
#define __PLUMA_LARGE_FILE_VIEW_H__

#include <gtk/gtk.h>
#include "pluma-text-buffer.h"

G_BEGIN_DECLS

/*
 * A minimal, non-GtkTextView text widget for files too large to load into
 * a real GtkSourceBuffer. It only ever asks PlumaTextBuffer for the lines
 * currently on screen (plus a small margin), so its memory use and paint
 * time stay flat no matter how big the underlying file is.
 *
 * On purpose this is *not* a drop-in GtkSourceView replacement: no syntax
 * highlighting, no line wrapping, no text selection/clipboard, no input
 * method support beyond plain keysym-to-Unicode. It exists to make a huge
 * file viewable and lightly editable without the normal editor choking
 * on it; PlumaLargeFileWindow's "Open in Full Editor" action is the
 * escape hatch back to the real thing.
 */

#define PLUMA_TYPE_LARGE_FILE_VIEW (pluma_large_file_view_get_type ())
G_DECLARE_FINAL_TYPE (PlumaLargeFileView, pluma_large_file_view, PLUMA, LARGE_FILE_VIEW, GtkWidget)

/* Takes ownership of @buffer. */
GtkWidget        *pluma_large_file_view_new          (PlumaTextBuffer *buffer);

PlumaTextBuffer  *pluma_large_file_view_get_buffer    (PlumaLargeFileView *view);

gboolean          pluma_large_file_view_get_modified  (PlumaLargeFileView *view);

/* Called by PlumaLargeFileWindow after a successful save. */
void              pluma_large_file_view_mark_saved    (PlumaLargeFileView *view);

G_END_DECLS

#endif /* __PLUMA_LARGE_FILE_VIEW_H__ */
