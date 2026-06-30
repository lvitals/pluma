#include <glib.h>
#include "pluma-git-commit.h"

static void
test_validation (void)
{
	g_assert_false (pluma_git_commit_message_is_valid (NULL));
	g_assert_false (pluma_git_commit_message_is_valid (""));
	g_assert_false (pluma_git_commit_message_is_valid (" \n\t"));
	g_assert_true (pluma_git_commit_message_is_valid ("  fix parser  "));
	g_assert_false (pluma_git_commit_message_is_valid ("bad\xff"));
}

static void
test_length (void)
{
	g_assert_cmpuint (pluma_git_commit_message_length ("message"), ==, 7);
	g_assert_cmpuint (pluma_git_commit_message_length ("ação"), ==, 4);
	g_assert_cmpuint (pluma_git_commit_message_length ("bad\xff"), ==, 0);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/git-commit/validation", test_validation);
	g_test_add_func ("/git-commit/length", test_length);
	return g_test_run ();
}
