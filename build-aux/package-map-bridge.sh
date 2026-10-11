#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(sed -n "s/.*version: '\([^']*\)'.*/\1/p" "$root/meson.build" | head -n1)
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Cannot determine the release version" >&2
  exit 1
fi
if (( $# > 1 )); then
  echo "Usage: bash build-aux/package-map-bridge.sh [output-directory]" >&2
  exit 1
fi

output_dir=$(realpath -m "${1:-$root/build-release}")
name="SmashedPumpkin-${version}-map-bridge-sources"
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
bundle="$temp/$name"
mkdir -p "$bundle/.cargo" "$temp/cargo-home"
cp "$root/plugins/map-bridge/Cargo.toml" "$root/plugins/map-bridge/Cargo.lock" \
  "$root/LICENSE" "$bundle/"
cp -R "$root/plugins/map-bridge/src" "$bundle/src"

(
  cd "$bundle"
  cargo vendor --locked --versioned-dirs vendor > .cargo/config.toml
)
api_manifest=$(cargo metadata --manifest-path "$root/plugins/map-bridge/Cargo.toml" \
  --locked --offline --format-version=1 |
  jq -er '.packages[] | select(.name == "pumpkin-plugin-api") | .manifest_path')
wit_source="$(dirname "$api_manifest")/../pumpkin-plugin-wit"
# The SDK's WIT input is a sibling directory, not a crate included by cargo vendor.
mkdir -p "$bundle/vendor/pumpkin-plugin-wit"
cp -R "$wit_source/v0.1" "$bundle/vendor/pumpkin-plugin-wit/v0.1"
cp "$wit_source/LICENSE-APACHE" "$wit_source/LICENSE-MIT" \
  "$wit_source/LICENSE-MIT-MICROSOFT" "$bundle/vendor/pumpkin-plugin-wit/"

(
  cd "$bundle"
  CARGO_HOME="$temp/cargo-home" CARGO_TARGET_DIR="$temp/target" \
    cargo build --frozen --release --target wasm32-wasip2
)
test -s "$temp/target/wasm32-wasip2/release/map_bridge.wasm"

epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct)}
mkdir -p "$output_dir"
tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
  -C "$temp" -cJf "$output_dir/$name.tar.xz" "$name"
(
  cd "$output_dir"
  sha256sum "$name.tar.xz" > "$name.tar.xz.sha256"
  sha256sum --check "$name.tar.xz.sha256"
)
printf 'Source archive: %s/%s.tar.xz\n' "$output_dir" "$name"
