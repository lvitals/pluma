/*
 * pluma-large-file-view.c
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

#include "pluma-large-file-view.h"
#include "pluma-text-iter.h"

#include <string.h>

/* Cap on how many bytes of a single line are ever fetched/measured/drawn.
 * Purely a display limit -- editing is never restricted by it -- that
 * keeps one absurdly long line (a minified JS blob, say) from turning a
 * single repaint into an unbounded Pango layout. */
#define PLUMA_LARGE_FILE_VIEW_MAX_LINE_BYTES 8192

/* Oldest undo entries are dropped past this many groups, so an very long
 * editing session can't grow the undo stack without bound. */
#define PLUMA_LARGE_FILE_VIEW_MAX_UNDO 5000

typedef struct
{
	gsize offset;
	gchar *removed;
	gsize removed_len;
	gchar *inserted;
	gsize inserted_len;
} PlumaLargeFileEdit;

struct _PlumaLargeFileView
{
	GtkWidget parent_instance;

	PlumaTextBuffer *buffer;

	PangoFontDescription *font_desc;
	gboolean metrics_valid;
	gint line_height;
	gint char_width;

	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;
	gint hadjustment_upper;

	gint left_margin;

	gsize cursor_offset;
	gdouble goal_x; /* -1 means "recompute from cursor_offset" */
	gboolean cursor_visible;
	guint blink_timeout_id;

	gboolean modified;
	gboolean last_edit_was_typing;
	gboolean last_edit_was_backspace;

	GArray *undo_stack;
	GArray *redo_stack;
};

enum
{
	PROP_0,
	PROP_HADJUSTMENT,
	PROP_VADJUSTMENT,
	PROP_HSCROLL_POLICY,
	PROP_VSCROLL_POLICY,
	PROP_MODIFIED
};

static void pluma_large_file_view_scrollable_init (GtkScrollableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PlumaLargeFileView, pluma_large_file_view, GTK_TYPE_WIDGET,
                          G_IMPLEMENT_INTERFACE (GTK_TYPE_SCROLLABLE, pluma_large_file_view_scrollable_init))

/* ---- small helpers ---------------------------------------------------- */

static gpointer
buf_dup (const gchar *data, gsize len)
{
	gpointer p;

	if (len == 0)
		return NULL;

	p = g_malloc (len);
	memcpy (p, data, len);
	return p;
}

static void
set_modified (PlumaLargeFileView *view,
              gboolean            modified)
{
	if (view->modified == modified)
		return;

	view->modified = modified;
	g_object_notify (G_OBJECT (view), "modified");
}

static void
update_font_metrics (PlumaLargeFileView *view)
{
	PangoContext *context = gtk_widget_get_pango_context (GTK_WIDGET (view));
	PangoFontMetrics *metrics = pango_context_get_metrics (context, view->font_desc, NULL);

	view->char_width = PANGO_PIXELS (pango_font_metrics_get_approximate_char_width (metrics));
	view->line_height = PANGO_PIXELS (pango_font_metrics_get_ascent (metrics) +
	                                   pango_font_metrics_get_descent (metrics)) + 2;

	if (view->char_width < 1)
		view->char_width = 1;
	if (view->line_height < 1)
		view->line_height = 1;

	pango_font_metrics_unref (metrics);
	view->metrics_valid = TRUE;
}

static gint
compute_gutter_width (PlumaLargeFileView *view,
                       gsize                total_lines)
{
	gint digits = 1;
	gsize n = total_lines;

	while (n >= 10)
	{
		n /= 10;
		digits++;
	}

	return digits * view->char_width + 16;
}

/* Fetches (a possibly-capped prefix of) the text of @line. This never
 * affects what is stored -- only what gets measured/drawn. */
static gchar *
fetch_line_text (PlumaLargeFileView *view,
                  gsize                line,
                  gsize               *out_start,
                  gsize               *out_len,
                  gboolean            *out_truncated)
{
	gsize total_lines = pluma_text_buffer_get_line_count (view->buffer);
	gsize start = pluma_text_buffer_offset_for_line (view->buffer, line);
	gsize end;
	gboolean truncated = FALSE;

	if (line + 1 < total_lines)
		end = pluma_text_buffer_offset_for_line (view->buffer, line + 1) - 1;
	else
		end = pluma_text_buffer_get_length (view->buffer);

	if (end - start > PLUMA_LARGE_FILE_VIEW_MAX_LINE_BYTES)
	{
		end = start + PLUMA_LARGE_FILE_VIEW_MAX_LINE_BYTES;
		truncated = TRUE;
	}

	if (out_start != NULL)
		*out_start = start;
	if (out_len != NULL)
		*out_len = end - start;
	if (out_truncated != NULL)
		*out_truncated = truncated;

	return pluma_text_buffer_get_text (view->buffer, start, end - start);
}

