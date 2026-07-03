# A parent project (Pluma) that wants this installed somewhere other
# than the system-wide Lua paths - e.g. into its own private plugin
# loader directory - can set 'runtime-install-dir' via the subproject's
# default_options instead of relying on lua_path/lua_cpath below.
runtime_install_dir = get_option('runtime-install-dir')
if runtime_install_dir != ''
  install_dir_core = join_paths(runtime_install_dir, 'LuaGObject')
  install_dir_lua = join_paths(runtime_install_dir, 'LuaGObject')
else
  install_dir_core = join_paths(lua_cpath, 'LuaGObject')
  install_dir_lua = join_paths(lua_path, 'LuaGObject')
endif

lua_gobject_core = shared_module('lua_gobject_core',
  sources: [
    'buffer.c',
    'callable.c',
    'core.c',
    'gi.c',
    'marshal.c',
    'object.c',
    'record.c',
  ],
  dependencies: [
    lua_dep,
    gi_dep,
    dependency('libffi'),
    dependency('gmodule-2.0'),
  ],
  name_prefix: '',
  install: true,
  install_dir: install_dir_core,
)

install_data([
  'class.lua',
  'component.lua',
  'core.lua',
  'enum.lua',
  'ffi.lua',
  'init.lua',
  'log.lua',
  'namespace.lua',
  'package.lua',
  'record.lua'
], install_dir: install_dir_lua)

install_subdir('override', install_dir: install_dir_lua)

conf = configuration_data()
conf.set('VERSION', meson.project_version())
configure_file(
  input: 'version.lua.in',
  output: 'version.lua',
  configuration: conf,
  install: true,
  install_dir: install_dir_lua,
)
