subdir('data')

shared_module(
  'snippets',
  sources: files('pluma-snippets-plugin.c'),
  include_directories: [
    include_directories('../..'),
    include_directories('../../pluma'),
  ],
  dependencies: pluma_deps,
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

plugin_desktop_in = configure_file(
  input: 'snippets.plugin.desktop.in.in',
  output: 'snippets.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'snippets.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)
