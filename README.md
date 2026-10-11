# triccheballacche

A minimal scriptable DAW: Janet scores, C audio, Perone, CLAP, LV2 and VST3 plugins.
The native and browser editors share the same interface, audio engine and 2D/3D score view.
Music can be finite or repeat indefinitely, with quantized live updates.

This is a development project. Linux and Chromium/Wasm are tested; macOS is
unverified and Windows still needs native backends. Scripts and plugins must be
trusted. Plugin crashes are not isolated, and APIs may change.

## Build and run

Run commands from the repository root. You need a C compiler, GNU Make, git,
curl and patch. The native build also needs Meson, Ninja and pkg-config for
Lilv and its dependencies; the plugin library needs Node.js and npm.

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
| `make tools` | `build/tools/perone-host` and `build/tools/plugin-scan`: plugin diagnostics. |

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
  Opening another file keeps audio running; Play starts it as a new session.
- **Stop** (Esc) holds the position and DSP state. Click the ruler or enter
  seconds to seek; seeking resets effect history and retriggers spanning notes.
- **Live updates** to `daw/score` enter on the next beat grid (`:quantum`, default
  4 beats). Give plugins stable keyword identities. Routing, identities, tempo,
  duration and plugin configurations must match; stop before changing them.
  Errors leave the current music running. Stop or seek cancels queued revisions.
- **Score view:** drag or Shift+wheel to pan time, wheel to zoom time,
  Ctrl/Command+wheel to resize tracks. Wheel over names scrolls tracks vertically.
  The left column shows compact routing blocks aligned with the score lanes;
  click a block to inspect its signal path. Drag the divider to resize the map:
  more width spreads nodes along the signal flow and shortens connections.
  Follow keeps the playhead in view. The 3D toggle changes the projection;
  drag then orbits, right-drag pans and Alt+wheel zooms time. Reset view restores
  the camera and zoom around the current playback position.
- **Automation** toggles visibility only. Each track's checklist selects curves.
  Source highlights and note/curve picking connect the view to the code.
- **Routing blocks** show plugins and explicit mixes, folding track/master gain
  stages into connections. Click a node to highlight its signal path and list
  it in the side panel, including downstream group/master effects; only the
  selected node opens. Click the background or press Escape to clear the highlight.
  Routing is read-only.
