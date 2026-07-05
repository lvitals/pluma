/*
 * pluma-sourcecodebrowser-plugin-ctags.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-sourcecodebrowser-plugin-ctags.h"

#include <string.h>

static ScbTag *
scb_tag_new (const gchar *name)
{
	ScbTag *tag = g_slice_new0 (ScbTag);

	tag->name = g_strdup (name);
	tag->kind = NULL;
	tag->fields = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	return tag;
}

static void
scb_tag_free (ScbTag *tag)
{
	if (tag == NULL)
		return;

	g_free (tag->name);
	g_free (tag->kind);
	g_hash_table_destroy (tag->fields);
	g_slice_free (ScbTag, tag);
}

void
scb_tag_list_free (GList *tags)
{
	g_list_free_full (tags, (GDestroyNotify) scb_tag_free);
}

const gchar *
scb_tag_get_field (ScbTag *tag, const gchar *key)
{
	g_return_val_if_fail (tag != NULL, NULL);

	return g_hash_table_lookup (tag->fields, key);
}

/* The bundled icon set only covers common OOP/C-ish ctags kinds. Kinds from
 * other ctags parsers (e.g. the Autoconf/M4 parser used for configure.ac)
 * have no dedicated icon, so map them onto the closest existing one instead
 * of always falling back to the generic "missing image" icon. */
static const struct { const gchar *kind; const gchar *icon; } kind_icon_aliases[] = {
	{ "definition", "define" },
	{ "subst",      "variable" },
	{ "package",    "namespace" },
	{ "optenable",  "property" },
	{ "condition",  "property" },
	{ "prototype",  "function" },
	{ "externvar",  "variable" },
	{ NULL, NULL }
};

gchar *
scb_kind_icon_name (const gchar *kind)
{
	gint i;

	g_return_val_if_fail (kind != NULL, g_strdup ("source-missing"));

	for (i = 0; kind_icon_aliases[i].kind != NULL; i++)
	{
		if (strcmp (kind, kind_icon_aliases[i].kind) == 0)
			return g_strconcat ("source-", kind_icon_aliases[i].icon, NULL);
	}

	return g_strconcat ("source-", kind, NULL);
}

/* Pluralizes and capitalizes a ctags "kind" name, e.g. "class" -> "Classes",
 * "property" -> "Properties". Mirrors Kind.group_name() from the original
 * Python plugin; ctags kind names are always plain ASCII. */
gchar *
scb_kind_group_name (const gchar *kind)
{
	gsize len;
	gchar *plural;
	gchar *result;

	g_return_val_if_fail (kind != NULL && *kind != '\0', g_strdup (""));

	len = strlen (kind);

	if (kind[len - 1] == 's')
		plural = g_strconcat (kind, "es", NULL);
	else if (kind[len - 1] == 'y')
		plural = g_strdup_printf ("%.*sies", (gint) (len - 1), kind);
	else
		plural = g_strconcat (kind, "s", NULL);

	result = g_ascii_strdown (plural, -1);
	result[0] = g_ascii_toupper (result[0]);

	g_free (plural);

	return result;
}

gchar *
scb_ctags_get_version (const gchar *executable)
{
	gchar *argv[3];
	gchar *stdout_str = NULL;
	gboolean ok;

	argv[0] = (gchar *) (executable != NULL ? executable : "ctags");
	argv[1] = "--version";
	argv[2] = NULL;

	ok = g_spawn_sync (NULL, argv, NULL,
	                   G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
	                   NULL, NULL,
	                   &stdout_str, NULL, NULL, NULL);

	if (!ok || stdout_str == NULL || *stdout_str == '\0')
	{
		g_free (stdout_str);
		return NULL;
	}

	return stdout_str;
}

/* Parses a single field of the form "key:value" (as emitted by
 * `ctags --fields=...`), splitting on the first colon only so that values
 * which themselves contain colons (e.g. C++ "class:Foo::Bar") are not
 * truncated. */
static void
parse_field (ScbTag *tag, const gchar *field)
{
	const gchar *colon = strchr (field, ':');
	gchar *key;
	gchar *value;

	if (colon == NULL)
		return;

	key = g_strndup (field, colon - field);
	value = g_strdup (colon + 1);

	if (strcmp (key, "kind") == 0)
	{
		g_free (tag->kind);
		tag->kind = g_strdup (value);
	}

	g_hash_table_replace (tag->fields, key, value);
}

static GList *
parse_ctags_output (const gchar *text)
{
	GList *tags = NULL;
	gchar **lines;
	gint i;

	lines = g_strsplit (text, "\n", -1);

	for (i = 0; lines[i] != NULL; i++)
	{
		gchar **columns;
		gint j;
		ScbTag *tag = NULL;

		if (*lines[i] == '\0')
			continue;

		columns = g_strsplit (lines[i], "\t", -1);

		for (j = 0; columns[j] != NULL; j++)
		{
			if (j == 0)
				tag = scb_tag_new (columns[j]);
			else if (j == 1 || j == 2)
				continue; /* file name / ex_command: unused by the browser */
			else
				parse_field (tag, columns[j]);
		}

		g_strfreev (columns);

		if (tag == NULL)
			continue;

		/* A tag without a "kind" cannot be placed in the tree. */
		if (tag->kind == NULL)
			scb_tag_free (tag);
		else
			tags = g_list_append (tags, tag);
	}

	g_strfreev (lines);

	return tags;
}

GList *
scb_ctags_parse_file (const gchar  *executable,
                       const gchar  *path,
                       GError      **error)
{
	gchar *argv[7];
	gchar *stdout_str = NULL;
	gchar *stderr_str = NULL;
	gboolean ok;
	GList *tags;

	g_return_val_if_fail (path != NULL, NULL);

	argv[0] = (gchar *) (executable != NULL ? executable : "ctags");
	argv[1] = "-nu";
	argv[2] = "--fields=fiKlmnsSzt";
	argv[3] = "-f";
	argv[4] = "-";
	argv[5] = (gchar *) path;
	argv[6] = NULL;

	ok = g_spawn_sync (NULL, argv, NULL,
	                   G_SPAWN_SEARCH_PATH,
	                   NULL, NULL,
	                   &stdout_str, &stderr_str, NULL, error);

	if (!ok)
	{
		g_free (stdout_str);
		g_free (stderr_str);
		return NULL;
	}

	tags = parse_ctags_output (stdout_str != NULL ? stdout_str : "");

	g_free (stdout_str);
	g_free (stderr_str);

	return tags;
}
