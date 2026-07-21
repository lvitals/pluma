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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-text-buffer-private.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* Every Nth newline within a single piece gets an index entry. A lookup
 * that falls between two checkpoints costs at most one memchr scan of
 * this many newlines' worth of bytes, regardless of how big the piece
 * is -- e.g. for a freshly mmap'd multi-hundred-MB file loaded as one
 * giant piece. */
#define PLUMA_LINE_CHECKPOINT_INTERVAL 128

/* Size of the fixed chunks the mmap'd original file is cut into at load
 * time (see pluma_text_buffer_new_from_file()). Bounds the cost of
 * splitting any single original-buffer piece, independent of file size. */
#define PLUMA_ORIGINAL_CHUNK_SIZE (64 * 1024)

/* ---- piece node helpers -------------------------------------------- */

static void
build_checkpoints (const gchar  *data,
                    gsize         len,
                    gsize        *out_line_breaks,
                    gsize       **out_checkpoints,
                    gsize        *out_n_checkpoints)
{
	GArray *cps = g_array_new (FALSE, FALSE, sizeof (gsize));
	const gchar *p = data;
	const gchar *end = data + len;
	gsize count = 0;

	while (p < end && (p = memchr (p, '\n', end - p)) != NULL)
	{
		if (count % PLUMA_LINE_CHECKPOINT_INTERVAL == 0)
		{
			gsize off = (gsize) (p - data);
			g_array_append_val (cps, off);
		}

		count++;
		p++;
	}

	*out_line_breaks = count;
	*out_n_checkpoints = cps->len;
	*out_checkpoints = (gsize *) g_array_free (cps, FALSE);
}

/* Takes ownership of @checkpoints. */
static PlumaPieceNode *
node_new (PlumaPieceBufferType  buffer_type,
          gsize                 buf_offset,
          gsize                 length,
          gsize                 line_breaks,
          gsize                *checkpoints,
          gsize                 n_checkpoints)
{
	PlumaPieceNode *node = g_new0 (PlumaPieceNode, 1);

	node->buffer_type = buffer_type;
	node->buf_offset = buf_offset;
	node->length = length;
	node->line_breaks = line_breaks;
	node->checkpoints = checkpoints;
	node->n_checkpoints = n_checkpoints;
	node->priority = g_random_int ();

	node->subtree_length = length;
	node->subtree_line_breaks = line_breaks;

	return node;
}

static void
node_free_recursive (PlumaPieceNode *node)
{
	if (node == NULL)
		return;

	node_free_recursive (node->left);
	node_free_recursive (node->right);
	g_free (node->checkpoints);
	g_free (node);
}

static void
node_update (PlumaPieceNode *node)
{
	gsize len = node->length;
	gsize lb = node->line_breaks;

	if (node->left != NULL)
	{
		len += node->left->subtree_length;
		lb += node->left->subtree_line_breaks;
	}

	if (node->right != NULL)
	{
		len += node->right->subtree_length;
		lb += node->right->subtree_line_breaks;
	}

	node->subtree_length = len;
	node->subtree_line_breaks = lb;
}

const gchar *
_pluma_piece_node_data (PlumaTextBuffer *buffer,
                         PlumaPieceNode  *node)
{
	if (node->buffer_type == PLUMA_PIECE_ORIGINAL)
		return buffer->original_data + node->buf_offset;
	else
		return (const gchar *) buffer->add_data->data + node->buf_offset;
}

static gsize
count_newlines_in_range (const gchar *data,
                          gsize        len)
{
	gsize count = 0;
	const gchar *p = data;
	const gchar *end = data + len;

	while (p < end && (p = memchr (p, '\n', end - p)) != NULL)
	{
		count++;
		p++;
	}

	return count;
}

/* Local (piece-relative) byte offset of newline number @n (0-indexed)
 * within @node's own piece. @n must be < node->line_breaks. Costs at
 * most one checkpoint array lookup plus a bounded memchr scan of up to
 * PLUMA_LINE_CHECKPOINT_INTERVAL newlines -- never the whole piece. */
