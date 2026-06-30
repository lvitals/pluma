#ifndef PLUMA_GIT_COMMIT_H
#define PLUMA_GIT_COMMIT_H

#include <glib.h>

gboolean pluma_git_commit_message_is_valid (const gchar *message);
guint    pluma_git_commit_message_length   (const gchar *message);

#endif
