# Release Pipeline

The `Linux Builds` workflow is defined in
`.github/workflows/desktop-builds.yml`. It supports Linux and Flatpak only.

## Checks And Packages

Native Linux builds and automated tests run on x86_64 and aarch64. Flatpak builds
run on the same architectures and produce:

* `SmashedPumpkin-x86_64.flatpak`
* `SmashedPumpkin-aarch64.flatpak`

The workflow also packages `SmashedPumpkin-X.Y.Z-map-bridge-sources.tar.xz` with
the locked Rust dependencies, WIT interfaces and licenses, and a SHA-256 checksum.
It verifies the archive's sources with an offline build and an empty Cargo cache,
then builds and tests the app with that source-built bridge.

Pull requests, `master` pushes and manual runs build and test without publishing
a GitHub Release. Only a pushed version tag triggers publication, after both
native builds, both Flatpak builds and the offline bridge source check succeed.

## Prepare A Version

1. Finish and commit the feature and fix changes.
2. Set the version in `meson.build`.
3. Update the release notes in `data/dev.rotstein.SmashedPumpkin.metainfo.xml.in`
   and the About dialog in `src/app.c`, including its release notes version.
4. Commit the version and notes together.
5. After publication is explicitly approved, push `master` and an annotated
   `vX.Y.Z` tag whose version exactly matches `meson.build`.
6. Verify the workflow, both Flatpak bundles and the bridge source archive in the
   GitHub Release.

The workflow rejects a tag that does not match the app version. The release job
has write permission; build jobs only have read permission.

GitHub release notes come from the matching AppStream release, with English and
German entries presented separately. The workflow rejects missing translations
and does not append an automatically generated changelog.

## Flathub

The Flathub manifest is maintained in the separate checkout at
`.flathub/dev.rotstein.SmashedPumpkin`. Point the app source at the exact commit
containing the version and release notes. Commit that manifest change separately
and update the Flathub pull request after approval.

The minimap bridge also needs an offline source build. The release's bridge
source archive can be extracted into `plugins/map-bridge`; the Meson option
`-Dbuild_minimap_bridge=true` builds and embeds it. This requires Cargo and a
matching `wasm32-wasip2` standard library in the build environment. The Rust SDK
extension alone does not necessarily include that target. A precompiled bridge
alone does not meet Flathub's source-build requirements. Check the current
Flathub requirements before updating the manifest or submitting a pull request.

Before announcing a release, verify that the app version, tag, About dialog and
AppStream release notes match, and that Flathub references the intended commit.