static gsize
piece_nth_newline_offset (PlumaTextBuffer *buffer,
                           PlumaPieceNode  *node,
                           gsize            n)
{
	gsize slot = n / PLUMA_LINE_CHECKPOINT_INTERVAL;
	gsize remaining = n % PLUMA_LINE_CHECKPOINT_INTERVAL;
	gsize pos;
	const gchar *data;
	const gchar *p;
	const gchar *end;
	gsize i;

	g_assert (slot < node->n_checkpoints);

	pos = node->checkpoints[slot];
	if (remaining == 0)
		return pos;

	data = _pluma_piece_node_data (buffer, node);
	p = data + pos + 1;
	end = data + node->length;

	for (i = 0; i < remaining; i++)
	{
		p = memchr (p, '\n', end - p);
		g_assert (p != NULL);

		if (i + 1 == remaining)
			return (gsize) (p - data);

		p++;
	}

	g_return_val_if_reached (0);
}

/* Number of newlines within @node's own piece that occur strictly before
 * local (piece-relative) byte offset @local. Binary-searches the
 * checkpoint array, then scans at most one checkpoint interval's worth
 * of bytes -- never the whole piece. */
static gsize
piece_count_newlines_before (PlumaTextBuffer *buffer,
                              PlumaPieceNode  *node,
                              gsize            local)
{
	gsize lo = 0;
	gsize hi = node->n_checkpoints;
	const gchar *data;

	while (lo < hi)
	{
		gsize mid = lo + (hi - lo) / 2;

		if (node->checkpoints[mid] < local)
			lo = mid + 1;
		else
			hi = mid;
	}

	data = _pluma_piece_node_data (buffer, node);

	if (lo == 0)
		return count_newlines_in_range (data, local);

	{
		gsize slot = lo - 1;
		gsize start = node->checkpoints[slot] + 1;
		gsize count = slot * PLUMA_LINE_CHECKPOINT_INTERVAL + 1;

		return count + count_newlines_in_range (data + start, local - start);
	}
}

/* ---- treap (implicit rope): split / merge --------------------------- */

/*
 * Splits the treap rooted at @node into two treaps holding, respectively,
 * the bytes [0, offset) and [offset, subtree_length) of its in-order byte
 * sequence. If @offset lands strictly inside a single piece, that piece is
 * cut into two sibling pieces and each half's newline count is recomputed
 * by scanning only that piece's own bytes (never the rest of the buffer).
 */
static void
treap_split (PlumaTextBuffer  *buffer,
             PlumaPieceNode   *node,
             gsize             offset,
             PlumaPieceNode  **out_left,
             PlumaPieceNode  **out_right)
{
	gsize left_len;
	gsize local;

	if (node == NULL)
	{
		*out_left = NULL;
		*out_right = NULL;
		return;
	}

	left_len = node->left ? node->left->subtree_length : 0;

	if (offset < left_len)
	{
		PlumaPieceNode *ll, *lr;

		treap_split (buffer, node->left, offset, &ll, &lr);
		node->left = lr;
		node_update (node);

		*out_left = ll;
		*out_right = node;
		return;
	}

	local = offset - left_len;

	if (local > node->length)
	{
		PlumaPieceNode *rl, *rr;

		treap_split (buffer, node->right, local - node->length, &rl, &rr);
		node->right = rl;
		node_update (node);

		*out_left = node;
		*out_right = rr;
		return;
	}

	if (local == 0)
	{
		*out_left = node->left;
		node->left = NULL;
		node_update (node);

		*out_right = node;
		return;
	}

	if (local == node->length)
	{
		*out_right = node->right;
		node->right = NULL;
		node_update (node);

		*out_left = node;
		return;
	}

	{
		const gchar *data = _pluma_piece_node_data (buffer, node);
		gsize left_line_breaks, right_line_breaks;
		gsize *left_checkpoints, *right_checkpoints;
		gsize n_left_checkpoints, n_right_checkpoints;
		PlumaPieceNode *right_piece;

		build_checkpoints (data, local, &left_line_breaks, &left_checkpoints, &n_left_checkpoints);
		build_checkpoints (data + local, node->length - local, &right_line_breaks, &right_checkpoints, &n_right_checkpoints);

		right_piece = node_new (node->buffer_type,
		                         node->buf_offset + local,
		                         node->length - local,
		                         right_line_breaks,
		                         right_checkpoints,
		                         n_right_checkpoints);

		g_free (node->checkpoints);
		node->checkpoints = left_checkpoints;
		node->n_checkpoints = n_left_checkpoints;
		node->line_breaks = left_line_breaks;
		node->length = local;

		right_piece->right = node->right;
		node->right = NULL;

		node_update (right_piece);
		node_update (node);

		*out_left = node;
		*out_right = right_piece;
	}
}

