#ifndef PLUMA_GIT_BLAME_H
#define PLUMA_GIT_BLAME_H

#include <glib.h>

/* One line of `git blame --porcelain` output. 'hash' is all zeros for a
 * line that hasn't been committed yet (see
 * pluma_git_blame_line_is_uncommitted()). */
typedef struct
{
	gchar *hash;
	gchar *author;
	gint64 author_time; /* unix timestamp; 0 if unavailable */
	gchar *summary;
	gchar *content;     /* the line's text, without the porcelain "\t" prefix */
	gint final_line;    /* 1-based line number in the blamed revision */
} PlumaGitBlameLine;

GPtrArray *pluma_git_blame_parse (const gchar *porcelain_output);
void       pluma_git_blame_line_free (PlumaGitBlameLine *line);
gboolean   pluma_git_blame_line_is_uncommitted (const PlumaGitBlameLine *line);

#endif
