# Discover catalogs during configuration; LINGUAS need not list translations.
# After adding or removing a PO file, run meson setup --reconfigure BUILD_DIR.
translation_python = find_program('python3', native: true)
translation_scan = run_command(
  translation_python,
  files('../build-aux/list-translations.py'),
  meson.current_source_dir(),
  check: true,
).stdout().strip()
translation_languages = []
if translation_scan != ''
  translation_languages = translation_scan.split('\n')
endif

# Track edits to existing catalogs as configure dependencies. These copies do
# not discover new files: changes to directory membership require reconfigure.
foreach language : translation_languages
  configure_file(
    input: language + '.po',
    output: language + '.po',
    copy: true,
  )
endforeach

# The GLib preset extracts marked C strings and GtkBuilder translatable entries.
# Do not use newer msgfmt_args here: the project still supports Meson 1.3.2.
# Keep the compatibility LINGUAS file empty: Meson reads it only when no PO
# files exist. Every nonempty catalog set is supplied explicitly by the scan.
i18n.gettext(gettext_package,
  languages: translation_languages,
  preset: 'glib',
  args: ['--add-comments=Translators:', '--keyword=ngettext:1,2'],
  install: true)
