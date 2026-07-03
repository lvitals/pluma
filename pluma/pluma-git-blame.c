#include "pluma-git-blame.h"
#include <stdio.h>
#include <string.h>

typedef struct
{
	gchar *author;
	gchar *author_mail;
	gint64 author_time;
	gchar *summary;
} CommitMeta;

static CommitMeta *
commit_meta_new (void)
{
	return g_new0 (CommitMeta, 1);
}

static void
commit_meta_free (gpointer data)
{
	CommitMeta *meta = data;
	if (meta == NULL)
		return;
	g_free (meta->author);
	g_free (meta->author_mail);
	g_free (meta->summary);
	g_free (meta);
}

void
pluma_git_blame_line_free (PlumaGitBlameLine *line)
{
	if (line == NULL)
		return;
	g_free (line->hash);
	g_free (line->author);
	g_free (line->author_mail);
	g_free (line->summary);
	g_free (line->content);
	g_free (line);
}

gboolean
pluma_git_blame_line_is_uncommitted (const PlumaGitBlameLine *line)
{
	if (line == NULL || line->hash == NULL)
		return FALSE;
	for (const gchar *p = line->hash; *p != '\0'; p++)
	{
		if (*p != '0')
			return FALSE;
	}
	return line->hash[0] != '\0';
}

/* A "@@ "-style header for blame: "<40-hex-hash> <orig-line> <final-line>
 * [<group-size>]". Only present (with full metadata following) the first
 * time a commit appears in the output; later lines belonging to an
 * already-seen commit repeat just this short form, with no metadata and
 * no group-size, directly followed by the "\t<content>" line. */
static gboolean
parse_header_line (const gchar *line, gchar hash_out[41], gint *final_line)
{
	gchar hash[64] = { 0 };
	gint orig_line = 0;
	gint final = 0;

	if (line == NULL)
		return FALSE;
	if (sscanf (line, "%63[0-9a-f] %d %d", hash, &orig_line, &final) < 3)
		return FALSE;
	if (strlen (hash) != 40 || line[40] != ' ')
		return FALSE;
	memcpy (hash_out, hash, 41);
	*final_line = final;
	return TRUE;
}

GPtrArray *
pluma_git_blame_parse (const gchar *porcelain_output)
{
	GPtrArray *result = g_ptr_array_new_with_free_func ((GDestroyNotify) pluma_git_blame_line_free);
	GHashTable *cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, commit_meta_free);
	gchar **lines;
	gchar current_hash[41] = { 0 };
	gint current_final_line = 0;

	if (porcelain_output == NULL)
	{
		g_hash_table_unref (cache);
		return result;
	}

	lines = g_strsplit (porcelain_output, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++)
	{
		const gchar *line = lines[i];
		gchar hash[41];
		gint final_line;

		if (line[0] == '\t')
		{
			CommitMeta *meta = current_hash[0] != '\0' ? g_hash_table_lookup (cache, current_hash) : NULL;
			PlumaGitBlameLine *blame_line = g_new0 (PlumaGitBlameLine, 1);

			blame_line->hash = g_strdup (current_hash);
			blame_line->author = g_strdup (meta != NULL && meta->author != NULL ? meta->author : "");
			blame_line->author_mail = g_strdup (meta != NULL && meta->author_mail != NULL ? meta->author_mail : "");
			blame_line->author_time = meta != NULL ? meta->author_time : 0;
			blame_line->summary = g_strdup (meta != NULL && meta->summary != NULL ? meta->summary : "");
			blame_line->content = g_strdup (line + 1);
			blame_line->final_line = current_final_line;
			g_ptr_array_add (result, blame_line);
			continue;
		}

		if (parse_header_line (line, hash, &final_line))
		{
			memcpy (current_hash, hash, 41);
			current_final_line = final_line;
			if (!g_hash_table_contains (cache, hash))
				g_hash_table_insert (cache, g_strdup (hash), commit_meta_new ());
			continue;
		}

		if (current_hash[0] == '\0')
			continue; /* malformed/unexpected input before any header; skip */

		{
			CommitMeta *meta = g_hash_table_lookup (cache, current_hash);
			if (meta == NULL)
				continue;
			if (g_str_has_prefix (line, "author-time "))
				meta->author_time = g_ascii_strtoll (line + strlen ("author-time "), NULL, 10);
			else if (g_str_has_prefix (line, "author-mail "))
			{
				g_free (meta->author_mail);
				meta->author_mail = g_strdup (line + strlen ("author-mail "));
			}
			else if (g_str_has_prefix (line, "author "))
			{
				g_free (meta->author);
				meta->author = g_strdup (line + strlen ("author "));
			}
			else if (g_str_has_prefix (line, "summary "))
			{
				g_free (meta->summary);
				meta->summary = g_strdup (line + strlen ("summary "));
			}
			/* author-tz, committer*, previous, filename, boundary: not
			 * currently surfaced to callers. */
		}
	}
	g_strfreev (lines);
	g_hash_table_unref (cache);
	return result;
}
