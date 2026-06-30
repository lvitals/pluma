subdir('dialogs')
subdir('mate-submodules/libegg')

inst_h_files = [
  'pluma-app.h',
  'pluma-app-activatable.h',
  'pluma-application.h',
  'pluma-commands.h',
  'pluma-debug.h',
  'pluma-document.h',
  'pluma-encodings.h',
  'pluma-encodings-combo-box.h',
  'pluma-help.h',
  'pluma-message-bus.h',
  'pluma-message-type.h',
  'pluma-message.h',
  'pluma-notebook.h',
  'pluma-panel.h',
  'pluma-progress-message-area.h',
  'pluma-statusbar.h',
  'pluma-status-combo-box.h',
  'pluma-tab.h',
  'pluma-utils.h',
  'pluma-view.h',
  'pluma-view-activatable.h',
  'pluma-window.h',
  'pluma-window-activatable.h',
]

if not enable_gvfs_metadata
  inst_h_files += 'pluma-metadata-manager.h'
endif

noinst_h_files = [
  'pluma-close-button.h',
  'pluma-dirs.h',
  'pluma-document-input-stream.h',
  'pluma-document-loader.h',
  'pluma-document-output-stream.h',
  'pluma-document-saver.h',
  'pluma-documents-panel.h',
  'pluma-file-chooser-dialog.h',
  'pluma-git-panel.h',
  'pluma-history-entry.h',
  'pluma-io-error-message-area.h',
  'pluma-language-manager.h',
  'pluma-pango.h',
  'pluma-plugins-engine.h',
  'pluma-print-job.h',
  'pluma-print-preview.h',
  'pluma-project-search-panel.h',
  'pluma-session.h',
  'pluma-settings.h',
  'pluma-smart-charset-converter.h',
  'pluma-style-scheme-manager.h',
  'pluma-tab-label.h',
  'plumatextregion.h',
  'pluma-ui.h',
  'pluma-window-private.h',
]

libpluma_c_files = [
  'pluma-app.c',
  'pluma-app-activatable.c',
  'pluma-application.c',
  'pluma-close-button.c',
  'pluma-commands-documents.c',
  'pluma-commands-edit.c',
  'pluma-commands-file.c',
  'pluma-commands-file-print.c',
  'pluma-commands-help.c',
  'pluma-commands-search.c',
  'pluma-commands-view.c',
  'pluma-debug.c',
  'pluma-dirs.c',
  'pluma-document.c',
  'pluma-document-input-stream.c',
  'pluma-document-loader.c',
  'pluma-document-output-stream.c',
  'pluma-document-saver.c',
  'pluma-documents-panel.c',
  'pluma-encodings.c',
  'pluma-encodings-combo-box.c',
  'pluma-file-chooser-dialog.c',
  'pluma-help.c',
  'pluma-history-entry.c',
  'pluma-io-error-message-area.c',
  'pluma-language-manager.c',
  'pluma-message-bus.c',
  'pluma-message-type.c',
  'pluma-message.c',
  'pluma-notebook.c',
  'pluma-panel.c',
  'pluma-pango.c',
  'pluma-plugins-engine.c',
  'pluma-print-job.c',
  'pluma-print-preview.c',
  'pluma-project-search-panel.c',
  'pluma-progress-message-area.c',
  'pluma-session.c',
  'pluma-settings.c',
  'pluma-smart-charset-converter.c',
  'pluma-statusbar.c',
  'pluma-status-combo-box.c',
  'pluma-style-scheme-manager.c',
  'pluma-tab.c',
  'pluma-tab-label.c',
  'pluma-utils.c',
  'pluma-view.c',
  'pluma-view-activatable.c',
  'pluma-window.c',
  'pluma-window-activatable.c',
  'plumatextregion.c',
  'bacon-message-connection.c',
]

if not enable_gvfs_metadata
  libpluma_c_files += 'pluma-metadata-manager.c'
endif

if enable_git
  libpluma_c_files += [
    'pluma-git-panel.c',
    'pluma-git-commit.c',
    'pluma-git-status-parser.c',
  ]
endif

# enum-types templates
pluma_enum_types = gnome.mkenums('pluma-enum-types',
  sources: inst_h_files,
  h_template: 'pluma-enum-types.h.template',
  c_template: 'pluma-enum-types.c.template',
)

pluma_c_args = [
  '-DDATADIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
  '-DPLUMA_DATADIR="@0@/pluma"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
  '-DPLUMA_LOCALEDIR="@0@/locale"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
  '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
  '-DPLUMA_LIBDIR="@0@/pluma"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
]

pluma_include_dirs = [
  include_directories('.'),
  include_directories('..'),
  include_directories('mate-submodules/libegg'),
]

libpluma = shared_library('pluma',
  sources: libpluma_c_files + noinst_h_files + inst_h_files + [pluma_enum_types],
  dependencies: pluma_deps,
  c_args: pluma_c_args,
  include_directories: pluma_include_dirs,
  link_with: [libdialogs, libegg],
  install: true,
  install_dir: pkglibdir,
)

pluma_exe = executable('pluma',
  sources: 'pluma.c',
  dependencies: pluma_deps,
  c_args: pluma_c_args,
  include_directories: pluma_include_dirs,
  link_with: libpluma,
  install: true,
  export_dynamic: true,
  install_rpath: join_paths(get_option('prefix'), pkglibdir),
)

# Header files
install_headers(inst_h_files, subdir: 'pluma')

# UI and metadata files
install_data([
    'pluma-ui.xml',
    'pluma-print-preferences.ui',
  ],
  install_dir: join_paths(get_option('datadir'), 'pluma/ui')
)

install_data('pluma.about',
  install_dir: join_paths(get_option('datadir'), 'pluma')
)

# GObject Introspection
if enable_introspection
  # Pluma-1.0.gir
  pluma_gir = gnome.generate_gir(libpluma,
    sources: inst_h_files + libpluma_c_files + [pluma_enum_types],
    namespace: 'Pluma',
    nsversion: '1.0',
    includes: ['Gtk-3.0', 'GtkSource-4'],
    install: true,
    install_dir_gir: join_paths(get_option('datadir'), 'gir-1.0'),
    install_dir_typelib: join_paths(get_option('libdir'), 'girepository-1.0'),
    extra_args: ['--warn-all', '-I' + meson.project_source_root()],
  )
endif
