# triccheballacche

A minimal scriptable DAW: Janet scores, C audio, Perone plugins. Mono and stereo
instruments feed effect chains and a stereo mix. Native and browser hosts share
the scheduler, mixer, player, editor and score projection.

## Build and run

Run commands from the repository root:

```sh
make                    # Build the native host and Janet DAW.
make test               # Core tests with local fixtures.
make -C plugins         # Build example plugins separately; see prerequisites below.
./build/daw examples/hello.janet build/hello.wav
./build/daw --play examples/hello.janet 48000
make prog               # Render Il polpo a sette gomiti.
```

The host needs a C compiler, make, git and curl. It fetches Janet 1.41.2,
miniaudio 0.11.25 (unless `../miniaudio.h` exists), and Spork's JSON module at a
pinned revision into `.deps/`. Janet and JSON are linked statically; no Janet,
Spork or jpm installation is needed. On Termux:
`pkg install clang make git curl libandroid-spawn`.

Plugins are built separately with Node.js, `dot`, Tibia and, for the original
Brickworks examples, Brickworks. See [plugin builds](plugins/README.md).
The host only loads precompiled bundles; it needs neither DSP sources nor the generator.

## Editors

The desktop editor uses [WebUI](https://github.com/webui-dev/webui) and native audio.
Its optional build needs X11 development libraries, GCC and `patch`; WebUI 2.4.2
is fetched at a pinned revision. Node.js is not a desktop runtime dependency.

```sh
make build/editor
./build/editor examples/prog/polpo.janet
./build/editor --serve examples/prog/polpo.janet  # Print a URL without opening a browser.
```

For the browser, install Emscripten, Node.js and `patch`, then build the Wasm DSPs:

```sh
make -C plugins synth_mono shape echo drums PERONE_PLATFORM=wasm32
make web-editor
node test/server.mjs
```

Open <http://localhost:8000/editor/index.html>. Polpo is the default score;
`?score=examples/rame.janet` selects another file. Publish its dependencies with
`WEB_CONTENT`, for example:

```sh
make web-editor WEB_CONTENT="lib examples plugins ../brickworks/build/perone ../asid/plugin/perone/build ../tibia/out/perone/c/build"
```

The catalog copies assets into `build/web/`; `?project=...` selects a different
catalog URL relative to the page. If Emscripten is outside `PATH`, pass absolute
`EMCC` and `EMXX` paths to the plugin build and `EMCC` to the host build.
Browser playback requires HTTPS or localhost, AudioWorklet and shared memory.
The test server supplies `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Chromium is verified; Firefox and
Safari remain unverified.

**Run** (Ctrl/Command+Enter) evaluates the current buffer at its original path,
preserving relative imports, and starts the new score. **Play** (Ctrl/Command+Space)
restarts the prepared score without evaluating Janet or recreating plugins.
**Stop** (Esc) silences playback and keeps the project and its UIs ready.
An evaluation error leaves the previous project available for Play.
Ctrl/Command+S saves.
Desktop saves replace the file atomically; web **Download** saves a local copy
and updates the session filesystem. Reloading restores the published files.
Scores are UTF-8 text without NUL bytes, up to 8 MiB.

Active event origins light up automatically, including note generators inside
functions. Editing suspends tracking until the buffer matches the running score
or is rerun. The timeline shows tracks, effect chains and MIDI notes from the last
successful run; it remains after Stop or a failed preparation. Drag or Shift+wheel
to pan, wheel to zoom in time, and Ctrl/Command+wheel to resize all tracks together.
**Follow** keeps playback in view; wheel over track names or use the scrollbar to
move vertically between tracks. Cached notes
remain visible during navigation, with density replacing individual notes when
needed. Clicking a note selects its source when available. Timing follows
rendered audio, without compensating for device latency or plugin release tails.

The editor uses a textarea and plain JavaScript. It does not yet provide syntax
highlighting, MIDI editing, parameter curves or live replacement of a running
score. Janet preparation is synchronous and cannot be interrupted by the editor.
The view can navigate without a known end; the scheduler still requires a finite
score. See [limits](#architecture-and-limits) and [tracking details](test/README.md#source-tracking).

## Score API

Save this as `score.janet` in the repository root and run
`./build/daw score.janet build/score.wav`:

```janet
(def synth (daw/plugin "plugins/synth_mono/build/plugin.perone" {:vcf_cutoff 900}))
(def filter (daw/plugin "plugins/tibia_test/build/plugin.perone" {:cutoff 2000}))
(def track (daw/track synth {:effects [filter] :gain 0.5 :pan -0.2}))

(daw/note synth 0 4 60)
(daw/param synth 1 :vcf_cutoff 6000)
(daw/param filter 2 :cutoff 500)
(daw/param track 3 :gain 0.2)
(daw/end 5)
```

Times are absolute seconds, rounded to samples. Imports are relative to the score;
plugin paths are relative to the host's working directory.

| Call | Contract |
| --- | --- |
| `daw/plugin path &opt params` | Create a plugin handle with optional initial parameter overrides. |
| `daw/track source &opt options` | Attach a source and effects; return the track mixer handle. |
| `daw/master &opt options` | Configure the optional master once; return its mixer handle. |
| `daw/note plugin start duration pitch &opt velocity` | MIDI pitch 0–127, velocity 1–127 (default 100), duration of at least one sample. |
| `daw/param node time parameter value` | Schedule an input parameter on a plugin or mixer, by keyword or index. |
| `daw/info node` | Immutable parameter descriptions: index, name, label, direction, unit, range, default, integer flag, mapping and scale points. |
| `daw/product node` | Complete immutable product metadata; `nil` for mixers. |
| `daw/schedule start bpm pattern` | Emit a pattern in quarter-note beats; return its nominal end in seconds. |
| `daw/end seconds &opt options` | Seal the score and set duration and export options. Playback/export follows successful preparation. |

Track options are `:effects [fx1 fx2 ...]`, `:gain` 0–4 (default 1), and `:pan`
−1–1 (default 0). The master accepts effects and gain; its default is unity.
Each plugin belongs to exactly one chain. Unattached plugins, unknown options,
invalid parameter names, out-of-range values and noninteger values for integer
parameters are errors. Output parameters are readable but cannot be scheduled.
`daw/info` retains the product defaults, independently of initial overrides.

At the same sample, parameters precede note-offs, then note-ons. Insertion order
breaks ties; the last value of a parameter wins. Controls are discrete events;
DSP smoothing depends on the plugin. The mono synth's `:volume` sets amplitude;
its MIDI velocity does not. Track gain remains independent and automatable.

## Patterns and musical time

`lib/pattern.janet` provides finite, immutable data without calling `daw/*`:

```janet
{:length 4 :events [[0 0.75 60] [1 1.5 64] [3 3.5 67]]}
```

Times are relative quarter-note beats. `:length` is the nominal phrase length,
not the last event's end: pickups, overhangs and zero-length phrases are allowed.
An event `[t t value]` is a point. Values are generic Janet data; constructors
copy and freeze containers, subject to Janet's limits for opaque values.

| Call | Result |
| --- | --- |
| `p/events length items` | Validate and freeze events: finite times, `length >= 0`, `start <= end`. |
| `p/steps step values` | Equal intervals; `nil` occupies a rest. Length is `step * length(values)`. |
| `p/curve length steps shape` | `steps + 1` points from `shape(x)`, including `x=0` and `x=1`; positive length. |
| `p/serial patterns` | Place phrases consecutively and sum their lengths. |
| `p/parallel patterns` | Overlay at zero and take the largest length. |
| `p/map f pattern` | Transform values only; a returned `nil` remains an event. |
| `p/stretch factor pattern` | Scale times and length by a positive factor. |
| `p/reverse pattern` | Map `[a b]` to `[length-b length-a]`, keeping values and insertion order. |

Serial and parallel preserve overhangs and list order; an empty list gives a
zero-length pattern. Repetition and alternation use ordinary Janet functions.
With `synth` already attached to a track, replace the scheduling calls above with:

```janet
(import ./lib/music)
(import ./lib/pattern :as p)

(def motif (p/steps 0.5 [60 nil 64 67]))
(def theme (p/serial [motif (p/reverse motif) (p/map |(+ $ 12) motif)]))
(def cutoff (p/curve (theme :length) 96 |(music/lerp 400 4000 $)))
(def score
  (p/parallel [(p/map |[:note synth $ 100] theme)
               (p/map |[:param synth :vcf_cutoff $] cutoff)]))
(def end (daw/schedule 0 112 score))
(daw/end (+ end 2))
```

`daw/schedule` accepts `[:note node pitch velocity]` on positive intervals and
`[:param node parameter value]` on points. Scheduling emits immediately and
preserves DSP state across repetitions. Choose `daw/end` to include note-offs and
effect tails; a control at the nominal end needs at least one extra sample.
All placed events and the nominal end must fit the host's duration limit. If a
scheduling error is caught, events already emitted remain in the session.

`lib/music.janet` contains five independent mathematical functions:

| Call | Result |
| --- | --- |
| `music/seconds bpm beats` | Beats to seconds, including fractions and negative offsets. |
| `music/bars bpm count &opt numerator denominator` | Bar duration; default meter 4/4. |
| `music/degree root intervals n` | Zero-based scale degree, repeating by octaves, including negative degrees. |
| `music/chord root intervals` | MIDI pitches in the given order; no voice allocation. |
| `music/lerp a b x` | Linear interpolation without clamping `x`. |

## Audio and export

```text
source → effects → gain/pan → stereo sum → master effects → master gain
```

A mono 1→1 effect on stereo uses two independent instances. Mono is duplicated
at equal amplitude for a 2→2 effect; a 1→2 effect accepts only mono input.
Stereo-to-mono conversion requires a 2→1 plugin. The master always outputs stereo.
Mono panning uses constant power (about −3 dB per channel at center); stereo uses
linear balance, with unity at center and no channel mixing.

The default export is stereo float32 WAV, preserving samples outside ±1.
There is no implicit clipping, DC filter, fade or normalization.
`(daw/end 30 {:format :pcm16 :normalize 0.94})` selects PCM16 (clipped to ±1)
and a target peak. Normalization ranges from 0 to 1; zero disables it. Native
normalization uses temporary disk storage. Export replaces the destination only
after successful finalization, preserving an existing WAV on failure.

`--play` streams the float mix through miniaudio and ignores export format and
normalization. The common player pads the last block with silence and drains the
device before completion; Ctrl-C or SIGTERM stops native playback. The default
sample rate is 44100 Hz, configurable as the final CLI argument; editors use 48000 Hz.

## Perone and plugin UIs

Perone is the canonical plugin format. A bundle contains `product.json` and
`<platform>/<bundleName>.so` or `wasm32/<bundleName>.wasm`, plus optional UI assets.
The binary name comes from `product.bundleName`; the bundle directory may be renamed.
`PERONE_PLATFORM` and `PERONE_SUFFIX` select the host target, normally
`<uname -m>-<lowercase TARGET_OS>` and `.so` on native builds.

Janet reads the JSON and passes numeric configuration to C. The DSP loader uses
`perone_get_api(PERONE_ABI_VERSION)`; `perone.h` is an unchanged copy of Tibia's
ABI v2. No internal `parameters.h`, C metadata tables or per-plugin Janet wrapper
is needed. Native UIs use the separate ABI v1 in `perone_ui.h`.

Click a timeline track header to open its instrument and effects in the **GUI**
panel, alongside both the editor and timeline. Each plugin has a collapsible
section with its web UI or generated controls;
expanded sections update together. The **≡** button switches to generated controls.
On Linux/X11, **↗** toggles the original native window when a `*-ui.so` is present.
Native windows remain open across track selection and panel hiding. Opening a
native window collapses that plugin's inline UI; expanding it closes the window.
`make gui` runs `examples/gui.janet` with the original Tibia and A-SID windows;
`build/daw-ui` accepts other scores. All DSP and UI binaries must be built separately.

A web UI declares `product.ui.web = "ui/index.js"`. That ES module exports
`create(element, callbacks)` and returns a view with mandatory `free()` and optional
`set_parameter(index, value)` and `msg_in(bytes)`. Callbacks provide `product`,
`set_parameter_begin`, `set_parameter`, `set_parameter_end` and `msg_write(Uint8Array)`.
UI code runs in a Shadow DOM and may load its own Wasm; resolve assets relative to
`import.meta.url`. Tibia packages it with `make ui-web UI_WEB_DIR=...`.

Tibia's original Vinci UI is available in
`../tibia/out/perone/ui/{c,cxx}/build/tibia-test.perone`. To try it, substitute one
of those paths in `examples/gui.janet` and add its build directory to `WEB_CONTENT`.
The C/C++ UI runs through `ui/index.js`, `ui/vinci-web.js` and
`wasm32/tibia-test-ui.wasm`; desktop DSP playback also needs a native DSP binary
in the same bundle.

Both backends show initial overrides, automation and output parameters. UI gestures
are clamped and rounded using JSON metadata; scheduled automation can overwrite
them at the next event. Gestures are not written back into the score.
Updates are coalesced to the latest value per parameter and applied before FIFO
messages; there is no combined ordering between parameters and messages.
Messages have 64 queue slots per direction and a maximum payload of 4096 bytes,
subject to smaller product limits. One view per node consumes output messages;
duplicated mono effects report from the left instance.

Collapsing a section or changing tracks releases its inline UI and invalidates
its callbacks. Stop and Play preserve inline and native UIs; controls remain usable
while stopped. Run replaces the plugin instances and their views.
Asynchronous creation queues gestures until attachment; a view arriving after
disposal is freed. Invalid callbacks or communication errors detach the web view.
A new attachment clears old notifications and overflow while preserving accepted
input changes. Native and web UI lifecycle tests are described in the [test guide](test/README.md).

## Examples

| Score | Content and required bundles |
| --- | --- |
| [hello](examples/hello.janet) | Minimal local synth score. |
| [automation](examples/automation.janet) | One minute of effect and mixer automation; local plugins. |
| [patterns](examples/patterns.janet) | Repetition, reversal, transposition, stretching and a filter curve; Brickworks synth. |
| [brickworks](examples/brickworks.janet) | Original C/C++ synth, compressor, mono-to-stereo pan and reverb bundles. |
| [Rame](examples/rame.janet) | About 57 seconds of electro, using only Brickworks bundles. |
| [Il polpo a sette gomiti](examples/prog/polpo.janet) | 30 seconds; local `synth_mono`, `drums`, `shape` and `echo`. |
| [Denti di vetro](examples/denti.janet) | About 63 seconds of irregular IDM; Polpo's plugins plus A-SID. |
| [Sempiterno](examples/sempiterno.janet) | 92 seconds of organ techno/electro on Soto de Langa's three-part lauda, with driving percussion and a toccata; Brickworks, A-SID, local `drums`, `shape` and `echo`. |
| [Falalalan](examples/falalalan.janet) | 110 seconds of electro/techno at 144 BPM: a melody from the supplied MIDI, new bass and harmony, acid sequences, a dub passage and chopped variations; Brickworks, A-SID, local `drums`, `shape` and `echo`. |
| [gui](examples/gui.janet) | 65 seconds of automation and UI feedback; local synth, Tibia and A-SID. |

Scores read `BRICKWORKS_PERONE`, `TIBIA_PERONE` and `ASID_PERONE` where applicable.
Defaults are `../brickworks/build/perone`, `../tibia/out/perone/c/build/tibia-test.perone`
and `../asid/plugin/perone/build/asid.perone`. Include these bundles in the web
catalog when needed. The [historical Polpo WAV](examples/prog/il_polpo_a_sette_gomiti.wav)
is preserved; `make prog` writes the current arrangement to `build/`.

`make run` plays an eight-second standalone synth demo. `build/host` accepts a
bundle with `--wav output.wav` or `--input` for microphone processing.
`make keys` runs the independent 16-voice terminal synth at 48 kHz:
`a w s e d f t g y h u j k`, with `q` to quit.

## Architecture and limits

| Location | Responsibility |
| --- | --- |
| Root C files | Engine, session, player, Janet adapter, trace bridge and score projection. |
| `lib/` | Perone metadata, score API, music, patterns and optional source tracking. |
| `posix/` | Native loader, file export, CLI, X11 and WebUI backend. |
| `web/` | Wasm loader, AudioWorklet lifecycle and browser editor backend. |
| `editor/` | Shared HTML, controller, plugin panel and timeline. |
| `plugins/`, `test/`, `examples/` | Separate plugin builds, verification and scores. |

Core files contain no platform branches or direct POSIX calls. Native Linux and
Wasm are verified. `TARGET_OS=Darwin` omits `-ldl`, but macOS remains unverified;
Windows still needs native loader, export and CLI backends. Desktop UI hosts
currently require X11. Each native platform needs matching plugin binaries.

Preparation builds and sorts all events, then closes Janet before audio starts.
The editor owns a cache of loaded modules; each prepared session owns its DSP
instances, and the player owns the audio device. Stop retains all three.
Play restores initial input parameters and mixer settings, resets DSPs and event
cursors, and discards pending host messages and edits. UI gestures are temporary;
put lasting changes in the score. Plugin reset semantics govern internal state,
including random generators; replay does not restore a serialized plugin snapshot.
Native binaries stay loaded until the editor closes, so restart it after rebuilding
a plugin. Janet caches immutable bundle metadata within each evaluation and rereads
it on Run. The web host caches compiled Wasm modules and creates DSP instances
directly in the worklet, once per prepared score.
The engine allocates no memory during processing; mixing buffers are independent
of duration, while stored events are not. Current limits are 32 tracks, 128 nodes,
8 effects per chain, 64 parameters per plugin, 3600 seconds and one export per run.
Sample rates range from 1 to 384000 Hz and remain fixed for the session.

Plugins may have one main mono/stereo audio output, at most one main mono/stereo
input, and one MIDI input. Optional sidechains remain disconnected, with at most
8 total input channels. CV, required sidechains, extra main buses and required
synchronized transport are rejected. Optional transport is left unused.
Custom plugin state persistence and asynchronous score mutation are not exposed.
Scripts and plugins must be trusted; native crashes are not isolated.

The browser uses the same miniaudio callback as native playback. Standalone DSP
Wasm modules have separate memories; allocation and memory growth are confined to
preparation. `web/host.js` provides `createHost`, `addFile` and `renderScore` for
offline interleaved stereo PCM, including score normalization; this API collects
the whole render in memory. `web/player.js` provides `createPlayerHost`,
`preparePlayer(host, path, sampleRate, source?)` and `closePlayer(host)` for streaming.
Preload scripts, imports and bundles with `addFile`, preserving their paths.

A player exposes `start()`, `stop()`, `restart()`, `time`, `status` (0 ready/running,
1 done, −1 failed, 2 stopped), `context`, `node` and `close()`. Stop suspends the
context; restart rewinds the existing score and resumes it. Await closure before
attaching another score to its host. `prepareScore` evaluates a draft while the
old player is stopped; `attachPlayer` takes ownership after closing the old player.
Cleanup retains shared memory if AudioContext closure cannot be confirmed and can
be retried.
Preparing from `source` also captures the immutable score projection; `takeView()`
transfers it once, and the caller must release it with `view_free`. Projection
queries and serialization stay outside the audio thread.

Native objects are shared under `build/native/`, with compiler-generated header
dependencies. `make clean` preserves renders; plugin cleanup is separate.
Use English for prose, comments and UI text; keep musical names.
Local C uses tabs displayed at four columns, Janet two spaces, and JavaScript
four spaces. `.editorconfig` and `.clang-format` define formatting;
`make format-check` uses clang-format 21 and excludes upstream/generated files.
The build patches copies of WebUI (asset MIME types) and miniaudio (worklet stack
cleanup); `web/audio.js` handles Emscripten's already-closed AudioContext case.
Sources and dependencies retain their respective licenses.
See the [test guide](test/README.md) for commands, prerequisites and coverage.
