#include <glib.h>
#include <string.h>
#include "pluma-git-blame.h"

static void
test_empty (void)
{
	GPtrArray *lines = pluma_git_blame_parse (NULL);
	g_assert_cmpuint (lines->len, ==, 0);
	g_ptr_array_unref (lines);

	lines = pluma_git_blame_parse ("");
	g_assert_cmpuint (lines->len, ==, 0);
	g_ptr_array_unref (lines);
}

/* Real `git blame --porcelain` output for a 3-line file where line 1 comes
 * from one commit and lines 2-3 come from a second commit, captured from an
 * actual repository - not hand-written, so it reflects git's real format
 * (including the compact repeat form for line 3, which reuses commit
 * 1c5f098...'s metadata from line 2 instead of repeating it). */
static const gchar *sample =
	"c1154ebccf73b71769fb460c6641381ee654c988 1 1 1\n"
	"author Author A\n"
	"author-mail <a@a.com>\n"
	"author-time 1783119117\n"
	"author-tz -0300\n"
	"committer Author A\n"
	"committer-mail <a@a.com>\n"
	"committer-time 1783119117\n"
	"committer-tz -0300\n"
	"summary first commit\n"
	"boundary\n"
	"filename f.txt\n"
	"\tline one\n"
	"1c5f09813b10ee219c9f65fa4d5828e55284d25e 2 2 2\n"
	"author Author B\n"
	"author-mail <b@b.com>\n"
	"author-time 1783119200\n"
	"author-tz -0300\n"
	"committer Author B\n"
	"committer-mail <b@b.com>\n"
	"committer-time 1783119200\n"
	"committer-tz -0300\n"
	"summary second commit\n"
	"previous c1154ebccf73b71769fb460c6641381ee654c988 f.txt\n"
	"filename f.txt\n"
	"\tchanged two\n"
	"1c5f09813b10ee219c9f65fa4d5828e55284d25e 3 3\n"
	"\tline three\n";

static void
test_parses_first_occurrence_metadata (void)
{
	GPtrArray *lines = pluma_git_blame_parse (sample);
	PlumaGitBlameLine *line1;

	g_assert_cmpuint (lines->len, ==, 3);
	line1 = g_ptr_array_index (lines, 0);
	g_assert_cmpstr (line1->hash, ==, "c1154ebccf73b71769fb460c6641381ee654c988");
	g_assert_cmpstr (line1->author, ==, "Author A");
	g_assert_cmpstr (line1->author_mail, ==, "<a@a.com>");
	g_assert_cmpint (line1->author_time, ==, 1783119117);
	g_assert_cmpstr (line1->summary, ==, "first commit");
	g_assert_cmpstr (line1->content, ==, "line one");
	g_assert_cmpint (line1->final_line, ==, 1);
	g_ptr_array_unref (lines);
}

static void
test_reuses_metadata_for_compact_repeat_form (void)
{
	GPtrArray *lines = pluma_git_blame_parse (sample);
	PlumaGitBlameLine *line2, *line3;

	line2 = g_ptr_array_index (lines, 1);
	line3 = g_ptr_array_index (lines, 2);

	/* Line 3's porcelain record has no metadata lines at all (git only
	 * repeats them the first time a commit appears) - the parser must
	 * still resolve the same author/time/summary as line 2, which does
	 * carry the full record for the same commit. */
	g_assert_cmpstr (line3->hash, ==, line2->hash);
	g_assert_cmpstr (line3->author, ==, "Author B");
	g_assert_cmpstr (line3->author_mail, ==, "<b@b.com>");
	g_assert_cmpint (line3->author_time, ==, 1783119200);
	g_assert_cmpstr (line3->summary, ==, "second commit");
	g_assert_cmpstr (line3->content, ==, "line three");
	g_assert_cmpint (line3->final_line, ==, 3);
	g_ptr_array_unref (lines);
}

static void
test_uncommitted_line_detection (void)
{
	static const gchar *uncommitted =
		"0000000000000000000000000000000000000000 2 2 1\n"
		"author Not Committed Yet\n"
		"author-mail <not.committed.yet>\n"
		"author-time 1783119126\n"
		"author-tz -0300\n"
		"committer Not Committed Yet\n"
		"committer-mail <not.committed.yet>\n"
		"committer-time 1783119126\n"
		"committer-tz -0300\n"
		"summary Version of f.txt from f.txt\n"
		"previous 408f5755915e6876ab785c5c79a45f685e9c2d79 f.txt\n"
		"filename f.txt\n"
		"\tuncommitted change\n";
	GPtrArray *lines = pluma_git_blame_parse (uncommitted);
	PlumaGitBlameLine *line;

	g_assert_cmpuint (lines->len, ==, 1);
	line = g_ptr_array_index (lines, 0);
	g_assert_true (pluma_git_blame_line_is_uncommitted (line));
	g_ptr_array_unref (lines);

	g_assert_false (pluma_git_blame_line_is_uncommitted (NULL));
}

static void
test_committed_line_is_not_uncommitted (void)
{
	GPtrArray *lines = pluma_git_blame_parse (sample);
	PlumaGitBlameLine *line1 = g_ptr_array_index (lines, 0);

	g_assert_false (pluma_git_blame_line_is_uncommitted (line1));
	g_ptr_array_unref (lines);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/git-blame/empty", test_empty);
	g_test_add_func ("/git-blame/first-occurrence-metadata", test_parses_first_occurrence_metadata);
	g_test_add_func ("/git-blame/reuses-metadata-for-compact-repeat", test_reuses_metadata_for_compact_repeat_form);
	g_test_add_func ("/git-blame/uncommitted-line-detection", test_uncommitted_line_detection);
	g_test_add_func ("/git-blame/committed-line-is-not-uncommitted", test_committed_line_is_not_uncommitted);
	return g_test_run ();
}
