# Bundled libpeas lua5.1 plugin loader, patched to require the vendored
# LuaGObject (subprojects/LuaGObject) instead of the system 'lgi', and to
# point Lua's package.path/cpath at Pluma's own private runtime bundle
# instead of any system-wide Lua install. See VENDORED.md and the "Lua
# plugin support" project notes for why: the distro's lgi is linked
# against the legacy libgirepository-1.0, which crashes the whole process
# ("cannot register existing type 'GIRepository'") the instant it's
# loaded alongside Pluma/libpeas' libgirepository-2.0.

lua_runtime_reldir = join_paths('plugin-loaders', 'lua5.1', 'lua-runtime')
lua_runtime_install_dir = join_paths(pkglibdir, lua_runtime_reldir)
lua_runtime_abs_dir = join_paths(get_option('prefix'), lua_runtime_install_dir)

subproject('LuaGObject', default_options: [
  'lua-pc=lua5.1',
  'runtime-install-dir=' + lua_runtime_install_dir,
])

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
