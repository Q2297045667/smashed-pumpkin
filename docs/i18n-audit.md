# Internationalization phase 1: text audit and logic separation

This document records phase 1 findings and verification. For the gettext pipeline
introduced in phase 2, see [internationalization infrastructure](<i18n.md>).

## Scope and findings

This phase establishes boundaries for the future GNU gettext integration: business state, build identity, HTTP error classification, and process events must not depend on UI wording. It does not introduce gettext, change language preferences, or add a language selector. The existing English interface and partial English/German content are retained.

The project uses C, GTK 4, libadwaita, and Meson. Some translation preparation already exists, but there is no complete translation build and initialization pipeline:

- The [Linux template](<../resources/ui/window.ui>) and [Windows template](<../resources/ui/window-windows.ui>) already contain many `translatable="yes"` attributes, but do not configure a shared translation domain.
- Feedback and About content in the [application code](<../src/app.c>) uses manual English/German selection. The [AppStream metadata](<../data/dev.rotstein.SmashedPumpkin.metainfo.xml.in>) also contains German text. Preserve and migrate this content rather than deleting it.
- The [data build definition](<../data/meson.build>) currently substitutes placeholders and installs metadata without merging PO translations.

This is an audit of translation boundaries and high-risk call sites, not a complete deduplicated string catalog. Repeated markers in the two templates are not independent translation entries. Generate the full catalog through gettext extraction in a later phase.

## Text sources and translation boundaries

| Source | Content to translate later | Content that must remain stable | Main entry points |
| --- | --- | --- | --- |
| Static GTK templates | Titles, labels, subtitles, tooltips, placeholders, menus, and dropdown labels | Widget IDs, action/page/response IDs, CSS classes, icon names, dropdown ordering, and enum/index mappings | [Linux UI](<../resources/ui/window.ui>), [Windows UI](<../resources/ui/window-windows.ui>) |
| Dynamic application UI | Status text, validation messages, confirmations, buttons, counts, relative time, and accessibility labels | User input, server/player/world names, UUIDs, paths, URLs, and build IDs | [Main window](<../src/window.c>), [lifecycle UI](<../src/window-lifecycle.c>), [worlds page](<../src/window-worlds.c>), [minimap](<../src/window-minimap.c>) |
| Widget wrappers | Visible text supplied by callers and the corresponding accessibility labels | Internal object names and behavioral identifiers; do not translate already translated text again | [Refresh button](<../src/refresh-button.c>) |
| Console and third-party content | Application-provided explanations and filter button labels | Raw server output, log levels, the `SMPK` marker, plugin names/descriptions, and user-supplied ban reasons | [Console](<../src/window-console.c>), [player events](<../src/window-player-events.c>) |
| Protocols and configuration | Display names and help text for fields | JSON/API/INI keys, `stable/nightly`, marketplace `free/paid/adwall` values, commands, plugin state suffixes, and serialized numbers | [Application configuration](<../src/app-config.c>), [server model](<../src/server.c>), [plugin store](<../src/plugin-store.c>), [player protocol](<../src/player-protocol.c>) |
| Tray | User-visible actions such as Open/Quit | IPC, window classes, lock identifiers, and application branding | [Linux tray](<../src/tray.c>), [Windows tray](<../src/tray-win.c>) |
| Desktop and release metadata | Summaries, descriptions, screenshot captions, and release notes | Application IDs, branding, Exec/Icon/Categories, licenses, URLs, version numbers, and dates | [Desktop source](<../data/dev.rotstein.SmashedPumpkin.desktop.in>), [AppStream source](<../data/dev.rotstein.SmashedPumpkin.metainfo.xml.in>) |

Specific rules:

- Do not translate `minecraft:*` dimension IDs. Application display names such as Overworld/Nether/End may be translated.
- `session-*.log` filenames and their timestamp format participate in discovery, sorting, and parsing. Localize displayed timestamps, not filenames.
- Do not translate plugin state suffixes such as `.disabled/.deactivated`.
- A new server's default name may be translated later, but switching languages must not rewrite existing names, directories, or IDs.
- Use complete translation entries for sentences and plurals instead of appending English `s` or `server(s)`. Preserve the types and meanings of format arguments.

## Logic separation implemented in this phase

