uidir = $(PLUMA_PLUGINS_DATA_DIR)/externaltools/ui
ui_files = tools.ui \
	outputpanel.ui
include $(top_srcdir)/gla11y.mk
ui_DATA = $(ui_files)

EXTRA_DIST = $(ui_DATA) ui-a11y.suppr

CLEANFILES = *.bak *.gladep
DISTCLEANFILES = *.bak *.gladep

-include $(top_srcdir)/git.mk
