#ifndef PLUMA_TOOL_CAPTURE_H
#define PLUMA_TOOL_CAPTURE_H

#include <glib.h>

typedef struct _PlumaToolCapture PlumaToolCapture;

typedef void (*PlumaToolCaptureLineFunc)  (PlumaToolCapture *capture, const gchar *line, gpointer user_data);
typedef void (*PlumaToolCaptureBeginFunc) (PlumaToolCapture *capture, gpointer user_data);
typedef void (*PlumaToolCaptureEndFunc)   (PlumaToolCapture *capture, gint exit_code, gpointer user_data);

/* command is the path to the tool script to run. needs_shell should be
 * TRUE when the script has no shebang line (it is then run as
 * "/bin/sh -c <command>" instead of executed directly). */
PlumaToolCapture *pluma_tool_capture_new (const gchar *command, gboolean needs_shell, const gchar *cwd);
void pluma_tool_capture_free (PlumaToolCapture *capture);

void pluma_tool_capture_set_env   (PlumaToolCapture *capture, const gchar *name, const gchar *value);
void pluma_tool_capture_set_input (PlumaToolCapture *capture, const gchar *text);

void pluma_tool_capture_set_callbacks (PlumaToolCapture         *capture,
                                        PlumaToolCaptureLineFunc  stdout_line,
                                        PlumaToolCaptureLineFunc  stderr_line,
                                        PlumaToolCaptureBeginFunc begin_execute,
                                        PlumaToolCaptureEndFunc   end_execute,
                                        gpointer                  user_data);

void pluma_tool_capture_execute (PlumaToolCapture *capture);
void pluma_tool_capture_stop    (PlumaToolCapture *capture);

#endif