static PlumaPieceNode *
treap_merge (PlumaPieceNode *a,
             PlumaPieceNode *b)
{
	if (a == NULL)
		return b;

	if (b == NULL)
		return a;

	if (a->priority > b->priority)
	{
		a->right = treap_merge (a->right, b);
		node_update (a);
		return a;
	}
	else
	{
		b->left = treap_merge (a, b->left);
		node_update (b);
		return b;
	}
}

PlumaPieceNode *
_pluma_text_buffer_locate (PlumaTextBuffer *buffer,
                            gsize            offset,
                            gsize           *piece_start)
{
	PlumaPieceNode *node = buffer->root;
	gsize base = 0;

	while (node != NULL)
	{
		gsize left_len = node->left ? node->left->subtree_length : 0;
		gsize start;

		if (offset < base + left_len)
		{
			node = node->left;
			continue;
		}

		start = base + left_len;

		if (offset < start + node->length)
		{
			if (piece_start != NULL)
				*piece_start = start;
			return node;
		}

		base = start + node->length;
		node = node->right;
	}

	if (piece_start != NULL)
		*piece_start = base;
	return NULL;
}

/* ---- line/offset range queries --------------------------------------- */

static gsize
nth_newline_offset (PlumaTextBuffer *buffer,
                     PlumaPieceNode  *node,
                     gsize            n)
{
	gsize left_lines = node->left ? node->left->subtree_line_breaks : 0;
	gsize left_len = node->left ? node->left->subtree_length : 0;

	if (n < left_lines)
		return nth_newline_offset (buffer, node->left, n);

	n -= left_lines;

	if (n < node->line_breaks)
		return left_len + piece_nth_newline_offset (buffer, node, n);

	n -= node->line_breaks;
	return left_len + node->length + nth_newline_offset (buffer, node->right, n);
}

static gsize
count_newlines_before (PlumaTextBuffer *buffer,
                        PlumaPieceNode  *node,
                        gsize            offset)
{
	gsize left_len;
	gsize left_lines;
	gsize local;

	if (node == NULL || offset == 0)
		return 0;

	left_len = node->left ? node->left->subtree_length : 0;
	left_lines = node->left ? node->left->subtree_line_breaks : 0;

	if (offset <= left_len)
		return count_newlines_before (buffer, node->left, offset);

	local = offset - left_len;

	if (local <= node->length)
		return left_lines + piece_count_newlines_before (buffer, node, local);

	local -= node->length;
	return left_lines + node->line_breaks + count_newlines_before (buffer, node->right, local);
}

static void
collect_range (PlumaTextBuffer *buffer,
               PlumaPieceNode  *node,
               gsize            base,
               gsize            range_start,
               gsize            range_end,
               GString         *out)
{
	gsize left_len;
	gsize piece_start;
	gsize piece_end;

	if (node == NULL || base >= range_end)
		return;

	left_len = node->left ? node->left->subtree_length : 0;
	piece_start = base + left_len;
	piece_end = piece_start + node->length;

	if (node->left != NULL && piece_start > range_start)
		collect_range (buffer, node->left, base, range_start, range_end, out);

	if (piece_end > range_start && piece_start < range_end)
	{
		gsize copy_start = MAX (piece_start, range_start);
		gsize copy_end = MIN (piece_end, range_end);
		const gchar *data = _pluma_piece_node_data (buffer, node);

		g_string_append_len (out, data + (copy_start - piece_start), copy_end - copy_start);
	}

	if (node->right != NULL && piece_end < range_end)
		collect_range (buffer, node->right, piece_end, range_start, range_end, out);
}

