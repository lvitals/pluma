project('pluma', 'c',
  version: '1.28.1',
  meson_version: '>= 0.56.0',
  default_options: [
    'warning_level=1',
    'c_std=gnu99',
  ]
)

# Version definitions
version_array = meson.project_version().split('.')
pluma_major_version = version_array[0].to_int()
pluma_minor_version = version_array[1].to_int()
pluma_micro_version = version_array[2].to_int()

# Imports
gnome = import('gnome')
i18n = import('i18n')
python = import('python')
fs = import('fs')

# Dependencies
glib_req = '>= 2.50.0'
gtk_req = '>= 3.22.0'
gtksourceview_req = '>= 4.0.2'
mate_desktop_req = '>= 1.27.1'
libpeas_req = '>= 1.2.0'

libxml_dep = dependency('libxml-2.0', version: '>= 2.5.0')
glib_dep = dependency('glib-2.0', version: glib_req)
gmodule_dep = dependency('gmodule-2.0')
gthread_dep = dependency('gthread-2.0', version: '>= 2.13.0')
gio_dep = dependency('gio-2.0', version: glib_req)
gtk_dep = dependency('gtk+-3.0', version: gtk_req)
gtksourceview_dep = dependency('gtksourceview-4', version: gtksourceview_req)
libpeas_dep = dependency('libpeas-1.0', version: libpeas_req)
libpeas_gtk_dep = dependency('libpeas-gtk-1.0', version: libpeas_req)
mate_desktop_dep = dependency('mate-desktop-2.0', version: mate_desktop_req)
x11_dep = dependency('x11')
sm_dep = dependency('sm', version: '>= 1.0.0')
ice_dep = dependency('ice', version: '>= 1.0.0')

# The Quick Open, Python Console, External Tools and Snippets plugins use the
# optional Python 3 loader shipped separately by some distributions.
libpeas_loaders_dir = join_paths(
  libpeas_dep.get_variable(pkgconfig: 'libdir'),
  'libpeas-1.0',
  'loaders',
)
build_bundled_python_loader = not fs.exists(join_paths(libpeas_loaders_dir, 'python3/libpython3loader.so')) and not fs.exists(join_paths(libpeas_loaders_dir, 'libpython3loader.so'))
if build_bundled_python_loader
  message('libpeas Python 3 loader not found; building the bundled loader for Python plugins')
endif

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

# Optional runtime helpers used by Pluma and pluma-plugins. Report missing
# components during configuration instead of failing later at runtime.
ctags_program = find_program('ctags', required: false)
if not ctags_program.found()
  warning('ctags not found: the Source Code Browser plugin will be unavailable. Install a package that provides the ctags executable.')
endif

ripgrep_program = find_program('rg', required: false)
if not ripgrep_program.found()
  warning('ripgrep not found: project search will use the slower grep fallback. Install a package that provides the rg executable for best performance.')
endif

optional_enchant_backends = {
  'aspell': 'aspell',
  'hspell': 'hspell',
  'hunspell-1.7': 'hunspell',
  'nuspell': 'nuspell',
  'voikko': 'libvoikko',
}
foreach library_name, package_name : optional_enchant_backends
  backend_library = cc.find_library(library_name, required: false)
  if not backend_library.found()
    warning('@0@ not found: Enchant cannot load this spell-checking backend. Install a package that provides the @1@ backend.'.format(library_name, package_name))
  endif
endforeach

pluma_deps = [
  libxml_dep,
  glib_dep,
  gmodule_dep,
  gthread_dep,
  gio_dep,
  gtk_dep,
  gtksourceview_dep,
  libpeas_dep,
  libpeas_gtk_dep,
  mate_desktop_dep,
  x11_dep,
  ice_dep,
  m_dep,
]

# Gvfs-metadata option
enable_gvfs_metadata = get_option('gvfs-metadata')

