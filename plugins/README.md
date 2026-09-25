# Plugin library

## Canonical Brickworks C library

From the repository root:

```sh
make -j4 library
make test-brickworks
./build/cli examples/brickworks.janet renders/brickworks.wav 48000
```

The build requires git, GNU Make, a C compiler, Node.js and npm. On Termux,
`pkg install clang make git nodejs` supplies those tools. The target downloads
[Orastron/brickworks](https://github.com/Orastron/brickworks) and
[paolomarrone/tibia](https://github.com/paolomarrone/tibia/tree/perone), installs
`dot` 1.1.3 locally, generates the API and Perone wrappers, and compiles the
**40 C examples** (`fx_*` and `synth_*`).

Exact revisions are pinned in [library.mk](library.mk). Tibia's templates come
from its **perone** branch. Revision-specific caches are under `.deps/library/`;
generated projects are under `build/generated/library/`. Dependency checkouts
keep their upstream DSP sources unchanged. Existing sibling repositories and
local plugin projects have independent builds.

Bundles appear at:

```text
build/library/brickworks/<example>/build/bw_example_<example>.perone/
    product.json
    aarch64-linux/bw_example_<example>.so
```

The platform directory follows the host (`aarch64-linux` on Termux). Copy the
whole `.perone` directory to move a plugin. The native editor's catalog and
Brickworks example scores use this collection by default.

`make library-deps` only downloads prerequisites. Repeating `make library` uses
the cache and rebuilds changed or missing artifacts; `make -j` parallelizes
generation and compilation. `make clean` removes generated projects and bundles
under `build/`, preserving downloaded dependencies. Failed downloads have no
completion marker and are retried on the next build.

For a different output directory, use `make library BRICKWORKS_PERONE=/path/to/library`
and set `BRICKWORKS_PERONE=/path/to/library` when running the host or example scores.
`LIBRARY_DEPS` selects the cache directory. `BRICKWORKS_REV`, `TIBIA_REV`,
`BRICKWORKS_URL` and `TIBIA_URL` allow explicit revision or mirror overrides;
changing revisions uses a separate cache. `CC`, `CFLAGS`, `CPPFLAGS` and `LDFLAGS`
are forwarded to the generated builds.

With Emscripten installed, `make library PERONE_PLATFORM=wasm32` adds Wasm DSPs
alongside native binaries; `EMCC=/absolute/path/to/emcc` selects the compiler.
The host's `make web` remains a separate build. Native generation and all 40 DSPs
have been verified on Termux; WebAssembly requires its own toolchain and checks.

`make test-library-build` builds the library and tests native DSP loading,
incremental builds without network/compiler use, compiler failure propagation,
repair of a missing binary, catalog discovery and the Brickworks score render.

## Local plugin projects

Plugins are generated with Tibia and built separately from the host. They require
a C compiler, make, Node.js and `dot` (for example, run `npm install dot` in Tibia).
The generator must provide `perone` and `perone-make`; Brickworks must use the
current plugin API, where `plugin_init` returns `int`.

Manual local builds use existing checkouts, by default `../tibia` and
`../brickworks` relative to the host root. From the host root:

```sh
make -C plugins/echo
make -C plugins
make -C plugins TIBIA=/path/to/tibia BRICKWORKS=/path/to/brickworks
make -C plugins synth_mono shape echo drums PERONE_PLATFORM=wasm32
make -C plugins clean
```

Use `make -C plugins PERONE_PLATFORM=wasm32` for all local Wasm plugins.
Emscripten supplies libc/libm; `EMCC` and `EMXX` select its compilers. Use absolute
paths when they are outside `PATH`, since make enters generated build directories.
Wasm modules are standalone, without JavaScript or WASI imports. Their initial
memory is 1 MiB and may grow during preparation. Native binaries already in the
bundle are preserved.

| Project | DSP source | Controls |
| --- | --- | --- |
| `synth_mono` | Original Brickworks `examples/synth_mono/src/plugin.h` | 38 inputs and the `level` output meter. |
| `fx_svf` | Original Brickworks `examples/fx_svf/src/plugin.h` | State-variable filter. |
| `drums` | Local `plugin.h` | Percussion gain and seed. |
| `shape` | Local `plugin.h` | Waveshaper drive/level and DC/low-pass filters. |
| `echo` | Local `plugin.h` | Three delay taps in milliseconds, individual levels and dry signal. |
| `tibia_test` | Local `plugin.h` | Gain, filter, delay, bypass and output. |

The optional native [sampled piano](piano/README.md) is built separately with
`make -C plugins/piano` after fetching its dependencies. It is not part of the
default build or web catalog; its bundle includes the piano SoundFont asset.

`drums` has 32 voices; MIDI notes 0–6 select kick, snare, hat, open hat, crash,
high tom and low tom. Velocity scales each hit, `:gain` scales the instance and
`:seed` affects subsequent hits. Sounds decay naturally, ignore note-off and
steal the oldest voice when full.

Each project supplies `plugin.h` and `product.json`, locally or through Brickworks.
`plugin.mk` generates the wrapper, API and Makefile in `build/gen`, producing
`build/plugin.perone/product.json` and `<platform>/<bundleName>.so` or
`wasm32/<bundleName>.wasm` inside that bundle. The generated build tracks headers.
JSON supplies defaults and metadata; musical presets remain in Janet scores.
Plugins require no Janet, miniaudio or host sources.

Pass the complete `.perone` directory to `build/tools/perone-host` or `daw/plugin`; JSON must
travel with the binary. The host uses the unchanged Perone ABI from Tibia, with
no DSP patches or pitch-bend compensation. See the [host contract and UI support](../README.md#perone-and-plugin-uis).

The canonical bundles in `build/library/brickworks` can be loaded directly;
`examples/brickworks.janet` demonstrates this. Set `BRICKWORKS_PERONE` to use an
external collection such as `../brickworks/build/perone`. The default web catalog
includes the 40 C examples; equivalent C++ variants remain supported by the host
and are excluded from the canonical build.

From the host root, `make test-plugins` checks the six local prebuilt bundles,
including synth pitch bend at 440/220/880/440 Hz and block-size independence.
`make test-brickworks` checks all bundles in `BRICKWORKS_PERONE` (default
`build/library/brickworks`). Both commands test existing binaries without building them.
