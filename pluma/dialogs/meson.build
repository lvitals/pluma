libdialogs = static_library('dialogs',
  sources: [
    'pluma-preferences-dialog.c',
    'pluma-preferences-dialog.h',
    'pluma-close-confirmation-dialog.c',
    'pluma-close-confirmation-dialog.h',
    'pluma-encodings-dialog.c',
    'pluma-encodings-dialog.h',
    'pluma-search-dialog.c',
    'pluma-search-dialog.h',
  ],
  dependencies: pluma_deps,
  include_directories: [
    include_directories('../..'),
    include_directories('..'),
  ],
  c_args: [
    '-DPLUMA_DATADIR="@0@/pluma"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
  ],
)

install_data([
    'pluma-encodings-dialog.ui',
    'pluma-preferences-dialog.ui',
    'pluma-search-dialog.ui',
  ],
  install_dir: join_paths(get_option('datadir'), 'pluma/ui')
)
