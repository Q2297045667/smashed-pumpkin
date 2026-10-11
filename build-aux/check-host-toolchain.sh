#!/usr/bin/env bash
set -euo pipefail

status=0

check_cmd() {
  local cmd="$1"
  if command -v "$cmd" >/dev/null 2>&1; then
    printf '[ok] command: %s -> %s\n' "$cmd" "$(command -v "$cmd")"
  else
    printf '[missing] command: %s\n' "$cmd"
    status=1
  fi
}

check_pkg() {
  local pkg="$1"
  if pkg-config --exists "$pkg" 2>/dev/null; then
    printf '[ok] pkg-config: %s -> %s\n' "$pkg" "$(pkg-config --modversion "$pkg")"
  else
    printf '[missing] pkg-config: %s\n' "$pkg"
    status=1
  fi
}

check_cmd meson
check_cmd ninja
check_cmd pkg-config

echo
check_pkg gtk4
check_pkg libadwaita-1
check_pkg libsoup-3.0

echo
if pkg-config --exists gtk+-3.0 2>/dev/null; then
  printf '[optional] pkg-config: gtk+-3.0 -> %s\n' "$(pkg-config --modversion gtk+-3.0)"
else
  printf '[optional-missing] pkg-config: gtk+-3.0\n'
fi

if pkg-config --exists ayatana-appindicator3-0.1 2>/dev/null; then
  printf '[optional] pkg-config: ayatana-appindicator3-0.1 -> %s\n' "$(pkg-config --modversion ayatana-appindicator3-0.1)"
elif pkg-config --exists appindicator3-0.1 2>/dev/null; then
  printf '[optional] pkg-config: appindicator3-0.1 -> %s\n' "$(pkg-config --modversion appindicator3-0.1)"
else
  printf '[optional-missing] pkg-config: ayatana-appindicator3-0.1/appindicator3-0.1\n'
fi

echo
if [ "$status" -ne 0 ]; then
  echo 'Host toolchain is incomplete for CLion/Meson.'
else
  echo 'Host toolchain looks ready for CLion/Meson.'
fi

exit "$status"
