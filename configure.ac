dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63.2)

m4_define(pluma_major_version, 1)
m4_define(pluma_minor_version, 28)
m4_define(pluma_micro_version, 1)
m4_define(pluma_version, pluma_major_version.pluma_minor_version.pluma_micro_version)

AC_INIT([pluma], [pluma_version],
    [https://github.com/mate-desktop/pluma/issues],
    [pluma], [http://mate-desktop.org])

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_SRCDIR(pluma/pluma.c)
AC_CONFIG_MACRO_DIR([m4])

AC_DEFINE(PLUMA_MAJOR_VERSION, pluma_major_version, [Pluma major version])
AC_SUBST(PLUMA_MAJOR_VERSION, pluma_major_version)
AC_DEFINE(PLUMA_MINOR_VERSION, pluma_minor_version, [Pluma minor version])
AC_SUBST(PLUMA_MINOR_VERSION, pluma_minor_version)
AC_DEFINE(PLUMA_MICRO_VERSION, pluma_micro_version, [Pluma micro version])
AC_SUBST(PLUMA_MICRO_VERSION, pluma_micro_version)

AM_INIT_AUTOMAKE([1.10 foreign dist-xz no-dist-gzip -Wno-portability check-news])
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

MATE_COMPILE_WARNINGS

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_SYS_LARGEFILE
PKG_PROG_PKG_CONFIG
AC_PATH_PROG([GLA11Y], [gla11y], [true])

# Initialize libtool
LT_PREREQ([2.2.6])
LT_INIT(disable-static)

GTK_DOC_CHECK([1.0],[--flavour=no-tmpl])

AC_CHECK_LIB(m, floor)

dnl make sure we keep ACLOCAL_FLAGS around for maintainer builds to work
AC_SUBST(ACLOCAL_AMFLAGS, "$ACLOCAL_FLAGS -I m4")

dnl toolbar style for GSettings schemas
TOOLBAR_STYLE="PLUMA_TOOLBAR_SYSTEM"
AC_SUBST(TOOLBAR_STYLE)

dnl ===============================================================
dnl Expanded dirs
dnl ===============================================================

# This macro expands DIR and assigns it to RET.
# If DIR is NONE, then it's replaced by DEFAULT.
# Based on AC_DEFINE_DIR
AC_DEFUN([PLUMA_FULLPATH], [
  test "x$prefix" = xNONE && prefix="$ac_default_prefix"
  test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
  ac_expand=[$]$1
  test "x$ac_expand" = xNONE && ac_expand="[$]$2"
  ac_expand=`eval echo [$]ac_expand`
  $3=`eval echo [$]ac_expand`
])

dnl FULL_LIBEXECDIR is used for X-MATE-Bugzilla-ExtraInfoScript expansion
dnl in data/pluma.desktop.in.in
PLUMA_FULLPATH(libexecdir, NONE, FULL_LIBEXECDIR)
AC_SUBST(FULL_LIBEXECDIR)

dnl ================================================================
dnl Gettext stuff.
dnl ================================================================
AM_GNU_GETTEXT_VERSION([0.19.8])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.8])
AM_GNU_GETTEXT([external])


GETTEXT_PACKAGE=pluma
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Define to the Gettext package name])

dnl ================================================================
dnl Use gvfs metadata or the old xml file store.
dnl ================================================================

AC_ARG_ENABLE([gvfs-metadata],
	AS_HELP_STRING([--enable-gvfs-metadata], [Enable using gvfs to store metadata]),
	[enable_gvfs_metadata=$enableval],
	[enable_gvfs_metadata=yes])

AM_CONDITIONAL(ENABLE_GVFS_METADATA, test "x$enable_gvfs_metadata" != "xno")

if test "x$enable_gvfs_metadata" != "xno"; then
	AC_DEFINE([ENABLE_GVFS_METADATA], [1], [Define to enable using gvfs metadata])
fi

dnl ================================================================
dnl spell plugins checks: enchant and iso-codes
dnl ================================================================

ENCHANT_REQUIRED=1.6.0
ISO_CODES_REQUIRED=0.35

AC_ARG_ENABLE([spell],
	AS_HELP_STRING([--disable-spell],[Disable spell plugin (default: enabled)]),
	[enable_enchant=$enableval],
	[enable_enchant=yes])

