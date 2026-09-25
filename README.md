# triccheballacche

A minimal scriptable DAW: Janet scores, C audio, Perone plugins. Mono and stereo
audio nodes form a graph of instruments, effects and stereo mixes. Native and
browser hosts share the scheduler, mixer, player, editor and score projection.

## Build and run

Run commands from the repository root:

```sh
make                    # Build the native CLI.
make test               # Core tests with local fixtures.
make -C plugins         # Build example plugins separately; see prerequisites below.
mkdir -p renders
./build/cli examples/hello.janet renders/hello.wav
./build/cli --play examples/hello.janet 48000
make prog               # Render Il polpo a sette gomiti.
```

The host needs a C compiler, make, git, curl and patch. It fetches Janet 1.42.1,
miniaudio 0.11.25 (unless `../miniaudio.h` exists), and Spork's JSON module at a
pinned revision into `.deps/`. Janet and JSON are linked statically; no Janet,
Spork or jpm installation is needed. [janet.patch](janet.patch) fixes collection of
top-level dynamic bindings in both runtimes, keeping error diagnostics valid after GC.
On Termux: `pkg install clang make git curl patch libandroid-spawn`.

Plugins are built separately with Node.js, `dot`, Tibia and, for the original
Brickworks examples, Brickworks. See [plugin builds](plugins/README.md).
The host only loads precompiled bundles; it needs neither DSP sources nor the generator.

| Build command | Output |
| --- | --- |
| `make` or `make cli` | `build/cli`: score playback and WAV export. |
| `make gui` | `build/gui`: desktop editor and native plugin windows. |
| `make web` | `build/web/player.{mjs,wasm}` and the browser project catalog. |
| `make web-offline` | `build/web/offline.{mjs,wasm}`: offline browser/Node rendering. |
| `make tools` | `build/tools/perone-host`: single-plugin diagnostics. |

These commands build without launching. Tests and their artifacts live under
`build/test/`, objects under `build/obj/`, and generated sources under
`build/generated/`. Keep exported audio in `renders/`; it is ignored by Git.
The browser editor needs only the playback runtime; offline rendering is optional.

## Editors

