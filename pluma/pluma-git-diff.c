#include "pluma-git-diff.h"
#include <stdio.h>
#include <string.h>

gboolean
pluma_git_numstat_has_binary (const gchar *output)
{
	const gchar *line = output;

	while (line != NULL && *line != '\0')
	{
		if (g_str_has_prefix (line, "-\t-\t"))
			return TRUE;
		line = strchr (line, '\n');
		if (line != NULL)
			line++;
	}
	return FALSE;
}

static PlumaGitHunk *
pluma_git_hunk_new (const gchar *label, gchar *patch)
{
	PlumaGitHunk *hunk = g_new (PlumaGitHunk, 1);
	hunk->label = g_strdup (label);
	hunk->patch = patch; /* takes ownership */
	return hunk;
}

void
pluma_git_hunk_free (PlumaGitHunk *hunk)
{
	if (hunk == NULL)
		return;
	g_free (hunk->label);
	g_free (hunk->patch);
	g_free (hunk);
}

GPtrArray *
pluma_git_diff_split_hunks (const gchar *diff)
{
	GPtrArray *hunks = g_ptr_array_new_with_free_func ((GDestroyNotify) pluma_git_hunk_free);
	GString *header = g_string_new (NULL);
	GString *current = NULL;
	gchar *current_label = NULL;
	gchar **lines = g_strsplit (diff != NULL ? diff : "", "\n", -1);

	for (guint i = 0; lines[i] != NULL; i++)
	{
		if (g_str_has_prefix (lines[i], "@@ "))
		{
			if (current != NULL)
				g_ptr_array_add (hunks, pluma_git_hunk_new (current_label, g_string_free (current, FALSE)));
			g_free (current_label);
			current_label = g_strdup (lines[i]);
			current = g_string_new (header->str);
		}
		if (current != NULL)
			g_string_append_printf (current, "%s\n", lines[i]);
		else
			g_string_append_printf (header, "%s\n", lines[i]);
	}
	if (current != NULL)
		g_ptr_array_add (hunks, pluma_git_hunk_new (current_label, g_string_free (current, FALSE)));
	g_free (current_label);
	g_string_free (header, TRUE);
	g_strfreev (lines);

	return hunks;
}

guint
pluma_git_diff_count_change_lines (const PlumaGitHunk *hunk)
{
	guint count = 0;
	gchar **lines;
	gboolean in_body = FALSE;

	if (hunk == NULL || hunk->patch == NULL)
		return 0;

	lines = g_strsplit (hunk->patch, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++)
	{
		if (!in_body)
		{
			if (g_str_has_prefix (lines[i], "@@ "))
				in_body = TRUE;
			continue;
		}
		if (lines[i][0] == '+' || lines[i][0] == '-')
			count++;
	}
	g_strfreev (lines);
	return count;
}

/* Parses just the start line numbers out of a "@@ -A[,B] +C[,D] @@[ ...]"
 * hunk header - the ,B/,D counts are recomputed by the caller from the
 * reconstructed body instead of being read here, so both the "count
 * omitted" (means 1) and explicit forms parse the same way. */
static gboolean
parse_hunk_header_starts (const gchar *label, gint *old_start, gint *new_start)
{
	const gchar *plus;

	if (label == NULL || sscanf (label, "@@ -%d", old_start) != 1)
		return FALSE;
	plus = strchr (label, '+');
	if (plus == NULL || sscanf (plus, "+%d", new_start) != 1)
		return FALSE;
	return TRUE;
}

