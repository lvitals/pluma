shared_module('docinfo',
  sources: [
    'pluma-docinfo-plugin.c',
    'pluma-docinfo-plugin.h',
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
  input: 'docinfo.plugin.desktop.in.in',
  output: 'docinfo.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'docinfo.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)

install_data('docinfo.ui',
  install_dir: join_paths(pkgdatadir, 'plugins/docinfo')
)
