subdir('de/LC_MESSAGES')

i18n_tests = executable('test-i18n',
  ['../test-i18n.c', '../../src/i18n.c', '../../src/build-metadata.c'],
  include_directories: inc,
  dependencies: [dependency('glib-2.0'), intl_dep, cjson_dep],
  install: false)

# Each environment gets a fresh process: gettext and GLib cache locale data.
# Use an available non-C UTF-8 locale: some libc versions suppress LANGUAGE
# even for C.UTF-8. The German catalog does not need a German system locale.
foreach scenario : [
  ['translated', 'en_US.UTF-8', 'de', 'translated'],
  ['english', 'C', 'de', 'english'],
  ['fallback', 'C.UTF-8', 'zz_ZZ', 'english'],
  ['fallback-chain', 'en_US.UTF-8', 'zz_ZZ:de', 'translated'],
  ['invalid-locale', 'smashed_invalid_locale', 'de', 'english'],
]
  test('i18n-' + scenario[0], i18n_tests,
    args: [scenario[3]],
    env: {'LC_ALL': scenario[1], 'LANGUAGE': scenario[2], 'TZ': 'UTC',
          'SMASHED_PUMPKIN_LOCALEDIR': meson.current_build_dir()},
    depends: i18n_test_catalog)
endforeach

# Missing-catalog fallback must run with translations enabled, not a C locale
# that would disable lookups regardless of the catalog directory.
test('i18n-missing-catalog', i18n_tests,
  args: ['missing-catalog'],
  env: {'LC_ALL': 'en_US.UTF-8', 'LANGUAGE': 'de', 'TZ': 'UTC',
        'SMASHED_PUMPKIN_LOCALEDIR': meson.current_build_dir() / 'missing'},
  depends: i18n_test_catalog)
test('i18n-relative-path', i18n_tests,
  args: ['english'],
  env: {'LC_ALL': 'C', 'LANGUAGE': 'de', 'TZ': 'UTC',
        'SMASHED_PUMPKIN_LOCALEDIR': 'relative-catalog-directory'})

xgettext = find_program('xgettext', native: true)
test('i18n-sources', find_program('python3'),
  args: [meson.project_source_root() / 'tests/test-i18n-sources.py',
         meson.project_source_root(), xgettext.full_path()],
  env: {'PYTHONDONTWRITEBYTECODE': '1'})
