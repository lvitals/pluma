python3_embed_dep = dependency('python3-embed')
pygobject_dep = dependency('pygobject-3.0')

pygobject_version = pygobject_dep.version().split('.')
python_loader_resources = gnome.compile_resources(
  'peas-python3-resources',
  'peas-python3.gresource.xml',
  export: true,
)

shared_module('python3loader',
  'peas-plugin-loader-python.c',
  'peas-python-internal.c',
  python_loader_resources,
  dependencies: [libpeas_dep, python3_embed_dep, pygobject_dep],
  include_directories: [include_directories('.'), include_directories('../..')],
  c_args: [
    '-DPEAS_LOCALEDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('localedir'))),
    '-DPYGOBJECT_MAJOR_VERSION=@0@'.format(pygobject_version[0]),
    '-DPYGOBJECT_MINOR_VERSION=@0@'.format(pygobject_version[1]),
    '-DPYGOBJECT_MICRO_VERSION=@0@'.format(pygobject_version[2]),
  ],
  install: true,
  install_dir: join_paths(pkglibdir, 'plugin-loaders/python3'),
)
