#include "pluma-git-diff.h"
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
