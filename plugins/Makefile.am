DIST_SUBDIRS =		\
	docinfo 	\
	externaltools	\
	luaconsole	\
	lualoader	\
	modelines	\
	pythonconsole	\
	snippets	\
	sort 		\
	sourcecodebrowser \
	spell 		\
	taglist 	\
	terminal	\
	time		\
	trailsave

SUBDIRS = 		\
	docinfo		\
	externaltools	\
	luaconsole	\
	modelines	\
	pythonconsole	\
	snippets	\
	sort		\
	sourcecodebrowser \
	taglist		\
	terminal	\
	time		\
	trailsave

if ENABLE_ENCHANT
SUBDIRS      += spell
endif

if BUILD_LUALOADER
SUBDIRS      += lualoader
endif

-include $(top_srcdir)/git.mk
