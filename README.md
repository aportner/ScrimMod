# ScrimMod

ScrimMod is a Metamod-R plugin for automating competitive Counter-Strike 1.6
scrimmages. The behavioral specification is in [`PLAN.md`](PLAN.md).

## Target environment

- Linux Counter-Strike 1.6 server
- ReHLDS
- ReGameDLL_CS
- Metamod-R
- GCC-built 32-bit Linux shared object: `scrimmod_mm_i386.so`

Original HLDS/GameDLL, Windows server binaries, and macOS server binaries are not
initial targets. Exact dependency revisions will be pinned before implementing the
plugin adapter.

## Architecture

`scrimmod_core` is a platform-independent C++17 match engine. It owns authoritative
logical state and does not call game-server APIs. A thin Linux adapter will translate
Metamod/ReGameDLL callbacks into core events and apply explicit effects returned by
the core.

The core and its tests build on macOS with AppleClang. The plugin itself must be
compiled with GCC and tested on a Linux system with a 32-bit toolchain and the pinned
server SDK dependencies.

## Build the core and tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`SCRIMMOD_BUILD_PLUGIN` remains gated until the server dependency revisions and
adapter ABI are pinned.