if test "x$enable_enchant" = "xyes" ; then
        ENCHANT_MODNAME=enchant
        PKG_CHECK_EXISTS([enchant-2 >= $ENCHANT_REQUIRED], [have_enchant_2=yes], [have_enchant_2=no])
        if test "x$have_enchant_2" = "xyes"; then
                ENCHANT_MODNAME=enchant-2
        fi

        AC_MSG_CHECKING([the version of enchant])
        ENCHANT_VERSION=`pkg-config --modversion $ENCHANT_MODNAME`
        AC_MSG_RESULT([$ENCHANT_VERSION])

        PKG_CHECK_MODULES(ENCHANT, [$ENCHANT_MODNAME >= $ENCHANT_REQUIRED], \
                          have_enchant=yes, have_enchant=no)
	if test "x$have_enchant" = "xyes"; then

		PKG_CHECK_EXISTS([iso-codes >= $ISO_CODES_REQUIRED],
				 [have_iso_codes=yes],[have_iso_codes=no])

		if test "x$have_iso_codes" = "xyes"; then
			AC_MSG_CHECKING([whether iso-codes has iso-639 and iso-3166 domains])
			if $PKG_CONFIG --variable=domains iso-codes | grep 639 > /dev/null && \
			   $PKG_CONFIG --variable=domains iso-codes | grep 3166 > /dev/null ; then
				result=yes
			else
				result=no
				have_iso_codes=no
			fi
			AC_MSG_RESULT([$result])
		fi

		if test "x$have_iso_codes" = "xyes"; then
			AC_DEFINE_UNQUOTED([ISO_CODES_PREFIX],["`$PKG_CONFIG --variable=prefix iso-codes`"],[ISO codes prefix])
			AC_DEFINE([HAVE_ISO_CODES],[1],[Define if you have the iso-codes package])
		else
			AC_MSG_ERROR([iso-codes is required to build the spell plugin. Use --disable-spell to build without spell plugin.])
		fi
	else
		enable_enchant=no
		AC_MSG_ERROR([Enchant library not found or too old. Use --disable-spell to build without spell plugin.])
	fi
fi

AM_CONDITIONAL(ENABLE_ENCHANT, test x"$enable_enchant" = "xyes")

GTK_REQUIRED=3.22.0
GTKSOURCEVIEW_REQUIRED=4.0.2
MATE_DESKTOP_REQUIRED=1.27.1

PKG_CHECK_MODULES(GMODULE,gmodule-2.0,[GMODULE_ADD="gmodule-2.0"],[GMODULE_ADD=""])
PKG_CHECK_MODULES(PLUMA, [
	libxml-2.0 >= 2.5.0
	libarchive >= 3.0
	glib-2.0 >= 2.50.0
	$GMODULE_ADD
	gthread-2.0 >= 2.13.0
	gio-2.0 >= 2.50.0
	gtk+-3.0 >= $GTK_REQUIRED
	gtksourceview-4 >= $GTKSOURCEVIEW_REQUIRED
	libpeas-1.0 >= 1.2.0
	libpeas-gtk-1.0 >= 1.2.0
])
PKG_CHECK_MODULES(VTE, [vte-2.91 >= 0.60])

PKG_CHECK_MODULES(MATE_DESKTOP, mate-desktop-2.0 >= $MATE_DESKTOP_REQUIRED)
AC_SUBST(MATE_DESKTOP_CFLAGS)
AC_SUBST(MATE_DESKTOP_LIBS)

PKG_CHECK_MODULES(X11, [x11])

PLUMA_CFLAGS="$PLUMA_CFLAGS $X11_CFLAGS"
PLUMA_LIBS="$PLUMA_LIBS $X11_LIBS"

AC_SUBST(PLUMA_CFLAGS)
AC_SUBST(PLUMA_LIBS)

PKG_CHECK_MODULES(LIBEGG, [sm >= 1.0.0 ice >= 1.0.0 gtk+-3.0])

AC_SUBST(LIBEGG_CFLAGS)
AC_SUBST(LIBEGG_LIBS)

# Introspection
GOBJECT_INTROSPECTION_CHECK([0.9.3])

if test "$found_introspection" = "yes"; then
	have_introspection=yes
	AC_DEFINE([HAVE_INTROSPECTION], [1], [Define to enable GObject Introspection])
else
	have_introspection=no
fi

# The plugin engine requires GIRepository at runtime even when GIR generation
# is disabled. Match the API generation used by libpeas to avoid mixing the
# incompatible 1.0 and 2.0 repositories in one process.
AS_IF([pkg-config --print-requires libpeas-1.0 | grep -q "girepository-2.0"], [
	PKG_CHECK_MODULES([GIREPOSITORY], [girepository-2.0])
	AC_DEFINE([HAVE_GIREPOSITORY_2], [1], [Using girepository-2.0 API])
	have_girepository_2=yes
], [
	PKG_CHECK_MODULES([GIREPOSITORY], [gobject-introspection-1.0])
	have_girepository_2=no
])
PLUMA_CFLAGS="$PLUMA_CFLAGS $GIREPOSITORY_CFLAGS"
PLUMA_LIBS="$PLUMA_LIBS $GIREPOSITORY_LIBS"

