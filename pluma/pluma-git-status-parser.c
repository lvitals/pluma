#include "pluma-git-status-parser.h"
#include <stdio.h>
#include <string.h>

static void
entry_free (gpointer data)
{
	PlumaGitStatusEntry *entry = data;
	g_free (entry->path);
	g_free (entry->original_path);
	g_free (entry);
}

static gchar *
field_after_spaces (const gchar *record, gsize length, guint spaces)
{
	const gchar *cursor = record;
	const gchar *end = record + length;

	while (cursor < end && spaces > 0)
	{
		if (*cursor++ == ' ')
			spaces--;
	}
	return spaces == 0 ? g_utf8_make_valid (cursor, end - cursor) : NULL;
}

PlumaGitStatus *
pluma_git_status_parse (const guint8 *data, gsize length)
{
	PlumaGitStatus *status = g_new0 (PlumaGitStatus, 1);
	gsize offset = 0;

	status->entries = g_ptr_array_new_with_free_func (entry_free);
	while (offset < length)
	{
		const guint8 *nul = memchr (data + offset, '\0', length - offset);
		gsize record_length = nul != NULL ? (gsize) (nul - data - offset) : length - offset;
		const gchar *record = (const gchar *) data + offset;
		PlumaGitStatusEntry *entry = NULL;

		if (record_length == 0)
		{
			offset++;
			continue;
		}
		if (g_str_has_prefix (record, "# branch.head "))
			status->branch = g_strndup (record + 14, record_length - 14);
		else if (g_str_has_prefix (record, "# branch.upstream "))
			status->upstream = g_strndup (record + 18, record_length - 18);
		else if (g_str_has_prefix (record, "# branch.ab "))
			sscanf (record + 12, "+%d -%d", &status->ahead, &status->behind);
		else if (record_length > 2 && (record[0] == '?' || record[0] == '!') && record[1] == ' ')
		{
			entry = g_new0 (PlumaGitStatusEntry, 1);
			entry->kind = record[0];
			entry->index_status = entry->worktree_status = record[0];
			entry->path = g_utf8_make_valid (record + 2, record_length - 2);
		}
		else if (record_length > 4 && strchr ("12u", record[0]) != NULL && record[1] == ' ')
		{
			entry = g_new0 (PlumaGitStatusEntry, 1);
			entry->kind = record[0];
			entry->index_status = record[2];
			entry->worktree_status = record[3];
			entry->path = field_after_spaces (record, record_length,
			                                  record[0] == '1' ? 8 : record[0] == '2' ? 9 : 10);
			if (record[0] == '2' && nul != NULL && offset + record_length + 1 < length)
			{
				gsize original_offset = offset + record_length + 1;
				const guint8 *original_nul = memchr (data + original_offset, '\0', length - original_offset);
				gsize original_length = original_nul != NULL
				                      ? (gsize) (original_nul - data - original_offset)
				                      : length - original_offset;
				entry->original_path = g_utf8_make_valid ((const gchar *) data + original_offset,
				                                          original_length);
				if (entry->path != NULL)
					g_ptr_array_add (status->entries, entry);
				else
					entry_free (entry);
				offset = original_offset + original_length + (original_nul != NULL ? 1 : 0);
				continue;
			}
		}
		if (entry != NULL && entry->path != NULL)
			g_ptr_array_add (status->entries, entry);
		else if (entry != NULL)
			entry_free (entry);
		offset += record_length + (nul != NULL ? 1 : 0);
	}
	return status;
}

void
pluma_git_status_free (PlumaGitStatus *status)
{
	if (status == NULL)
		return;
	g_free (status->branch);
	g_free (status->upstream);
	g_ptr_array_unref (status->entries);
	g_free (status);
}