### 1. Installation state is separate from version display

The [main window](<../src/window.c>) uses actual installation state to decide which cards and actions are available. The display function only produces `Not installed`, `Installing...`, or version text. Installation is no longer inferred by comparing version display strings.

Regression coverage includes installed servers whose opaque stored build labels happen to be `Not installed` or `Nicht installiert`. These labels do not incorrectly classify an installed server as missing.

### 2. Build identity and persisted data are separate from formatted labels

The new [build metadata interface](<../src/build-metadata.h>) and [implementation](<../src/build-metadata.c>) provide:

- `pumpkin_build_metadata_from_unix()`: produces raw UTC metadata independent of UI date formats and time zones.
- `pumpkin_build_metadata_equal()`: compares raw identity, with compatibility normalization limited to known legacy YMD syntax.
- `pumpkin_build_metadata_format()`: formats display text using the user's date/time preferences. Its result must not become an update comparison value, cache identity, or INI value.

The [download interface](<../src/download.h>), [release metadata parser](<../src/release-metadata.c>), and [internal window data](<../src/window-internal.h>) explicitly document the raw metadata contract. Direct downloads, cache hits, cache application after waiting for another download, and latest-release parsing all retain raw labels. When metadata is unavailable, a raw UTC value is generated from the file timestamp instead of persisting a localized display label.

Update decisions still prefer build IDs. When comparable IDs are unavailable, they compare raw metadata before using existing fallback rules such as URLs. Changing date formats or, later, the UI language must not change the identity of a build.

Legacy compatibility rules:

- `Build `, `Stable `, and `Nightly ` are stable tokens in legacy stored syntax, not UI messages to translate. Translate independently generated display text in a later phase.
- Recognize only exact ASCII `YYYY-MM-DD`, `YYYY-MM-DD HH:MM`, or `YYYY-MM-DD HH:MM UTC`, with an optional exact `Build ` prefix and valid calendar/time fields. Variable-width fields, signs, tabs/newlines, extra spaces, lowercase zone suffixes, and seconds extensions remain opaque. Numeric `scanf` conversions are deliberately not used: they accept more than this persisted grammar.
- Known timestamps explicitly marked `UTC` are parsed as UTC and converted to local time for display.
- Legacy timestamps without a time zone retain their wall-time components. Do not guess their original time zone or treat date-only and minute-precision values as identical build identities.
- Preserve legacy DMY/MDY display labels, custom labels, and unrecognized data unchanged. Do not guess date ordering from the user's current preferences or perform destructive migration in this phase.

### 3. HTTP errors are classified by domain/code

The [file download implementation](<../src/download.c>) uses `PUMPKIN_DOWNLOAD_HTTP_ERROR` for non-2xx responses, with the numeric HTTP status as the error code. Marketplace UI distinguishes 401 and 403 by domain/code instead of searching English error messages for `HTTP status ...`.

Network and cancellation errors retain their corresponding GIO errors. This contract applies to file downloads; it is not a refactor of every request or every upstream error parser. Future translation of diagnostic messages will not change HTTP classification.

### 4. Process lifecycle uses typed signals

The [server model interface](<../src/server.h>) defines separate signals:

- `process-exited()`: emitted after process cleanup, including the process-wait error path.
- `restart-scheduled(guint delay_ms)`: emitted when automatic restart has been scheduled, with the actual delay.

The [server implementation](<../src/server.c>) no longer injects `Server process exited` or `Auto-restart scheduled` into `log-line` to control the UI. The [window](<../src/window.c>) handles the exit signal by clearing the running hint, command tracking, player/statistics state, automatic update countdown, and manual restart state. Ordinary server output remains raw output even when it matches these English messages or their German test equivalents.

Persisted session logs still contain the application's exit diagnostic. Buffered output delivered after exit continues to be written to its original session log, but loses its output-stream identity for the current model. It no longer emits `log-line` for a stopped or subsequently restarted process, preventing the UI from returning to a running state. Linux cleanup now follows the existing Windows stream-identity boundary.

### 5. Application notices cannot drive raw-output protocol state

