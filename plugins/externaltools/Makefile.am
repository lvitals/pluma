# External Tools plugin
SUBDIRS = tools data scripts
plugindir = $(PLUMA_PLUGINS_LIBS_DIR)

AM_CPPFLAGS = -I$(top_srcdir) $(PLUMA_CFLAGS) $(WARN_CFLAGS)
plugin_LTLIBRARIES = libexternaltools.la
libexternaltools_la_SOURCES = \
	pluma-tool-library.c		\
	pluma-tool-library.h		\
	pluma-tool-link-parser.c	\
	pluma-tool-link-parser.h	\
	pluma-tool-file-lookup.c	\
	pluma-tool-file-lookup.h	\
	pluma-tool-capture.c		\
	pluma-tool-capture.h		\
	pluma-tool-output-panel.c	\
	pluma-tool-output-panel.h	\
	pluma-tool-functions.c		\
	pluma-tool-functions.h		\
	pluma-tool-manager.c		\
	pluma-tool-manager.h		\
	pluma-externaltools-plugin.c	\
	pluma-externaltools-plugin.h
libexternaltools_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libexternaltools_la_LIBADD = $(PLUMA_LIBS)

plugin_DATA = externaltools.plugin
plugin_in_files = $(plugin_DATA:.plugin=.plugin.desktop.in)
plugin_in_in_files = $(plugin_in_files:.plugin.desktop.in=.plugin.desktop.in.in)

$(plugin_DATA): $(plugin_in_files)
	$(AM_V_GEN) $(MSGFMT) --keyword=Name --keyword=Description --desktop --template $< -d $(top_srcdir)/po -o $@

EXTRA_DIST = $(plugin_in_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_in_files)

-include $(top_srcdir)/git.mk