The desktop editor uses [WebUI](https://github.com/webui-dev/webui)'s embedded
WebView and native audio, with its own window title and icon. Its optional build
needs X11 development libraries; WebUI 2.5 prerelease is fetched at a pinned revision.
On Linux, the window uses GTK 3 and WebKitGTK 4.1 (or 4.0) runtime libraries.
Node.js is not a desktop runtime dependency. `--serve` runs without a WebView and
prints a localhost URL for opening the same editor in a browser.

```sh
make gui
./build/gui examples/prog/polpo.janet
./build/gui --serve examples/prog/polpo.janet  # Print a URL without opening a browser.
```

For the browser, install Emscripten, Node.js and `patch`. Build the Perone `wasm32`
bundles separately in Brickworks, A-SID and Tibia, then the local plugins:

```sh
make -C plugins PERONE_PLATFORM=wasm32
make web
node test/server.mjs
```

Open <http://localhost:8000/editor/index.html>. Polpo is the default score;
`?score=examples/rame.janet` selects another file. The default catalog includes all
scores, six local bundles, the 40 Brickworks C examples, A-SID and Tibia's C test
product: 48 bundles. Brickworks C++ variants are excluded. The local `synth_mono`
and `fx_svf` bundles remain available at the paths used by existing scores.
All default examples have their dependencies in this catalog.

Override `WEB_CONTENT` to publish a smaller or different project, for example
Polpo with only the local plugins:

```sh
make web WEB_CONTENT="lib examples/prog plugins"
```

The catalog copies assets into `build/web/`; `?project=...` selects a different
catalog URL relative to the page. If Emscripten is outside `PATH`, pass absolute
`EMCC` and `EMXX` paths to the plugin build and `EMCC` to the host build.
Browser playback requires HTTPS or localhost, AudioWorklet and shared memory.
The test server supplies `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Chromium is verified; Firefox and
Safari remain unverified.

**Run** (Ctrl/Command+Enter) evaluates the current buffer at its original path,
preserving relative imports. A running `daw/score` accepts compatible musical
revisions on its beat grid without restarting audio or plugins. Otherwise Run
prepares and starts a new session. **Play** (Ctrl/Command+Space)
resumes the prepared score without evaluating Janet or recreating plugins.
**Stop** (Esc) holds the position and DSP state, keeping the project and its UIs ready.
Click the timeline ruler to seek, or enter seconds in the time field and press Enter.
The return-to-start button, or Home in the timeline, seeks to zero. Play at the end
of a finite score starts it again.
An evaluation error leaves the previous project available for Play.
Ctrl/Command+S saves.
Desktop saves replace the file atomically; web **Download** saves a local copy
and updates the session filesystem. Reloading restores the published files.
Scores are UTF-8 text without NUL bytes, up to 8 MiB.

The path bar contains **Open** and **Save** icons. Open selects a Janet file;
pressing Enter in the path field opens the typed path. **Examples** loads a score
immediately from its dropdown. Replacing an unsaved buffer asks for confirmation.
The shared file picker browses the desktop filesystem or the web session's files.
On the web, **Choose from device** imports a local file into the selected session
directory; its relative imports must already be available there.

The top-right **Settings** icon opens a modal with a **Plugins** category: plugin
sources, available bundles and instance counts from the last successful Run.
The browser uses its published catalog; the desktop discovers local plugins,
Brickworks C bundles, A-SID, Tibia C and files under `examples/`, honoring the
plugin path environment variables below. Discovery reads metadata without
creating DSPs. Reload after adding bundles. The right panel controls the selected
track's plugin instances.

Active event origins light up automatically, including note generators inside
functions. Editing suspends tracking until the buffer matches the running score
or is rerun. The timeline shows tracks, effect chains and MIDI notes from the last
successful run; it remains after Stop or a failed preparation. Drag or Shift+wheel
to pan, wheel to zoom in time, and Ctrl/Command+wheel to resize all tracks together.
**Follow** keeps playback in view; wheel over track names or use the scrollbar to
move vertically between tracks. Cached notes
remain visible during navigation, with density replacing individual notes when
needed. Clicking a note selects its source when available. Seeking restores
automation and retriggers notes spanning the destination; envelopes start anew
and delay/reverb history is cleared. It never renders the intervening audio,
including for unbounded scores. Timing follows
rendered audio, without compensating for device latency or plugin release tails.

**M** mutes a track; **S** solos it. Multiple solos play together, and mute takes
precedence. Solo follows graph paths: a group retains its upstream sources; a
source retains its downstream processing. Other parallel routes are excluded,
including direct paths that share the same source. Audition changes fade over
5 ms and leave score automation and DSP clocks running. Stop/Play preserves them;
a new session clears them; live revisions preserve them. A track over a mix is a group row; its notes stay on the
original source rows, and its plugin panel stops at upstream track boundaries.

The editor uses a textarea and plain JavaScript. It does not yet provide syntax
highlighting, MIDI editing or parameter curves. Preparation runs outside the audio
thread: a native preparation thread or a browser Worker. CPU-bound Janet evaluation
is interrupted after five seconds natively; browser preparation has a five-second
timeout including Worker startup. Native blocking I/O and extensions remain trusted.
The timeline projects the current musical revision, rather than recording a history
of the performance. After a live update, tracking starts with events actually
started by the new revision. See [live scores](#live-scores) and [tracking details](test/README.md#source-tracking).

## Score API

Save this as `score.janet` in the repository root and run
`./build/cli score.janet renders/score.wav`:

```janet
(def synth (daw/plugin "plugins/synth_mono/build/plugin.perone" {:vcf_cutoff 900}))
(def filter (daw/plugin "plugins/tibia_test/build/plugin.perone" {:cutoff 2000}))
(def track (daw/track synth {:effects [filter] :gain 0.5 :pan -0.2}))
(daw/output track)

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
| `daw/plugin path &opt params` | Create a plugin handle with optional initial parameter overrides. A leading keyword (`daw/plugin :bass path params`) gives it a stable live identity. |
| `daw/through signal effect` | Bind an effect's input once; return its output handle. |
| `daw/mix signals &opt options` | Sum one or more signals into a stereo gain/pan node. |
| `daw/track signal &opt options` | Add effects and a visible mixer point with mute/solo. |
| `daw/master signal &opt options` | Add an optional mixer point labeled Master, without mute/solo. |
| `daw/output signal` | Select the sole final output. Tracks and master never connect implicitly. |
| `daw/note plugin start duration pitch &opt velocity` | MIDI pitch 0–127, velocity 1–127 (default 100), duration of at least one sample. |
| `daw/param node time parameter value` | Schedule an input parameter on a plugin or mixer, by keyword or index. |
| `daw/info node` | Immutable parameter descriptions: index, name, label, direction, unit, range, default, integer flag, mapping and scale points. |
| `daw/product node` | Complete immutable product metadata; `nil` for mixers. |
| `daw/schedule start bpm pattern` | Emit a pattern in quarter-note beats; return its nominal end in seconds. |
| `daw/tempo bpm` | Set the tempo for `daw/score`; default 120. |
| `daw/score pattern &opt options` | Prepare finite or repeating music; options `:quantum` (beats, default 4) and `:duration` (seconds). |
| `daw/end seconds &opt options` | Seal the score and set duration and export options. Playback/export follows successful preparation. |

Mix options are `:gain` 0–4 (default 1) and `:pan` −1–1 (default 0).
Tracks and master also accept `:effects [fx1 fx2 ...]` and an optional `:name`.
A handle always refers to the same node: sharing it reuses the audio, while calling
`daw/plugin` again creates another instance. Each effect has one input; mix signals
before a shared effect. Cycles, nodes that do not reach the output, unknown options,
invalid parameter names, out-of-range values and noninteger values for integer
parameters are errors. Output parameters are readable but cannot be scheduled.
`daw/info` retains the product defaults, independently of initial overrides.

At the same sample, parameters precede note-offs, then note-ons. Insertion order
breaks ties; the last value of a parameter wins. Controls are discrete events;
DSP smoothing depends on the plugin. The mono synth's `:volume` sets amplitude;
its MIDI velocity does not. Track gain remains independent and automatable.

## Patterns and musical time

`lib/pattern.janet` provides immutable musical data without calling `daw/*`.
A finite phrase retains its simple representation:

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
| `p/reverse pattern` | Reflect a finite phrase around its length; reverse before looping. |
| `p/loop pattern` | Repeat a positive-length finite phrase indefinitely. |
| `p/query pattern from to &opt limit` | Whole events overlapping the half-open beat interval, with stable `:id`, `:start`, `:end`, `:value`; default limit 65536. |

Serial and parallel preserve overhangs and list order; an empty list gives a
zero-length pattern. Finite repetition and alternation use ordinary Janet functions. Unbounded sources
compose with `parallel`, `map` and `stretch`; only the last member of `serial` may
be unbounded. Each loop keeps its own period and begins at occurrence zero.
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

## Live scores

[examples/live.janet](examples/live.janet) runs indefinitely on native and web:

```janet
(import ./lib/pattern :as p)
(def bass (daw/plugin :bass "plugins/synth_mono/build/plugin.perone" {:vcf_cutoff 900}))
(daw/output (daw/track bass {:gain 0.3}))
(daw/tempo 132)
(daw/score (p/loop (p/map |[:note bass $ 100] (p/steps 0.5 [36 nil 43 39]))))
```

Run again after editing the notes, automation or initial parameters. A compatible
revision enters at the next `:quantum` boundary, preserving global phase, DSP state,
UI instances and mute/solo. Preparation errors leave the current music running.
Only one revision may be queued. Stop or seeking cancels a pending revision;
seeking uses the latest active revision across the timeline. Play resumes it.
Changed declared parameters are applied at the boundary;
unchanged declarations preserve the current values, including temporary UI edits.

Plugin identities are explicit keywords. A track defaults to `track/<source-id>`;
use `{:id :name}` for mixers or tracks that need an explicit identity. Updates require
the same identities, plugin configurations, routing, visible track order, duration
and tempo. Stop before changing these. Declaration order may change; identities
map the new descriptions to the existing instances.

Events are compiled into finite templates with optional repetition periods. The
scheduler keeps one cursor per template in a heap and computes occurrence times
from absolute positions, without accumulating rounding error. It allocates nothing
and executes no Janet during audio processing. Memory is independent of elapsed
time; the sample clock is 64-bit on native and Wasm. There is a 65536-template limit
and periods/note lengths must be at least one sample. Timing remains exact to the
nearest sample within the double-precision integer range.

A revision replaces future note starts and parameter events. Already-started notes
retain their note-offs. Retriggering the same pitch on the same plugin explicitly
ends the previous note and replaces its deadline; an obsolete note-off cannot stop
the replacement. This is a MIDI note policy, not independent per-note expression.

Janet functions build a revision once; they are not callbacks invoked on every
cycle. `p/query` is independent of previous queries and retains whole note intervals
and stable `[source,event,cycle]` identities. Rendering skips note starts before
zero. The timeline computes repetitions and density only for the requested window.
Arbitrary stateful generators and changes to the audio graph during playback are
outside this first live contract.

Omitting `:duration` uses a finite pattern's nominal length, or runs an unbounded
pattern until stopped. Export needs an explicit finite interval: add
`{:duration 30}` to `daw/score` to render thirty seconds with the same scheduler.
This also sets the playback duration. Allow extra time explicitly for effect tails.
The existing `daw/note`, `daw/param`, `daw/schedule` and `daw/end` score API remains
available; choose one scheduling API per score.

## Audio and export

Audio topology is an acyclic graph, fixed during preparation. Janet functions
compose signals; existing patterns automate gains and plugin parameters over time.
For example, with `drums` and `bass` already defined:

```janet
(def rhythm (daw/mix [drums bass]))
(def dry (daw/mix [rhythm]))
(def wet (daw/mix [(daw/through rhythm
  (daw/plugin "plugins/shape/build/plugin.perone" {:drive 3}))] {:gain 0}))
(daw/output (daw/mix [dry wet]))
(daw/param dry 8 :gain 0)
(daw/param wet 8 :gain 1)
```

Use a curve of parameter events for a gradual crossfade. Gain before an effect
controls its input and preserves its tail; gain after it controls the tail too.
Each node runs once per block even when inaudible, keeping DSP state and automation
advancing. Master uses the same graph renderer as every other mixer.
[The routing example](examples/routing.janet) demonstrates a shared rhythm bus,
parallel distortion/echo and a crossfade.

A mono 1→1 effect on stereo uses two independent instances. Mono is duplicated
at equal amplitude for a 2→2 effect; a 1→2 effect accepts only mono input.
Stereo-to-mono conversion requires a 2→1 plugin. Mixers output stereo; a mono final output is duplicated at equal amplitude.
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
Run `./build/gui examples/gui.janet` to try the original Tibia and A-SID UIs.
All DSP and UI binaries must be built separately.

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
while stopped. A compatible live Run preserves plugin instances and their views;
a new session replaces them.
Asynchronous creation queues gestures until attachment; a view arriving after
disposal is freed. Invalid callbacks or communication errors detach the web view.
A new attachment clears old notifications and overflow while preserving accepted
input changes. Native and web UI lifecycle tests are described in the [test guide](test/README.md).

## Examples

| Score | Content and required bundles |
| --- | --- |
| [live](examples/live.janet) | Unbounded bass and independent cutoff loop; quantized live revisions, local synth and echo. |
| [hello](examples/hello.janet) | Minimal local synth score. |
| [routing](examples/routing.janet) | Shared rhythm bus, parallel distortion/echo and a crossfade; local plugins. |
| [automation](examples/automation.janet) | One minute of effect and mixer automation; local plugins. |
| [patterns](examples/patterns.janet) | Repetition, reversal, transposition, stretching and a filter curve; Brickworks synth. |
| [brickworks](examples/brickworks.janet) | Original C synth, compressor, mono-to-stereo pan and reverb bundles. |
| [Rame](examples/rame.janet) | About 57 seconds of electro, using only Brickworks bundles. |
| [Il polpo a sette gomiti](examples/prog/polpo.janet) | 30 seconds; local `synth_mono`, `drums`, `shape` and `echo`. |
| [Denti di vetro](examples/denti.janet) | About 63 seconds of irregular IDM; Polpo's plugins plus A-SID. |
| [Sempiterno](examples/sempiterno.janet) | 92 seconds of organ techno/electro on Soto de Langa's three-part lauda, with driving percussion and a toccata; Brickworks, A-SID, local `drums`, `shape` and `echo`. |
| [Oculus](examples/oculus.janet) | 161 seconds of techno trance in 4/4 at 150 BPM, using both complete voices of the supplied “Oculus non vidit” MIDI; detuned leads, rolling bass, acid sequences, double-time variations and the original cadence. Local `synth_mono`, `drums`, `shape` and `echo`; [source and arrangement notes](examples/oculus.md). |
| [Campagnola Stomp](examples/campagnola.janet) | 138 seconds of solo honky-tonk piano on the supplied Gigione MIDI: habanera, ragtime and Charleston, a lyrical trio, a B-flat bridge, exchanges between hands and a slowing final cadence. Requires the optional native [sampled piano](plugins/piano/README.md); [arrangement notes](examples/campagnola.md). |
| [Falalalan](examples/falalalan.janet) | 110 seconds of electro/techno at 144 BPM: a melody from the supplied MIDI, new bass and harmony, acid sequences, a dub passage and chopped variations; Brickworks, A-SID, local `drums`, `shape` and `echo`. |
| [gui](examples/gui.janet) | 65 seconds of automation and UI feedback; local synth, Tibia and A-SID. |

Scores read `BRICKWORKS_PERONE`, `TIBIA_PERONE` and `ASID_PERONE` where applicable.
Defaults are `../brickworks/build/perone`, `../tibia/out/perone/c/build/tibia-test.perone`
and `../asid/plugin/perone/build/asid.perone`. The default web catalog includes
these bundles. The [historical Polpo WAV](examples/prog/il_polpo_a_sette_gomiti.wav)
is preserved; `make prog` writes the current arrangement to `renders/`.

`make tools` builds `build/tools/perone-host`, a standalone plugin diagnostic host.
Pass it a bundle to play an eight-second demo, add `--wav renders/demo.wav` to
export it, or `--input` to process microphone input.

## Architecture and limits

| Location | Responsibility |
| --- | --- |
| Root C files | Engine, session, player, Janet adapter, trace bridge and score projection. |
| `lib/` | Perone metadata, score API, music, patterns and optional source tracking. |
| `posix/` | Native loader, file export, CLI, X11 and WebUI backend. |
| `web/` | Wasm loader, AudioWorklet lifecycle and browser editor backend. |
| `editor/` | Shared HTML, controller, plugin panel and timeline. |
| `tools/` | Standalone Perone diagnostic host. |
| `plugins/`, `test/`, `examples/` | Separate plugin builds, verification and scores. |

Core files contain no platform branches or direct POSIX calls. Native Linux and
Wasm are verified. `TARGET_OS=Darwin` omits `-ldl`, but macOS remains unverified;
Windows still needs native loader, export and CLI backends. Desktop UI hosts
currently require X11. Each native platform needs matching plugin binaries.

Preparation validates channel layouts, orders the audio graph and compiles events,
then closes Janet. A description owns no DSP instances or audio buffers.
Initial activation creates those resources in one place; compatible
revisions replace only the sequence. Browser workers transfer owned descriptions
using a private same-build snapshot, without sharing Janet objects or plugin pointers.
The editor owns a cache of loaded modules; each prepared session owns its DSP
instances, and the player owns the audio device. Stop retains all three.
Play resumes the existing state. Seeking restores score parameters and held notes,
resets DSPs and event cursors, and discards pending host messages and edits.
UI gestures are temporary; put lasting changes in the score. Plugin reset semantics govern internal state,
including random generators; replay does not restore a serialized plugin snapshot.
Native binaries stay loaded until the editor closes, so restart it after rebuilding
a plugin. Janet caches immutable bundle metadata within each evaluation and rereads
it on Run. The web host caches compiled Wasm modules and creates DSP instances
directly in the worklet, once per activated session.
The engine allocates no memory during processing; mixing buffers are independent
of duration, while stored events are not. Current limits are 32 tracks, 128 nodes,
8 effects in a track convenience call (longer paths use `daw/through`),
64 parameters per plugin, and one export per run. Finite exports and the original
absolute-time API retain the 3600-second limit; unbounded `daw/score` playback does not.
Sample rates range from 1 to 384000 Hz and remain fixed for the session.

Plugins may have one main mono/stereo audio output, at most one main mono/stereo
input, and one MIDI input. Optional sidechains remain disconnected, with at most
8 total input channels. CV, required sidechains, extra main buses and required
synchronized transport are rejected. Optional transport is left unused.
Custom plugin state persistence and live graph replacement are not exposed.
Scripts and plugins must be trusted; native crashes are not isolated.

The browser uses the same miniaudio callback as native playback. Standalone DSP
Wasm modules have separate memories; allocation and memory growth are confined to
preparation. `web/host.js` provides `createHost`, `addFile` and `renderScore` for
offline interleaved stereo PCM, including score normalization; this API collects
the whole render in memory. `web/player.js` provides `createPlayerHost`,
`preparePlayer(host, path, sampleRate, source?)` and `closePlayer(host)` for streaming.
Preload scripts, imports and bundles with `addFile`, preserving their paths.

A player exposes `start()`, `stop()`, `seek(seconds)`, `time`, `duration`, `status` (0 ready/running,
1 done, −1 failed, 2 stopped), `context`, `node` and `close()`. Stop suspends the
context; start resumes it. Await stop before seeking, then start to resume.
`canSeek(seconds)` validates a position without stopping audio; `duration` is infinite
for unbounded scores. Await closure before
attaching another score to its host. `listen(track, flags)` sets audition state
for a zero-based track index (`1` mute, `2` solo, `3` both, `0` neither), also while
playing or stopped. The state is independent of plugin parameters.
`prepareScore` evaluates a draft while the old player is stopped; `attachPlayer`
takes ownership after closing the old player.
Cleanup retains shared memory if AudioContext closure cannot be confirmed and can
be retried.
Preparing from `source` also captures the immutable score projection; `takeView()`
transfers it once, and the caller must release it with `view_free`. Projection
queries and serialization stay outside the audio thread.

Native programs share objects under `build/obj/native/`. Wasm playback and offline
rendering use separate object directories under `build/obj/`; all three builds
track header dependencies. `make clean` removes `build/`, preserving `renders/`,
`.deps/` and independently built plugins.
Use English for prose, comments and UI text; keep musical names.
Local C uses tabs displayed at four columns, Janet two spaces, and JavaScript
four spaces. `.editorconfig` and `.clang-format` define formatting;
`make format-check` uses clang-format 21 and excludes upstream/generated files.
The build patches a copy of miniaudio for worklet stack cleanup;
`web/audio.js` handles Emscripten's already-closed AudioContext case.
Sources and dependencies retain their respective licenses.
See the [test guide](test/README.md) for commands, prerequisites and coverage.
