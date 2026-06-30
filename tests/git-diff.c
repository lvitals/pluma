#include <glib.h>
#include "pluma-git-diff.h"

static void
test_numstat (void)
{
	g_assert_false (pluma_git_numstat_has_binary (NULL));
	g_assert_false (pluma_git_numstat_has_binary ("12\t3\tsource.c\n"));
	g_assert_true (pluma_git_numstat_has_binary ("-\t-\timage.bin\n"));
	g_assert_true (pluma_git_numstat_has_binary ("1\t0\ttext\n-\t-\tbinary file\n"));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/git-diff/numstat", test_numstat);
	return g_test_run ();
}
