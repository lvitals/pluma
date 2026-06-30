shared_module('modelines',
  sources: [
    'pluma-modeline-plugin.c',
    'pluma-modeline-plugin.h',
    'modeline-parser.c',
    'modeline-parser.h',
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
  input: 'modelines.plugin.desktop.in.in',
  output: 'modelines.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'modelines.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)

install_data('language-mappings',
  install_dir: join_paths(pkgdatadir, 'plugins/modelines')
)
