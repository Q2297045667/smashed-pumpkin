#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output="$root/resources/plugins/map-bridge.wasm"
cargo_args=()
while (( $# )); do
  case "$1" in
    --output)
      if (( $# < 2 )); then
        echo "--output requires a destination path" >&2
        exit 1
      fi
      output=$(realpath -m "$2")
      shift 2
      ;;
    --offline|--frozen)
      cargo_args+=("$1")
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done
target_dir=$(realpath -m "${CARGO_TARGET_DIR:-$root/plugins/map-bridge/target}")
# Cargo discovers vendored source configuration relative to its working directory.
(
  cd "$root/plugins/map-bridge"
  "${CARGO:-cargo}" build --target wasm32-wasip2 --target-dir "$target_dir" \
    --release --locked "${cargo_args[@]}"
)
mkdir -p "$(dirname "$output")"
install -m 644 "$target_dir/wasm32-wasip2/release/map_bridge.wasm" "$output"
if command -v sha256sum >/dev/null; then
  sha256sum "$output"
else
  shasum -a 256 "$output"
fi
