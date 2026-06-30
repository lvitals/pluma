shared_module('time',
  sources: [
    'pluma-time-plugin.c',
    'pluma-time-plugin.h',
  ],
  dependencies: pluma_deps,
  include_directories: [
    include_directories('../..'),
    include_directories('../../pluma'),
  ],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

plugin_desktop_in = configure_file(
  input: 'time.plugin.desktop.in.in',
  output: 'time.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'time.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)

i18n.merge_file(
  input: 'org.mate.pluma.plugins.time.gschema.xml.in',
  output: 'org.mate.pluma.plugins.time.gschema.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas'),
)

install_data([
    'pluma-time-dialog.ui',
    'pluma-time-setup-dialog.ui',
  ],
  install_dir: join_paths(pkgdatadir, 'plugins/time')
)
