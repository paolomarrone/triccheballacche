# Tests

Run from the repository root. Core tests build local Perone fixtures and need no
Tibia or Brickworks checkout. Integration targets require production bundles to
be built separately; see [plugin builds](../plugins/README.md).

| Command | Coverage and additional requirements |
| --- | --- |
| `make test` | Native scheduler, Perone lifecycle, score API, patterns, routing, export, player and projection. No audio device required. |
| `make test-plugins` | Six local prebuilt plugins, metadata, pitch bend and block-size independence. |
| `make test-brickworks` | All prebuilt bundles under `BRICKWORKS_PERONE`, default `../brickworks/build/perone`. |
| `make test-prog` | Two identical renders of Polpo; requires the local plugin bundles. |
| `make test-web` | Native/Wasm PCM and projection parity, control queues and request handling; Emscripten, Node.js and `patch`. |
| `make test-browser` | AudioWorklet PCM, playback and cleanup in Chromium; includes `test-web`. |
| `make test-polpo-web` | Original Polpo score: live/offline PCM comparison and cleanup; Chromium and its four prebuilt Wasm plugins. |
| `make test-trace` | Source provenance and unchanged PCM; includes `test-web`. |
| `make test-editor` | Native editor in Chromium, with local fixtures; X11 development libraries, GCC, `patch` and an audio device. |
| `make test-editor-web` | Polpo in the shared browser editor; Node.js, Chromium and Polpo's Wasm plugins. |
| `make test-editor-ui` | The same custom UI with native and Wasm DSPs; editor/browser prerequisites, no external plugin checkout. |
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
separation and duplicated mono effects. Export tests preserve the previous WAV
and clean temporary files after rendering, write, finalization and rename failures.
The player uses a simulated device to check startup errors, interruption, final
silence, draining and export options without requiring audio hardware.

`view-json.mjs` compares native and Wasm projection replies, including density,
overlapping notes, imported origins, stale revisions and invalid queries. It frees
the audio score before querying to verify independent projection ownership.
`playback.mjs` and `player-lifecycle.mjs` cover real AudioWorklet output, stop/restart,
context closure, timeouts, cancellation during preparation and cleanup retries.

Editor tests cover Unicode paths, unsaved relative imports, run/stop, atomic saves
or downloads, diagnostics and recovery, source tracking, scrolling and selection.
They check timeline persistence, stale replies, large times, unknown ends and
bounded requests/canvas sizes. `chromium.mjs` shares browser startup, DevTools and
cleanup; logs, reports and screenshots go under `build/`. Unicode fixtures retain
characters such as `音` to test encoding independently of the interface language.

## Source tracking

`lib/trace.janet` installs once in a fresh score environment, before compilation
and imports. Import alone is inert. The C bridge observes `array/push`, retaining
the producer's Janet frame; pattern wrappers preserve origins through composition.
User callbacks still run once. Direct `daw/note` and `daw/param` calls are captured
as well. Tracking remains separate from musical values and audio events.

The report contains `:locations` (stacks of file/line/column positions) and
`:events` (`[start end origins kind node order]`, in absolute seconds).
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
512 individual notes or 512 density bins per lane. These bounds affect the view,
not audio; the scheduler's finite-duration limits still apply.

## Plugin UIs

`test-editor-ui` mounts the same ES UI against both backends. Its fixture loads
relative JavaScript, CSS and a separate UI Wasm with external imports through
`instantiateStreaming`, verifying asset paths and MIME types. It checks initial
values, automation, meters, 400-value gesture bursts, binary messages, generic
controls, stale/invalid callbacks and restart. Asynchronous creation must preserve
early gestures and free a view that arrives after Stop. Screenshots are saved as
`build/editor-ui-{native,web}.png`.

`loader.c` and `perone-controls.mjs` test parameter coalescing, FIFO messages,
concurrent acknowledgements, stereo copies, automation precedence, overflow and
reattachment without losing accepted changes. `request.mjs` ensures a late reply
cannot acknowledge a later request. `test-ui` adds real X11 embedding, deferred
widget creation, resizing and shutdown with the original upstream UIs.