The follow-up audit found that typed lifecycle events alone were insufficient. Automatic restart failures, session-logging warnings, process-wait errors, stop/force-stop notices, and minimap bridge failures still used `log-line`. A nonempty message on that channel updates the UI running hint and is eligible for startup, player, statistics, polling, and bridge-response parsing. For example, an automatic restart failure after removing the binary could leave the model stopped but revive the UI's running state.

The [server signal contract](<../src/server.h>) now separates origins rather than guessing from message prefixes or language:

| Channel | Payload | Allowed consumers |
| --- | --- | --- |
| `log-line(const char *line)` | Raw process stdout/stderr | Console display and upstream protocol parsers |
| `console-notice(const char *message)` | Application-provided explanation | Console display only; no lifecycle, readiness, player, TPS, polling, or bridge state changes |
| `process-exited()` / `restart-scheduled(guint delay_ms)` | Typed lifecycle event | Lifecycle state handlers |

The [window](<../src/window.c>) attaches each notice handler once and sends notices directly to console rendering. The [bridge controller](<../src/minimap-bridge-control.c>) also reports failures through the display-only channel while continuing to recognize upstream acknowledgments on `log-line`. Raw output can still legitimately contain any notice-like text; it is not reclassified by wording.

Session archives retain their existing stable diagnostics independently of signal routing. When adding gettext later, keep archived diagnostic text and structural level/marker tokens stable. In particular, [server notices](<../src/server.c>) currently share one message between archive and console formatting: translate the presentation path separately, not the shared value before archive serialization. This phase changes routing, not archive formats or the diagnostic translation policy.

### 6. Cache recovery preserves raw identity and optional candidates

The [cache recovery path](<../src/window.c>) had two nullable-array bugs: a missing first installed URL ended the whole search, and a missing identity was treated as an early URL-only fallback. The latter could choose an older generic cache entry before a valid raw-label or later-ID entry.

Recovery now checks all URL positions in their existing priority order, skipping missing or empty fields. For each URL, it checks all nonempty raw build IDs/labels before a single legacy URL-only fallback. It continues past invalid cache files and preserves the stored raw label. Cache directory hashing and the `URL|identity` key convention are unchanged; formatted or translated labels must never be passed as identities.

This is a targeted candidate-selection fix, not a redesign of cache priority, integrity verification, or latest-release fallback policy. URL-only cache recovery remains supported for legacy entries with no usable build identity.

## Retained raw-text parsing is not display-text coupling

Not every English string should be removed or translated. The following matches belong to upstream syntax or persisted data:

| Feature | Retained syntax | Content that may be translated later |
| --- | --- | --- |
| Startup readiness | Server output tokens such as `Done` and `Listening`; see the [main window](<../src/window.c>) | Application status labels such as Starting/Running |
| Players and statistics | Login, UUID, join/leave, online-list, TPS, and rename responses; see [player events](<../src/window-player-events.c>), [window protocol](<../src/window-protocol.c>), and [player protocol](<../src/player-protocol.c>) | Application statistics labels and player-management explanations |
| Map bridge loading | Confirmation conditions such as `Plugin ... loaded/unloaded successfully` and already/not loaded; see [bridge control](<../src/minimap-bridge-control.c>) | Application-generated loading failure explanations |
| Bridge permission errors | Upstream responses such as `Permission denied` and `approved: false`; see the [minimap](<../src/window-minimap.c>) | Application-provided permission explanations and recovery suggestions |
| Historical plugin summaries | Permission, incompatible API, and initialization failure prefixes/suffixes in persisted logs; see the [main window](<../src/window.c>) | Application summaries; embedded raw log content remains unchanged |

Do not mechanically wrap raw output syntax in `_()`. If an upstream protocol changes, update its parser and fixtures independently; changing translations is not a protocol fix.

## Constraints for the later gettext integration

