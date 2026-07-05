/*
 * pluma-sourcecodebrowser-plugin-ctags.h
 * This file is part of pluma
 *
 * Copyright (c) 2011, Micah Carrick
 * Copyright (C) 2020-2021 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __PLUMA_SOURCECODEBROWSER_PLUGIN_CTAGS_H__
#define __PLUMA_SOURCECODEBROWSER_PLUGIN_CTAGS_H__

#include <glib.h>

G_BEGIN_DECLS

/* A single symbol ("tag") reported by ctags for a source file. */
typedef struct _ScbTag ScbTag;

struct _ScbTag
{
	gchar      *name;
	gchar      *kind;    /* ctags "kind" name, e.g. "function", "class" */
	GHashTable *fields;  /* gchar* -> gchar*, e.g. "line" -> "42" */
};

const gchar *scb_tag_get_field (ScbTag      *tag,
                                 const gchar *key);

void         scb_tag_list_free (GList *tags);

gchar       *scb_kind_icon_name  (const gchar *kind);
gchar       *scb_kind_group_name (const gchar *kind);

gchar       *scb_ctags_get_version (const gchar *executable);

GList       *scb_ctags_parse_file (const gchar  *executable,
                                    const gchar  *path,
                                    GError      **error);

G_END_DECLS

#endif /* __PLUMA_SOURCECODEBROWSER_PLUGIN_CTAGS_H__ */
