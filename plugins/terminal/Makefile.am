plugindir = $(PLUMA_PLUGINS_LIBS_DIR)
AM_CPPFLAGS = -I$(top_srcdir) $(PLUMA_CFLAGS) $(VTE_CFLAGS) $(WARN_CFLAGS)
plugin_LTLIBRARIES = libterminal.la
libterminal_la_SOURCES = pluma-terminal-plugin.c pluma-terminal-plugin.h
libterminal_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libterminal_la_LIBADD = $(PLUMA_LIBS) $(VTE_LIBS)

plugin_DATA = terminal.plugin
terminal.plugin: terminal.plugin.desktop.in
	$(AM_V_GEN) $(MSGFMT) --keyword=Name --keyword=Description --desktop --template $< -d $(top_srcdir)/po -o $@

gsettings_SCHEMAS = org.mate.pluma.plugins.terminal.gschema.xml
@GSETTINGS_RULES@

metainfodir = $(datadir)/metainfo
metainfo_DATA = pluma-terminal.metainfo.xml
pluma-terminal.metainfo.xml: pluma-terminal.metainfo.xml.in
	$(AM_V_GEN) GETTEXTDATADIR=$(top_srcdir) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@
EXTRA_DIST = terminal.plugin.desktop.in.in org.mate.pluma.plugins.terminal.gschema.xml.in pluma-terminal.metainfo.xml.in.in
CLEANFILES = $(plugin_DATA) $(gsettings_SCHEMAS) $(metainfo_DATA)
DISTCLEANFILES = terminal.plugin.desktop.in pluma-terminal.metainfo.xml.in
-include $(top_srcdir)/git.mk
