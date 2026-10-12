subdir('de/LC_MESSAGES')

i18n_tests = executable('test-i18n',
  ['../test-i18n.c', '../../src/i18n.c', '../../src/language.c', '../../src/build-metadata.c'],
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

language_startup_tests = executable('test-language-startup',
  ['../test-language-startup.c', '../../src/i18n.c', '../../src/language.c'],
  include_directories: inc, dependencies: [dependency('gio-2.0'), intl_dep], install: false)
foreach scenario : [
  ['first-launch', 'absent', 'C.UTF-8', 'de', 'de', 'Übersicht', 'private'],
  ['system', 'system', 'C.UTF-8', 'de', 'de', 'Übersicht', 'private'],
  ['manual-c', 'de', 'C', 'en', 'de', 'Übersicht', 'private'],
  ['manual-posix', 'de', 'POSIX', 'en', 'de', 'Übersicht', 'private'],
  ['manual-invalid-locale', 'de', 'smashed_invalid_locale', 'en', 'de', 'Übersicht', 'private'],
  ['manual-en', 'en', 'en_US.UTF-8', 'de', 'en', 'Overview', 'normal'],
  ['manual-zh', 'zh_CN', 'C', 'de', 'zh_CN', 'Overview', 'private'],
  ['unsupported', 'unsupported-id', 'C', 'de', 'en', 'Overview', 'normal'],
  ['system-posix', 'system', 'C', 'de', 'en', 'Overview', 'normal'],
  ['first-unsupported', 'absent', 'C.UTF-8', 'fr', 'en', 'Overview', 'normal'],
]
  if scenario[6] != 'private' or private_message_locale
    test('language-startup-' + scenario[0], language_startup_tests,
      args: [scenario[1], scenario[4], scenario[5], scenario[6]],
      env: {'LC_ALL': scenario[2], 'LANGUAGE': scenario[3],
            'SMASHED_PUMPKIN_LOCALEDIR': meson.current_build_dir(),
            'SMASHED_PUMPKIN_MESSAGE_LOCALEDIR': meson.project_build_root() / 'src/message-locales'},
      depends: [i18n_test_catalog, message_locale])
  endif
endforeach

xgettext = find_program('xgettext', native: true)
test('i18n-sources', find_program('python3'),
  args: [meson.project_source_root() / 'tests/test-i18n-sources.py',
         meson.project_source_root(), xgettext.full_path()],
  env: {'PYTHONDONTWRITEBYTECODE': '1'})