/* Rebuilds 'hunk' keeping only a caller-chosen subset of its '+'/'-'
 * lines, for line-level (rather than whole-hunk) stage/unstage/discard.
 * 'line_selected' has one entry per '+'/'-' line in the hunk body, in the
 * order they appear (context lines don't consume a slot and are always
 * kept) - its length must be pluma_git_diff_count_change_lines(hunk).
 *
 * 'reverse' must match how the caller is about to `git apply` the
 * result (see apply_selected_hunk() in pluma-git-panel.c: FALSE for
 * staging, TRUE for unstaging/discarding, both already `--reverse` or
 * not for the whole-hunk case). This is the standard `git add --patch`
 * line-splitting rule: whichever symbol represents "this specific
 * change" in the given apply direction gets dropped entirely when
 * unselected, while unselected lines of the *other* symbol must remain
 * present as context, since their state isn't actually being touched
 * (dropping them instead of keeping them as context would silently
 * change lines the user didn't select).
 *
 * Returns NULL if the header doesn't parse, or if nothing ends up
 * selected (an all-context result - nothing to apply). */
gchar *
pluma_git_diff_hunk_subset (const PlumaGitHunk *hunk, const gboolean *line_selected,
                           guint n_line_selected, gboolean reverse)
{
	gchar **lines;
	GString *file_header;
	GString *body;
	gint old_start = 0, new_start = 0;
	gint old_count = 0, new_count = 0;
	gboolean found_hunk_line = FALSE;
	gboolean any_selected = FALSE;
	guint slot = 0;
	gchar *result;

	if (hunk == NULL || hunk->patch == NULL)
		return NULL;

	file_header = g_string_new (NULL);
	body = g_string_new (NULL);
	lines = g_strsplit (hunk->patch, "\n", -1);

	for (guint i = 0; lines[i] != NULL; i++)
	{
		const gchar *line = lines[i];

		if (!found_hunk_line)
		{
			if (g_str_has_prefix (line, "@@ "))
			{
				if (!parse_hunk_header_starts (line, &old_start, &new_start))
				{
					g_strfreev (lines);
					g_string_free (file_header, TRUE);
					g_string_free (body, TRUE);
					return NULL;
				}
				found_hunk_line = TRUE;
			}
			else
				g_string_append_printf (file_header, "%s\n", line);
			continue;
		}

		/* g_strsplit() always yields one trailing "" element for the
		 * final newline in hunk->patch; skip just that artifact. */
		if (line[0] == '\0' && lines[i + 1] == NULL)
			continue;

		if (line[0] == '+')
		{
			gboolean selected = slot < n_line_selected && line_selected[slot];
			slot++;
			if (selected)
			{
				g_string_append_printf (body, "%s\n", line);
				new_count++;
				any_selected = TRUE;
			}
			else if (reverse)
			{
				/* Not being reverted: keep as unchanged context. */
				g_string_append_printf (body, " %s\n", line + 1);
				old_count++;
				new_count++;
			}
			/* else (!reverse): not being staged, drop entirely. */
		}
		else if (line[0] == '-')
		{
			gboolean selected = slot < n_line_selected && line_selected[slot];
			slot++;
			if (selected)
			{
				g_string_append_printf (body, "%s\n", line);
				old_count++;
				any_selected = TRUE;
			}
			else if (!reverse)
			{
				/* Not being staged: keep as unchanged context. */
				g_string_append_printf (body, " %s\n", line + 1);
				old_count++;
				new_count++;
			}
			/* else (reverse): not being reverted, drop entirely. */
		}
		else if (line[0] == ' ')
		{
			g_string_append_printf (body, "%s\n", line);
			old_count++;
			new_count++;
		}
		/* Any other line (e.g. "\ No newline at end of file") passes
		 * through unchanged without affecting the header counts. */
		else
			g_string_append_printf (body, "%s\n", line);
	}
	g_strfreev (lines);

	if (!found_hunk_line || !any_selected)
	{
		g_string_free (file_header, TRUE);
		g_string_free (body, TRUE);
		return NULL;
	}

	{
		gchar *new_header_line = g_strdup_printf ("@@ -%d,%d +%d,%d @@\n",
		                                          old_start, old_count, new_start, new_count);
		result = g_strconcat (file_header->str, new_header_line, body->str, NULL);
		g_free (new_header_line);
	}
	g_string_free (file_header, TRUE);
	g_string_free (body, TRUE);
	return result;
}
