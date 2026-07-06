shared_module('sort',
  sources: [
    'pluma-sort-plugin.c',
    'pluma-sort-plugin.h',
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
  input: 'sort.plugin.desktop.in.in',
  output: 'sort.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'sort.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

install_data('sort.ui',
  install_dir: join_paths(pkgdatadir, 'plugins/sort')
)
