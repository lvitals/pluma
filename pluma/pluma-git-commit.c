#include "pluma-git-commit.h"

gboolean
pluma_git_commit_message_is_valid (const gchar *message)
{
	if (message == NULL || !g_utf8_validate (message, -1, NULL))
		return FALSE;
	for (const gchar *p = message; *p != '\0'; p = g_utf8_next_char (p))
	{
		gunichar character = g_utf8_get_char (p);
		if (!g_unichar_isspace (character))
			return TRUE;
	}
	return FALSE;
}

guint
pluma_git_commit_message_length (const gchar *message)
{
	return message != NULL && g_utf8_validate (message, -1, NULL)
	       ? (guint) g_utf8_strlen (message, -1) : 0;
}
