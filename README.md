# triccheballacche

A minimal scriptable DAW: Janet scores, C audio, Perone plugins. The native and
browser editors share the same interface, audio engine and 2D/3D score view.
Music can be finite or repeat indefinitely, with quantized live updates.

This is a development project. Linux and Chromium/Wasm are tested; macOS is
unverified and Windows still needs native backends. Scripts and plugins must be
trusted. Plugin crashes are not isolated, and APIs may change.

## Build and run

Run commands from the repository root. You need a C compiler, GNU Make, git,
curl and patch; the plugin library also needs Node.js and npm.

```sh
make -j4 cli library
mkdir -p renders
./build/cli examples/brickworks.janet renders/brickworks.wav 48000
./build/cli --play examples/brickworks.janet 48000
```

Dependencies are downloaded into `.deps/` at pinned versions. Janet and Spork's
JSON module are linked into the host; no Janet or jpm installation is needed.
The host uses Brickworks headers for its mixer. Plugins are compiled separately:
`make library` builds the 40 Brickworks C examples as Perone bundles, excluding
their C++ duplicates. No sibling repositories are needed for the commands above.

| Command | Output |
| --- | --- |
| `make` or `make cli` | `build/cli`: playback and WAV export. |
| `make gui` | `build/gui`: native editor. |
| `make library` | `build/library/brickworks/`: plugin bundles. |
| `make web` | `build/web/player.{mjs,wasm}` and project catalog. |
| `make web-offline` | `build/web/offline.{mjs,wasm}`: offline rendering runtime. |
| `make tools` | `build/tools/perone-host`: single-plugin diagnostics. |

The native editor needs X11 development libraries and, on Linux, GTK 3 and
WebKitGTK 4.1 (or 4.0) runtime libraries. WebUI is fetched automatically.

```sh
make gui
./build/gui examples/brickworks.janet
./build/gui --serve examples/brickworks.janet  # Use a browser instead of a WebView.
```

For browser playback, install Emscripten **5.0.6** and Node.js. Version 6.0.9 has a
known repeated-playback memory issue with our pthread/worklet setup.

```sh
make -j4 library PERONE_PLATFORM=wasm32
make web
node test/server.mjs
```

Open <http://localhost:8000/editor/index.html?score=examples/brickworks.janet>.
The test server supplies the required COOP/COEP headers. Hosting requires HTTPS
or localhost, AudioWorklet and shared memory; the score view requires WebGL 2.
Without WebGL, editing and audio remain available.

`make web` publishes the scripts and already-built Wasm bundles; it does not
compile plugins. Override `WEB_CONTENT` to select files and bundle directories,
for example `make web WEB_CONTENT="lib examples build/library/brickworks"`.
The catalog is under `build/web/`; `?project=URL` selects another catalog.
Pass absolute `EMCC`/`EMXX` paths if Emscripten is outside `PATH`.

## Write music

Save this as `score.janet` in the repository root after building the library:

```janet
(import ./lib/pattern :as p)

(def bass (daw/plugin :bass
  "build/library/brickworks/synth_mono/build/bw_example_synth_mono.perone"
  {:vcf_cutoff 900}))
(daw/output (daw/track bass {:gain 0.3}))
(daw/tempo 132)

(def notes (p/steps 0.5 [36 nil 43 39]))
(daw/score (p/loop (p/map |[:note bass $ 100] notes)))
```

Run `./build/cli --play score.janet` or `./build/gui score.janet`. Imports are
relative to the score; plugin paths are relative to the host's working directory.
For a finite score or WAV export, give `daw/score` a duration:
`(daw/score pattern {:duration 30})`, then `./build/cli score.janet renders/score.wav`.

The audio graph is explicit: `daw/plugin` creates an instance, `daw/through`
connects an effect, `daw/mix` combines signals and `daw/output` selects the final
output. `daw/track` adds a visible gain/pan point with optional `:effects`;
`daw/master` adds an optional master mixer. Reusing a handle shares its audio.
Mix before a shared effect; cycles and disconnected nodes are errors.