static gint
measure_prefix_width (PlumaLargeFileView *view,
                       gsize                start,
                       gsize                len)
{
	gchar *text;
	PangoLayout *layout;
	gint w = 0;

	if (len == 0)
		return 0;

	if (len > PLUMA_LARGE_FILE_VIEW_MAX_LINE_BYTES)
		len = PLUMA_LARGE_FILE_VIEW_MAX_LINE_BYTES;

	text = pluma_text_buffer_get_text (view->buffer, start, len);

	layout = pango_layout_new (gtk_widget_get_pango_context (GTK_WIDGET (view)));
	pango_layout_set_font_description (layout, view->font_desc);
	pango_layout_set_text (layout, text, (gint) len);
	pango_layout_get_pixel_size (layout, &w, NULL);

	g_object_unref (layout);
	g_free (text);

	return w;
}

static gsize
prev_char_offset (PlumaLargeFileView *view,
                   gsize                offset)
{
	if (offset == 0)
		return 0;

	offset--;

	while (offset > 0)
	{
		PlumaTextIter iter;
		const gchar *chunk;

		pluma_text_iter_init (&iter, view->buffer, offset);
		pluma_text_iter_read_chunk (&iter, &chunk);

		if (((guchar) chunk[0] & 0xC0) != 0x80)
			break;

		offset--;
	}

	return offset;
}

static gsize
next_char_offset (PlumaLargeFileView *view,
                   gsize                offset)
{
	PlumaTextIter iter;

	pluma_text_iter_init (&iter, view->buffer, offset);
	pluma_text_iter_next_char (&iter, NULL);

	return pluma_text_iter_get_offset (&iter);
}

/* ---- cursor blink ------------------------------------------------------ */

static gboolean
on_blink_timeout (gpointer data)
{
	PlumaLargeFileView *view = data;

	view->cursor_visible = !view->cursor_visible;
	gtk_widget_queue_draw (GTK_WIDGET (view));

	return G_SOURCE_CONTINUE;
}

static void
reset_cursor_blink (PlumaLargeFileView *view)
{
	GtkSettings *settings = gtk_widget_get_settings (GTK_WIDGET (view));
	gint blink_time = 1200;

	if (view->blink_timeout_id != 0)
	{
		g_source_remove (view->blink_timeout_id);
		view->blink_timeout_id = 0;
	}

	view->cursor_visible = TRUE;

	if (settings != NULL)
		g_object_get (settings, "gtk-cursor-blink-time", &blink_time, NULL);

	if (gtk_widget_has_focus (GTK_WIDGET (view)))
		view->blink_timeout_id = g_timeout_add (MAX (blink_time / 2, 100), on_blink_timeout, view);
}

static void
stop_cursor_blink (PlumaLargeFileView *view)
{
	if (view->blink_timeout_id != 0)
	{
		g_source_remove (view->blink_timeout_id);
		view->blink_timeout_id = 0;
	}

	view->cursor_visible = FALSE;
}

/* ---- adjustments -------------------------------------------------------- */

static void
on_adjustment_value_changed (GtkAdjustment *adjustment,
                              gpointer       data)
{
	gtk_widget_queue_draw (GTK_WIDGET (data));
}

static void
update_adjustments (PlumaLargeFileView *view)
{
	GtkAllocation allocation;
	gsize total_lines;
	gdouble v_upper;

	if (view->vadjustment == NULL || view->hadjustment == NULL)
		return;

	gtk_widget_get_allocation (GTK_WIDGET (view), &allocation);

	total_lines = pluma_text_buffer_get_line_count (view->buffer);
	v_upper = (gdouble) total_lines * view->line_height;

	gtk_adjustment_configure (view->vadjustment,
	                           gtk_adjustment_get_value (view->vadjustment),
	                           0, MAX (v_upper, allocation.height),
	                           view->line_height, allocation.height, allocation.height);

	gtk_adjustment_configure (view->hadjustment,
	                           gtk_adjustment_get_value (view->hadjustment),
	                           0, MAX (view->hadjustment_upper, allocation.width),
	                           view->char_width, allocation.width, allocation.width);
}

static void
set_hadjustment (PlumaLargeFileView *view,
                  GtkAdjustment       *adjustment)
{
	if (adjustment == view->hadjustment)
		return;

	if (view->hadjustment != NULL)
	{
		g_signal_handlers_disconnect_by_func (view->hadjustment, on_adjustment_value_changed, view);
		g_object_unref (view->hadjustment);
	}

	if (adjustment == NULL)
		adjustment = gtk_adjustment_new (0, 0, 0, 0, 0, 0);

	view->hadjustment = g_object_ref_sink (adjustment);
	g_signal_connect (view->hadjustment, "value-changed", G_CALLBACK (on_adjustment_value_changed), view);

	update_adjustments (view);
	g_object_notify (G_OBJECT (view), "hadjustment");
}