AM_PATH_PYTHON([3.0])

dnl ================================================================
dnl Lua detection for the native Lua plugin loader (plugins/lualoader)
dnl ================================================================
dnl libpeas' native Lua loader embeds Lua 5.1 specifically, and the
dnl vendored LuaGObject runtime (plugins/lualoader/LuaGObject, see
dnl plugins/lualoader/VENDORED.md) is built for 5.1 too, so this probes
dnl lua5.1 specifically - not "whichever Lua version is newest". Distros
dnl package it under different pkg-config names (lua5.1, lua-5.1,
dnl lua51), so try each spelling.
dnl
dnl It also needs girepository-2.0: the distro-packaged Lua/GObject-
dnl Introspection binding (lgi) is still linked against the legacy
dnl libgirepository-1.0, which crashes the whole process ("cannot
dnl register existing type 'GIRepository'") if loaded alongside Pluma's
dnl libgirepository-2.0 - see plugins/lualoader/VENDORED.md. So instead
dnl of detecting system lgi, plugins/lualoader vendors LuaGObject (a
dnl maintained lgi fork already ported to girepository-2.0) and bundles
dnl it privately; it's only usable when Pluma itself is also using
dnl girepository-2.0 ($have_girepository_2 above).
have_lua=no
LUA_PC_NAME=
for lua_pc in lua5.1 lua-5.1 lua51; do
	if test "x$have_lua" = xno; then
		if $PKG_CONFIG --exists "$lua_pc" 2>/dev/null; then
			LUA_PC_NAME="$lua_pc"
			have_lua=yes
		fi
	fi
done

AS_IF([test "x$have_lua" = xyes], [
	PKG_CHECK_MODULES([LUA], [$LUA_PC_NAME])
	PKG_CHECK_MODULES([LUALOADER_EXTRA], [libffi gmodule-2.0])
	AS_IF([test "x$have_girepository_2" = xyes], [
		AC_DEFINE([HAVE_LUA], [1], [Define if a Lua 5.1 development package was found])
		AC_DEFINE_UNQUOTED([LUA_VERSION], ["5.1"], [Lua version selected for the Lua plugin loader])
		AC_DEFINE([HAVE_BUNDLED_LUA_LOADER], [1], [Building the bundled Lua plugin loader against vendored LuaGObject])
		AC_MSG_NOTICE([Lua plugin loader: building bundled loader against Lua 5.1 + vendored LuaGObject])

		PLUMA_FULLPATH(libdir, NONE, FULL_LIBDIR)
		PLUMA_PLUGIN_LOADERS_DIR="$FULL_LIBDIR/pluma/plugin-loaders"
		AC_DEFINE_UNQUOTED([PLUMA_PLUGIN_LOADERS_DIR], ["$PLUMA_PLUGIN_LOADERS_DIR"], [Directory Pluma's bundled plugin loaders are installed in])
		AC_SUBST([PLUMA_PLUGIN_LOADERS_DIR])
	], [
		have_lua=no
		AC_MSG_WARN([Pluma was configured against the legacy girepository-1.0 API: the bundled Lua plugin loader (which needs girepository-2.0) will not be built])
	])
], [
	AC_MSG_WARN([No Lua 5.1 development package found via pkg-config: the Lua plugin loader will not be built])
])
AM_CONDITIONAL([BUILD_LUALOADER], [test "x$have_lua" = xyes])

dnl ================================================================
dnl GSettings related settings
dnl ================================================================

GLIB_GSETTINGS

dnl ================================================================
dnl Misc
dnl ================================================================
AC_PATH_PROG(GLIB_MKENUMS, glib-mkenums)
AC_PATH_PROG(GLIB_COMPILE_RESOURCES, glib-compile-resources)

AC_ARG_ENABLE([tests],
	AS_HELP_STRING([--enable-tests], [Enable the tests]),
	[enable_tests=$enableval],
	[enable_tests=yes])

AM_CONDITIONAL(ENABLE_TESTS, test x$enable_tests = xyes)

PLUGIN_LIBTOOL_FLAGS="-module -avoid-version"

AC_SUBST(PLUGIN_LIBTOOL_FLAGS)

PLUMA_PLUGINS_DATA_DIR="$datadir/pluma/plugins"
AC_SUBST(PLUMA_PLUGINS_DATA_DIR)

PLUMA_PLUGINS_LIBS_DIR="$libdir/pluma/plugins"
AC_SUBST(PLUMA_PLUGINS_LIBS_DIR)

# Built-in source-control panel (kept in parity with Meson's git option).
AC_ARG_ENABLE([git],
	AS_HELP_STRING([--disable-git], [do not build the built-in Git panel]),
	[], [enable_git=yes])
AS_IF([test "x$enable_git" = "xyes"], [
	AC_DEFINE([ENABLE_GIT], [1], [Build the built-in Git panel])
])
AM_CONDITIONAL([ENABLE_GIT], [test "x$enable_git" = "xyes"])

# Git panel unit/integration tests (tests/git-*) also need a real `git`
# binary at test-run time, same gate as Meson's
# "git_program.found() and enable_git" in tests/meson.build.
AC_PATH_PROG([GIT], [git])
AM_CONDITIONAL([ENABLE_GIT_TESTS], [test "x$enable_git" = "xyes" -a "x$GIT" != "x"])

YELP_HELP_INIT

AC_CONFIG_FILES([
Makefile
data/pluma.appdata.xml.in
data/pluma.desktop.in
data/pluma-bugreport.sh
data/pluma.pc
data/org.mate.pluma.gschema.xml
data/org.mate.pluma.plugins.filebrowser.gschema.xml
data/Makefile
docs/Makefile
docs/reference/Makefile
pluma/dialogs/Makefile
pluma/mate-submodules/Makefile
pluma/mate-submodules/libegg/Makefile
pluma/Makefile
help/Makefile
pixmaps/Makefile
plugins/Makefile
plugins/docinfo/Makefile
plugins/docinfo/docinfo.plugin.desktop.in
plugins/externaltools/Makefile
plugins/externaltools/externaltools.plugin.desktop.in
plugins/externaltools/data/Makefile
plugins/externaltools/scripts/Makefile
plugins/externaltools/tools/Makefile
plugins/luaconsole/Makefile
plugins/luaconsole/luaconsole.plugin.desktop.in
plugins/luaconsole/org.mate.pluma.plugins.luaconsole.gschema.xml
plugins/luaconsole/luaconsole/Makefile
plugins/lualoader/Makefile
plugins/modelines/Makefile
plugins/modelines/modelines.plugin.desktop.in
plugins/pythonconsole/Makefile
plugins/pythonconsole/pythonconsole.plugin.desktop.in
plugins/pythonconsole/org.mate.pluma.plugins.pythonconsole.gschema.xml
plugins/pythonconsole/pythonconsole/Makefile
plugins/snippets/Makefile
plugins/snippets/snippets.plugin.desktop.in
plugins/snippets/data/lang/Makefile
plugins/snippets/data/Makefile
plugins/sort/Makefile
plugins/sort/sort.plugin.desktop.in
plugins/spell/Makefile
plugins/spell/org.mate.pluma.plugins.spell.gschema.xml
plugins/spell/spell.plugin.desktop.in
plugins/taglist/Makefile
plugins/taglist/taglist.plugin.desktop.in
plugins/terminal/Makefile
plugins/terminal/terminal.plugin.desktop.in
plugins/terminal/org.mate.pluma.plugins.terminal.gschema.xml
plugins/terminal/pluma-terminal.metainfo.xml.in
plugins/time/Makefile
plugins/time/org.mate.pluma.plugins.time.gschema.xml
plugins/time/time.plugin.desktop.in
plugins/trailsave/Makefile
plugins/trailsave/trailsave.plugin.desktop.in
po/Makefile.in
tests/Makefile
])

AC_OUTPUT

echo "
Configure summary:

	${PACKAGE_STRING}
	`echo $PACKAGE_STRING | sed "s/./=/g"`

	Source code location:	${srcdir}
	Compiler:		${CC}
	Compiler flags:		${CFLAGS}
	Warning flags:		${WARN_CFLAGS}
	Spell Plugin enabled:	$enable_enchant
	Gvfs metadata enabled:	$enable_gvfs_metadata
	GObject Introspection:	${have_introspection}
	GIRepository 2.0:	${have_girepository_2:-no}
	Tests enabled:		$enable_tests
"

if expr ${PLUMA_MINOR_VERSION} % 2 > /dev/null; then
	stable_version=`expr ${PLUMA_MINOR_VERSION} - 1`

cat << EOF
 *** IMPORTANT ***

This is an unstable version of pluma.
It is for test purposes only.

Please, DO NOT use it in a production environment.
It will probably crash and you will lose your data.

If you are looking for a stable release of pluma, either download
it from:

  http://pub.mate-desktop.org/release/1.${stable_version}/

Or checkout the 1.${stable_version} branch of the pluma module from:

  git://github.com/mate-desktop/pluma.git

Thanks,
The pluma team

 *** END OF WARNING ***
EOF

fi
