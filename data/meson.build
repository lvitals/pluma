# GSettings schema
configure_file(
  input: 'org.mate.pluma.gschema.xml.in',
  output: 'org.mate.pluma.gschema.xml',
  configuration: conf_data_xml,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas'),
)

# File Browser is built into the core (not a plugin), but keeps its historic
# schema id so existing users don't lose their saved bookmarks/root folder.
configure_file(
  input: 'org.mate.pluma.plugins.filebrowser.gschema.xml.in',
  output: 'org.mate.pluma.plugins.filebrowser.gschema.xml',
  configuration: conf_data_xml,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas'),
)

# Desktop file
desktop_in = configure_file(
  input: 'pluma.desktop.in.in',
  output: 'pluma.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: desktop_in,
  output: 'pluma.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

# Appdata (Metainfo) file
appdata_in = configure_file(
  input: 'pluma.appdata.xml.in.in',
  output: 'pluma.appdata.xml.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: appdata_in,
  output: 'pluma.appdata.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)

# Pkg-config file
pc_data = configuration_data()
pc_data.set('prefix', get_option('prefix'))
pc_data.set('exec_prefix', '${prefix}')
pc_data.set('libdir', join_paths('${prefix}', get_option('libdir')))
pc_data.set('includedir', join_paths('${prefix}', get_option('includedir')))
pc_data.set('VERSION', meson.project_version())

configure_file(
  input: 'pluma.pc.in',
  output: 'pluma.pc',
  configuration: pc_data,
  install: true,
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
)

# Bugreport script
configure_file(
  input: 'pluma-bugreport.sh.in',
  output: 'pluma-bugreport.sh',
  configuration: conf_data,
  install: true,
  install_dir: join_paths(get_option('libexecdir'), 'pluma'),
  install_mode: 'rwxr-xr-x',
)

# Man page
install_man('pluma.1')

# GtkSourceView Styles
install_data(
  'styles/cupertino-dark.xml',
  'styles/classic.xml',
  'styles/cobalt.xml',
  'styles/kate.xml',
  'styles/oblivion.xml',
  'styles/solarized-dark.xml',
  'styles/solarized-light.xml',
  'styles/tango.xml',
  install_dir: join_paths(get_option('datadir'), 'pluma/styles')
)