static void
set_vadjustment (PlumaLargeFileView *view,
                  GtkAdjustment       *adjustment)
{
	if (adjustment == view->vadjustment)
		return;

	if (view->vadjustment != NULL)
	{
		g_signal_handlers_disconnect_by_func (view->vadjustment, on_adjustment_value_changed, view);
		g_object_unref (view->vadjustment);
	}

	if (adjustment == NULL)
		adjustment = gtk_adjustment_new (0, 0, 0, 0, 0, 0);

	view->vadjustment = g_object_ref_sink (adjustment);
	g_signal_connect (view->vadjustment, "value-changed", G_CALLBACK (on_adjustment_value_changed), view);

	update_adjustments (view);
	g_object_notify (G_OBJECT (view), "vadjustment");
}

static void
ensure_cursor_visible (PlumaLargeFileView *view)
{
	GtkAllocation allocation;
	gsize line;
	gdouble cursor_y;

	gtk_widget_get_allocation (GTK_WIDGET (view), &allocation);

	line = pluma_text_buffer_line_for_offset (view->buffer, view->cursor_offset);
	cursor_y = (gdouble) line * view->line_height;

	if (view->vadjustment != NULL)
	{
		gdouble v = gtk_adjustment_get_value (view->vadjustment);

		if (cursor_y < v)
			gtk_adjustment_set_value (view->vadjustment, cursor_y);
		else if (cursor_y + view->line_height > v + allocation.height)
			gtk_adjustment_set_value (view->vadjustment, cursor_y + view->line_height - allocation.height);
	}

	if (view->hadjustment != NULL)
	{
		gsize line_start = pluma_text_buffer_offset_for_line (view->buffer, line);
		gint cursor_x = view->left_margin + 4 +
		                measure_prefix_width (view, line_start, view->cursor_offset - line_start);
		gdouble h = gtk_adjustment_get_value (view->hadjustment);

		if (cursor_x - view->left_margin < h)
			gtk_adjustment_set_value (view->hadjustment, MAX (0, cursor_x - view->left_margin - 4));
		else if (cursor_x > h + allocation.width - 20)
			gtk_adjustment_set_value (view->hadjustment, cursor_x - allocation.width + 20);
	}
}

/* ---- cursor movement ---------------------------------------------------- */

static void
move_cursor_left (PlumaLargeFileView *view)
{
	view->cursor_offset = prev_char_offset (view, view->cursor_offset);
	view->goal_x = -1;
	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
}

static void
move_cursor_right (PlumaLargeFileView *view)
{
	if (view->cursor_offset >= pluma_text_buffer_get_length (view->buffer))
		return;

	view->cursor_offset = next_char_offset (view, view->cursor_offset);
	view->goal_x = -1;
	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
}

static void
move_cursor_home (PlumaLargeFileView *view)
{
	gsize line = pluma_text_buffer_line_for_offset (view->buffer, view->cursor_offset);

	view->cursor_offset = pluma_text_buffer_offset_for_line (view->buffer, line);
	view->goal_x = 0;
	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
}

static void
move_cursor_end (PlumaLargeFileView *view)
{
	gsize line = pluma_text_buffer_line_for_offset (view->buffer, view->cursor_offset);
	gsize total_lines = pluma_text_buffer_get_line_count (view->buffer);

	if (line + 1 < total_lines)
		view->cursor_offset = pluma_text_buffer_offset_for_line (view->buffer, line + 1) - 1;
	else
		view->cursor_offset = pluma_text_buffer_get_length (view->buffer);

	view->goal_x = -1;
	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
}

static void
move_cursor_vertical (PlumaLargeFileView *view,
                       gssize               delta_lines)
{
	gsize total_lines = pluma_text_buffer_get_line_count (view->buffer);
	gsize line = pluma_text_buffer_line_for_offset (view->buffer, view->cursor_offset);
	gsize new_line;
	gsize line_start, len;
	gchar *text;
	PangoLayout *layout;
	gint index, trailing;

	if (view->goal_x < 0)
	{
		gsize cur_line_start = pluma_text_buffer_offset_for_line (view->buffer, line);
		view->goal_x = measure_prefix_width (view, cur_line_start, view->cursor_offset - cur_line_start);
	}

	if (delta_lines < 0)
	{
		gsize dec = (gsize) (-delta_lines);
		new_line = (dec >= line) ? 0 : line - dec;
	}
	else
	{
		new_line = line + (gsize) delta_lines;
		if (new_line >= total_lines)
			new_line = total_lines - 1;
	}

	text = fetch_line_text (view, new_line, &line_start, &len, NULL);

	layout = pango_layout_new (gtk_widget_get_pango_context (GTK_WIDGET (view)));
	pango_layout_set_font_description (layout, view->font_desc);
	pango_layout_set_text (layout, text, (gint) len);

	pango_layout_xy_to_index (layout, (gint) (view->goal_x * PANGO_SCALE), 0, &index, &trailing);
	view->cursor_offset = line_start + (gsize) index;

	g_object_unref (layout);
	g_free (text);

	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
}

