# Bundled libpeas lua5.1 plugin loader + vendored LuaGObject runtime.
# Only built when configure found Lua 5.1 + girepository-2.0 (see
# configure.ac's "Lua detection" block). See VENDORED.md for what this
# is and why it exists instead of relying on the system's lgi.

AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = \
	-I$(top_srcdir)		\
	-I$(srcdir)		\
	$(PLUMA_CFLAGS)		\
	$(WARN_CFLAGS)

lualoaderdir = $(PLUMA_PLUGIN_LOADERS_DIR)/lua5.1
lualoader_LTLIBRARIES = liblua51loader.la

liblua51loader_la_SOURCES =		\
	peas-plugin-loader-lua.c	\
	peas-plugin-loader-lua.h	\
	peas-lua-utils.c		\
	peas-lua-utils.h		\
	peas-lua-internal.c		\
	peas-lua-internal.h		\
	libpeas/peas-plugin-loader.h	\
	libpeas/peas-plugin-info-priv.h \
	resources/peas-lua-resources.c

# LuaGObject's own _VERSION string; peas_lua_utils_check_version() just
# needs this to match what resources/peas-lua-internal.lua requires.
liblua51loader_la_CPPFLAGS =		\
	$(AM_CPPFLAGS)			\
	$(LUA_CFLAGS)			\
	-DLGI_MAJOR_VERSION=0		\
	-DLGI_MINOR_VERSION=10		\
	-DLGI_MICRO_VERSION=5		\
	-DPEAS_LUA_EXTRA_PATH='"$(PLUMA_PLUGIN_LOADERS_DIR)/lua5.1/lua-runtime"'
liblua51loader_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
liblua51loader_la_LIBADD  = $(PLUMA_LIBS) $(LUA_LIBS)

resources/peas-lua-resources.c: resources/peas-lua.gresource.xml resources/peas-lua-internal.lua resources/peas-lua-strict.lua
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir)/resources --generate-source $(srcdir)/resources/peas-lua.gresource.xml

# --- Vendored LuaGObject (github.com/vtrlx/LuaGObject, MIT). The Meson
# build vendors this at subprojects/LuaGObject/ and builds it as a
# subproject; autotools has no equivalent, so this references those same
# vendored sources directly by relative path instead of duplicating them
# here. ---

luagobject_srcdir = $(top_srcdir)/subprojects/LuaGObject/LuaGObject

luagobjectcoredir = $(PLUMA_PLUGIN_LOADERS_DIR)/lua5.1/lua-runtime/LuaGObject
luagobjectcore_LTLIBRARIES = lua_gobject_core.la

lua_gobject_core_la_SOURCES =		\
	$(luagobject_srcdir)/buffer.c	\
	$(luagobject_srcdir)/callable.c	\
	$(luagobject_srcdir)/core.c	\
	$(luagobject_srcdir)/gi.c	\
	$(luagobject_srcdir)/marshal.c	\
	$(luagobject_srcdir)/object.c	\
	$(luagobject_srcdir)/record.c	\
	$(luagobject_srcdir)/lua_gobject.h

lua_gobject_core_la_CPPFLAGS = $(LUA_CFLAGS) $(GIREPOSITORY_CFLAGS) $(LUALOADER_EXTRA_CFLAGS)
lua_gobject_core_la_LDFLAGS = -module -avoid-version -shared
lua_gobject_core_la_LIBADD  = $(LUA_LIBS) $(GIREPOSITORY_LIBS) $(LUALOADER_EXTRA_LIBS)

luagobjectdatadir = $(PLUMA_PLUGIN_LOADERS_DIR)/lua5.1/lua-runtime/LuaGObject
luagobjectdata_DATA =				\
	$(luagobject_srcdir)/class.lua		\
	$(luagobject_srcdir)/component.lua	\
	$(luagobject_srcdir)/core.lua		\
	$(luagobject_srcdir)/enum.lua		\
	$(luagobject_srcdir)/ffi.lua		\
	$(luagobject_srcdir)/init.lua		\
	$(luagobject_srcdir)/log.lua		\
	$(luagobject_srcdir)/namespace.lua	\
	$(luagobject_srcdir)/package.lua	\
	$(luagobject_srcdir)/record.lua	\
	version.lua

luagobjectoverridedir = $(PLUMA_PLUGIN_LOADERS_DIR)/lua5.1/lua-runtime/LuaGObject/override
luagobjectoverride_DATA =				\
	$(luagobject_srcdir)/override/Adw.lua		\
	$(luagobject_srcdir)/override/cairo.lua	\
	$(luagobject_srcdir)/override/Clutter.lua	\
	$(luagobject_srcdir)/override/Gdk.lua		\
	$(luagobject_srcdir)/override/Gio-DBus.lua	\
	$(luagobject_srcdir)/override/Gio.lua		\
	$(luagobject_srcdir)/override/GLib-Bytes.lua	\
	$(luagobject_srcdir)/override/GLib-Error.lua	\
	$(luagobject_srcdir)/override/GLib.lua		\
	$(luagobject_srcdir)/override/GLib-Markup.lua	\
	$(luagobject_srcdir)/override/GLib-Source.lua	\
	$(luagobject_srcdir)/override/GLib-Timer.lua	\
	$(luagobject_srcdir)/override/GLib-Variant.lua	\
	$(luagobject_srcdir)/override/GObject-Closure.lua \
	$(luagobject_srcdir)/override/GObject-Object.lua \
	$(luagobject_srcdir)/override/GObject-Type.lua	\
	$(luagobject_srcdir)/override/GObject-Value.lua \
	$(luagobject_srcdir)/override/GooCanvas.lua	\
	$(luagobject_srcdir)/override/Gst.lua		\
	$(luagobject_srcdir)/override/Gtk3.lua		\
	$(luagobject_srcdir)/override/Gtk4.lua		\
	$(luagobject_srcdir)/override/Gtk.lua		\
	$(luagobject_srcdir)/override/PangoCairo.lua	\
	$(luagobject_srcdir)/override/Pango.lua

luagobjectentrydir = $(PLUMA_PLUGIN_LOADERS_DIR)/lua5.1/lua-runtime
luagobjectentry_DATA = $(top_srcdir)/subprojects/LuaGObject/LuaGObject.lua

# Mirrors subprojects/LuaGObject/LuaGObject/version.lua.in -> version.lua
# (see subprojects/LuaGObject/LuaGObject/meson.build for the Meson
# equivalent of this substitution).
version.lua: $(luagobject_srcdir)/version.lua.in
	$(AM_V_GEN) $(SED) "s/@VERSION@/0.10.5/" $< > $@

CLEANFILES = resources/peas-lua-resources.c version.lua

-include $(top_srcdir)/git.mk
