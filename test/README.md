# Tests

Run from the repository root. Core tests build local Perone fixtures and fetch
the mixer's pinned Brickworks headers; no Tibia or sibling checkout is needed.
Test executables, fixtures, temporary renders and
reports live under `build/test/` and are removed by `make clean`. Integration targets
require production bundles to be built separately; see [plugin builds](../plugins/README.md).

| Command | Coverage and additional requirements |
| --- | --- |
| `make test` | Native scheduler, Perone lifecycle, score API, patterns, routing, export, player and projection. No audio device required. |
| `make test-build` | Incremental host builds, compiler/flag changes, linking, assertions, failure recovery and dependency overrides in an isolated directory; Node.js and `patch`. |
| `make test-build-web` | The same build checks across native and both Wasm variants; also requires Emscripten. |
| `make -C plugins/piano test` | Embedded piano, bank conversion, stereo, velocity, sustain, voice stealing and block-size independence; requires the downloaded Florestan bank and Tibia. |
| `make -C plugins/piano test-web PERONE_PLATFORM=wasm32` | Piano native/Wasm PCM parity and fixed playback memory through the Perone loader; also requires Emscripten and Node.js. |
| `make test-plugins` | Six local prebuilt plugins, metadata, pitch bend and block-size independence. |
| `make test-brickworks` | All prebuilt bundles under `BRICKWORKS_PERONE`, default `build/library/brickworks`. |
| `make test-library-build` | Fetch/build the canonical C library, audit all DSPs, check offline incremental builds, compiler failure propagation, repair, native catalog discovery and an example render. Requires git, Node.js, npm and a C compiler. |
| `make test-prog` | Two identical renders of Polpo; requires the local plugin bundles. |
| `make test-web` | Native/Wasm PCM and projection parity, web publishing, control queues and request handling; Emscripten, Node.js and `patch`. |
| `make test-browser` | AudioWorklet PCM, playback and cleanup in Chromium; includes `test-web`. |
| `make test-polpo-web` | Original Polpo score: live/offline PCM comparison and cleanup; Chromium and its four prebuilt Wasm plugins. |
| `make test-trace` | Source provenance and unchanged PCM; includes `test-web`. |
| `make test-editor` | Native backend via `--serve` in Chromium, with local fixtures; X11 development libraries and an audio device. |
| `make test-live` | Infinite playback, quantized revisions, seek and Stop/Play in both editors, UI continuity, errors and evaluation timeout; editor/browser prerequisites. |
| `make test-editor-web` | Polpo in the shared browser editor; Node.js, Chromium and Polpo's Wasm plugins. |
| `make test-editor-ui` | Custom plugin UIs and timeline automation with native and Wasm DSPs; editor/browser prerequisites, no external plugin checkout. |
| `make test-score-space` | Shared 3D score with native and Wasm backends: playback, source picking, automation/mute continuity, distant loops, live revisions, errors and WebGL recovery. Requires Chromium with WebGL 2 and the editor prerequisites; uses local fixtures. |
| `make test-library` | Shared Settings, published catalog, examples dropdown, Sempiterno playback, file picker, uploads and unsaved edits; the same production bundles built for native and Wasm, plus editor/browser prerequisites. |
| `make test-ui` | Original Tibia C/C++ and A-SID UIs, mouse gestures and DSP feedback; X11 display and prebuilt DSP/UI bundles. |
| `make format-check` | Local C formatting with clang-format 21; excludes upstream headers and generated code. |