static void
move_cursor_page (PlumaLargeFileView *view,
                   gint                 direction)
{
	GtkAllocation allocation;
	gsize visible_lines;

	gtk_widget_get_allocation (GTK_WIDGET (view), &allocation);
	visible_lines = (gsize) (allocation.height / MAX (1, view->line_height));
	if (visible_lines == 0)
		visible_lines = 1;

	move_cursor_vertical (view, (gssize) direction * (gssize) visible_lines);

	if (view->vadjustment != NULL)
	{
		gdouble v = gtk_adjustment_get_value (view->vadjustment);
		gdouble upper = gtk_adjustment_get_upper (view->vadjustment);
		gdouble page = gtk_adjustment_get_page_size (view->vadjustment);
		gdouble delta = direction * (gdouble) visible_lines * view->line_height;

		gtk_adjustment_set_value (view->vadjustment, CLAMP (v + delta, 0, MAX (0, upper - page)));
	}
}

/* ---- editing / undo-redo ------------------------------------------------ */

static void
clear_redo_stack (PlumaLargeFileView *view)
{
	guint i;

	for (i = 0; i < view->redo_stack->len; i++)
	{
		PlumaLargeFileEdit *e = &g_array_index (view->redo_stack, PlumaLargeFileEdit, i);

		g_free (e->removed);
		g_free (e->inserted);
	}

	g_array_set_size (view->redo_stack, 0);
}

static void
push_edit (PlumaLargeFileView *view,
           gsize                offset,
           const gchar         *removed,
           gsize                removed_len,
           const gchar         *inserted,
           gsize                inserted_len)
{
	PlumaLargeFileEdit edit;

	clear_redo_stack (view);

	edit.offset = offset;
	edit.removed = buf_dup (removed, removed_len);
	edit.removed_len = removed_len;
	edit.inserted = buf_dup (inserted, inserted_len);
	edit.inserted_len = inserted_len;

	g_array_append_val (view->undo_stack, edit);

	if (view->undo_stack->len > PLUMA_LARGE_FILE_VIEW_MAX_UNDO)
	{
		PlumaLargeFileEdit *oldest = &g_array_index (view->undo_stack, PlumaLargeFileEdit, 0);

		g_free (oldest->removed);
		g_free (oldest->inserted);
		g_array_remove_index (view->undo_stack, 0);
	}
}

static void
apply_edit_forward (PlumaLargeFileView *view,
                     PlumaLargeFileEdit *edit)
{
	if (edit->removed_len > 0)
		pluma_text_buffer_delete (view->buffer, edit->offset, edit->removed_len);
	if (edit->inserted_len > 0)
		pluma_text_buffer_insert (view->buffer, edit->offset, edit->inserted, (gssize) edit->inserted_len);
}

static void
apply_edit_backward (PlumaLargeFileView *view,
                      PlumaLargeFileEdit *edit)
{
	if (edit->inserted_len > 0)
		pluma_text_buffer_delete (view->buffer, edit->offset, edit->inserted_len);
	if (edit->removed_len > 0)
		pluma_text_buffer_insert (view->buffer, edit->offset, edit->removed, (gssize) edit->removed_len);
}

static void
do_insert_text (PlumaLargeFileView *view,
                 const gchar         *text,
                 gsize                len,
                 gboolean             is_typing)
{
	gsize offset = view->cursor_offset;
	gboolean coalesced = FALSE;

	if (is_typing && view->last_edit_was_typing && view->undo_stack->len > 0)
	{
		PlumaLargeFileEdit *last = &g_array_index (view->undo_stack, PlumaLargeFileEdit, view->undo_stack->len - 1);

		if (last->removed_len == 0 && offset == last->offset + last->inserted_len)
		{
			gsize new_len = last->inserted_len + len;
			gchar *grown = g_malloc (new_len);

			memcpy (grown, last->inserted, last->inserted_len);
			memcpy (grown + last->inserted_len, text, len);
			g_free (last->inserted);
			last->inserted = grown;
			last->inserted_len = new_len;

			coalesced = TRUE;
		}
	}

	pluma_text_buffer_insert (view->buffer, offset, text, (gssize) len);

	if (!coalesced)
		push_edit (view, offset, NULL, 0, text, len);

	view->cursor_offset = offset + len;
	view->last_edit_was_typing = is_typing;
	view->last_edit_was_backspace = FALSE;
	view->goal_x = -1;
	set_modified (view, TRUE);
}

static void
do_backspace (PlumaLargeFileView *view)
{
	gsize end = view->cursor_offset;
	gsize start;
	gsize removed_len;
	gchar *removed;
	gboolean coalesced = FALSE;

	if (end == 0)
		return;

	start = prev_char_offset (view, end);
	removed_len = end - start;
	removed = pluma_text_buffer_get_text (view->buffer, start, removed_len);

	if (view->last_edit_was_backspace && view->undo_stack->len > 0)
	{
		PlumaLargeFileEdit *last = &g_array_index (view->undo_stack, PlumaLargeFileEdit, view->undo_stack->len - 1);

		if (last->inserted_len == 0 && end == last->offset)
		{
			gsize new_len = removed_len + last->removed_len;
			gchar *grown = g_malloc (new_len);

			memcpy (grown, removed, removed_len);
			memcpy (grown + removed_len, last->removed, last->removed_len);
			g_free (last->removed);
			last->removed = grown;
			last->removed_len = new_len;
			last->offset = start;

			coalesced = TRUE;
		}
	}

	pluma_text_buffer_delete (view->buffer, start, removed_len);

	if (!coalesced)
	{
		push_edit (view, start, removed, removed_len, NULL, 0);
		view->last_edit_was_backspace = TRUE;
	}

	g_free (removed);

	view->cursor_offset = start;
	view->last_edit_was_typing = FALSE;
	view->goal_x = -1;
	set_modified (view, TRUE);
}

