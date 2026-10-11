# The GLib preset extracts marked C strings and GtkBuilder translatable entries.
# Do not use newer msgfmt_args here: the project still supports Meson 1.3.2.
i18n.gettext(gettext_package,
  preset: 'glib',
  args: ['--add-comments=Translators:', '--keyword=ngettext:1,2'],
  install: true)