- **Track names** open the plugin panel. Mute/solo affects audition, including
  routed groups. Plugin sections collapse; the window icon toggles a native UI
  where available. Manual parameter edits are temporary, not written to Janet.
  ↓/↑ import/export a plugin preset (`.tbpreset.json`, up to 16 MiB of state).
  Both pause playback. Native export asks for a destination path; web export
  downloads a file. Presets change the prepared instance, not the score or its
  automation; reevaluation and seeking still apply the score's parameter values.
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
[Crocevia](examples/crocevia.janet) combines local Perone drums/synths, Surge XT CLAP,
A-SID LV2 from `../asid` (override with `ASID_LV2`) and Brickworks `fx_comb` VST3.
Surge and the comb use the native plugin search paths described below.
[denoiser](examples/denoiser.janet) combines Clack.DIGITAL Ue Ue Wah on the lead with
Bertom Denoiser Classic on a noisy radio bus, both VST3. A 126 BPM electro-funk piece
with syncopated bass, electric keys and short denoiser A/B comparisons in the intro
and break. It also uses local Perone plugins and the canonical Brickworks library.
[Insetti di vetro](examples/insetti-di-vetro.janet) is 150 seconds of fractured
electronica with an original piano/glass theme, acid bass, broken drums and
eight sections. It uses the canonical Brickworks library, local drums and piano.
[Due corpi](examples/due-corpi.janet) is a 50-second electro miniature with Samplo
and Vitsel CLAP, Perone drums and Brickworks effects. Install
[Samplo](https://github.com/mlm-games/samplo-clap) and
[Vitsel](https://github.com/mlm-games/vitsel-clap) for your platform in a CLAP search
path. Generate its original SFZ bank with `python examples/due-corpi.py DIRECTORY`,
where `DIRECTORY` is a folder in Samplo's instrument library. Set
`SAMPLO_INSTRUMENT` to the bank's index (default: `0`), then use the usual CLI or editor.
Samplo 1.2.7 has an upstream limitation: it skips loading instrument `0` on startup
and plays a test tone instead. Correct playback at that index needs an upstream fix.

## Plugins

**Perone** works natively and in the browser. Copy whole `.perone` bundles: `product.json`,
the native `<platform>/<bundleName>.so` or `wasm32/<bundleName>.wasm`, and optional
UI assets. Janet reads metadata; no per-plugin Janet wrapper is needed.
DSP ABI v2 and optional native UI ABI v1 come from Tibia. Web UIs declare
`product.ui.web` and may load their own Wasm. Native UI hosting currently uses X11.
Native and Wasm DSPs receive playing/stopped status, BPM and quarter-note position;
other transport fields are not advertised as valid.

**CLAP** works natively on Linux, including original X11 plugin windows and the
shared parameter panel. Settings lists modules from `CLAP_PATH` (colon-separated),
`~/.clap`, `/usr/lib/clap` and `/usr/local/lib/clap`, including each factory ID.
An explicit path also works outside those directories:

```janet
(def synth (daw/plugin :synth
  {:format :clap :id "org.surge-synth-team.surge-xt"}))
# Or {:format :clap :path "/path/to/plugin.clap" :id "native.plugin.id"}.
# :id is optional only when the module exports one supported plugin.
```

[examples/clap.janet](examples/clap.janet) uses separately installed Surge XT and
Surge XT Effects; both were tested with version 1.3.4, including their original
windows. `CLAP_PATH=/path/to/plugins ./build/gui examples/clap.janet` runs it.
Use `build/tools/plugin-scan /path/to/plugin.clap` to inspect IDs and parameters.
CLAP parameter keywords are native numeric IDs, such as `:825615485`;
`daw/info` exposes their labels, ranges and automation flags. Enumeration indices
also work but are less stable across plugin versions. No wrapper or `product.json`
is required. CLAP native binaries do not run in the Wasm editor.

Discovery uses a separate process, with a timeout and a file-stamped memory cache.
Playback uses independent instances and cached modules in the host process.
The initial patch supplies initial control values; these can differ from a
parameter's declared default. Notes and automation retain sample boundaries,
and plugins receive tempo and transport position. Host callbacks, timers and file
descriptors run on the main thread. Plugin restarts pause audio; incompatible
port or parameter changes require preparing the score again.

**LV2** works natively on Linux, with Lilv reading the bundle's Turtle metadata.
Settings lists `.lv2` bundles from `LV2_PATH` (colon-separated), or from `~/.lv2`,
`/usr/local/lib/lv2` and `/usr/lib/lv2` when it is unset. References use the plugin
URI; an explicit `:path` selects a bundle outside those directories:

```janet
(def synth (daw/plugin :synth
  {:format :lv2 :id "https://surge-synthesizer.github.io/lv2/surge-xt"}))
# Or {:format :lv2 :path "/path/to/plugin.lv2" :id "urn:plugin:uri"}.
```

[examples/lv2.janet](examples/lv2.janet) uses Surge XT and Surge XT Effects 1.3.4.
Run `LV2_PATH=/path/to/bundles ./build/gui examples/lv2.janet` and use each
plugin's window icon to open its original X11 UI. GTK/Qt UIs are not embedded;
control ports still appear in the shared parameter panel. Parameter keywords
are port symbols, such as `:gain`; `daw/info` lists them. Numeric `patch` properties
are not yet exposed as Janet parameters, though original UIs can exchange Atom
messages with their DSP. Native LV2 bundles do not run in the Wasm editor.

LV2 supports MIDI and transport through Atom sequences, control automation,
sample-rate-scaled ranges, bundle default state and workers (background during
playback, synchronous offline). UI messages are serviced even while stopped.
Unknown required features/options, fixed/power-of-two block requirements, CV ports and
non-sequence Atom ports are rejected unless the port is optional. Atom buffers
use each port's declared minimum size, with a 64 KiB default and a 1 MiB per-port
limit. Buffers and UI message queues are allocated before processing; worker
messages remain limited to 4 KiB.
`build/tools/plugin-scan /path/to/plugin.lv2` lists IDs, parameters and rejection reasons.

**VST3** works natively on Linux, including original X11 windows. The adapter uses
Steinberg's pinned C API header; it does not compile or link the C++ SDK.
Settings searches `VST3_PATH` (colon-separated), `~/.vst3`, `/usr/local/lib/vst3`
and `/usr/lib/vst3`. References identify the processor class by its 32-digit ID:

```janet
(def synth (daw/plugin :synth
  {:format :vst3 :id "ABCDEF019182FAEB566D624153675854"}))
# Or {:format :vst3 :path "/path/to/plugin.vst3" :id "processor-class-id"}.
```

`build/tools/plugin-scan /path/to/plugin.vst3` lists classes and parameters.
Run `VST3_PATH=/path/to/bundles ./build/gui examples/vst3.janet` with separately
installed Surge XT and Surge XT Effects. Both original GUIs and DSPs were tested
with version 1.3.4; A-SID's Tibia-generated VST3 was also checked.
Parameter keywords are native numeric IDs. Continuous values use VST3's normalized
`0..1` range; discrete values use integer step indices and expose the plugin's
labels. MIDI notes, pressure and mapped controllers, sample-accurate automation,
transport, latency, and separate or combined controllers are supported.
Controller metadata updates preserve playing voices. Changes to the graph,
parameter identity or a request to reload the component require preparing the score again.
Native VST3 binaries do not run in the Wasm editor.

WAV export selects offline mode (VST3 processing, CLAP render and LV2 freewheel),
then restores the previous mode. Changes run with audio stopped and rebuild
latency compensation. A plugin that rejects offline rendering prevents export.

For CLAP, LV2 and VST3, the audio graph routes the main mono/stereo buses. Additional inputs
receive silence (VST3 auxiliary buses stay inactive); additional outputs and note outputs
are not routed. Plugins with more than one note input are currently rejected. CLAP note outputs may remain
disconnected; LV2 and VST3 plugins with note outputs are rejected. Mono
effects are not duplicated implicitly on stereo signals. Parallel paths are
latency-compensated with buffers allocated before rendering; total output latency
and effect tails still need room in the score's duration. Per-note expression,
plugin-driven note routing and whole-project persistence are not implemented.
Settings lists unavailable plugins with their rejection reason. Unsupported LV2
GUI requirements are reported separately and do not prevent using the DSP.
Plugin state uses a bounded memory buffer, independently of score snapshots.
Perone uses its custom state callbacks when available, otherwise input parameters;
Perone presets share an envelope across native and Wasm (custom payload portability
belongs to the plugin). CLAP uses its state extension; VST3 stores processor and
controller state together, not a `.vstpreset`. LV2 preserves input ports and portable
properties with URI-based serialization. External files remain references, not
embedded assets; LV2 states requiring `makePath` or non-portable properties are not
supported. A failed import may already have changed the plugin; exports replace
files only after success.

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

Root C files own scores, sequencing, sessions and projection. `dsp.h` is the
engine's plugin contract. `host/` implements native DSP instances, module caching
and Perone/CLAP/LV2/VST3 adapters; each format owns its module resources. `posix/`
provides system services and native applications; `x11/` embeds plugin windows.
`web/` supplies the browser backend, `lib/` Janet composition, and `editor/` the
shared editor. Keep system calls and platform headers out of core and `host/`.
Prose and code comments use English; musical names keep their spelling.
Formatting is in `.editorconfig` and
`.clang-format` (clang-format 21).

| Check | Requirements beyond the host build |
| --- | --- |
| `make test` | Local fixtures only; no audio device or sibling checkout. |
| `make test-clap` | X11 headers/library for the fixture; no display needed. |
| `make test-clap-ui` / `make test-clap-editor` | X11 display; Node.js/Chromium and audio for the editor. |
| `make test-lv2` | X11 headers/library for the fixture; no display needed. |
| `make test-lv2-ui` / `make test-lv2-editor` | X11 display; Node.js/Chromium and audio for the editor. |
| `make test-vst3` | X11 headers/library for the fixture; no display needed. |
| `make test-vst3-ui` / `make test-vst3-editor` | X11 display; Node.js/Chromium and audio for the editor. |
| `make format-check` | clang-format 21. |
| `make test-build` / `make test-build-web` | Node.js; also Emscripten for web. |
| `make test-plugins` / `make test-brickworks` | Corresponding prebuilt bundles. |
| `make test-library-build` | Node.js/npm; builds and checks the canonical library. |
| `make test-web` / `make test-browser` | Emscripten/Node.js; Chromium for browser. |
| `make test-editor` | Native GUI build, Node.js, Chromium and an audio device. |
| `make test-parameter-ui` | Node.js and Chromium; no audio build. |
| `make test-live test-editor-ui test-score-space test-graph` | Native and browser prerequisites. |
| `make test-library` | Native and Wasm production bundles, plus editor prerequisites. |
| `make -C plugins/piano test` | Downloaded bank and Tibia; `test-web` also needs Emscripten/Node.js. |

Other focused checks are listed in the [Makefile](Makefile). Set `CHROMIUM` to
override the browser executable. Tests and generated artifacts live in `build/`;
use `renders/` for exported audio. Both are ignored. `make clean` preserves
downloads and local plugin builds; `make -C plugins clean` removes the latter.
Use one `make -j` invocation for concurrent targets sharing a build directory.

Dependency versions are pinned in [Makefile](Makefile),
[plugins/library.mk](plugins/library.mk) and [tools/lv2-deps.sh](tools/lv2-deps.sh).
`MINIAUDIO`, `JANET`, `SPORK`, `WEBUI`, `CLAP`, `VST3` and `LV2_DEPS` accept explicit paths.
Fixes live in [patches/](patches/); the build applies them
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
