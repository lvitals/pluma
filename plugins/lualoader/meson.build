# Bundled libpeas lua5.1 plugin loader, patched to require LuaGObject
# (third_party/LuaGObject, a git submodule - see README.md) instead of
# the system 'lgi', and to point Lua's package.path/cpath at Pluma's own
# private runtime bundle instead of any system-wide Lua install. See
# VENDORED.md and the "Lua plugin support" project notes for why: the
# distro's lgi is linked against the legacy libgirepository-1.0, which
# crashes the whole process ("cannot register existing type
# 'GIRepository'") the instant it's loaded alongside Pluma/libpeas'
# libgirepository-2.0.
#
# third_party/LuaGObject is a pristine, unmodified git submodule (kept
# out of subprojects/ deliberately - Meson's sandboxing forbids
# referencing a nested subproject's files directly, which is exactly
# what this file does below instead of calling subproject()). Its own
# meson.build installs to system-wide Lua paths, which Pluma doesn't
# want, so it's not used at all. This builds and installs LuaGObject's C
# core and Lua files directly, the same way ../lualoader/Makefile.am
# does for the Autotools build.

lua_gobject_srcdir = join_paths(meson.project_source_root(), 'third_party', 'LuaGObject', 'LuaGObject')
lua_gobject_entry = join_paths(meson.project_source_root(), 'third_party', 'LuaGObject', 'LuaGObject.lua')

lua_runtime_reldir = join_paths('plugin-loaders', 'lua5.1', 'lua-runtime')
lua_runtime_install_dir = join_paths(pkglibdir, lua_runtime_reldir)
lua_runtime_abs_dir = join_paths(get_option('prefix'), lua_runtime_install_dir)
lua_gobject_install_dir = join_paths(lua_runtime_install_dir, 'LuaGObject')

girepository2_dep = dependency('girepository-2.0', version: '>= 2.80')

lua_gobject_core = shared_module('lua_gobject_core',
  sources: [
    join_paths(lua_gobject_srcdir, 'buffer.c'),
    join_paths(lua_gobject_srcdir, 'callable.c'),
    join_paths(lua_gobject_srcdir, 'core.c'),
    join_paths(lua_gobject_srcdir, 'gi.c'),
    join_paths(lua_gobject_srcdir, 'marshal.c'),
    join_paths(lua_gobject_srcdir, 'object.c'),
    join_paths(lua_gobject_srcdir, 'record.c'),
  ],
  dependencies: [
    lua51_dep,
    girepository2_dep,
    dependency('libffi'),
    dependency('gmodule-2.0'),
  ],
  name_prefix: '',
  install: true,
  install_dir: lua_gobject_install_dir,
)

install_data([
    join_paths(lua_gobject_srcdir, 'class.lua'),
    join_paths(lua_gobject_srcdir, 'component.lua'),
    join_paths(lua_gobject_srcdir, 'core.lua'),
    join_paths(lua_gobject_srcdir, 'enum.lua'),
    join_paths(lua_gobject_srcdir, 'ffi.lua'),
    join_paths(lua_gobject_srcdir, 'init.lua'),
    join_paths(lua_gobject_srcdir, 'log.lua'),
    join_paths(lua_gobject_srcdir, 'namespace.lua'),
    join_paths(lua_gobject_srcdir, 'package.lua'),
    join_paths(lua_gobject_srcdir, 'record.lua'),
  ],
  install_dir: lua_gobject_install_dir,
)

install_data(lua_gobject_entry, install_dir: lua_runtime_install_dir)

# Mirrors third_party/LuaGObject/LuaGObject/version.lua.in -> version.lua
# (the submodule's own meson.build does this itself, but only when built
# standalone - see the comment above for why we don't use it directly).
lua_gobject_version_conf = configuration_data()
lua_gobject_version_conf.set('VERSION', '0.10.5')
configure_file(
  input: join_paths(lua_gobject_srcdir, 'version.lua.in'),
  output: 'version.lua',
  configuration: lua_gobject_version_conf,
  install: true,
  install_dir: lua_gobject_install_dir,
)

install_subdir(join_paths(lua_gobject_srcdir, 'override'),
  install_dir: lua_gobject_install_dir,
)

lua_loader_resources = gnome.compile_resources(
  'peas-lua-resources',
  join_paths('resources', 'peas-lua.gresource.xml'),
  source_dir: 'resources',
  export: true,
)

# LuaGObject's own _VERSION string; peas_lua_utils_check_version() just
# needs this to match what resources/peas-lua-internal.lua actually
# requires at runtime.
lua_gobject_version = ['0', '10', '5']

shared_module('lua51loader',
  'peas-plugin-loader-lua.c',
  'peas-lua-utils.c',
  'peas-lua-internal.c',
  lua_loader_resources,
  dependencies: [libpeas_dep, lua51_dep],
  include_directories: [include_directories('.'), include_directories('../..')],
  c_args: [
    '-DLGI_MAJOR_VERSION=@0@'.format(lua_gobject_version[0]),
    '-DLGI_MINOR_VERSION=@0@'.format(lua_gobject_version[1]),
    '-DLGI_MICRO_VERSION=@0@'.format(lua_gobject_version[2]),
    '-DPEAS_LUA_EXTRA_PATH="@0@"'.format(lua_runtime_abs_dir),
  ],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugin-loaders', 'lua5.1'),
)
