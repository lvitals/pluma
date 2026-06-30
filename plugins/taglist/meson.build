shared_module('taglist',
  sources: [
    'pluma-taglist-plugin-parser.c',
    'pluma-taglist-plugin-parser.h',
    'pluma-taglist-plugin-panel.c',
    'pluma-taglist-plugin-panel.h',
    'pluma-taglist-plugin.c',
    'pluma-taglist-plugin.h',
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
  input: 'taglist.plugin.desktop.in.in',
  output: 'taglist.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'taglist.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)

taglist_in_files = [
  'HTML.tags.xml.in',
  'Latex.tags.xml.in',
  'XSLT.tags.xml.in',
  'XUL.tags.xml.in',
]

msgfmt = find_program('msgfmt')

foreach tag_in : taglist_in_files
  base = tag_in.replace('.xml.in', '') # e.g. HTML.tags
  
  xml = custom_target(base,
    input: tag_in,
    output: base,
    command: [
      find_program('env'),
      'GETTEXTDATADIR=' + meson.project_source_root(),
      msgfmt,
      '--xml',
      '--template', '@INPUT@',
      '-d', join_paths(meson.project_source_root(), 'po'),
      '-o', '@OUTPUT@',
    ],
    install: true,
    install_dir: join_paths(pkgdatadir, 'plugins/taglist'),
  )
endforeach
