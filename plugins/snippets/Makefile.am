# C snippets plugin
SUBDIRS = data
plugindir = $(PLUMA_PLUGINS_LIBS_DIR)

AM_CPPFLAGS = -I$(top_srcdir) $(PLUMA_CFLAGS) $(WARN_CFLAGS)
plugin_LTLIBRARIES = libsnippets.la
libsnippets_la_SOURCES = pluma-snippets-plugin.c pluma-snippets-plugin.h
libsnippets_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libsnippets_la_LIBADD = $(PLUMA_LIBS)

plugin_DATA = snippets.plugin
plugin_in_files = $(plugin_DATA:.plugin=.plugin.desktop.in)
plugin_in_in_files = $(plugin_in_files:.plugin.desktop.in=.plugin.desktop.in.in)

$(plugin_DATA): $(plugin_in_files)
	$(AM_V_GEN) $(MSGFMT) --keyword=Name --keyword=Description --desktop --template $< -d $(top_srcdir)/po -o $@

EXTRA_DIST = $(plugin_in_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_in_files)

-include $(top_srcdir)/git.mk
