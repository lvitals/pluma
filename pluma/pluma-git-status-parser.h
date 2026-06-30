#ifndef PLUMA_GIT_STATUS_PARSER_H
#define PLUMA_GIT_STATUS_PARSER_H

#include <glib.h>

typedef struct
{
	gchar kind;
	gchar index_status;
	gchar worktree_status;
	gchar *path;
	gchar *original_path;
} PlumaGitStatusEntry;

typedef struct
{
	gchar *branch;
	gchar *upstream;
	gint ahead;
	gint behind;
	GPtrArray *entries;
} PlumaGitStatus;

PlumaGitStatus *pluma_git_status_parse (const guint8 *data, gsize length);
void            pluma_git_status_free  (PlumaGitStatus *status);

#endif