static void
do_delete (PlumaLargeFileView *view)
{
	gsize start = view->cursor_offset;
	gsize end;
	gchar *removed;

	if (start >= pluma_text_buffer_get_length (view->buffer))
		return;

	end = next_char_offset (view, start);
	removed = pluma_text_buffer_get_text (view->buffer, start, end - start);

	pluma_text_buffer_delete (view->buffer, start, end - start);
	push_edit (view, start, removed, end - start, NULL, 0);

	g_free (removed);

	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
	view->goal_x = -1;
	set_modified (view, TRUE);
}

static void
do_undo (PlumaLargeFileView *view)
{
	PlumaLargeFileEdit edit;

	if (view->undo_stack->len == 0)
		return;

	edit = g_array_index (view->undo_stack, PlumaLargeFileEdit, view->undo_stack->len - 1);
	g_array_set_size (view->undo_stack, view->undo_stack->len - 1);

	apply_edit_backward (view, &edit);
	view->cursor_offset = edit.offset + edit.removed_len;

	g_array_append_val (view->redo_stack, edit);

	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
	view->goal_x = -1;
	set_modified (view, TRUE);
}

static void
do_redo (PlumaLargeFileView *view)
{
	PlumaLargeFileEdit edit;

	if (view->redo_stack->len == 0)
		return;

	edit = g_array_index (view->redo_stack, PlumaLargeFileEdit, view->redo_stack->len - 1);
	g_array_set_size (view->redo_stack, view->redo_stack->len - 1);

	apply_edit_forward (view, &edit);
	view->cursor_offset = edit.offset + edit.inserted_len;

	g_array_append_val (view->undo_stack, edit);

	view->last_edit_was_typing = FALSE;
	view->last_edit_was_backspace = FALSE;
	view->goal_x = -1;
	set_modified (view, TRUE);
}

/* ---- drawing ------------------------------------------------------------ */

static void
draw_line_number (PlumaLargeFileView *view,
                   cairo_t             *cr,
                   gsize                number,
                   gint                 gutter_width,
                   gdouble              y)
{
	gchar buf[32];
	PangoLayout *layout;
	gint w;

	g_snprintf (buf, sizeof (buf), "%" G_GSIZE_FORMAT, number);

	layout = pango_layout_new (gtk_widget_get_pango_context (GTK_WIDGET (view)));
	pango_layout_set_font_description (layout, view->font_desc);
	pango_layout_set_text (layout, buf, -1);
	pango_layout_get_pixel_size (layout, &w, NULL);

	cairo_save (cr);
	cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.8);
	cairo_move_to (cr, gutter_width - w - 8, y);
	pango_cairo_show_layout (cr, layout);
	cairo_restore (cr);

	g_object_unref (layout);
}

