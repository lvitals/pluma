#include "markdown-inline.h"
#include <string.h>

static InlineElement *
inline_element_new (InlineElementType type, const gchar *text, const gchar *extra)
{
    InlineElement *el = g_new0 (InlineElement, 1);
    el->type = type;
    el->text = g_strdup (text);
    el->extra = g_strdup (extra);
    return el;
}

static void
inline_element_free (InlineElement *el)
{
    if (el == NULL)
        return;
    g_free (el->text);
    g_free (el->extra);
    g_free (el);
}

void
markdown_inline_free_list (GList *elements)
{
    if (elements != NULL) {
        g_list_free_full (elements, (GDestroyNotify) inline_element_free);
    }
}

static const gchar *
lookup_emoji (const gchar *name)
{
    if (g_strcmp0 (name, "smile") == 0) return "😀";
    if (g_strcmp0 (name, "rocket") == 0) return "🚀";
    if (g_strcmp0 (name, "warning") == 0) return "⚠️";
    if (g_strcmp0 (name, "check") == 0 || g_strcmp0 (name, "tick") == 0) return "✓";
    if (g_strcmp0 (name, "cross") == 0) return "✗";
    return NULL;
}

static InlineElementType
get_current_type (gboolean bold, gboolean italic, gboolean strikethrough)
{
    if (strikethrough) return INLINE_STRIKE;
    if (bold && italic) return INLINE_BOLD_ITALIC;
    if (bold) return INLINE_BOLD;
    if (italic) return INLINE_ITALIC;
    return INLINE_TEXT;
}

GList *
markdown_inline_parse (const gchar *text)
{
    GList *list = NULL;
    const gchar *p = text;
    GString *acc = g_string_new ("");
    gboolean bold = FALSE;
    gboolean italic = FALSE;
    gboolean strikethrough = FALSE;
    gboolean code = FALSE;

    while (p != NULL && *p != '\0') {
        if (code) {
            if (*p == '\\' && *(p + 1) == '`') {
                g_string_append_c (acc, '`');
                p += 2;
            } else if (*p == '`') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (INLINE_CODE, acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                code = FALSE;
                p++;
            } else {
                g_string_append_c (acc, *p);
                p++;
            }
        } else {
            if (*p == '\\' && *(p + 1) != '\0') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                gchar esc[2] = { *(p + 1), '\0' };
                list = g_list_append (list, inline_element_new (INLINE_ESCAPED_CHAR, esc, NULL));
                p += 2;
            }
            else if (g_str_has_prefix (p, "<b>") || g_str_has_prefix (p, "<strong>")) {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                bold = TRUE;
                p += (g_str_has_prefix (p, "<b>") ? 3 : 8);
            } else if (g_str_has_prefix (p, "</b>") || g_str_has_prefix (p, "</strong>")) {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                bold = FALSE;
                p += (g_str_has_prefix (p, "</b>") ? 4 : 9);
            } else if (g_str_has_prefix (p, "<i>") || g_str_has_prefix (p, "<em>")) {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                italic = TRUE;
                p += (g_str_has_prefix (p, "<i>") ? 3 : 4);
            } else if (g_str_has_prefix (p, "</i>") || g_str_has_prefix (p, "</em>")) {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                italic = FALSE;
                p += (g_str_has_prefix (p, "</i>") ? 4 : 5);
            } else if (*p == '<' && strchr (p, '>') != NULL && !g_str_has_prefix (p, "<http")) {
                const gchar *end = strchr (p, '>');
                p = end + 1;
            }
            else if (*p == '[') {
                const gchar *close_bracket = strchr (p, ']');
                if (close_bracket != NULL && *(close_bracket + 1) == '(') {
                    const gchar *close_paren = strchr (close_bracket, ')');
                    if (close_paren != NULL) {
                        if (acc->len > 0) {
                            list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                            g_string_truncate (acc, 0);
                        }
                        gchar *link_text = g_strndup (p + 1, close_bracket - (p + 1));
                        gchar *url = g_strndup (close_bracket + 2, close_paren - (close_bracket + 2));

                        list = g_list_append (list, inline_element_new (INLINE_LINK, link_text, url));
                        g_free (link_text);
                        g_free (url);

                        p = close_paren + 1;
                        continue;
                    }
                }
                g_string_append_c (acc, '[');
                p++;
            }
            else if (*p == '<' && g_str_has_prefix (p + 1, "http")) {
                const gchar *close_bracket = strchr (p, '>');
                if (close_bracket != NULL) {
                    if (acc->len > 0) {
                        list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                        g_string_truncate (acc, 0);
                    }
                    gchar *url = g_strndup (p + 1, close_bracket - (p + 1));
                    list = g_list_append (list, inline_element_new (INLINE_AUTO_LINK, url, NULL));
                    g_free (url);
                    p = close_bracket + 1;
                    continue;
                }
                g_string_append_c (acc, '<');
                p++;
            }
            else if (g_str_has_prefix (p, "http://") || g_str_has_prefix (p, "https://")) {
                const gchar *end = p;
                while (*end != '\0' && *end != ' ' && *end != '\n' && *end != '\r' && *end != ')' && *end != ']' && *end != '<' && *end != '>') {
                    end++;
                }
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                gchar *url = g_strndup (p, end - p);
                list = g_list_append (list, inline_element_new (INLINE_AUTO_LINK, url, NULL));
                g_free (url);
                p = end;
            }
            else if (*p == '*' && *(p + 1) == '*' && *(p + 2) == '*') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                bold = !bold;
                italic = !italic;
                p += 3;
            } else if (*p == '*' && *(p + 1) == '*') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                bold = !bold;
                p += 2;
            } else if (*p == '_' && *(p + 1) == '_') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                bold = !bold;
                p += 2;
            } else if (*p == '*') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                italic = !italic;
                p += 1;
            } else if (*p == '_') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                italic = !italic;
                p += 1;
            }
            else if (*p == '~' && *(p + 1) == '~') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                strikethrough = !strikethrough;
                p += 2;
            }
            else if (*p == '`') {
                if (acc->len > 0) {
                    list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                    g_string_truncate (acc, 0);
                }
                code = TRUE;
                p += 1;
            }
            else if (*p == ':') {
                const gchar *next_colon = strchr (p + 1, ':');
                if (next_colon != NULL && next_colon - p < 20) {
                    gchar *emoji_name = g_strndup (p + 1, next_colon - (p + 1));
                    const gchar *emoji_char = lookup_emoji (emoji_name);
                    g_free (emoji_name);
                    if (emoji_char != NULL) {
                        if (acc->len > 0) {
                            list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
                            g_string_truncate (acc, 0);
                        }
                        list = g_list_append (list, inline_element_new (INLINE_EMOJI, emoji_char, NULL));
                        p = next_colon + 1;
                        continue;
                    }
                }
                g_string_append_c (acc, ':');
                p++;
            } else {
                g_string_append_c (acc, *p);
                p++;
            }
        }
    }

    if (acc->len > 0) {
        list = g_list_append (list, inline_element_new (get_current_type (bold, italic, strikethrough), acc->str, NULL));
    }

    g_string_free (acc, TRUE);
    return list;
}

