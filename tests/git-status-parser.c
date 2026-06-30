#include <glib.h>
#include "pluma-git-status-parser.h"

static void
test_branch_and_entries (void)
{
	static const guint8 data[] =
		"# branch.oid abc\0# branch.head development\0"
		"# branch.upstream origin/development\0# branch.ab +2 -1\0"
		"1 .M N... 100644 100644 100644 aaa bbb path with spaces.txt\0"
		"1 A. N... 000000 100644 100644 000 ccc caf\303\251-\346\226\207.txt\0"
		"? -leading-dash.txt\0";
	PlumaGitStatus *status = pluma_git_status_parse (data, sizeof data - 1);
	PlumaGitStatusEntry *entry;

	g_assert_cmpstr (status->branch, ==, "development");
	g_assert_cmpstr (status->upstream, ==, "origin/development");
	g_assert_cmpint (status->ahead, ==, 2);
	g_assert_cmpint (status->behind, ==, 1);
	g_assert_cmpuint (status->entries->len, ==, 3);
	entry = g_ptr_array_index (status->entries, 0);
	g_assert_cmpint (entry->worktree_status, ==, 'M');
	g_assert_cmpstr (entry->path, ==, "path with spaces.txt");
	entry = g_ptr_array_index (status->entries, 1);
	g_assert_cmpstr (entry->path, ==, "café-文.txt");
	entry = g_ptr_array_index (status->entries, 2);
	g_assert_cmpstr (entry->path, ==, "-leading-dash.txt");
	pluma_git_status_free (status);
}

static void
test_rename_and_unusual_path (void)
{
	static const guint8 data[] =
		"2 R. N... 100644 100644 100644 aaa bbb R100 new\nname.txt\0old name.txt\0"
		"u UU N... 100644 100644 100644 100644 aaa bbb ccc conflict.txt\0";
	PlumaGitStatus *status = pluma_git_status_parse (data, sizeof data - 1);
	PlumaGitStatusEntry *rename = g_ptr_array_index (status->entries, 0);
	PlumaGitStatusEntry *conflict = g_ptr_array_index (status->entries, 1);

	g_assert_cmpuint (status->entries->len, ==, 2);
	g_assert_cmpint (rename->kind, ==, '2');
	g_assert_cmpstr (rename->path, ==, "new\nname.txt");
	g_assert_cmpstr (rename->original_path, ==, "old name.txt");
	g_assert_cmpint (conflict->kind, ==, 'u');
	g_assert_cmpstr (conflict->path, ==, "conflict.txt");
	pluma_git_status_free (status);
}

static void
test_invalid_utf8_is_safe (void)
{
	static const guint8 data[] = "? bad-\xff-name\0";
	PlumaGitStatus *status = pluma_git_status_parse (data, sizeof data - 1);
	PlumaGitStatusEntry *entry = g_ptr_array_index (status->entries, 0);
	g_assert_true (g_utf8_validate (entry->path, -1, NULL));
	pluma_git_status_free (status);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/git-status/branch-and-entries", test_branch_and_entries);
	g_test_add_func ("/git-status/rename-and-unusual-path", test_rename_and_unusual_path);
	g_test_add_func ("/git-status/invalid-utf8", test_invalid_utf8_is_safe);
	return g_test_run ();
}
