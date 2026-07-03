AM_CPPFLAGS = -g -I$(top_srcdir) -I$(top_srcdir)/pluma $(PLUMA_DEBUG_FLAGS) $(PLUMA_CFLAGS)

noinst_PROGRAMS = $(TEST_PROGS)
progs_ldadd     = $(top_builddir)/pluma/libpluma.la

TEST_PROGS			= smart-converter
smart_converter_SOURCES		= smart-converter.c
smart_converter_LDADD		= $(progs_ldadd)

TEST_PROGS			+= action-migration
action_migration_SOURCES	= action-migration.c
action_migration_LDADD		= $(progs_ldadd)

TEST_PROGS			+= document-input-stream
document_input_stream_SOURCES	= document-input-stream.c
document_input_stream_LDADD	= $(progs_ldadd)

TEST_PROGS			+= document-output-stream
document_output_stream_SOURCES	= document-output-stream.c
document_output_stream_LDADD	= $(progs_ldadd)

TEST_PROGS			+= document-loader
document_loader_SOURCES		= document-loader.c
document_loader_LDADD		= $(progs_ldadd)

TEST_PROGS			+= document-saver
document_saver_SOURCES		= document-saver.c
document_saver_LDADD		= $(progs_ldadd)

if ENABLE_GIT_TESTS
TEST_PROGS			+= git-diff
git_diff_SOURCES		= git-diff.c
git_diff_LDADD			= $(progs_ldadd)

TEST_PROGS			+= git-commit
git_commit_SOURCES		= git-commit.c
git_commit_LDADD		= $(progs_ldadd)

TEST_PROGS			+= git-status-parser
git_status_parser_SOURCES	= git-status-parser.c
git_status_parser_LDADD	= $(progs_ldadd)

TESTS				= $(TEST_PROGS) git-integration.sh
else
TESTS = $(TEST_PROGS)
endif

# GIT_CONFIG_NOSYSTEM matches Meson's tests/meson.build env for this test:
# don't let a real /etc/gitconfig or $HOME/.gitconfig influence the
# throwaway repo git-integration.sh creates under $TMPDIR.
TESTS_ENVIRONMENT = GIT_CONFIG_NOSYSTEM=1

EXTRA_DIST = setup-document-saver.sh git-integration.sh
