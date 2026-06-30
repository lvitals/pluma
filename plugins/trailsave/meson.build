shared_module('trailsave',
  sources: [
    'pluma-trail-save-plugin.c',
    'pluma-trail-save-plugin.h',
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
  input: 'trailsave.plugin.desktop.in.in',
  output: 'trailsave.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'trailsave.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)
