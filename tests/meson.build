test_progs = [
  'action-migration',
  'smart-converter',
  'document-input-stream',
  'document-output-stream',
  'document-loader',
  'document-saver',
]

foreach t : test_progs
  exe = executable(t, t + '.c',
    dependencies: pluma_deps,
    include_directories: [
      include_directories('..'),
      include_directories('../pluma'),
    ],
    link_with: libpluma,
  )
  test(t, exe)
endforeach

git_program = find_program('git', required: false)
shell_program = find_program('sh')
if git_program.found() and enable_git
  git_diff_test = executable('git-diff',
    'git-diff.c',
    '../pluma/pluma-git-diff.c',
    dependencies: pluma_deps,
    include_directories: include_directories('../pluma'),
  )
  test('git-diff', git_diff_test)
  git_commit_test = executable('git-commit',
    'git-commit.c',
    '../pluma/pluma-git-commit.c',
    dependencies: pluma_deps,
    include_directories: include_directories('../pluma'),
  )
  test('git-commit', git_commit_test)
  git_parser_test = executable('git-status-parser',
    'git-status-parser.c',
    '../pluma/pluma-git-status-parser.c',
    dependencies: pluma_deps,
    include_directories: include_directories('../pluma'),
  )
  test('git-status-parser', git_parser_test)
  test('git-integration', shell_program,
    args: files('git-integration.sh'),
    env: ['GIT_CONFIG_NOSYSTEM=1'],
  )
endif