static gboolean
pluma_large_file_view_draw (GtkWidget *widget,
                             cairo_t   *cr)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (widget);
	GtkStyleContext *style_context;
	GtkAllocation allocation;
	gsize total_lines;
	gsize cursor_line;
	gsize line;
	gdouble v, h;
	gdouble y;
	gint gutter_width;
	gint max_line_width = 0;

	if (!view->metrics_valid)
		update_font_metrics (view);

	style_context = gtk_widget_get_style_context (widget);
	gtk_widget_get_allocation (widget, &allocation);

	gtk_render_background (style_context, cr, 0, 0, allocation.width, allocation.height);

	total_lines = pluma_text_buffer_get_line_count (view->buffer);
	cursor_line = pluma_text_buffer_line_for_offset (view->buffer, view->cursor_offset);

	gutter_width = compute_gutter_width (view, total_lines);
	view->left_margin = gutter_width;

	v = view->vadjustment ? gtk_adjustment_get_value (view->vadjustment) : 0;
	h = view->hadjustment ? gtk_adjustment_get_value (view->hadjustment) : 0;

	line = (gsize) (v / view->line_height);
	if (line >= total_lines)
		line = total_lines - 1;

	y = (gdouble) line * view->line_height - v;

	while (line < total_lines && y < allocation.height)
	{
		gsize line_start, len;
		gboolean truncated;
		gchar *text;
		PangoLayout *layout;
		gint w;

		text = fetch_line_text (view, line, &line_start, &len, &truncated);

		layout = pango_layout_new (gtk_widget_get_pango_context (widget));
		pango_layout_set_font_description (layout, view->font_desc);

		if (truncated)
		{
			gchar *display = g_strconcat (text, "\xe2\x80\xa6" /* U+2026 */, NULL);

			pango_layout_set_text (layout, display, -1);
			g_free (display);
		}
		else
		{
			pango_layout_set_text (layout, text, (gint) len);
		}

		pango_layout_get_pixel_size (layout, &w, NULL);
		if (w > max_line_width)
			max_line_width = w;

		draw_line_number (view, cr, line + 1, gutter_width, y);
		gtk_render_layout (style_context, cr, gutter_width + 4 - h, y, layout);

		g_object_unref (layout);

		if (line == cursor_line && gtk_widget_has_focus (widget) && view->cursor_visible)
		{
			gint cx = gutter_width + 4 - (gint) h +
			          measure_prefix_width (view, line_start, view->cursor_offset - line_start);
			GdkRGBA color;

			gtk_style_context_get_color (style_context, gtk_widget_get_state_flags (widget), &color);

			cairo_save (cr);
			gdk_cairo_set_source_rgba (cr, &color);
			cairo_set_line_width (cr, 1.5);
			cairo_move_to (cr, cx + 0.5, y);
			cairo_line_to (cr, cx + 0.5, y + view->line_height);
			cairo_stroke (cr);
			cairo_restore (cr);
		}

		g_free (text);

		line++;
		y += view->line_height;
	}

	view->hadjustment_upper = MAX (max_line_width + gutter_width + 40, 0);
	update_adjustments (view);

	return FALSE;
}

/* ---- events -------------------------------------------------------------- */

static gboolean
pluma_large_file_view_key_press (GtkWidget   *widget,
                                  GdkEventKey *event)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (widget);
	gboolean handled = TRUE;
	guint mods = event->state & gtk_accelerator_get_default_mod_mask ();
	gboolean shift = (mods & GDK_SHIFT_MASK) != 0;
	gboolean ctrl = (mods & GDK_CONTROL_MASK) != 0 && (mods & ~(GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 0;

	switch (event->keyval)
	{
		case GDK_KEY_Left:
			move_cursor_left (view);
			break;
		case GDK_KEY_Right:
			move_cursor_right (view);
			break;
		case GDK_KEY_Up:
			move_cursor_vertical (view, -1);
			break;
		case GDK_KEY_Down:
			move_cursor_vertical (view, 1);
			break;
		case GDK_KEY_Home:
			move_cursor_home (view);
			break;
		case GDK_KEY_End:
			move_cursor_end (view);
			break;
		case GDK_KEY_Page_Up:
			move_cursor_page (view, -1);
			break;
		case GDK_KEY_Page_Down:
			move_cursor_page (view, 1);
			break;
		case GDK_KEY_BackSpace:
			do_backspace (view);
			break;
		case GDK_KEY_Delete:
		case GDK_KEY_KP_Delete:
			do_delete (view);
			break;
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
			do_insert_text (view, "\n", 1, FALSE);
			break;
		case GDK_KEY_Tab:
			do_insert_text (view, "\t", 1, FALSE);
			break;
		case GDK_KEY_z:
		case GDK_KEY_Z:
			if (ctrl && shift)
				do_redo (view);
			else if (ctrl)
				do_undo (view);
			else
				handled = FALSE;
			break;
		case GDK_KEY_y:
		case GDK_KEY_Y:
			if (ctrl)
				do_redo (view);
			else
				handled = FALSE;
			break;
		default:
		{
			gunichar ch = gdk_keyval_to_unicode (event->keyval);

			if (!ctrl && ch != 0 && g_unichar_isprint (ch))
			{
				gchar utf8[6];
				gint len = g_unichar_to_utf8 (ch, utf8);

				do_insert_text (view, utf8, (gsize) len, TRUE);
			}
			else
			{
				handled = FALSE;
			}
		}
	}

	if (handled)
	{
		reset_cursor_blink (view);
		ensure_cursor_visible (view);
		gtk_widget_queue_draw (widget);
	}

	return handled;
}

static gboolean
pluma_large_file_view_button_press (GtkWidget      *widget,
                                     GdkEventButton *event)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (widget);

	gtk_widget_grab_focus (widget);

	if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
	{
		gsize total_lines = pluma_text_buffer_get_line_count (view->buffer);
		gdouble v = view->vadjustment ? gtk_adjustment_get_value (view->vadjustment) : 0;
		gdouble h = view->hadjustment ? gtk_adjustment_get_value (view->hadjustment) : 0;
		gsize line;
		gsize line_start, len;
		gchar *text;
		PangoLayout *layout;
		gint index, trailing;
		gint x_in_text;

		line = (gsize) ((event->y + v) / view->line_height);
		if (line >= total_lines)
			line = total_lines - 1;

		text = fetch_line_text (view, line, &line_start, &len, NULL);

		layout = pango_layout_new (gtk_widget_get_pango_context (widget));
		pango_layout_set_font_description (layout, view->font_desc);
		pango_layout_set_text (layout, text, (gint) len);

		x_in_text = (gint) (event->x + h) - view->left_margin - 4;
		if (x_in_text < 0)
			x_in_text = 0;

		pango_layout_xy_to_index (layout, x_in_text * PANGO_SCALE, 0, &index, &trailing);

		view->cursor_offset = line_start + (gsize) index;
		view->goal_x = -1;
		view->last_edit_was_typing = FALSE;
		view->last_edit_was_backspace = FALSE;

		g_object_unref (layout);
		g_free (text);

		reset_cursor_blink (view);
		gtk_widget_queue_draw (widget);
	}

	return TRUE;
}

