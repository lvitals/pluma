#include "markdown-parser.h"
#include <string.h>

enum {
    BLOCK_NONE,
    BLOCK_PARAGRAPH,
    BLOCK_TABLE,
    BLOCK_BLOCKQUOTE
};

typedef struct {
    int level;
    gchar *text;
} QuoteLine;

static void
free_quote_line (QuoteLine *ql)
{
    if (ql == NULL)
        return;
    g_free (ql->text);
    g_free (ql);
}

void
markdown_parser_trim (const gchar *str, const gchar **start, const gchar **end)
{
    if (str == NULL) {
        *start = NULL;
        *end = NULL;
        return;
    }
    const gchar *s = str;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    *start = s;

    const gchar *e = s + strlen (s) - 1;
    while (e >= s && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')) {
        e--;
    }
    *end = e + 1;
}

static gboolean
is_line_horizontal_rule (const gchar *line)
{
    const gchar *p = line;
    while (*p == ' ') p++;

    char c = *p;
    if (c != '-' && c != '*' && c != '_')
        return FALSE;

    int count = 0;
    while (*p != '\0') {
        if (*p == c) {
            count++;
        } else if (*p != ' ' && *p != '\r' && *p != '\n') {
            return FALSE;
        }
        p++;
    }
    return count >= 3;
}

static gboolean
is_str_empty (const gchar *s)
{
    if (s == NULL)
        return TRUE;
    while (*s != '\0') {
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
            return FALSE;
        s++;
    }
    return TRUE;
}

static void
flush_parser_accumulator (ASTNode *root, GString *acc, int current_type, int extra_val)
{
    if (acc->len == 0)
        return;

    gchar *text = g_strstrip (g_strdup (acc->str));
    if (*text != '\0') {
        if (current_type == BLOCK_PARAGRAPH) {
            ASTNode *node = ast_node_new (AST_NODE_PARAGRAPH);
            node->text = g_strdup (text);
            ast_node_append_child (root, node);
        } else if (current_type == BLOCK_TABLE) {
            ASTNode *table_node = ast_node_new (AST_NODE_TABLE);

            gchar **lines = g_strsplit (text, "\n", -1);
            int n_lines = 0;
            while (lines[n_lines] != NULL) n_lines++;
            if (n_lines > 0 && g_strcmp0 (lines[n_lines - 1], "") == 0) n_lines--;

            int max_cols = 0;
            GList *row_nodes = NULL;

            for (int r = 0; r < n_lines; r++) {
                gchar *line = lines[r];
                gchar **raw_cells = g_strsplit (line, "|", -1);
                int raw_count = 0;
                while (raw_cells[raw_count] != NULL) raw_count++;

                int start_idx = 0;
                int end_idx = raw_count;
                if (raw_count > 0 && is_str_empty (raw_cells[0])) {
                    start_idx = 1;
                }
                if (raw_count > start_idx && is_str_empty (raw_cells[raw_count - 1])) {
                    end_idx = raw_count - 1;
                }

                int n_cells = end_idx - start_idx;
                if (n_cells < 0) n_cells = 0;

                ASTNode *row_node = ast_node_new (AST_NODE_TABLE_ROW);
                row_node->n_columns = n_cells;

                for (int c = 0; c < n_cells; c++) {
                    ASTNode *cell_node = ast_node_new (AST_NODE_TABLE_CELL);
                    cell_node->text = g_strdup (g_strstrip (raw_cells[start_idx + c]));
                    ast_node_append_child (row_node, cell_node);
                }

                if (n_cells > max_cols) max_cols = n_cells;
                row_nodes = g_list_append (row_nodes, row_node);
                g_strfreev (raw_cells);
            }

            table_node->n_columns = max_cols;
            table_node->alignments = g_new0 (int, max_cols);

            if (n_lines > 1) {
                GList *row_item = g_list_nth (row_nodes, 1);
                ASTNode *sep_row = (ASTNode *) row_item->data;
                int c_idx = 0;
                GList *cell_item = sep_row->children;
                while (cell_item != NULL && c_idx < max_cols) {
                    ASTNode *cell = (ASTNode *) cell_item->data;
                    gboolean left_colon = g_str_has_prefix (cell->text, ":");
                    gboolean right_colon = g_str_has_suffix (cell->text, ":");
                    if (left_colon && right_colon) table_node->alignments[c_idx] = 1;
                    else if (right_colon) table_node->alignments[c_idx] = 2;
                    else table_node->alignments[c_idx] = 0;

                    c_idx++;
                    cell_item = cell_item->next;
                }
            }

            int r_idx = 0;
            GList *curr_row = row_nodes;
            while (curr_row != NULL) {
                ASTNode *row = (ASTNode *) curr_row->data;
                if (r_idx != 1) {
                    ast_node_append_child (table_node, row);
                } else {
                    ast_node_free (row);
                }
                r_idx++;
                curr_row = curr_row->next;
            }
            g_list_free (row_nodes);
            g_strfreev (lines);

            ast_node_append_child (root, table_node);
        }
    }
    g_free (text);
    g_string_truncate (acc, 0);
}

