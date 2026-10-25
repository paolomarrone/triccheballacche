# triccheballacche

A minimal scriptable DAW: Janet scores, C audio, Perone, CLAP, LV2 and VST3 plugins.
Native and browser editors share the interface, audio engine and 2D/3D score view.
Music can be finite or repeat indefinitely, with quantized live updates.

Linux, Windows x64 and Chromium/Wasm are supported; macOS is unverified. This is a development project: APIs may change, scripts and plugins
must be trusted, and plugin crashes are not isolated.

## Build and run

Run commands from the repository root. Install a C compiler, GNU Make, git, curl,
patch, Meson, Ninja and pkg-config. The plugin library also needs C++17, Node.js
and npm. Dependencies are fetched at pinned versions into `.deps/`; Janet and
Spork's JSON module are embedded. No Janet installation or sibling checkout is
needed for the canonical library.

```sh
make -j4 cli library
mkdir -p renders
./build/cli examples/patterns.janet renders/patterns.wav 48000
./build/cli --play examples/patterns.janet 48000
```

| Target | Output |
| --- | --- |
| `make` / `make cli` | `build/cli`: playback and WAV export. |
| `make gui` | `build/gui`: native editor. |
| `make library` | `build/library/{brickworks,mda}/`: 40 Brickworks C examples and 39 MDA plugins. |
| `make web` | `build/web/player.{mjs,wasm}` and a catalog of scripts and prebuilt Wasm bundles. |
| `make web-offline` | `build/web/offline.{mjs,wasm}`: offline rendering runtime. |
| `make tools` | `build/tools/{perone-host,plugin-scan}`: plugin diagnostics. |

All library plugins have HTML/CSS/JS UIs shared by native and Wasm. MDA retains
its C++ DSP, normalized 0–1 parameters and factory presets; the host remains C.
Looplex reserves 200 seconds during preparation; its Reset control applies the
selected length without reallocating during audio processing. Recorded loops
are not included in presets.

On Linux, the native editor needs X11 development libraries, CMake, ALSA headers,
GTK 3 and WebKitGTK 4.1 or 4.0 runtime libraries.
WebUI and PortMidi are fetched automatically.

```sh
make gui
./build/gui examples/patterns.janet
./build/gui --serve examples/patterns.janet
```

`--serve` uses a browser to control the **native** engine; audio and plugin windows
remain on the server machine. It survives page reloads and tab closure; Ctrl+C
stops it. Reopening restores the running score, but reloads the startup file from
disk; unsaved text belongs to the previous page.

For audio running **in the browser**, install Emscripten **6.0.12** and Node.js:

```sh
make -j4 library PERONE_PLATFORM=wasm32
make web
node test/server.mjs
```

Open <http://localhost:8000/editor/index.html?score=examples/patterns.janet>.
Hosting needs HTTPS or localhost, COOP/COEP headers, AudioWorklet and shared
memory. The test server supplies the headers. The score view needs WebGL 2;
editing and audio work without it. Pass absolute `EMCC`/`EMXX` paths if needed.

`make web` packages existing Wasm plugins; it does not compile them. Override
`WEB_CONTENT` to choose scripts and bundle directories, for example
`make web WEB_CONTENT="lib examples build/library/brickworks"`.
`?project=URL` selects another catalog. These are development assets, not a
complete release package.

### Windows x64