Set `EMCC=/absolute/path/to/emcc` if Emscripten is outside `PATH`, and optionally
`CHROMIUM=/path/to/chromium`. To run browser checks manually, use
`node test/server.mjs` and open `http://localhost:8000/test/web.html` or
`http://localhost:8000/test/polpo.html`. The server supplies the required COOP/COEP
headers. See [editor setup](../README.md#editors) for the shared editor and catalog.
`node test/editor-web.mjs examples/denti.janet` accepts another score when its
imports and Wasm bundles are included in `WEB_CONTENT`.

## What the tests protect

Core tests compare rendering at 44.1/48 kHz, variable block sizes, stereo channel
separation and duplicated mono effects. Direct, pattern and mixed scheduling must
produce the same PCM, seek state and source origins, including overlapping pitches
and one-sample notes. Export tests preserve the previous WAV
and clean temporary files after rendering, write, finalization and rename failures.
The player uses a simulated device to check startup errors, interruption, final
silence, draining and export options without requiring audio hardware. Replays
after partial and complete playback must match the original PCM, retain the device
and DSPs, restore automation/defaults and discard stale edits and messages.
Seek tests compare restored notes and controls against sequential playback at
44.1/48 kHz, including exact event boundaries, repeated pitches, distant loops,
stereo effects, mute/solo and queued-revision cancellation. Stop/Play must retain
the accumulated effect state. The shared editor tests cover ruler clicks, typed
positions, seeking during playback, invalid positions and finite endpoints.
Loader tests check canonical module reuse and cache/instance lifetime ordering.
Diagnostic tests force garbage collection before errors and evaluation timeout,
then prepare another score to check recovery in both editors.
The shared transport tests exercise Stop before and after revision activation,
failed updates, ownership transfer, stable DSP control revisions, projection lifetime
after detachment, seek and revision numbering across fresh sessions. Device operations
remain platform-specific; both editor integrations exercise the same C state transitions.
Routing tests cover shared DSP state, nested buses, branch-specific solo,
sample-accurate crossfades, block invariance, rewind and graph rejection.
Mixer tests cover mute/solo precedence, multiple solos, 5 ms fades, automation
and DSP advancement while inaudible, restart persistence and the last track slot.

`view-json.mjs` compares native, directly prepared Wasm and reconstructed worker
projection replies, including density,
overlapping notes, automation, imported origins, stale revisions and invalid queries. It frees
the audio score before querying to verify independent projection ownership.
Automation tests check declared defaults, simultaneous writes, held values,
parameter isolation and peak-preserving bins. Periodic values are compared with
scheduled PCM at 44.1/48 kHz, including pickups, sample rounding, loop boundaries
and distant windows. `automation.mjs` checks parameter checklists, global visibility,
logarithmic scales, source selection and live revision continuity in both editors.
It also checks that late replies cannot restore hidden curves or keep queuing queries.
`playback.mjs` and `player-lifecycle.mjs` cover real AudioWorklet output, stop/restart,
live mute/solo, context closure, timeouts, cancellation during preparation and cleanup retries.

Editor tests cover Unicode paths, unsaved relative imports, Run/Play/Stop, atomic saves
or downloads, diagnostics and recovery, source tracking, scrolling and selection.
They check timeline persistence, notes retained while dragging with delayed replies,
independent time/track zoom, stale replies, large times, unknown ends and bounded
requests/canvas sizes. `chromium.mjs` shares browser startup, DevTools, bounded waits,
input actions and cleanup. `native.mjs` owns GUI startup, logs and shutdown.
Logs, reports and screenshots go under `build/test/`. Unicode fixtures retain
characters such as `音` to test encoding independently of the interface language.
Catalog checks compare the editor with the published files, including optional
plugins. Support modules remain importable without appearing as scores.
`catalog.mjs` checks that publishing skips native-only bundles and retains score
imports, relative UI assets and auxiliary Wasm modules.
`score-space.mjs` also verifies that Tracks does not load Three.js and that the
editor remains usable when WebGL is unavailable. Its graphics-enabled Chromium
session uses ANGLE/OpenGL on Linux; `CHROMIUM` can point to a wrapper for another
driver setup. Normal editor checks keep GPU rendering disabled.

Build checks use the real Makefile and compilers with isolated artifacts. They verify
unchanged builds, target-order independence, compile versus link invalidation,
native/Wasm separation, assertions with `-DNDEBUG`, recovery after compiler failure,
and regeneration from older headers at new paths. They leave working binaries intact.

## Source tracking

`lib/trace.janet` installs once in a fresh score environment, before compilation
and imports. Import alone is inert. The C bridge observes `array/push`, retaining
the producer's Janet frame; pattern wrappers preserve origins through composition.
User callbacks still run once. Direct `daw/note` and `daw/param` calls are captured
as well. Tracking remains separate from musical values and audio events.

The report contains `:locations` (stacks of file/line/column positions) and
`:events` (`[start end origins kind node order]`, in absolute template seconds).
Repetition and sample rounding belong to the score; annotations identify template events.
`trace-host.mjs` exports a complete report as a test oracle. Editors use the C
projection index, requesting only visible notes and active origins after Janet
has closed. The clock is `player_time`; highlights last for the programmed note
length, with an 80 ms minimum pulse, excluding release tails and device latency.

Tracking records event construction, not every executed expression. Janet tail
calls may remove intermediate frames; merged immutable constants can produce
multiple candidate origins. Raw pattern structs fall back to their scheduling
site. Arbitrary array mutations, alternate import caches, `:fresh` imports and
partially recovered scheduling errors have no complete provenance guarantee.
The tracer is experimental, not a public reflection API.

Replies cap tracking at 8192 active events and 256 distinct frames, reporting
partial origins when exceeded. Timeline queries cover at most eight lanes, with
512 individual notes or 512 density bins per lane. Automation queries return up
to 512 changes plus the initial value, or up to 512 min/max bins for scheduled
writes. Visible tracks request their selected parameters, stopping obsolete
requests when the viewport or selection changes. Hiding automation suspends its queries.
These bounds affect the view, not audio; the scheduler's finite-duration limits still apply.

## Plugin UIs

`test-editor-ui` mounts the same ES UI against both backends. Its fixture loads
relative JavaScript, CSS and a separate UI Wasm with external imports through
`instantiateStreaming`, verifying asset paths and MIME types. It checks initial
values, automation, meters, 400-value gesture bursts, binary messages, concurrent
plugin sections, track selection, collapse, generic controls, stale/invalid callbacks
and restart. Stop/Play must retain the GUI object and allow parameter edits while
stopped; Run replaces it. Asynchronous creation must preserve early gestures
across Stop and free a view that arrives after disposal. Screenshots are saved as
`build/test/editor-ui-{native,web}.png`.
The same test checks track audition buttons on both backends: independent chain
selection, mute/solo precedence, Stop/Play persistence and reset on a new Run.

`loader.c` and `perone-controls.mjs` test parameter coalescing, FIFO messages,
concurrent acknowledgements, stereo copies, automation precedence, overflow and
reattachment without losing accepted changes. `request.mjs` ensures a late reply
cannot acknowledge a later request. `test-ui` adds real X11 embedding, deferred
widget creation, resizing and shutdown with the original upstream UIs.

## Live sequences

`test/sequence.c` checks bounded periodic scheduling, sample timing across the
wasm32 counter boundary, block-size independence, DSP reuse, note-offs crossing
revisions, same-pitch retriggers, rounded loop boundaries and worker snapshot round trips.
Preparation must work without creating DSPs; failed activation retains the caller's
score, and successful activation transfers ownership. Concurrent revision publication
and retirement must preserve metadata and existing DSPs. Snapshots carry musical
data and source annotations; the receiver rebuilds graph metadata, events and
query indices. Truncated snapshots must fail without leaking partially decoded
descriptions. Projection tests check negative
pickups, export crops and source tracking after activation for finite and looping scores.
`test/pattern.janet` compares whole-window queries with arbitrary partitions,
including pickups, overhangs, points, independent periods and bounded output.
Finite and repeating patterns use the same sources; flattening resolves finite
offsets, and looping a composed phrase preserves endpoint ordering across cycles.
`test/live-editor.mjs` runs the same scenario against both editors: submit a new
pattern without restarting playback or the inline UI, retain audio after errors,
interrupt runaway Janet, cancel pending changes, and resume or seek the active revision.