/* ---- public API ------------------------------------------------------ */

PlumaTextBuffer *
pluma_text_buffer_new (void)
{
	PlumaTextBuffer *buffer = g_new0 (PlumaTextBuffer, 1);

	buffer->add_data = g_byte_array_new ();
	buffer->original_fd = -1;
	buffer->ref_count = 1;

	return buffer;
}

PlumaTextBuffer *
pluma_text_buffer_new_from_file (const gchar   *path,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
	PlumaTextBuffer *buffer;
	gint fd;
	struct stat st;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	fd = g_open (path, O_RDONLY, 0);
	if (fd < 0)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
		             "%s", g_strerror (errno));
		return NULL;
	}

	if (fstat (fd, &st) < 0)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
		             "%s", g_strerror (errno));
		close (fd);
		return NULL;
	}

	buffer = pluma_text_buffer_new ();

	if (st.st_size > 0)
	{
		gpointer addr = mmap (NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

		if (addr == MAP_FAILED)
		{
			g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
			             "%s", g_strerror (errno));
			pluma_text_buffer_free (buffer);
			close (fd);
			return NULL;
		}

#ifdef MADV_SEQUENTIAL
		madvise (addr, (size_t) st.st_size, MADV_SEQUENTIAL);
#endif

		buffer->original_data = (gchar *) addr;
		buffer->original_size = (gsize) st.st_size;
		buffer->original_is_mmap = TRUE;
		buffer->original_fd = fd;

		/*
		 * The mapped file is *not* handed to the tree as a single giant
		 * piece: an edit landing inside a piece has to split it, and
		 * splitting means rebuilding both halves' checkpoint indexes by
		 * scanning their bytes. One piece covering the whole file would
		 * make the very first keystroke into a multi-hundred-MB scan.
		 * Chopping the original buffer into fixed-size chunks up front
		 * bounds every future split (hence every future edit) to at most
		 * one chunk's worth of work, independent of file size. The one-
		 * time cost of chunking + indexing here is the same O(file size)
		 * scan we'd need anyway just to know the line count.
		 */
		{
			gsize pos = 0;
			PlumaPieceNode *root = NULL;

			while (pos < buffer->original_size)
			{
				gsize chunk_len;
				gsize line_breaks;
				gsize *checkpoints;
				gsize n_checkpoints;
				PlumaPieceNode *chunk;

				/* Checked once per 64KB chunk: frequent enough that
				 * cancelling a multi-GB scan (e.g. the tab it belongs
				 * to was closed) takes effect promptly, without adding
				 * meaningful overhead to the scan itself. */
				if (g_cancellable_set_error_if_cancelled (cancellable, error))
				{
					node_free_recursive (root);
					pluma_text_buffer_free (buffer);
					return NULL;
				}

				chunk_len = MIN (buffer->original_size - pos, (gsize) PLUMA_ORIGINAL_CHUNK_SIZE);

				build_checkpoints (buffer->original_data + pos, chunk_len,
				                    &line_breaks, &checkpoints, &n_checkpoints);

				chunk = node_new (PLUMA_PIECE_ORIGINAL, pos, chunk_len,
				                   line_breaks, checkpoints, n_checkpoints);

				root = treap_merge (root, chunk);
				pos += chunk_len;
			}

			buffer->root = root;
		}
	}
	else
	{
		buffer->original_fd = fd;
	}

	return buffer;
}

