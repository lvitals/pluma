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

/* Two removals followed by two additions, so a partial selection produces
 * old/new counts that actually differ from each other and from the
 * original hunk - a stronger check than a single -/+ pair, where the
 * counts could accidentally match by coincidence. */
static const gchar *subset_diff =
	"diff --git a/file.txt b/file.txt\n"
	"index abc123..def456 100644\n"
	"--- a/file.txt\n"
	"+++ b/file.txt\n"
	"@@ -1,4 +1,4 @@\n"
	" context\n"
	"-old1\n"
	"-old2\n"
	"+new1\n"
	"+new2\n"
	" trailing\n";

static PlumaGitHunk *
get_subset_test_hunk (GPtrArray **out_hunks)
{
	*out_hunks = pluma_git_diff_split_hunks (subset_diff);
	g_assert_cmpuint ((*out_hunks)->len, ==, 1);
	return g_ptr_array_index (*out_hunks, 0);
}

static void
test_count_change_lines (void)
{
	GPtrArray *hunks;
	PlumaGitHunk *hunk = get_subset_test_hunk (&hunks);

	g_assert_cmpuint (pluma_git_diff_count_change_lines (hunk), ==, 4);
	g_ptr_array_unref (hunks);
}

static void
test_hunk_subset_all_selected_matches_whole_hunk (void)
{
	GPtrArray *hunks;
	PlumaGitHunk *hunk = get_subset_test_hunk (&hunks);
	gboolean selected[4] = { TRUE, TRUE, TRUE, TRUE };
	gchar *patch = pluma_git_diff_hunk_subset (hunk, selected, 4, FALSE);

	g_assert_nonnull (patch);
	g_assert_nonnull (strstr (patch, "@@ -1,4 +1,4 @@\n"
	                                 " context\n-old1\n-old2\n+new1\n+new2\n trailing\n"));
	g_free (patch);
	g_ptr_array_unref (hunks);
}

static void
test_hunk_subset_stage_only_one_addition (void)
{
	GPtrArray *hunks;
	PlumaGitHunk *hunk = get_subset_test_hunk (&hunks);
	/* slots in body order: -old1, -old2, +new1, +new2 */
	gboolean selected[4] = { FALSE, FALSE, TRUE, FALSE };
	gchar *patch = pluma_git_diff_hunk_subset (hunk, selected, 4, FALSE);

	g_assert_nonnull (patch);
	/* Unselected removals must survive as context (their state isn't
	 * being touched by this partial stage); the unselected addition must
	 * be dropped outright (nothing chose to stage it). */
	g_assert_nonnull (strstr (patch, "@@ -1,4 +1,5 @@\n"
	                                 " context\n old1\n old2\n+new1\n trailing\n"));
	g_assert_null (strstr (patch, "new2"));
	g_free (patch);
	g_ptr_array_unref (hunks);
}

static void
test_hunk_subset_unstage_only_one_addition (void)
{
	GPtrArray *hunks;
	PlumaGitHunk *hunk = get_subset_test_hunk (&hunks);
	gboolean selected[4] = { FALSE, FALSE, TRUE, FALSE };
	/* reverse=TRUE: mirrors stage's rule - unselected removals are
	 * dropped (their staged-removed status is untouched), the unselected
	 * addition survives as context (not being reverted). */
	gchar *patch = pluma_git_diff_hunk_subset (hunk, selected, 4, TRUE);

	g_assert_nonnull (patch);
	g_assert_nonnull (strstr (patch, "@@ -1,3 +1,4 @@\n"
	                                 " context\n+new1\n new2\n trailing\n"));
	g_assert_null (strstr (patch, "old1"));
	g_assert_null (strstr (patch, "old2"));
	g_free (patch);
	g_ptr_array_unref (hunks);
}

static void
test_hunk_subset_nothing_selected_returns_null (void)
{
	GPtrArray *hunks;
	PlumaGitHunk *hunk = get_subset_test_hunk (&hunks);
	gboolean selected[4] = { FALSE, FALSE, FALSE, FALSE };

	g_assert_null (pluma_git_diff_hunk_subset (hunk, selected, 4, FALSE));
	g_assert_null (pluma_git_diff_hunk_subset (hunk, selected, 4, TRUE));
	g_ptr_array_unref (hunks);
}

static void
test_hunk_subset_preserves_shared_header (void)
{
	GPtrArray *hunks;
	PlumaGitHunk *hunk = get_subset_test_hunk (&hunks);
	gboolean selected[4] = { TRUE, FALSE, FALSE, FALSE };
	gchar *patch = pluma_git_diff_hunk_subset (hunk, selected, 4, FALSE);

	g_assert_nonnull (patch);
	g_assert_true (g_str_has_prefix (patch, "diff --git a/file.txt b/file.txt\n"
	                                        "index abc123..def456 100644\n"
	                                        "--- a/file.txt\n"
	                                        "+++ b/file.txt\n"));
	g_free (patch);
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
	g_test_add_func ("/git-diff/hunk-subset/count-change-lines", test_count_change_lines);
	g_test_add_func ("/git-diff/hunk-subset/all-selected-matches-whole-hunk", test_hunk_subset_all_selected_matches_whole_hunk);
	g_test_add_func ("/git-diff/hunk-subset/stage-only-one-addition", test_hunk_subset_stage_only_one_addition);
	g_test_add_func ("/git-diff/hunk-subset/unstage-only-one-addition", test_hunk_subset_unstage_only_one_addition);
	g_test_add_func ("/git-diff/hunk-subset/nothing-selected-returns-null", test_hunk_subset_nothing_selected_returns_null);
	g_test_add_func ("/git-diff/hunk-subset/preserves-shared-header", test_hunk_subset_preserves_shared_header);
	return g_test_run ();
}
