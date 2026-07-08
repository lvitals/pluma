/*
 * pluma-bracket-completion.h
 * This file is part of pluma
 *
 * Copyright (C) 2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __PLUMA_BRACKET_COMPLETION_H__
#define __PLUMA_BRACKET_COMPLETION_H__

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PlumaView PlumaView;
typedef struct _PlumaBracketCompletion PlumaBracketCompletion;

PlumaBracketCompletion *pluma_bracket_completion_new       (PlumaView              *view,
                                                            GSettings              *settings);
void                    pluma_bracket_completion_free      (PlumaBracketCompletion *completion);
void                    pluma_bracket_completion_attach    (PlumaBracketCompletion *completion,
                                                            GtkTextBuffer          *buffer);
void                    pluma_bracket_completion_detach    (PlumaBracketCompletion *completion);
gboolean                pluma_bracket_completion_key_press (PlumaBracketCompletion *completion,
                                                            GdkEventKey            *event);
void                    pluma_bracket_completion_event_after
                                                           (PlumaBracketCompletion *completion,
                                                            GdkEvent               *event);

G_END_DECLS

#endif /* __PLUMA_BRACKET_COMPLETION_H__ */
