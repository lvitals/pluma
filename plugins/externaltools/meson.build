subdir('data')
subdir('tools')

shared_module('externaltools',
  sources: [
    'pluma-tool-library.c',
    'pluma-tool-library.h',
    'pluma-tool-link-parser.c',
    'pluma-tool-link-parser.h',
    'pluma-tool-file-lookup.c',
    'pluma-tool-file-lookup.h',
    'pluma-tool-capture.c',
    'pluma-tool-capture.h',
    'pluma-tool-output-panel.c',
    'pluma-tool-output-panel.h',
    'pluma-tool-functions.c',
    'pluma-tool-functions.h',
    'pluma-tool-manager.c',
    'pluma-tool-manager.h',
    'pluma-externaltools-plugin.c',
    'pluma-externaltools-plugin.h',
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
  input: 'externaltools.plugin.desktop.in.in',
  output: 'externaltools.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'externaltools.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)
