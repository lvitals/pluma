shared_module('markdown-preview',
  sources: files('pluma-markdown-preview-plugin.c'),
  dependencies: pluma_deps,
  include_directories: [include_directories('../..'), include_directories('../../pluma')],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

markdown_preview_desktop = configure_file(
  input: 'markdown-preview.plugin.desktop.in.in',
  output: 'markdown-preview.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: markdown_preview_desktop,
  output: 'markdown-preview.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

markdown_preview_metainfo_in = configure_file(
  input: 'pluma-markdown-preview.metainfo.xml.in.in',
  output: 'pluma-markdown-preview.metainfo.xml.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: markdown_preview_metainfo_in,
  output: 'pluma-markdown-preview.metainfo.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)
