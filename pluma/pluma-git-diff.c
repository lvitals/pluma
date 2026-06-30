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
