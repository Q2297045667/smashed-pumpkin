# Smashed Pumpkin

A Linux app for managing [Pumpkin](https://pumpkinmc.org/) Minecraft servers.
Built with GTK 4 and libadwaita, with server controls, plugins, players and worlds
in one native interface. Free software under GPL-3.0-or-later.

## Install

Get Smashed Pumpkin from [Flathub](https://flathub.org/apps/dev.rotstein.SmashedPumpkin).

```bash
flatpak install flathub dev.rotstein.SmashedPumpkin
flatpak run dev.rotstein.SmashedPumpkin
```

[GitHub Releases](https://github.com/Rotstein007/smashed-pumpkin/releases)
also provide Flatpak bundles for x86_64 and aarch64. Install a downloaded bundle
with `flatpak install ./SmashedPumpkin-x86_64.flatpak`, or use the aarch64 file
on an ARM64 system.

## Manage Your Servers

Install and update Pumpkin, start or stop servers, and group them into networks.
The console supports commands and colored log levels. Each server start creates
a separate session log. CPU, memory and TPS graphs help keep track of performance.

Manage players, whitelist entries and bans. Browse the Pumpkin Marketplace,
install free plugins without an API key, or import local plugin files.
Plugins can be enabled, disabled, updated and removed. Installing while the
server runs is supported; restart the server to use the new plugin.

The optional minimap displays saved terrain and live player positions. Follow
a player across dimensions or open moderation actions directly from the map.

These descriptions cover the current source tree. Published builds may not yet
include all of the latest changes.

### Pumpkin Updates

Choose **Nightly** or **Stable** in the Application settings. Nightly is the
default. The channel applies to future server installations and updates;
switching does not replace an existing server automatically.

Update checks run every five minutes and pause during installations and updates.
Updates still follow the server's automatic update settings. Only compatible
published downloads are used. Back up worlds before changing channels,
particularly when moving from Nightly to Stable. Plugins may need a matching
Pumpkin SDK.

### Minimap

Enabling the map requires explicit consent. Its bundled local `map-bridge`
plugin supplies live player data and needs write access to its own data folder.
The map reads existing saved chunks without generating new terrain.

Turning it off stops map tasks, releases terrain held in memory and disables
the bridge for future server starts. A running server is asked to unload the
bridge. If that Pumpkin build cannot unload it, restart the server to finish
disabling it. Saved map tiles remain on disk for faster loading next time.

### Startup And Recovery

Application settings control starting with the system, background operation and
the initial window mode. New servers restart four seconds after an unexpected
exit. Normal stops do not trigger recovery. Existing recovery preferences are
preserved; the delay and automatic restart can be changed in server settings.

## Screenshots

![Interactive minimap](data/screenshots/minimap.png)
![Pumpkin Marketplace](data/screenshots/plugins.png)
![Server console](data/screenshots/console.png)

More views are available on the [website](https://smashed-pumpkin.rotstein.dev/).

## Build On Linux

Requires a C compiler, Meson 1.3.2 or newer, Ninja, pkg-config and development
headers for GTK 4.12 or newer, libadwaita 1.5 or newer and libsoup 3.4 or newer.
GTK 3 and Ayatana AppIndicator are optional dependencies for the tray helper.

```bash
meson setup buildDir
meson compile -C buildDir
./buildDir/src/smashed-pumpkin
meson test -C buildDir --print-errorlogs
```

If Meson reports that an existing build directory uses an incompatible version,
run `meson setup --reconfigure buildDir` before compiling again.

Normal app builds use the bundled minimap component and do not require Rust.
To rebuild the bridge from `plugins/map-bridge`, install Rust and its WASI target:

```bash
rustup target add wasm32-wasip2
bash build-aux/build-map-bridge.sh
meson compile -C buildDir
```

The bridge uses locked dependencies and a pinned Pumpkin SDK. A server with a
different plugin API may require rebuilding against its matching SDK.

## Contribute

Report bugs or suggest improvements through
[GitHub Issues](https://github.com/Rotstein007/smashed-pumpkin/issues).
See [platform support](docs/platform-support.md) and
[the release pipeline](docs/release-pipeline.md) for build and publishing details.
The [website](https://smashed-pumpkin.rotstein.dev/) is maintained in its own
[repository](https://github.com/Rotstein007/smashed-website).

## License

[GPL-3.0-or-later](LICENSE).