1. Establish a shared domain, locale installation path, PO source-file list, and gettext build in Meson. Initialize translation before instantiating GTK templates.
2. Use the same translation domain for both UI templates. Use `_()` for C display messages, `N_()` for deferred translation tables, contexts for ambiguous short words, and `ngettext()` for plurals. Do not extract generated resource C files.
3. The [Cambalache project](<../resources/smashed-pumpkin.cmb>) currently lists UI/resource/CSS paths and SHA-256 references rather than embedding text, and only registers the Linux template. The [actual resource manifest](<../resources/smashed-pumpkin.gresource.xml.in>) compiles both UI templates. Check hashes, translation attributes, and Windows template consistency after editor round trips. Do not treat the Cambalache file as the only text source.
4. Date and relative-time formatting must respect existing user preferences. UI locale must not affect JSON/protocol numeric serialization. In particular, review cJSON locale handling in the [build definition](<../src/meson.build>).
5. Tray helpers are separate processes and each needs initialization of the same effective language. The Windows tray also requires UTF-8 to UTF-16 conversion, wide-character drawing, and sizing for longer translations. Do not pass gettext UTF-8 directly to `DrawTextA`.
6. Substitute project placeholders before merging Desktop/AppStream translations. Define a maintenance source for duplicated About/AppStream release content, preserve current English/German text, and coordinate the bilingual release-note checks in the [release pipeline](<release-pipeline.md>).
7. Initial language detection, INI language preferences, settings selection, and restart-required notices belong to later phases. This phase does not change these behaviors.
8. Fix the language for English text assertions and prefer stable widget identities, dialog response IDs, typed events, and captured payloads in behavior tests. Future non-English UI tests should specify their locale explicitly; server log fixtures should retain raw protocol text. The notice tests deliberately check state and captured rendering rather than an English error sentence.
9. Keep the message-origin boundary when extracting strings: application notices are eligible for presentation translation, while `log-line` payloads and parser literals are not. Do not introduce notice handling into protocol parsers or route translated explanations back through raw-output signals. Preserve stable archive diagnostics separately from translated display messages.

## Regression verification

Commands used, with Meson available on PATH:

```sh
meson setup build-i18n --wrap-mode=nodownload
meson compile -C build-i18n
meson test -C build-i18n --no-rebuild --print-errorlogs
bash build-aux/test-window-ui.sh build-i18n i18n-boundaries
```

Local verification for this phase: the full native build succeeded, all 15 Meson test suites passed, the isolated `i18n-boundaries` UI regression passed, and `git diff --check` passed.

New or extended coverage:

- [Build metadata tests](<../tests/test-build-metadata.c>): raw values remain unchanged across multiple time zones and three date formats; valid legacy syntax is supported without guessing unknown or invalid labels. Twenty-two malformed/custom spelling cases verify that formatting preserves opaque values and comparisons do not equate them with canonical timestamps.
- [HTTP download tests](<../tests/test-download-http.c>): 401/403/503 responses, successful downloads, cancellation, classification after replacing the error message with German text, rejection of unrelated English message matches, and preservation of existing files on failure.
- [Server maintenance tests](<../tests/test-server-maintenance.c>) and [fake server](<../tests/fake-managed-server.c>): English/German log collisions, exit and automatic restart signals, and buffered output after exit that cannot restore running state but is fully retained in its original session log. Stop notices and automatic restart failures stay out of the raw-output channel; stable stopping diagnostics remain in the archive.
- [Bridge control tests](<../tests/test-minimap-bridge-control.c>): an unverified local bridge file exercises failure reporting without filesystem-permission assumptions; the explanation uses the notice channel, emits no raw output, and does not modify that file.
- [UI regression](<../tests/test-window-ui.c>): installation state independent of display wording; update decisions stable across date formats; build ID precedence; late-polling and lifecycle isolation; failed or invalid downloads that do not stop existing servers; raw INI metadata retained for direct downloads, cache hits, and shared-cache waits; manual stop taking precedence over post-update restart. Synthetic readiness/TPS/player-list/German notices display exactly once without changing running or protocol state, and a real automatic restart failure remains visible while the UI stays stopped. Eight cache-recovery cases cover nullable URLs, empty fields, raw-label/latest-ID/latest-label identities, an invalid primary cache, and legacy URL-only fallback.
- The [isolated test script](<../build-aux/test-window-ui.sh>) uses temporary configuration, private D-Bus/runtime directories, and local download fixtures. It fixes the English assertion environment and checks that the host desktop's document portal mount remains unchanged. A bounded cleanup retry handles temporary-directory writes during private service shutdown without stopping host services.

These results cover only the local Linux native build and the listed tests. They do not constitute acceptance of every UI page, an actual translated UI, Windows, or Flatpak builds. The project's formal platform scope remains defined by the [platform support documentation](<platform-support.md>).
