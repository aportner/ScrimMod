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
initial targets. MetaMod-R SDK headers are pinned in `cmake/dependencies.cmake`.

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

## Build the empty plugin on Linux

Install GCC, CMake, Git, and the 32-bit C/C++ development libraries. On Debian or
Ubuntu this generally includes `gcc-multilib`, `g++-multilib`, and `libc6-dev-i386`.
Then configure with the checked-in toolchain:

```sh
cmake -S . -B build-linux \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-gcc-i386.cmake \
  -DSCRIMMOD_BUILD_PLUGIN=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
file build-linux/scrimmod_mm_i386.so
```

The `file` output must identify a 32-bit Intel 80386 ELF shared object. CMake fetches
the pinned official MetaMod-R source revision for its SDK headers during configure.

## Server smoke test

Copy the artifact to:

```text
cstrike/addons/scrimmod/scrimmod_mm_i386.so
```

Add this line to `cstrike/addons/metamod/plugins.ini`:

```text
linux addons/scrimmod/scrimmod_mm_i386.so
```

Start the server and run `meta list`. ScrimMod should appear with status `RUN`, and
the console should contain:

```text
[ScrimMod] Empty plugin scaffold loaded.
```

Finally, use MetaMod's normal unload and reload commands for the ScrimMod plugin and
verify that the server remains stable and the unload/load messages each appear once.