static void
escape_and_append (GString *str, const gchar *text)
{
    if (text == NULL)
        return;
    const gchar *p = text;
    while (*p != '\0') {
        if (*p == '&') g_string_append (str, "&amp;");
        else if (*p == '<') g_string_append (str, "&lt;");
        else if (*p == '>') g_string_append (str, "&gt;");
        else if (*p == '"') g_string_append (str, "&quot;");
        else if (*p == '\'') g_string_append (str, "&apos;");
        else g_string_append_c (str, *p);
        p++;
    }
}

gchar *
markdown_inline_to_pango_markup (GList *elements, gboolean is_dark)
{
    GString *str = g_string_new ("");
    GList *curr = elements;

    while (curr != NULL) {
        InlineElement *el = (InlineElement *) curr->data;
        switch (el->type) {
            case INLINE_TEXT:
                escape_and_append (str, el->text);
                break;
            case INLINE_BOLD:
                g_string_append (str, "<b>");
                escape_and_append (str, el->text);
                g_string_append (str, "</b>");
                break;
            case INLINE_ITALIC:
                g_string_append (str, "<i>");
                escape_and_append (str, el->text);
                g_string_append (str, "</i>");
                break;
            case INLINE_BOLD_ITALIC:
                g_string_append (str, "<b><i>");
                escape_and_append (str, el->text);
                g_string_append (str, "</i></b>");
                break;
            case INLINE_STRIKE:
                g_string_append (str, "<s>");
                escape_and_append (str, el->text);
                g_string_append (str, "</s>");
                break;
            case INLINE_CODE: {
                const gchar *bg = is_dark ? "#2d2f31" : "#f1f3f4";
                const gchar *fg = is_dark ? "#f28b82" : "#c7254e";
                g_string_append_printf (str, "<span font_family=\"monospace\" background=\"%s\" foreground=\"%s\">", bg, fg);
                escape_and_append (str, el->text);
                g_string_append (str, "</span>");
                break;
            }
            case INLINE_LINK: {
                gchar *escaped_url = g_markup_escape_text (el->extra, -1);
                g_string_append_printf (str, "<a href=\"%s\">", escaped_url);
                escape_and_append (str, el->text);
                g_string_append (str, "</a>");
                g_free (escaped_url);
                break;
            }
            case INLINE_AUTO_LINK: {
                gchar *escaped_url = g_markup_escape_text (el->text, -1);
                g_string_append_printf (str, "<a href=\"%s\">", escaped_url);
                escape_and_append (str, el->text);
                g_string_append (str, "</a>");
                g_free (escaped_url);
                break;
            }
            case INLINE_EMOJI:
                escape_and_append (str, el->text);
                break;
            case INLINE_ESCAPED_CHAR:
                escape_and_append (str, el->text);
                break;
        }
        curr = curr->next;
    }

    return g_string_free (str, FALSE);
}
