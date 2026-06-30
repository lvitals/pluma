subdir('data')
subdir('tools')

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
  install_dir: join_paths(pkgdatadir, 'plugins'),
)