static ASTNode *
parse_blockquote_recursive (GList **lines_list, int active_level)
{
    ASTNode *bq_node = ast_node_new (AST_NODE_BLOCK_QUOTE);
    bq_node->level = active_level;

    GString *para_acc = g_string_new ("");

    while (*lines_list != NULL) {
        QuoteLine *ql = (QuoteLine *) ((*lines_list)->data);

        if (ql->level < active_level) {
            break;
        }

        if (ql->level > active_level) {
            if (para_acc->len > 0) {
                ASTNode *p_node = ast_node_new (AST_NODE_PARAGRAPH);
                p_node->text = g_strstrip (g_strdup (para_acc->str));
                ast_node_append_child (bq_node, p_node);
                g_string_truncate (para_acc, 0);
            }

            ASTNode *child_bq = parse_blockquote_recursive (lines_list, ql->level);
            ast_node_append_child (bq_node, child_bq);
            continue;
        }

        /* Here ql->level == active_level */
        if (is_str_empty (ql->text)) {
            if (para_acc->len > 0) {
                ASTNode *p_node = ast_node_new (AST_NODE_PARAGRAPH);
                p_node->text = g_strstrip (g_strdup (para_acc->str));
                ast_node_append_child (bq_node, p_node);
                g_string_truncate (para_acc, 0);
            }
        } else {
            if (para_acc->len > 0) {
                g_string_append_c (para_acc, ' ');
            }
            g_string_append (para_acc, ql->text);
        }

        *lines_list = (*lines_list)->next;
    }

    if (para_acc->len > 0) {
        ASTNode *p_node = ast_node_new (AST_NODE_PARAGRAPH);
        p_node->text = g_strstrip (g_strdup (para_acc->str));
        ast_node_append_child (bq_node, p_node);
    }

    g_string_free (para_acc, TRUE);
    return bq_node;
}

