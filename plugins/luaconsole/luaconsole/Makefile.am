# Lua console plugin

plugindir = $(PLUMA_PLUGINS_LIBS_DIR)/luaconsole
plugin_DATA =		\
	init.lua	\
	console.lua	\
	config.lua	\
	compat.lua

uidir = $(PLUMA_PLUGINS_DATA_DIR)/luaconsole/ui
ui_files = config.ui
include $(top_srcdir)/gla11y.mk
ui_DATA = $(ui_files)

EXTRA_DIST = $(ui_DATA) ui-a11y.suppr

CLEANFILES =
DISTCLEANFILES =

-include $(top_srcdir)/git.mk
