#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cargo build --manifest-path "$root/plugins/map-bridge/Cargo.toml" \
  --target wasm32-wasip2 --release --locked
mkdir -p "$root/resources/plugins"
install -m 644 "$root/plugins/map-bridge/target/wasm32-wasip2/release/map_bridge.wasm" \
  "$root/resources/plugins/map-bridge.wasm"
if command -v sha256sum >/dev/null; then
  sha256sum "$root/resources/plugins/map-bridge.wasm"
else
  shasum -a 256 "$root/resources/plugins/map-bridge.wasm"
fi