ASTNode *
markdown_parser_parse (const gchar *text)
{
    ASTNode *root = ast_node_new (AST_NODE_DOCUMENT);
    if (text == NULL)
        return root;

    GString *accumulator = g_string_new ("");
    int current_block_type = BLOCK_NONE;
    gboolean in_code_block = FALSE;
    GString *code_accumulator = NULL;
    gchar *code_lang = NULL;
    GList *collected_quote_lines = NULL;

    const gchar *p = text;
    while (p != NULL && *p != '\0') {
        const gchar *next = strchr (p, '\n');
        gchar *line;
        if (next != NULL) {
            line = g_strndup (p, next - p);
            p = next + 1;
        } else {
            line = g_strdup (p);
            p = NULL;
        }

        gsize len = strlen (line);
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }

        if (g_str_has_prefix (line, "```")) {
            if (collected_quote_lines != NULL) {
                GList *curr = collected_quote_lines;
                ASTNode *bq_node = parse_blockquote_recursive (&curr, 1);
                ast_node_append_child (root, bq_node);
                g_list_free_full (collected_quote_lines, (GDestroyNotify) free_quote_line);
                collected_quote_lines = NULL;
            }

            flush_parser_accumulator (root, accumulator, current_block_type, 0);
            current_block_type = BLOCK_NONE;

            in_code_block = !in_code_block;
            if (in_code_block) {
                const gchar *lang = line + 3;
                while (*lang == ' ') lang++;
                g_free (code_lang);
                code_lang = g_strdup (lang);
                code_accumulator = g_string_new ("");
            } else {
                if (code_accumulator != NULL) {
                    ASTNode *code_node = ast_node_new (AST_NODE_CODE_BLOCK);
                    code_node->text = g_strdup (code_accumulator->str);
                    code_node->extra = g_strdup (code_lang);
                    ast_node_append_child (root, code_node);
                    g_string_free (code_accumulator, TRUE);
                    code_accumulator = NULL;
                }
                g_free (code_lang);
                code_lang = NULL;
            }
            g_free (line);
            continue;
        }

        if (in_code_block) {
            g_string_append (code_accumulator, line);
            g_string_append_c (code_accumulator, '\n');
            g_free (line);
            continue;
        }

        int leading_spaces = 0;
        const gchar *s = line;
        while (*s == ' ') {
            leading_spaces++;
            s++;
        }

        gboolean is_h_rule = is_line_horizontal_rule (line);

        int heading_level = 0;
        if (s[0] == '#') {
            const gchar *h = s;
            while (*h == '#') {
                heading_level++;
                h++;
            }
            if (*h != ' ') {
                heading_level = 0;
            }
        }

        gboolean is_list_item = FALSE;
        gboolean is_ordered = FALSE;
        const gchar *content = s;
        gchar *ordered_num = NULL;
        if (g_str_has_prefix (s, "- ") || g_str_has_prefix (s, "* ") || g_str_has_prefix (s, "+ ")) {
            is_list_item = TRUE;
            content = s + 2;
        } else {
            const gchar *o = s;
            while (*o >= '0' && *o <= '9') {
                o++;
            }
            if (o > s && g_str_has_prefix (o, ". ")) {
                is_list_item = TRUE;
                is_ordered = TRUE;
                ordered_num = g_strndup (s, o - s);
                content = o + 2;
            }
        }

        int quote_level = 0;
        if (s[0] == '>') {
            const gchar *q = s;
            while (*q == '>') {
                quote_level++;
                q++;
            }
            if (*q == ' ') q++;
            content = q;
        }

        gboolean is_empty = (g_strcmp0 (g_strstrip (g_strdup (line)), "") == 0);
        gboolean is_table_line = (s[0] == '|');

        gboolean is_image_block = FALSE;
        gchar *img_alt = NULL;
        gchar *img_url = NULL;
        if (s[0] == '!' && s[1] == '[') {
            const gchar *close_bracket = strchr (s, ']');
            if (close_bracket != NULL && *(close_bracket + 1) == '(') {
                const gchar *close_paren = strchr (close_bracket, ')');
                if (close_paren != NULL && *(close_paren + 1) == '\0') {
                    is_image_block = TRUE;
                    img_alt = g_strndup (s + 2, close_bracket - (s + 2));
                    img_url = g_strndup (close_bracket + 2, close_paren - (close_bracket + 2));
                }
            }
        }

        int new_block_type = BLOCK_NONE;
        if (is_table_line) {
            new_block_type = BLOCK_TABLE;
        } else if (quote_level > 0) {
            new_block_type = BLOCK_BLOCKQUOTE;
        } else if (!is_empty && heading_level == 0 && !is_list_item && !is_h_rule && !is_image_block) {
            new_block_type = BLOCK_PARAGRAPH;
        }

        if (new_block_type != current_block_type) {
            if (current_block_type == BLOCK_BLOCKQUOTE && collected_quote_lines != NULL) {
                GList *curr = collected_quote_lines;
                ASTNode *bq_node = parse_blockquote_recursive (&curr, 1);
                ast_node_append_child (root, bq_node);
                g_list_free_full (collected_quote_lines, (GDestroyNotify) free_quote_line);
                collected_quote_lines = NULL;
            }
            flush_parser_accumulator (root, accumulator, current_block_type, 0);
            current_block_type = new_block_type;
        }

        if (current_block_type == BLOCK_PARAGRAPH) {
            if (accumulator->len > 0) {
                g_string_append_c (accumulator, ' ');
            }
            g_string_append (accumulator, line);
        } else if (current_block_type == BLOCK_TABLE) {
            g_string_append (accumulator, line);
            g_string_append_c (accumulator, '\n');
        } else if (current_block_type == BLOCK_BLOCKQUOTE) {
            QuoteLine *ql = g_new0 (QuoteLine, 1);
            ql->level = quote_level;
            ql->text = g_strdup (content);
            collected_quote_lines = g_list_append (collected_quote_lines, ql);
        } else {
            flush_parser_accumulator (root, accumulator, BLOCK_NONE, 0);

            if (is_empty) {
                ASTNode *spacer_node = ast_node_new (AST_NODE_PARAGRAPH);
                spacer_node->text = NULL;
                ast_node_append_child (root, spacer_node);
            } else if (is_h_rule) {
                ASTNode *hr_node = ast_node_new (AST_NODE_HORIZONTAL_RULE);
                ast_node_append_child (root, hr_node);
            } else if (is_image_block) {
                ASTNode *img_node = ast_node_new (AST_NODE_IMAGE);
                img_node->text = g_strdup (img_alt);
                img_node->extra = g_strdup (img_url);
                ast_node_append_child (root, img_node);
            } else if (heading_level > 0) {
                const gchar *title_text = s + heading_level;
                while (*title_text == ' ') title_text++;

                ASTNode *h_node = ast_node_new (AST_NODE_HEADING);
                h_node->level = heading_level;
                h_node->text = g_strdup (title_text);
                ast_node_append_child (root, h_node);
            } else if (is_list_item) {
                while (*content == ' ') content++;

                gboolean is_checkbox = FALSE;
                gboolean is_checked = FALSE;
                if (g_str_has_prefix (content, "[ ] ")) {
                    is_checkbox = TRUE;
                    content += 4;
                } else if (g_str_has_prefix (content, "[x] ") || g_str_has_prefix (content, "[X] ")) {
                    is_checkbox = TRUE;
                    is_checked = TRUE;
                    content += 4;
                }

                ASTNode *li_node = ast_node_new (AST_NODE_LIST_ITEM);
                li_node->level = leading_spaces;
                li_node->text = g_strdup (content);
                li_node->extra = g_strdup (ordered_num);
                li_node->bool_val = is_ordered;

                li_node->alignments = g_new0 (int, 2);
                li_node->alignments[0] = is_checkbox;
                li_node->alignments[1] = is_checked;

                ast_node_append_child (root, li_node);
            }
        }

        g_free (ordered_num);
        g_free (img_alt);
        g_free (img_url);
        g_free (line);
    }

    if (current_block_type == BLOCK_BLOCKQUOTE && collected_quote_lines != NULL) {
        GList *curr = collected_quote_lines;
        ASTNode *bq_node = parse_blockquote_recursive (&curr, 1);
        ast_node_append_child (root, bq_node);
        g_list_free_full (collected_quote_lines, (GDestroyNotify) free_quote_line);
        collected_quote_lines = NULL;
    }
    flush_parser_accumulator (root, accumulator, current_block_type, 0);

    g_string_free (accumulator, TRUE);
    g_free (code_lang);
    return root;
}
