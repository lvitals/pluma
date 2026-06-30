noinst_h_files = [
  'pluma-file-bookmarks-store.h',
  'pluma-file-browser-store.h',
  'pluma-file-browser-view.h',
  'pluma-file-browser-widget.h',
  'pluma-file-browser-error.h',
  'pluma-file-browser-utils.h',
  'pluma-file-browser-plugin.h',
  'pluma-file-browser-messages.h',
]

filebrowser_sources = [
  'pluma-file-bookmarks-store.c',
  'pluma-file-browser-store.c',
  'pluma-file-browser-view.c',
  'pluma-file-browser-widget.c',
  'pluma-file-browser-utils.c',
  'pluma-file-browser-plugin.c',
  'pluma-file-browser-messages.c',
]

mkenums = find_program('glib-mkenums')

filebrowser_enum_h = custom_target('pluma-file-browser-enum-types.h',
  input: ['pluma-file-browser-enum-types.h.template'] + noinst_h_files,
  output: 'pluma-file-browser-enum-types.h',
  command: [
    mkenums,
    '--template', '@INPUT0@',
    '@INPUT1@', '@INPUT2@', '@INPUT3@', '@INPUT4@',
    '@INPUT5@', '@INPUT6@', '@INPUT7@', '@INPUT8@',
  ],
  capture: true,
  install: false,
)

mkenums_concat_py = '''
import subprocess
import sys

mkenums = sys.argv[1]
template1 = sys.argv[2]
template2 = sys.argv[3]
headers = sys.argv[4:]

out1 = subprocess.check_output([mkenums, '--template', template1] + headers)
out2 = subprocess.check_output([mkenums, '--template', template2] + headers)

sys.stdout.buffer.write(out1)
sys.stdout.buffer.write(out2)
'''

filebrowser_enum_c = custom_target('pluma-file-browser-enum-types.c',
  input: [
    'pluma-file-browser-enum-types.c.template',
    'pluma-file-browser-enum-register.c.template',
  ] + noinst_h_files,
  output: 'pluma-file-browser-enum-types.c',
  command: [
    py_installation, '-c', mkenums_concat_py,
    mkenums.full_path(),
    '@INPUT0@',
    '@INPUT1@',
    '@INPUT2@', '@INPUT3@', '@INPUT4@', '@INPUT5@',
    '@INPUT6@', '@INPUT7@', '@INPUT8@', '@INPUT9@',
  ],
  capture: true,
  install: false,
)

shared_module('filebrowser',
  sources: filebrowser_sources + [filebrowser_enum_h, filebrowser_enum_c],
  dependencies: pluma_deps,
  include_directories: [
    include_directories('../..'),
    include_directories('../../pluma'),
  ],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

# metadata/plugin config
plugin_desktop_in = configure_file(
  input: 'filebrowser.plugin.desktop.in.in',
  output: 'filebrowser.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'filebrowser.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)

# GSettings schema
i18n.merge_file(
  input: 'org.mate.pluma.plugins.filebrowser.gschema.xml.in',
  output: 'org.mate.pluma.plugins.filebrowser.gschema.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas'),
)

# UI data
install_data('pluma-file-browser-widget-ui.xml',
  install_dir: join_paths(pkgdatadir, 'plugins/filebrowser')
)
