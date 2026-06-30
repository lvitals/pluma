tool_merge = files('../scripts/pluma-tool-merge.pl')
tools = [
  'build',
  'open-terminal-here',
  'remove-trailing-spaces',
  'run-command',
  'search-recursive',
  'switch-c',
]

foreach t : tools
  desktop_in = t + '.desktop.in'
  desktop = i18n.merge_file(
    input: desktop_in,
    output: t + '.desktop',
    type: 'desktop',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    install: false,
  )
  
  custom_target(t,
    input: [t + '.tool.in', desktop],
    output: t,
    command: [find_program('perl'), tool_merge, '-o', '@OUTPUT@', '@INPUT0@', '@INPUT1@'],
    install: true,
    install_dir: join_paths(pkgdatadir, 'plugins/externaltools/tools'),
    install_mode: 'rwxr-xr-x',
  )
endforeach
