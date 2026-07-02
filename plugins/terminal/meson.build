shared_module('terminal',
  sources: files('pluma-terminal-plugin.c'),
  dependencies: pluma_deps + [vte_dep],
  include_directories: [include_directories('../..'), include_directories('../../pluma')],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

terminal_desktop = configure_file(
  input: 'terminal.plugin.desktop.in.in',
  output: 'terminal.plugin.desktop.in',
  configuration: conf_data_xml,
)
i18n.merge_file(
  input: terminal_desktop,
  output: 'terminal.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)
configure_file(
  input: 'org.mate.pluma.plugins.terminal.gschema.xml.in',
  output: 'org.mate.pluma.plugins.terminal.gschema.xml',
  configuration: conf_data_xml,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'),
)

terminal_metainfo_in = configure_file(
  input: 'pluma-terminal.metainfo.xml.in.in',
  output: 'pluma-terminal.metainfo.xml.in',
  configuration: conf_data_xml,
)
i18n.merge_file(
  input: terminal_metainfo_in,
  output: 'pluma-terminal.metainfo.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)
