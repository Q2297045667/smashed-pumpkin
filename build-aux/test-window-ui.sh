#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Linux ]]; then
  printf '%s\n' 'This isolated desktop UI smoke test is Linux-only.' >&2
  exit 77
fi

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/buildDir"}
scope=${2:-all}
case "$scope" in all|minimap|worlds-map-access|releases) ;; *) printf 'Unknown UI test scope: %s\n' "$scope" >&2; exit 2 ;; esac
if [[ "$build" != /* ]]; then
  build="$PWD/$build"
fi
meson compile -C "$build" smashed-pumpkin

profile=$(mktemp -d /tmp/smashed-pumpkin-ui-check-XXXXXX)
trap 'rm -rf "$profile"' EXIT
mkdir -m 700 "$profile/runtime"
# A private bus also needs a private runtime directory: a test document portal
# must never mount over (and subsequently unmount) the desktop's doc filesystem.
wayland_display=${WAYLAND_DISPLAY:-}
desktop_runtime=${XDG_RUNTIME_DIR:-}
desktop_doc_mount=
if [[ -n "$desktop_runtime" ]] && command -v findmnt >/dev/null; then
  desktop_doc_mount=$(findmnt -rn -M "$desktop_runtime/doc" -o ID || true)
fi
if [[ -n "$wayland_display" && "$wayland_display" != /* ]]; then
  : "${XDG_RUNTIME_DIR:?The desktop runtime directory is required for Wayland}"
  wayland_display="$XDG_RUNTIME_DIR/$wayland_display"
fi
objects=()
for object in "$build"/src/smashed-pumpkin.p/*.o; do
  case "$object" in
    */main.c.o|*/server.c.o) ;;
    *) objects+=("$object") ;;
  esac
done
packages=(gtk4 libadwaita-1 libsoup-3.0 gmodule-2.0)
test_defines=()
if pkg-config --exists xtst; then
  packages+=(xtst)
  test_defines+=(-DSMASHED_TEST_XTEST)
fi
if pkg-config --exists x11; then
  packages+=(x11)
fi
read -r -a flags <<< "$(pkg-config --cflags --libs "${packages[@]}")"
# Use the build's compiler; PATH's cc may be an incompatible wrapper.
mapfile -t compiler < <(meson introspect --compilers "$build" | python3 -c \
  'import json, sys; print("\n".join(json.load(sys.stdin)["host"]["c"]["exelist"]))')
[[ ${#compiler[@]} -gt 0 ]]
"${compiler[@]}" -Wall -Wextra -Werror "$root/tests/fake-pumpkin.c" -o "$profile/fake-pumpkin"
read -r -a glib_flags <<< "$(pkg-config --cflags --libs glib-2.0)"
"${compiler[@]}" -Wall -Wextra -Werror "$root/tests/fake-managed-server.c" \
  "${glib_flags[@]}" -o "$profile/fake-managed-server"
# Exercise the production shutdown logic with a shorter test-only deadline.
read -r -a gio_flags <<< "$(pkg-config --cflags gio-2.0)"
"${compiler[@]}" -Wall -Wextra -Werror -DPUMPKIN_STOP_TIMEOUT_SECONDS=1 \
  -I "$root/src" "${gio_flags[@]}" -c "$root/src/server.c" -o "$profile/server.o"
objects+=("$profile/server.o")
"${compiler[@]}" -Wall -Wextra -Werror -I "$root/src" -I "$build/src" \
  "${test_defines[@]}" "$root/tests/test-window-ui.c" "${objects[@]}" \
  "$build/src/libsmashed-pumpkin-json.a" "${flags[@]}" -lm \
  -o "$profile/test-window-ui"

# Never use the developer's servers, credentials, config or autostart entries.
env XDG_RUNTIME_DIR="$profile/runtime" WAYLAND_DISPLAY="$wayland_display" \
  HOME="$profile" XDG_DATA_HOME="$profile/data" \
  XDG_CONFIG_HOME="$profile/config" XDG_CACHE_HOME="$profile/cache" \
  dbus-run-session -- timeout 90s env \
  FAKE_PUMPKIN_BINARY="$profile/fake-pumpkin" \
  FAKE_MANAGED_PUMPKIN_BINARY="$profile/fake-managed-server" \
  SMASHED_UI_TEST_SCOPE="$scope" \
  GTK_A11Y=none GSK_RENDERER="${SMASHED_UI_TEST_RENDERER:-cairo}" G_DEBUG=fatal-warnings \
  "$profile/test-window-ui"
if [[ -n "$desktop_doc_mount" ]]; then
  current_doc_mount=$(findmnt -rn -M "$desktop_runtime/doc" -o ID || true)
  if [[ "$current_doc_mount" != "$desktop_doc_mount" ]]; then
    printf '%s\n' 'FAIL: the desktop document portal mount changed during the UI test.' >&2
    exit 1
  fi
  printf '%s\n' 'PASS: the desktop document portal mount is unchanged'
fi
if [[ "$scope" == releases ]]; then
  printf '%s\n' 'Only release-channel settings were exercised; no server was started.'
  exit 0
fi
if [[ "$scope" == worlds-map-access ]]; then
  printf '%s\n' 'Only world loading and minimap access were exercised; no server was started.'
  exit 0
fi
if [[ "$scope" == minimap ]]; then
  printf '%s\n' 'Screenshot: /tmp/smashed-minimap.png (only the minimap was exercised)'
  exit 0
fi
printf '%s\n' 'Screenshots: /tmp/smashed-startup-settings.png, /tmp/smashed-plugin-actions.png,' \
  '             /tmp/smashed-stats-hover.png, /tmp/smashed-plugin-details.png,' \
  '             /tmp/smashed-logs-light.png, /tmp/smashed-logs-dark.png,' \
  '             /tmp/smashed-players.png, /tmp/smashed-players-dark.png,' \
  '             /tmp/smashed-players-compact.png, /tmp/smashed-players-empty.png,' \
  '             /tmp/smashed-players-whitelist.png, /tmp/smashed-players-bans.png,' \
  '             /tmp/smashed-players-bans-compact.png,' \
  '             /tmp/smashed-worlds-light.png, /tmp/smashed-worlds-dark.png,' \
  '             /tmp/smashed-worlds-compact.png, /tmp/smashed-worlds-error.png,' \
  '             /tmp/smashed-player-details.png, /tmp/smashed-player-manage.png'