static void
text_buffer_really_free (PlumaTextBuffer *buffer)
{
	node_free_recursive (buffer->root);

	if (buffer->original_is_mmap && buffer->original_data != NULL)
		munmap (buffer->original_data, buffer->original_size);

	if (buffer->original_fd >= 0)
		close (buffer->original_fd);

	if (buffer->add_data != NULL)
		g_byte_array_free (buffer->add_data, TRUE);

	g_free (buffer);
}

PlumaTextBuffer *
pluma_text_buffer_ref (PlumaTextBuffer *buffer)
{
	g_return_val_if_fail (buffer != NULL, NULL);

	g_atomic_int_inc (&buffer->ref_count);

	return buffer;
}

void
pluma_text_buffer_unref (PlumaTextBuffer *buffer)
{
	if (buffer == NULL)
		return;

	if (g_atomic_int_dec_and_test (&buffer->ref_count))
		text_buffer_really_free (buffer);
}

void
pluma_text_buffer_free (PlumaTextBuffer *buffer)
{
	pluma_text_buffer_unref (buffer);
}

gsize
pluma_text_buffer_get_length (PlumaTextBuffer *buffer)
{
	g_return_val_if_fail (buffer != NULL, 0);

	return buffer->root ? buffer->root->subtree_length : 0;
}

gsize
pluma_text_buffer_get_line_count (PlumaTextBuffer *buffer)
{
	g_return_val_if_fail (buffer != NULL, 1);

	return (buffer->root ? buffer->root->subtree_line_breaks : 0) + 1;
}

void
pluma_text_buffer_insert (PlumaTextBuffer *buffer,
                           gsize            offset,
                           const gchar     *text,
                           gssize           length)
{
	gsize len;
	gsize add_offset;
	gsize line_breaks;
	gsize *checkpoints;
	gsize n_checkpoints;
	PlumaPieceNode *left, *right, *middle;

	g_return_if_fail (buffer != NULL);
	g_return_if_fail (text != NULL);

	len = (length < 0) ? strlen (text) : (gsize) length;
	if (len == 0)
		return;

	g_return_if_fail (offset <= pluma_text_buffer_get_length (buffer));

	add_offset = buffer->add_data->len;
	g_byte_array_append (buffer->add_data, (const guint8 *) text, len);

	build_checkpoints (text, len, &line_breaks, &checkpoints, &n_checkpoints);
	middle = node_new (PLUMA_PIECE_ADD, add_offset, len, line_breaks, checkpoints, n_checkpoints);

	treap_split (buffer, buffer->root, offset, &left, &right);
	buffer->root = treap_merge (treap_merge (left, middle), right);
}

void
pluma_text_buffer_delete (PlumaTextBuffer *buffer,
                           gsize            offset,
                           gsize            length)
{
	PlumaPieceNode *left, *middle, *right;

	g_return_if_fail (buffer != NULL);

	if (length == 0)
		return;

	g_return_if_fail (offset + length <= pluma_text_buffer_get_length (buffer));

	treap_split (buffer, buffer->root, offset, &left, &right);
	treap_split (buffer, right, length, &middle, &right);

	node_free_recursive (middle);

	buffer->root = treap_merge (left, right);
}

gsize
pluma_text_buffer_offset_for_line (PlumaTextBuffer *buffer,
                                    gsize            line)
{
	g_return_val_if_fail (buffer != NULL, 0);
	g_return_val_if_fail (line < pluma_text_buffer_get_line_count (buffer), 0);

	if (line == 0)
		return 0;

	return nth_newline_offset (buffer, buffer->root, line - 1) + 1;
}

gsize
pluma_text_buffer_line_for_offset (PlumaTextBuffer *buffer,
                                    gsize            offset)
{
	g_return_val_if_fail (buffer != NULL, 0);
	g_return_val_if_fail (offset <= pluma_text_buffer_get_length (buffer), 0);

	return count_newlines_before (buffer, buffer->root, offset);
}

