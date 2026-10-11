# Platform Support

Smashed Pumpkin currently supports Linux, with Flatpak packages for x86_64 and
aarch64. Native Linux builds are available from source.

## Build Requirements

The native app requires GTK 4.12 or newer, libadwaita 1.5 or newer and libsoup
3.4 or newer. The Flatpak manifest supplies the GNOME runtime and dependencies.
GTK 3 and Ayatana AppIndicator are optional for the native tray helper.

The `Linux Builds` workflow builds and tests natively on both architectures and
builds a Flatpak bundle for each. See [the release pipeline](release-pipeline.md).

## Pumpkin Downloads

Application settings offer Nightly and Stable channels. Nightly is the default.
Release metadata is resolved from the official Pumpkin repository:

* Nightly: `https://api.github.com/repos/Pumpkin-MC/Pumpkin/releases/tags/nightly`
* Stable: `https://api.github.com/repos/Pumpkin-MC/Pumpkin/releases/latest`

Linux uses `pumpkin-X64-Linux` or `pumpkin-ARM64-Linux`, depending on the host
architecture. The app reports missing downloads rather than switching channels.

## Repositories

The app, website and Flathub manifest have separate Git histories. The local
`smashed-website` and `.flathub` checkouts are not part of app commits.
