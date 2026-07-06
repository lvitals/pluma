subdir('pythonconsole')

plugin_desktop_in = configure_file(
  input: 'pythonconsole.plugin.desktop.in.in',
  output: 'pythonconsole.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'pythonconsole.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

i18n.merge_file(
  input: 'org.mate.pluma.plugins.pythonconsole.gschema.xml.in',
  output: 'org.mate.pluma.plugins.pythonconsole.gschema.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas'),
)