gchar *
pluma_text_buffer_get_text (PlumaTextBuffer *buffer,
                             gsize            offset,
                             gsize            length)
{
	GString *out;

	g_return_val_if_fail (buffer != NULL, NULL);
	g_return_val_if_fail (offset + length <= pluma_text_buffer_get_length (buffer), NULL);

	out = g_string_sized_new (length + 1);

	if (length > 0)
		collect_range (buffer, buffer->root, 0, offset, offset + length, out);

	return g_string_free (out, FALSE);
}

static gboolean
write_all (gint          fd,
           const gchar  *data,
           gsize         len,
           GError      **error)
{
	while (len > 0)
	{
		gssize written = write (fd, data, len);

		if (written < 0)
		{
			if (errno == EINTR)
				continue;

			g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
			             "%s", g_strerror (errno));
			return FALSE;
		}

		data += written;
		len -= (gsize) written;
	}

	return TRUE;
}

static gboolean
save_node (PlumaTextBuffer  *buffer,
           PlumaPieceNode   *node,
           gint              fd,
           GError          **error)
{
	if (node == NULL)
		return TRUE;

	if (!save_node (buffer, node->left, fd, error))
		return FALSE;

	if (!write_all (fd, _pluma_piece_node_data (buffer, node), node->length, error))
		return FALSE;

	return save_node (buffer, node->right, fd, error);
}

/*
 * Streams the document to @path node by node, without ever materializing
 * the whole content in memory. This is a plain synchronous helper for the
 * engine's own tests; it is not a replacement for PlumaDocumentSaver
 * (backups, encoding conversion, async off the UI thread) which is a
 * separate, GTK-facing concern.
 *
 * Crucially, this never opens @path itself for writing. If @path is the
 * same file this buffer was loaded from (the common case: plain Save),
 * buffer->original_data is still mmap'd onto it, and any piece not yet
 * overwritten by an edit is read from that mapping while saving. Opening
 * @path with O_TRUNC would truncate the file out from under those mapped
 * pages -- any read of them after that (this save's own remaining
 * pieces, or another thread redrawing the view concurrently) faults
 * with SIGBUS, not a catchable error. So instead this writes to a fresh
 * temp file in the same directory (never touching @path) and only
 * replaces @path with an atomic rename() once the write has fully
 * succeeded -- rename() doesn't invalidate @path's old inode for
 * whoever still has it mapped or open, it just repoints the directory
 * entry.
 */
gboolean
pluma_text_buffer_save (PlumaTextBuffer  *buffer,
                         const gchar      *path,
                         GError          **error)
{
	gchar *dir;
	gchar *tmp_path;
	gint fd;
	gboolean ok;

	g_return_val_if_fail (buffer != NULL, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	dir = g_path_get_dirname (path);
	tmp_path = g_build_filename (dir, ".pluma-save-XXXXXX", NULL);
	g_free (dir);

	fd = g_mkstemp (tmp_path);
	if (fd < 0)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
		             "%s", g_strerror (errno));
		g_free (tmp_path);
		return FALSE;
	}

	ok = save_node (buffer, buffer->root, fd, error);

	/* A successful write() only means the data was handed to the page
	 * cache; without fsync(), a crash or power loss right after the
	 * rename() below could still leave the "saved" file truncated or
	 * empty on some filesystems. This is what makes the replace not
	 * just atomically *visible*, but actually durable. */
	if (ok && fsync (fd) < 0)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
		             "%s", g_strerror (errno));
		ok = FALSE;
	}

	if (close (fd) < 0 && ok)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
		             "%s", g_strerror (errno));
		ok = FALSE;
	}

	if (ok)
	{
		struct stat st;

		/* g_mkstemp() creates the file 0600 regardless of the original
		 * file's permissions; preserve them across the replace when
		 * possible (best-effort: a new/never-saved file has nothing to
		 * match, and permission errors here shouldn't fail the save). */
		if (stat (path, &st) == 0)
			chmod (tmp_path, st.st_mode & 07777);

		if (g_rename (tmp_path, path) != 0)
		{
			g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
			             "%s", g_strerror (errno));
			ok = FALSE;
		}
	}

	if (!ok)
		g_unlink (tmp_path);

	g_free (tmp_path);

	return ok;
}
