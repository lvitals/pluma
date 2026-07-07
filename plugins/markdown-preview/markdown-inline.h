#ifndef MARKDOWN_INLINE_H
#define MARKDOWN_INLINE_H

#include <glib.h>

typedef enum {
    INLINE_TEXT,
    INLINE_BOLD,
    INLINE_ITALIC,
    INLINE_BOLD_ITALIC,
    INLINE_STRIKE,
    INLINE_CODE,
    INLINE_LINK,
    INLINE_AUTO_LINK,
    INLINE_EMOJI,
    INLINE_ESCAPED_CHAR
} InlineElementType;

typedef struct _InlineElement InlineElement;

struct _InlineElement {
    InlineElementType type;
    gchar *text;
    gchar *extra;
};

GList *markdown_inline_parse (const gchar *text);
void markdown_inline_free_list (GList *elements);
gchar *markdown_inline_to_pango_markup (GList *elements, gboolean is_dark);

#endif /* MARKDOWN_INLINE_H */