static gboolean
pluma_large_file_view_scroll (GtkWidget      *widget,
                               GdkEventScroll *event)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (widget);
	gdouble delta = 0;

	if (event->direction == GDK_SCROLL_UP)
		delta = -3 * view->line_height;
	else if (event->direction == GDK_SCROLL_DOWN)
		delta = 3 * view->line_height;
	else if (event->direction == GDK_SCROLL_SMOOTH)
		delta = event->delta_y * 3 * view->line_height;
	else
		return FALSE;

	if (view->vadjustment != NULL)
	{
		gdouble v = gtk_adjustment_get_value (view->vadjustment);
		gdouble upper = gtk_adjustment_get_upper (view->vadjustment);
		gdouble page = gtk_adjustment_get_page_size (view->vadjustment);

		gtk_adjustment_set_value (view->vadjustment, CLAMP (v + delta, 0, MAX (0, upper - page)));
	}

	return TRUE;
}

static gboolean
pluma_large_file_view_focus_in (GtkWidget     *widget,
                                 GdkEventFocus *event)
{
	reset_cursor_blink (PLUMA_LARGE_FILE_VIEW (widget));
	gtk_widget_queue_draw (widget);
	return FALSE;
}

static gboolean
pluma_large_file_view_focus_out (GtkWidget     *widget,
                                  GdkEventFocus *event)
{
	stop_cursor_blink (PLUMA_LARGE_FILE_VIEW (widget));
	gtk_widget_queue_draw (widget);
	return FALSE;
}

/* ---- widget lifecycle ----------------------------------------------------- */

static void
pluma_large_file_view_realize (GtkWidget *widget)
{
	GtkAllocation allocation;
	GdkWindowAttr attributes;
	gint attributes_mask;
	GdkWindow *window;

	gtk_widget_set_realized (widget, TRUE);
	gtk_widget_get_allocation (widget, &allocation);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = allocation.x;
	attributes.y = allocation.y;
	attributes.width = allocation.width;
	attributes.height = allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.event_mask = gtk_widget_get_events (widget)
	                       | GDK_EXPOSURE_MASK
	                       | GDK_BUTTON_PRESS_MASK
	                       | GDK_BUTTON_RELEASE_MASK
	                       | GDK_KEY_PRESS_MASK
	                       | GDK_POINTER_MOTION_MASK
	                       | GDK_SCROLL_MASK
	                       | GDK_SMOOTH_SCROLL_MASK;

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

	window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
	gtk_widget_set_window (widget, window);
	gtk_widget_register_window (widget, window);

	update_font_metrics (PLUMA_LARGE_FILE_VIEW (widget));
}

static void
pluma_large_file_view_get_preferred_width (GtkWidget *widget,
                                            gint      *minimum,
                                            gint      *natural)
{
	*minimum = *natural = 200;
}

static void
pluma_large_file_view_get_preferred_height (GtkWidget *widget,
                                             gint      *minimum,
                                             gint      *natural)
{
	*minimum = *natural = 200;
}

static void
pluma_large_file_view_size_allocate (GtkWidget     *widget,
                                      GtkAllocation *allocation)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (widget);

	GTK_WIDGET_CLASS (pluma_large_file_view_parent_class)->size_allocate (widget, allocation);

	if (gtk_widget_get_realized (widget))
	{
		gdk_window_move_resize (gtk_widget_get_window (widget),
		                         allocation->x, allocation->y,
		                         allocation->width, allocation->height);
	}

	update_adjustments (view);
}

static void
pluma_large_file_view_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			g_value_set_object (value, view->hadjustment);
			break;
		case PROP_VADJUSTMENT:
			g_value_set_object (value, view->vadjustment);
			break;
		case PROP_HSCROLL_POLICY:
			g_value_set_enum (value, view->hscroll_policy);
			break;
		case PROP_VSCROLL_POLICY:
			g_value_set_enum (value, view->vscroll_policy);
			break;
		case PROP_MODIFIED:
			g_value_set_boolean (value, view->modified);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pluma_large_file_view_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			set_hadjustment (view, g_value_get_object (value));
			break;
		case PROP_VADJUSTMENT:
			set_vadjustment (view, g_value_get_object (value));
			break;
		case PROP_HSCROLL_POLICY:
			view->hscroll_policy = g_value_get_enum (value);
			break;
		case PROP_VSCROLL_POLICY:
			view->vscroll_policy = g_value_get_enum (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pluma_large_file_view_dispose (GObject *object)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (object);

	stop_cursor_blink (view);

	if (view->hadjustment != NULL)
	{
		g_signal_handlers_disconnect_by_func (view->hadjustment, on_adjustment_value_changed, view);
		g_clear_object (&view->hadjustment);
	}

	if (view->vadjustment != NULL)
	{
		g_signal_handlers_disconnect_by_func (view->vadjustment, on_adjustment_value_changed, view);
		g_clear_object (&view->vadjustment);
	}

	G_OBJECT_CLASS (pluma_large_file_view_parent_class)->dispose (object);
}

