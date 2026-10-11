# Vendored cJSON

Source: https://github.com/DaveGamble/cJSON
Version: 1.7.19
Commit: c859b25da02955fef659d658b8f324b5cde87be3
License: MIT (see LICENSE)

Unmodified upstream parser and header. Built as an internal static library for
all platforms; no extra system package or build-time network access is needed.

Meson explicitly enables `ENABLE_LOCALES`, matching upstream's default. cJSON
uses the C library's locale-dependent `strtod` and needs this option to handle
JSON decimal points when the application uses comma-decimal locales such as
German or French. Do not disable it or change the application's global locale
to work around JSON parsing. App and tests share the same configured library.
