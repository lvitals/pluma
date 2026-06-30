shared_module('spell',
  sources: [
    'pluma-spell-plugin.c',
    'pluma-spell-plugin.h',
    'pluma-spell-checker.c',
    'pluma-spell-checker.h',
    'pluma-spell-checker-dialog.c',
    'pluma-spell-checker-dialog.h',
    'pluma-spell-checker-language.c',
    'pluma-spell-checker-language.h',
    'pluma-spell-language-dialog.c',
    'pluma-spell-language-dialog.h',
    'pluma-automatic-spell-checker.c',
    'pluma-automatic-spell-checker.h',
    'pluma-spell-utils.c',
    'pluma-spell-utils.h',
  ],
  dependencies: [
    pluma_deps,
    enchant_dep,
    iso_codes_dep,
  ],
  include_directories: [
    include_directories('../..'),
    include_directories('../../pluma'),
  ],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugins'),
)

plugin_desktop_in = configure_file(
  input: 'spell.plugin.desktop.in.in',
  output: 'spell.plugin.desktop.in',
  configuration: conf_data_xml,
)

i18n.merge_file(
  input: plugin_desktop_in,
  output: 'spell.plugin',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(pkgdatadir, 'plugins'),
)

i18n.merge_file(
  input: 'org.mate.pluma.plugins.spell.gschema.xml.in',
  output: 'org.mate.pluma.plugins.spell.gschema.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas'),
)

install_data([
    'spell-checker.ui',
    'languages-dialog.ui',
    'pluma-spell-setup-dialog.ui',
  ],
  install_dir: join_paths(pkgdatadir, 'plugins/spell')
)