Requires Windows 10 version 1903 or newer. Install [MSYS2](https://www.msys2.org/),
open **MSYS2 UCRT64**, then:

```sh
pacman -Syu
pacman -S --needed make git patch diffutils unzip mingw-w64-ucrt-x86_64-{gcc,cmake,meson,pkgconf,nodejs}
make -j4 cli gui tools library
make test test-clap test-brickworks
./build/gui.exe examples/patterns.janet
```

Restart the shell if the MSYS2 update requests it. The same Make targets produce
native Windows binaries with an `.exe` suffix and statically linked compiler
runtimes. They also run from PowerShell without MSYS2 in `PATH`; keep DLLs, bundles,
`editor/` and scores alongside the build. Run from the repository root and close
programs before rebuilding. Repository paths may contain spaces; use relative
dependency overrides. Executables embed a UTF-8 process manifest.

The editor needs the Microsoft Edge WebView2 Runtime; Make fetches its pinned
loader DLL. For Wasm, install `mingw-w64-ucrt-x86_64-emscripten` and reopen UCRT64.
The build sets `BINARYEN_CORES=1` on MSYS2 to avoid a Binaryen 133 worker-pool hang;
`make -j` still builds independent targets concurrently.

Windows validation covers core tests, Brickworks DSPs, WAV export, editor
playback, WebView2 and CLAP audio/state/windows. MDA, LV2/VST3 with real plugins
and MIDI hardware still need Windows validation. Native Perone UI ABI v1 only
defines X11 and web; use web UIs or shared parameter controls on Windows.
Run `node test/windows.mjs` for the editor smoke test (`CHROMIUM` may point to
Edge), `make test-webview` and `make test-clap-ui` for native windows.
LV2/VST3 fixtures and Unix fault-injection tests remain Linux-only.

Plugin path lists use `;`. Discovery defaults to
`%LOCALAPPDATA%/Programs/Common/{CLAP,VST3}`, `%APPDATA%/LV2` and
`%COMMONPROGRAMFILES%/{CLAP,LV2,VST3}`; settings live in
`%APPDATA%/triccheballacche/settings.json`. Prefer forward slashes in Janet paths.

## Write music

Save as `score.janet` in the repository root after building the library:

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
For a finite score, use `(daw/score pattern {:duration 30})`; export with
`./build/cli score.janet renders/score.wav`. A graph without scheduled events needs
neither `daw/score` nor `daw/end`: Apply lets you play MIDI and adjust plugins with
the timeline stopped. WAV export requires a positive duration.

- **Graph:** `daw/plugin` creates an instance, `daw/through` connects an effect,
  `daw/mix` combines signals and `daw/output` selects the output. `daw/track` adds
  gain/pan and a visible lane, with optional `:effects`; `daw/master` adds a master
  mixer. Reusing a handle shares its audio. Mix before a shared effect; cycles
  and disconnected nodes are errors.
- **Patterns:** immutable data in quarter-note beats. `p/steps`, `p/events` and
  `p/curve` create them; `p/serial`, `p/parallel`, `p/map`, `p/stretch`, `p/reverse`
  and `p/loop` compose them. Notes use `[:note node pitch velocity]` on intervals;
  automation uses `[:param node parameter value]` on points. Repetitions are
  queried as needed. Janet runs once per revision, outside the audio callback.
- **Seconds:** `daw/note`, `daw/param`, `daw/midi-event` and `daw/end` build an
  absolute timeline. `daw/schedule start bpm pattern` places a finite pattern in it.
- **Export:** stereo float32 WAV by default, without clipping or normalization.
  `(daw/end 30 {:format :pcm16 :normalize 0.94})` selects PCM16 and a target peak.
  Leave time for release and effect tails. Playback ignores export normalization.
  CLI defaults to 44100 Hz (last argument overrides); editors start at 48000 Hz.

Function documentation is in [lib/daw.janet](lib/daw.janet),
[lib/pattern.janet](lib/pattern.janet) and [lib/music.janet](lib/music.janet).
`daw/info` and `daw/product` expose plugin metadata.

## Editor

Open with the folder icon, enter a path and press Enter, or choose an example.
Save writes to disk on native; web Download saves a copy and updates session
files. A web reload restores published files.

| Control | Behavior |
| --- | --- |
| Apply · Ctrl/Command+Enter | Evaluate code/imports and enable audio. Preserve stopped/playing state. |
| Play/Stop · Ctrl/Command+Space | Evaluate changed code before playing; stop holds position and releases sequenced notes. Esc also stops. MIDI and effect tails remain active. |
| Ruler / time field | Seek, resetting DSP history and restoring controls. Spanning notes restart on Play. |
| Follow | Keep the playhead visible. |
| Automation | Show/hide curves; each lane's checklist selects parameters. Audio automation is unaffected. |
| Routing node | Inspect its signal path in the plugin panel. Background/Escape clears selection. Routing is read-only. |
| M / S | Mute/solo tracks and routed groups for audition. |
| Plugin window icon | Detach controls/web UI into a floating window, fullscreen on small screens. Native-only UIs open their original window. Closing returns web controls to the panel. |
| Plugin ↓ / ↑ | Import/export `.tbpreset.json` (up to 16 MiB). Both pause playback. |

Opening a file keeps the current music running; Apply switches scores. During
playback, Apply queues `daw/score` revisions on the next beat grid (`:quantum`,
default 4). Use stable keyword identities for plugins. Graph, identities, tempo,
duration and plugin configurations must match; stop before changing them.
Errors leave current music running; stop or seek cancels queued revisions.

In 2D, drag or Shift+wheel pans time, wheel zooms time, and Ctrl/Command+wheel
resizes tracks. Wheel over names scrolls tracks. Touch supports drag and pinch.
The left divider resizes the routing map. In 3D, drag orbits, right-drag pans and
Alt+wheel zooms time; Reset view restores camera and zoom at the playhead.
Code/Score switches views on small screens. Notes, curves and execution highlights
link back to the code.

The CodeJar editor provides Janet highlighting, indentation, paired brackets and
undo/redo. Known limits: whole-document paste can append a newline, and delayed
highlighting can reclaim focus immediately after typing.

Manual edits and presets affect the prepared plugin, not Janet; seek/reevaluation
restores score parameters. Knobs in library UIs support vertical drag, Shift for
fine adjustment, double-click reset and numeric entry (normalized 0–1 for MDA).

Settings contains Audio, Plugins and MIDI. Turn audio off to change sample rate;
the next Play/Apply rebuilds the graph. Native plugin paths accept `~/`, folders
and bundles. Apply saves and rescans; Rescan only refreshes discovery. Empty lists
disable discovery, while explicit score paths still work. Paths also affect CLI
lookup and, on Linux, persist in `$XDG_CONFIG_HOME/triccheballacche/settings.json`
(default `~/.config/…`). The browser lists its published Perone catalog.
Restart after rebuilding a loaded native plugin.

## MIDI

Connect devices to score nodes in Settings → MIDI. Apply enables monitoring
without starting the timeline. Turning audio off releases device-held notes;
new graphs clear bindings, live revisions retain them. Native Refresh disconnects
ports and rescans. Bindings are session-only. Native uses PortMidi; browsers use
Web MIDI with device/SysEx permission where supported.

```janet
(def keys (daw/midi :keys))
(daw/midi-through keys synth)    # MIDI routing is separate from audio.
(daw/midi-output keys)           # Retain output for an external device.
(daw/note keys 0 0.5 60 100 3)   # Optional channel 0–15; default 0.
(daw/midi-event keys 0 [0xb3 1 64])
```

Patterns accept `[:midi node [bytes...]]` and `[:note node pitch velocity channel]`.
Routing supports fan-in/out and MIDI-only plugins, preserving sample offsets.
Device input enters at the next audio block; output follows the audio clock with
OS/browser timer precision. Offline rendering excludes devices.

CLAP/LV2/VST3 expose at most one MIDI input/output, with MIDI 1.0 and SysEx up to
1 KiB (256 events / 16 KiB per stream; overflow stops playback). Perone accepts
short input messages only. CLAP controllers require the MIDI dialect; VST3 uses
plugin MIDI mappings. MIDI 2.0, per-note expression, recording and `.mid`
import/export are not implemented.

## Plugins

| Format | Reference to `daw/plugin` | Parameters / UI |
| --- | --- | --- |
| Perone | `"path/to/plugin.perone"` | `product.json` IDs; native X11 or web UI. Native and Wasm DSP. |
| CLAP | `{:format :clap :id "plugin.id"}` | Native numeric IDs; X11/Win32 UI. |
| LV2 | `{:format :lv2 :id "urn:plugin:uri"}` | Port symbols; X11/Win32 UI. |
| VST3 | `{:format :vst3 :id "32-digit-processor-class-id"}` | Native numeric IDs; normalized 0–1 values, integer indices for discrete parameters; X11/Win32 UI. |

CLAP/LV2/VST3 run in the native host only. Add `:path` to select an explicit module or
bundle; `:id` can be omitted only for a single supported plugin. No Janet wrapper
is needed. `build/tools/plugin-scan PATH` lists IDs, parameters and rejection reasons.

Perone bundles contain `product.json`, `<platform>/<bundleName>.so` (`.dll` on
Windows) or `wasm32/<bundleName>.wasm`, and optional UI assets. Copy the whole bundle.
Tibia supplies DSP ABI v2 and native UI ABI v1; web UIs declare `product.ui.web`
and may load their own Wasm. Perone transport supplies play state, BPM and beat.

| Discovery override | Default |
| --- | --- |
| `BRICKWORKS_PERONE` | `build/library/brickworks` |
| `MDA_PERONE` | `build/library/mda` |
| `ASID_PERONE` | `../asid/plugin/perone/build/asid.perone` |
| `TIBIA_PERONE` | `../tibia/out/perone/c/build/tibia-test.perone` |
| `CLAP_PATH`, `VST3_PATH` | Colon-separated additions to `~/.<format>`, `/usr/local/lib/<format>`, `/usr/lib/<format>`. |
| `LV2_PATH` | Replaces the same default locations for LV2 when set. |

Perone also searches `plugins/`. Each platform lists only bundles with its DSP.
Native discovery runs in a separate process with a timeout/cache; playback uses
independent instances and cached modules in the host. Original GUIs and shared
parameter panels use the same DSP. Unavailable plugins include a rejection reason.

The graph routes main mono/stereo buses; additional inputs receive silence and
additional outputs are not routed. CLAP/LV2/VST3 mono effects are not implicitly
duplicated on stereo signals. Parallel paths are latency-compensated; leave room
for output latency and tails in the score. WAV export requests offline processing;
a plugin that rejects it prevents export. Structural plugin changes require
preparing the score again.

LV2 supports Atom MIDI/transport, workers and bundle default state. Unknown required
features/options and fixed/power-of-two blocks are unsupported. CV and non-sequence
Atom ports must be optional. Atom buffers honor declared sizes (64 KiB default, 1 MiB limit);
worker messages are limited to 4 KiB. Numeric `patch` properties are not Janet
parameters; original UIs can exchange Atom messages. GTK/Qt-only UIs are not embedded.

Presets use Perone callbacks or input parameters, CLAP state, LV2 portable
properties/input ports, or VST3 processor/controller state (not `.vstpreset`).
Perone's envelope is shared across native/Wasm; custom payload portability belongs
to the plugin. External files remain references. LV2 `makePath` and non-portable
state are unsupported. Failed imports can partially change a plugin. Whole-project
persistence is not implemented.

Local plugin projects use sibling Tibia/Brickworks checkouts by default. Tibia
needs the Perone templates and its `dot` npm dependency. To use other checkouts,
including the pinned downloads from `make library-deps`, override their paths:

```sh
make -C plugins TIBIA=/path/to/tibia BRICKWORKS=/path/to/brickworks
# Add PERONE_PLATFORM=wasm32 for the browser.
```

On Windows, use relative paths without spaces, resolved from `plugins/<name>/`.

This builds `synth_mono`, `fx_svf`, `tibia_test`, `drums`, `shape` and `echo` under
`plugins/<name>/build/plugin.perone`. Drums uses notes 0–6: kick, snare, hat, open
hat, crash, high tom, low tom. The optional piano needs Python 3:
`python3 plugins/piano/fetch.py`, then `make -C plugins/piano` with the same Tibia
setting. Build A-SID and Tibia's original DSP/UI bundles in their own repositories.

## Examples

| Examples | Requirements / focus |
| --- | --- |
| [patterns](examples/patterns.janet), [controls](examples/controls.janet), [Rame](examples/rame.janet) | Canonical Brickworks library; patterns, automation and plugin UIs. Start with patterns. |
| [mda](examples/mda.janet) | MDA library instruments and effects. |
| [live](examples/live.janet), [routing](examples/routing.janet), [midi](examples/midi.janet), [Polpo](examples/polpo.janet), [Oculus](examples/oculus.janet) | Local plugins; loops, routing, MIDI and complete pieces. |
| [gui](examples/gui.janet) | A-SID and Tibia UIs. |
| [Denti](examples/denti.janet), [Sempiterno](examples/sempiterno.janet), [Falalalan](examples/falalalan.janet) | Local plugins and A-SID. |
| [Campagnola](examples/campagnola.janet), [Insetti di vetro](examples/insetti-di-vetro.janet) | Also require the sampled piano. |
| [surge](examples/surge.janet) | Surge XT/Effects 1.3.4; choose CLAP, LV2 or VST3 in the score. |
| [Crocevia](examples/crocevia.janet) | Perone, Surge CLAP, A-SID LV2 (`ASID_LV2` override) and Brickworks comb VST3. |
| [Denoiser](examples/denoiser.janet) | Perone, Clack.DIGITAL Ue Ue Wah and Bertom Denoiser Classic VST3. |
| [Due corpi](examples/due-corpi.janet) | Samplo/Vitsel CLAP, local drums and Brickworks. |

For Due corpi, generate its SFZ bank with `python examples/due-corpi.py DIRECTORY`
in Samplo's library; `SAMPLO_INSTRUMENT` selects its index (default 0). Samplo 1.2.7
skips instrument 0 at startup and plays a test tone; that needs an upstream fix.
Imported themes live in `examples/sources/`, outside the examples dropdown.

## Development

Core C files own scores, sequencing, sessions and projection. `dsp.h` is the plugin
contract; `host/` implements native formats and module caching. `native/` holds
shared native applications/services; `posix/` and `win32/` supply system backends.
`x11/` and `win32/` host plugin windows. `web/` is the browser backend, `editor/`
the shared UI, `lib/` the Janet API. Plugin DSP and UI sources
live in `plugins/`. Keep system calls/platform headers out of core and `host/`.
Use English for prose/comments; keep musical names. Formatting is defined by
`.editorconfig` and `.clang-format` (clang-format 21).

```sh
make -j4 test test-clap test-lv2 test-vst3 test-midi
make format-check
make test-web test-editor-ui       # Emscripten, Node.js, Chromium, native audio
make test-mda test-brickworks-ui   # Also requires the production plugin library
```

Prebuild the library for both native and Wasm before `test-mda`.
`make test` uses local fixtures without an audio device or sibling checkout.
On Linux, format tests need X11 headers/library, but no display; `test-<format>-ui` needs
X11 and `test-<format>-editor` also needs Chromium/audio. Focused checks live in
[Makefile](Makefile) and [test/](test/); set `CHROMIUM` to override the browser.
Tests/artifacts go in `build/`, exported audio in `renders/`. `make clean` preserves
`.deps/` and local plugin builds; `make -C plugins clean` removes the latter.
Use one `make -j` invocation for targets sharing a build directory.

Dependency pins live in [Makefile](Makefile), [plugins/library.mk](plugins/library.mk),
[plugins/mda/library.mk](plugins/mda/library.mk) and [tools/lv2-deps.sh](tools/lv2-deps.sh).
Fixes in [patches/](patches/) apply to generated copies, leaving downloads intact.

## License

Copyright (C) 2024–2026 Paolo Marrone. Original code, documentation and original
example contributions are **GPL-3.0-only**; see [LICENSE](LICENSE).
Third-party code and musical material retain their terms; credits and notices
are in [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES), including piano samples,
Three.js and CodeJar. Distributions must include notices and corresponding source,
dependency versions and local patches as required by their licenses.
