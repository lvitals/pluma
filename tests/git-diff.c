#include <glib.h>
#include <string.h>
#include "pluma-git-diff.h"

static void
test_numstat (void)
{
	g_assert_false (pluma_git_numstat_has_binary (NULL));
	g_assert_false (pluma_git_numstat_has_binary ("12\t3\tsource.c\n"));
	g_assert_true (pluma_git_numstat_has_binary ("-\t-\timage.bin\n"));
	g_assert_true (pluma_git_numstat_has_binary ("1\t0\ttext\n-\t-\tbinary file\n"));
}

static void
test_split_hunks_empty (void)
{
	GPtrArray *hunks;

	hunks = pluma_git_diff_split_hunks (NULL);
	g_assert_cmpuint (hunks->len, ==, 0);
	g_ptr_array_unref (hunks);

	hunks = pluma_git_diff_split_hunks ("");
	g_assert_cmpuint (hunks->len, ==, 0);
	g_ptr_array_unref (hunks);
}

static void
test_split_hunks_header_only (void)
{
	static const gchar *diff =
		"diff --git a/file.txt b/file.txt\n"
		"index abc123..def456 100644\n"
		"--- a/file.txt\n"
		"+++ b/file.txt\n";
	GPtrArray *hunks = pluma_git_diff_split_hunks (diff);

	/* No "@@ " line at all (e.g. a rename-only diff) means no hunks to
	 * stage/unstage/discard - must not fabricate one from the header. */
	g_assert_cmpuint (hunks->len, ==, 0);
	g_ptr_array_unref (hunks);
}

static void
test_split_hunks_single (void)
{
	static const gchar *diff =
		"diff --git a/file.txt b/file.txt\n"
		"index abc123..def456 100644\n"
		"--- a/file.txt\n"
		"+++ b/file.txt\n"
		"@@ -1,3 +1,4 @@\n"
		" context line\n"
		"-old line\n"
		"+new line\n";
	GPtrArray *hunks = pluma_git_diff_split_hunks (diff);
	PlumaGitHunk *hunk;

	g_assert_cmpuint (hunks->len, ==, 1);
	hunk = g_ptr_array_index (hunks, 0);
	g_assert_cmpstr (hunk->label, ==, "@@ -1,3 +1,4 @@");
	/* The patch must carry the full "diff --git"/"index"/"---"/"+++"
	 * header plus this hunk's body, since that's what `git apply` needs
	 * to know which file to patch. */
	g_assert_true (g_str_has_prefix (hunk->patch, "diff --git a/file.txt b/file.txt\n"));
	g_assert_nonnull (strstr (hunk->patch, "@@ -1,3 +1,4 @@\n context line\n-old line\n+new line\n"));
	g_ptr_array_unref (hunks);
}

static void
test_split_hunks_multiple_share_header (void)
{
	static const gchar *diff =
		"diff --git a/file.txt b/file.txt\n"
		"index abc123..def456 100644\n"
		"--- a/file.txt\n"
		"+++ b/file.txt\n"
		"@@ -1,3 +1,4 @@\n"
		" context line\n"
		"-old line\n"
		"+new line\n"
		"@@ -10,2 +11,3 @@\n"
		" more context\n"
		"+added line\n";
	GPtrArray *hunks = pluma_git_diff_split_hunks (diff);
	PlumaGitHunk *first, *second;

	g_assert_cmpuint (hunks->len, ==, 2);
	first = g_ptr_array_index (hunks, 0);
	second = g_ptr_array_index (hunks, 1);
	g_assert_cmpstr (first->label, ==, "@@ -1,3 +1,4 @@");
	g_assert_cmpstr (second->label, ==, "@@ -10,2 +11,3 @@");

	/* Each hunk's patch must independently carry the shared header, and
	 * only its own body - not the other hunk's lines. */
	g_assert_true (g_str_has_prefix (first->patch, "diff --git a/file.txt b/file.txt\n"));
	g_assert_true (g_str_has_prefix (second->patch, "diff --git a/file.txt b/file.txt\n"));
	g_assert_null (strstr (first->patch, "@@ -10,2 +11,3 @@"));
	g_assert_null (strstr (second->patch, "@@ -1,3 +1,4 @@"));
	g_assert_nonnull (strstr (first->patch, "-old line"));
	g_assert_nonnull (strstr (second->patch, "+added line"));
	g_ptr_array_unref (hunks);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/git-diff/numstat", test_numstat);
	g_test_add_func ("/git-diff/split-hunks/empty", test_split_hunks_empty);
	g_test_add_func ("/git-diff/split-hunks/header-only", test_split_hunks_header_only);
	g_test_add_func ("/git-diff/split-hunks/single", test_split_hunks_single);
	g_test_add_func ("/git-diff/split-hunks/multiple-share-header", test_split_hunks_multiple_share_header);
	return g_test_run ();
}
