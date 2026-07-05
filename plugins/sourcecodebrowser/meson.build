subdir('icons')

shared_module('sourcecodebrowser',
  sources: [
    'pluma-sourcecodebrowser-plugin.c',
    'pluma-sourcecodebrowser-plugin.h',
    'pluma-sourcecodebrowser-plugin-panel.c',
    'pluma-sourcecodebrowser-plugin-panel.h',
    'pluma-sourcecodebrowser-plugin-ctags.c',
    'pluma-sourcecodebrowser-plugin-ctags.h',
  ],
  dependencies: pluma_deps,
  include_directories: [
    include_directories('../..'),
    include_directories('../../pluma'),
  ],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

sourcecodebrowser_desktop = configure_file(
  input: 'sourcecodebrowser.plugin.desktop.in.in',
  output: 'sourcecodebrowser.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: sourcecodebrowser_desktop,
  output: 'sourcecodebrowser.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

configure_file(
  input: 'org.mate.pluma.plugins.sourcecodebrowser.gschema.xml.in',
  output: 'org.mate.pluma.plugins.sourcecodebrowser.gschema.xml',
  configuration: conf_data_xml,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'),
)
