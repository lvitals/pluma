/*
 * pluma-sourcecodebrowser-plugin.h
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

#ifndef __PLUMA_SOURCECODEBROWSER_PLUGIN_H__
#define __PLUMA_SOURCECODEBROWSER_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include <libpeas/peas-extension-base.h>
#include <libpeas/peas-object-module.h>

G_BEGIN_DECLS

#define PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN		(pluma_sourcecodebrowser_plugin_get_type ())
#define PLUMA_SOURCECODEBROWSER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN, PlumaSourcecodebrowserPlugin))
#define PLUMA_SOURCECODEBROWSER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN, PlumaSourcecodebrowserPluginClass))
#define PLUMA_IS_SOURCECODEBROWSER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN))
#define PLUMA_IS_SOURCECODEBROWSER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN))
#define PLUMA_SOURCECODEBROWSER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PLUMA_TYPE_SOURCECODEBROWSER_PLUGIN, PlumaSourcecodebrowserPluginClass))

typedef struct _PlumaSourcecodebrowserPluginPrivate PlumaSourcecodebrowserPluginPrivate;

typedef struct _PlumaSourcecodebrowserPlugin PlumaSourcecodebrowserPlugin;

struct _PlumaSourcecodebrowserPlugin
{
	PeasExtensionBase parent_instance;

	/*< private >*/
	PlumaSourcecodebrowserPluginPrivate *priv;
};

typedef struct _PlumaSourcecodebrowserPluginClass PlumaSourcecodebrowserPluginClass;

struct _PlumaSourcecodebrowserPluginClass
{
	PeasExtensionBaseClass parent_class;
};

GType	pluma_sourcecodebrowser_plugin_get_type		(void) G_GNUC_CONST;

/* All the plugins must implement this function */
G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

G_END_DECLS

#endif /* __PLUMA_SOURCECODEBROWSER_PLUGIN_H__ */