static void
pluma_large_file_view_finalize (GObject *object)
{
	PlumaLargeFileView *view = PLUMA_LARGE_FILE_VIEW (object);
	guint i;

	for (i = 0; i < view->undo_stack->len; i++)
	{
		PlumaLargeFileEdit *e = &g_array_index (view->undo_stack, PlumaLargeFileEdit, i);

		g_free (e->removed);
		g_free (e->inserted);
	}
	g_array_free (view->undo_stack, TRUE);

	for (i = 0; i < view->redo_stack->len; i++)
	{
		PlumaLargeFileEdit *e = &g_array_index (view->redo_stack, PlumaLargeFileEdit, i);

		g_free (e->removed);
		g_free (e->inserted);
	}
	g_array_free (view->redo_stack, TRUE);

	pango_font_description_free (view->font_desc);
	pluma_text_buffer_free (view->buffer);

	G_OBJECT_CLASS (pluma_large_file_view_parent_class)->finalize (object);
}

static void
pluma_large_file_view_scrollable_init (GtkScrollableInterface *iface)
{
}

static void
pluma_large_file_view_class_init (PlumaLargeFileViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = pluma_large_file_view_get_property;
	object_class->set_property = pluma_large_file_view_set_property;
	object_class->dispose = pluma_large_file_view_dispose;
	object_class->finalize = pluma_large_file_view_finalize;

	widget_class->realize = pluma_large_file_view_realize;
	widget_class->size_allocate = pluma_large_file_view_size_allocate;
	widget_class->get_preferred_width = pluma_large_file_view_get_preferred_width;
	widget_class->get_preferred_height = pluma_large_file_view_get_preferred_height;
	widget_class->draw = pluma_large_file_view_draw;
	widget_class->key_press_event = pluma_large_file_view_key_press;
	widget_class->button_press_event = pluma_large_file_view_button_press;
	widget_class->scroll_event = pluma_large_file_view_scroll;
	widget_class->focus_in_event = pluma_large_file_view_focus_in;
	widget_class->focus_out_event = pluma_large_file_view_focus_out;

	g_object_class_override_property (object_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (object_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (object_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (object_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	g_object_class_install_property (object_class, PROP_MODIFIED,
	                                  g_param_spec_boolean ("modified", "Modified",
	                                                         "Whether the buffer has unsaved changes",
	                                                         FALSE, G_PARAM_READABLE));
}

static void
pluma_large_file_view_init (PlumaLargeFileView *view)
{
	gtk_widget_set_can_focus (GTK_WIDGET (view), TRUE);
	gtk_widget_set_has_window (GTK_WIDGET (view), TRUE);

	view->font_desc = pango_font_description_from_string ("Monospace 10");
	view->line_height = 18;
	view->char_width = 8;
	view->hadjustment_upper = 800;

	view->cursor_offset = 0;
	view->goal_x = -1;
	view->cursor_visible = TRUE;

	view->undo_stack = g_array_new (FALSE, FALSE, sizeof (PlumaLargeFileEdit));
	view->redo_stack = g_array_new (FALSE, FALSE, sizeof (PlumaLargeFileEdit));
}

/* ---- public API ----------------------------------------------------------- */

GtkWidget *
pluma_large_file_view_new (PlumaTextBuffer *buffer)
{
	PlumaLargeFileView *view;

	g_return_val_if_fail (buffer != NULL, NULL);

	view = g_object_new (PLUMA_TYPE_LARGE_FILE_VIEW, NULL);
	view->buffer = buffer;

	return GTK_WIDGET (view);
}

PlumaTextBuffer *
pluma_large_file_view_get_buffer (PlumaLargeFileView *view)
{
	g_return_val_if_fail (PLUMA_IS_LARGE_FILE_VIEW (view), NULL);

	return view->buffer;
}

gboolean
pluma_large_file_view_get_modified (PlumaLargeFileView *view)
{
	g_return_val_if_fail (PLUMA_IS_LARGE_FILE_VIEW (view), FALSE);

	return view->modified;
}

void
pluma_large_file_view_mark_saved (PlumaLargeFileView *view)
{
	g_return_if_fail (PLUMA_IS_LARGE_FILE_VIEW (view));

	set_modified (view, FALSE);
}
