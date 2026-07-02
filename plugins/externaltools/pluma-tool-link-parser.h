#ifndef PLUMA_TOOL_LINK_PARSER_H
#define PLUMA_TOOL_LINK_PARSER_H

#include <glib.h>

typedef struct {
    gchar *path;
    gint   line_nr;
    gint   start;
    gint   end;
} PlumaToolLink;

void pluma_tool_link_free  (PlumaToolLink *link);

/* Scans text for file:line references (compiler/interpreter style output)
 * and returns a newly-allocated GList of PlumaToolLink*, or NULL if none
 * were found. Free with g_list_free_full (list, (GDestroyNotify) pluma_tool_link_free). */
GList *pluma_tool_link_parse (const gchar *text);

#endif
