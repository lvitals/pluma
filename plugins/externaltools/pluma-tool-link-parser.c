#include "pluma-tool-link-parser.h"

/* Named groups: "lnk" is the span to mark as a link, "pth" the file path,
 * "ln" the line number. Same intent as the Python re.VERBOSE patterns this
 * replaces, just collapsed to single-line form (PCRE supports the Python
 * (?P<name>...) named-group syntax directly). */
static const gchar *pattern_strings[] = {
    /* gcc/javac/ruby/scalac/6g(go): "test.c:13: warning: ..." */
    "^(?P<lnk>(?P<pth>.*[a-z0-9]):(?P<ln>\\d+)):\\s",
    /* python: '  File "test.py", line 13' */
    "^\\s\\sFile\\s(?P<lnk>\"(?P<pth>[^\"]+)\",\\sline\\s(?P<ln>\\d+)),",
    /* bash: 'test.sh: line 5:' */
    "^(?P<lnk>(?P<pth>.*):\\sline\\s(?P<ln>\\d+)):",
    /* valac: 'Test.vala:13.1-13.3: ...' */
    "^(?P<lnk>(?P<pth>.*vala):(?P<ln>\\d+)\\.\\d+-\\d+\\.\\d+):",
    /* ruby: '   from test.rb:3:in `each'' */
    "^\\s+from\\s(?P<lnk>(?P<pth>.*):(?P<ln>\\d+))",
    /* perl: 'syntax error at test.pl line 88, near ...' */
    "\\sat\\s(?P<lnk>(?P<pth>.*)\\sline\\s(?P<ln>\\d+))",
    /* mcs (C#): 'Test.cs(12,7): error CS0103: ...' */
    "^(?P<lnk>(?P<pth>.*\\.[cC][sS])\\((?P<ln>\\d+),\\d+\\)):\\s",
};

static GRegex *patterns[G_N_ELEMENTS (pattern_strings)];
static gboolean patterns_ready = FALSE;

static void
ensure_patterns (void)
{
    guint i;

    if (patterns_ready)
        return;

    for (i = 0; i < G_N_ELEMENTS (pattern_strings); i++) {
        GError *error = NULL;
        patterns[i] = g_regex_new (pattern_strings[i], G_REGEX_MULTILINE, 0, &error);
        if (patterns[i] == NULL) {
            g_warning ("External Tools: failed to compile link regex %u: %s", i, error->message);
            g_error_free (error);
        }
    }
    patterns_ready = TRUE;
}

void
pluma_tool_link_free (PlumaToolLink *link)
{
    if (link == NULL)
        return;
    g_free (link->path);
    g_free (link);
}

GList *
pluma_tool_link_parse (const gchar *text)
{
    GList *links = NULL;
    guint i;

    ensure_patterns ();

    for (i = 0; i < G_N_ELEMENTS (pattern_strings); i++) {
        GMatchInfo *match_info = NULL;

        if (patterns[i] == NULL)
            continue;

        g_regex_match (patterns[i], text, 0, &match_info);
        while (g_match_info_matches (match_info)) {
            gint lnk_start, lnk_end;
            gchar *path = g_match_info_fetch_named (match_info, "pth");
            gchar *line_str = g_match_info_fetch_named (match_info, "ln");

            if (path != NULL && line_str != NULL &&
                g_match_info_fetch_named_pos (match_info, "lnk", &lnk_start, &lnk_end)) {
                PlumaToolLink *link = g_new0 (PlumaToolLink, 1);
                link->path = path;
                path = NULL;
                link->line_nr = (gint) g_ascii_strtoll (line_str, NULL, 10);
                link->start = lnk_start;
                link->end = lnk_end;
                links = g_list_prepend (links, link);
            }

            g_free (path);
            g_free (line_str);
            g_match_info_next (match_info, NULL);
        }
        g_match_info_free (match_info);
    }

    return g_list_reverse (links);
}
