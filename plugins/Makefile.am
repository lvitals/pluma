DIST_SUBDIRS =		\
	docinfo 	\
	externaltools	\
	luaconsole	\
	lualoader	\
	markdown-preview \
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
	markdown-preview \
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
