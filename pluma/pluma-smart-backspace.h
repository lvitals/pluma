/*
 * pluma-smart-backspace.h
 * This file is part of pluma
 *
 * Copyright (C) 2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __PLUMA_SMART_BACKSPACE_H__
#define __PLUMA_SMART_BACKSPACE_H__

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PlumaView PlumaView;

gboolean pluma_smart_backspace_key_press (PlumaView   *view,
                                          GSettings   *settings,
                                          GdkEventKey *event);

G_END_DECLS

#endif /* __PLUMA_SMART_BACKSPACE_H__ */
