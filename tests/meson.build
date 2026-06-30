test_progs = [
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