Patterns are immutable musical data in quarter-note beats. `p/steps`, `p/events`
and `p/curve` create them; `p/serial`, `p/parallel`, `p/map`, `p/stretch`,
`p/reverse` and `p/loop` compose them. Notes use `[:note node pitch velocity]`
on intervals; automation uses `[:param node parameter value]` on points.
Loops stay compact: playback and the timeline query repetitions as needed.
Janet evaluates once per revision, never inside the audio callback.

For absolute seconds, use `daw/note`, `daw/param` and `daw/end`.
`daw/schedule start bpm pattern` places a finite pattern in that same timeline.
`daw/info` and `daw/product` expose parameter and product metadata.
Function descriptions live in [lib/daw.janet](lib/daw.janet),
[lib/pattern.janet](lib/pattern.janet) and [lib/music.janet](lib/music.janet).

Export defaults to stereo float32 WAV without clipping or normalization.
`(daw/end 30 {:format :pcm16 :normalize 0.94})` selects PCM16 and a target peak.
Allow time for release and effect tails. Playback ignores export normalization.
The CLI defaults to 44100 Hz; its last argument selects the sample rate.
Editors use 48000 Hz.

## Use the editor

Open a file with the path bar's folder icon, enter a path and press Enter, or
choose an example. Save uses the disk on native; web Download saves a local copy
and updates the session's files. Reloading the page restores the published files.

- **Play** (Ctrl/Command+Space) evaluates changed code or resumes the prepared
  score. Ctrl/Command+Enter forces evaluation, including changed imports.
- **Stop** (Esc) holds the position and DSP state. Click the ruler or enter
  seconds to seek; seeking resets effect history and retriggers spanning notes.
- **Live updates** to `daw/score` enter on the next beat grid (`:quantum`, default
  4 beats). Give plugins stable keyword identities. Routing, identities, tempo,
  duration and plugin configurations must match; stop before changing them.
  Errors leave the current music running. Stop or seek cancels queued revisions.
- **Score view:** drag or Shift+wheel to pan time, wheel to zoom time,
  Ctrl/Command+wheel to resize tracks. Wheel over names scrolls tracks vertically.
  Follow keeps the playhead in view. The 3D toggle changes the projection;
  drag then orbits, right-drag pans and Alt+wheel zooms time.
- **Automation** toggles visibility only. Each track's checklist selects curves.
  Source highlights and note/curve picking connect the view to the code.
- **Track names** open the plugin panel. Mute/solo affects audition, including
  routed groups. Plugin sections collapse; the window icon toggles a native UI
  where available. Manual parameter edits are temporary, not written to Janet.
- **Settings** lists discovered bundles and active instances. Restart the native
  editor after rebuilding a loaded plugin.

Start with [brickworks](examples/brickworks.janet) and [patterns](examples/patterns.janet)
for the canonical library, or [Rame](examples/rame.janet) for a complete piece.
[hello](examples/hello.janet), [live](examples/live.janet),
[routing](examples/routing.janet), [automation](examples/automation.janet) and
[Polpo](examples/prog/polpo.janet) use the local plugins below.
[Denti](examples/denti.janet), [Sempiterno](examples/sempiterno.janet) and
[Falalalan](examples/falalalan.janet) also use A-SID;
[gui](examples/gui.janet) demonstrates A-SID and Tibia's plugin UIs.
[Oculus](examples/oculus.janet) uses the local synth, drums, shape and echo;
[Campagnola](examples/campagnola.janet) uses the optional sampled piano.

## Plugins

Perone is the only plugin format. Copy whole `.perone` bundles: `product.json`,
the native `<platform>/<bundleName>.so` or `wasm32/<bundleName>.wasm`, and optional
UI assets. Janet reads metadata; no per-plugin Janet wrapper is needed.
DSP ABI v2 and optional native UI ABI v1 come from Tibia. Web UIs declare
`product.ui.web` and may load their own Wasm. Native UI hosting currently uses X11.

The canonical library build is self-contained. Local plugin projects instead
default to sibling `../tibia` and `../brickworks` checkouts. Tibia must provide
the Perone templates and have its `dot` npm dependency installed. To use the
pinned downloads instead, run `make library-deps` and pass their absolute paths:

