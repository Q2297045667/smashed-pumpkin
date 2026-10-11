# Vendored tomlc17

Source: https://github.com/cktan/tomlc17
Commit: fba134d7d1f9b58414f908600b2869f7e55ff2c4
License: MIT (see LICENSE)

Used to parse ppm's TOML lockfile and Pumpkin's world configuration.
Kept in-tree so desktop and Android builds do not require network access or
an additional platform-specific system dependency.

Local change: float conversion uses GLib's `g_ascii_strtod()` instead of
locale-sensitive `strtod()`. TOML decimal points must work with German and other
numeric locales after GTK initialization, including in background workers.
GLib is already a dependency of every consumer. No global locale changes are
made; upstream syntax validation and overflow/subnormal handling are retained.
Regression coverage is in `tests/test-world-store.c`.
