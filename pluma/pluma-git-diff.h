#ifndef PLUMA_GIT_DIFF_H
#define PLUMA_GIT_DIFF_H

#include <glib.h>

gboolean pluma_git_numstat_has_binary (const gchar *output);

/* One hunk out of a unified diff, split for per-hunk stage/unstage/discard.
 * 'patch' is the diff header (everything before the first "@@ " line, e.g.
 * the "diff --git"/"index"/"---"/"+++" lines) followed by just this hunk's
 * body - i.e. a complete, standalone patch ready to feed to `git apply`.
 */
typedef struct
{
	gchar *label; /* the "@@ ... @@" hunk header line */
	gchar *patch;
} PlumaGitHunk;

GPtrArray *pluma_git_diff_split_hunks (const gchar *diff);
void       pluma_git_hunk_free        (PlumaGitHunk *hunk);

guint pluma_git_diff_count_change_lines (const PlumaGitHunk *hunk);
gchar *pluma_git_diff_hunk_subset (const PlumaGitHunk *hunk, const gboolean *line_selected,
                                   guint n_line_selected, gboolean reverse);

#endif