```sh
make -C plugins TIBIA=/path/to/tibia BRICKWORKS=/path/to/brickworks
make -C plugins TIBIA=/path/to/tibia BRICKWORKS=/path/to/brickworks PERONE_PLATFORM=wasm32
```

This builds `synth_mono`, `fx_svf`, `tibia_test`, `drums`, `shape` and `echo` under
`plugins/<name>/build/plugin.perone`. `drums` maps MIDI notes 0–6 to kick, snare,
hat, open hat, crash, high tom and low tom. The optional sampled piano needs
Python 3: run `python3 plugins/piano/fetch.py`, then `make -C plugins/piano`
with the same Tibia setting. It embeds the downloaded Florestan samples.

Build A-SID and Tibia's original DSP/UI bundles in their own repositories.
Native discovery and applicable examples accept these environment overrides:

| Variable | Default |
| --- | --- |
| `BRICKWORKS_PERONE` | `build/library/brickworks` |
| `ASID_PERONE` | `../asid/plugin/perone/build/asid.perone` |
| `TIBIA_PERONE` | `../tibia/out/perone/c/build/tibia-test.perone` |

The web catalog uses `WEB_CONTENT` and includes those default locations when
built. Each platform lists only bundles with its DSP binary. Scores requiring
missing bundles cannot play. Put support modules under `examples/sources/` to
keep them out of the examples dropdown.

## Development

Core C owns scores, sequencing, DSP sessions and projection. `lib/` owns Janet
composition; `posix/` and `web/` adapt the platform; `editor/` is shared UI.
Keep platform calls out of core files. Prose and code comments use English;
musical names keep their spelling. Formatting is in `.editorconfig` and
`.clang-format` (clang-format 21).

| Check | Requirements beyond the host build |
| --- | --- |
| `make test` | Local fixtures only; no audio device or sibling checkout. |
| `make format-check` | clang-format 21. |
| `make test-build` / `make test-build-web` | Node.js; also Emscripten for web. |
| `make test-plugins` / `make test-brickworks` | Corresponding prebuilt bundles. |
| `make test-library-build` | Node.js/npm; builds and checks the canonical library. |
| `make test-web` / `make test-browser` | Emscripten/Node.js; Chromium for browser. |
| `make test-editor` | Native GUI build, Node.js, Chromium and an audio device. |
| `make test-live test-editor-ui test-score-space` | Native and browser prerequisites. |
| `make test-library` | Native and Wasm production bundles, plus editor prerequisites. |
| `make -C plugins/piano test` | Downloaded bank and Tibia; `test-web` also needs Emscripten/Node.js. |

Other focused checks are listed in the [Makefile](Makefile). Set `CHROMIUM` to
override the browser executable. Tests and generated artifacts live in `build/`;
use `renders/` for exported audio. Both are ignored. `make clean` preserves
downloads and local plugin builds; `make -C plugins clean` removes the latter.
Use one `make -j` invocation for concurrent targets sharing a build directory.

Dependency versions are pinned in [Makefile](Makefile) and
[plugins/library.mk](plugins/library.mk). `MINIAUDIO`, `JANET`, `SPORK` and `WEBUI`
accept explicit paths. Fixes live in [patches/](patches/); the build applies them
to generated copies, leaving downloaded sources intact.

## License

Copyright (C) 2024–2026 Paolo Marrone.

Original code, documentation and original contributions to the examples are
licensed under **GPL-3.0-only**; see [LICENSE](LICENSE). Third-party code and
musical material retain their own terms. See [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES)
for attributions and dependency notices, including the optional piano samples.
Three.js 0.180.0 is vendored from [r180](https://github.com/mrdoob/three.js/tree/r180)
(`build/` modules and `examples/jsm/controls/OrbitControls.js`) under its
[MIT license](editor/vendor/three/LICENSE).

When distributing native or browser builds, include the license notices and
provide the corresponding source, dependency versions and local patches under
their terms. `make web` produces development assets, not a complete release package.