# Spell option
enable_spell = get_option('spell')
enchant_dep = dependency('', required: false)
iso_codes_dep = dependency('', required: false)
iso_codes_prefix = ''
if enable_spell
  enchant_dep = dependency('enchant-2', version: '>= 1.6.0', required: false)
  if not enchant_dep.found()
    enchant_dep = dependency('enchant', version: '>= 1.6.0', required: true)
  endif
  iso_codes_dep = dependency('iso-codes', version: '>= 0.35', required: true)
  iso_codes_prefix = iso_codes_dep.get_variable(pkgconfig: 'prefix')
endif

# Introspection option
enable_introspection = get_option('introspection')

# Python 3 dependency (required for plugins)
py_installation = python.find_installation('python3')

# Config.h generation
conf_data = configuration_data()
conf_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf_data.set('PLUMA_MAJOR_VERSION', pluma_major_version)
conf_data.set('PLUMA_MINOR_VERSION', pluma_minor_version)
conf_data.set('PLUMA_MICRO_VERSION', pluma_micro_version)
conf_data.set_quoted('VERSION', meson.project_version())
conf_data.set_quoted('PACKAGE_VERSION', meson.project_version())
conf_data.set_quoted('PACKAGE_URL', 'http://mate-desktop.org')
conf_data.set('TOOLBAR_STYLE', 'PLUMA_TOOLBAR_SYSTEM')

if build_bundled_python_loader
  conf_data.set('HAVE_BUNDLED_PYTHON_LOADER', 1)
  conf_data.set_quoted('PLUMA_PLUGIN_LOADERS_DIR', join_paths(get_option('prefix'), get_option('libdir'), meson.project_name(), 'plugin-loaders'))
endif

if enable_gvfs_metadata
  conf_data.set('ENABLE_GVFS_METADATA', 1)
endif

if enable_spell
  conf_data.set('HAVE_ISO_CODES', 1)
  conf_data.set_quoted('ISO_CODES_PREFIX', iso_codes_prefix)
endif

if enable_introspection
  conf_data.set('HAVE_INTROSPECTION', 1)
  have_girepository_2 = false
  girepository_2_dep = dependency('girepository-2.0', required: false)
  if girepository_2_dep.found()
    r = run_command('pkg-config', '--print-requires', 'libpeas-1.0', check: false)
    if r.returncode() == 0 and r.stdout().contains('girepository-2.0')
      conf_data.set('HAVE_GIREPOSITORY_2', 1)
      have_girepository_2 = true
    endif
  endif
endif

# Paths
pkglibdir = join_paths(get_option('libdir'), meson.project_name())
pkgdatadir = join_paths(get_option('datadir'), meson.project_name())
libexecdir = get_option('libexecdir')
full_libexecdir = join_paths(get_option('prefix'), libexecdir)

conf_data.set_quoted('FULL_LIBEXECDIR', full_libexecdir)

configure_file(
  output: 'config.h',
  configuration: conf_data
)

conf_data_xml = configuration_data()
conf_data_xml.set('GETTEXT_PACKAGE', meson.project_name())
conf_data_xml.set('VERSION', meson.project_version())
conf_data_xml.set('PACKAGE_VERSION', meson.project_version())
conf_data_xml.set('PACKAGE_URL', 'http://mate-desktop.org')
conf_data_xml.set('FULL_LIBEXECDIR', full_libexecdir)
conf_data_xml.set('TOOLBAR_STYLE', 'PLUMA_TOOLBAR_SYSTEM')

# Compiler warning flags
warn_flags = [
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wpointer-arith',
  '-Wstrict-prototypes',
  '-Wno-sign-compare',
]
add_project_arguments(cc.get_supported_arguments(warn_flags), language: 'c')
add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

# Subdirs
subdir('po')
subdir('pixmaps')
subdir('data')
subdir('pluma')
subdir('plugins')
if get_option('tests')
  subdir('tests')
endif
if get_option('gtk_doc')
  subdir('docs')
endif
subdir('help')

# Post install script to compile schemas & update icon cache
gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: false,
)
