# ScrimMod

ScrimMod is a Metamod-R plugin for automating competitive Counter-Strike 1.6
scrimmages. The behavioral specification is in [`PLAN.md`](PLAN.md).

## Target environment

- Linux Counter-Strike 1.6 server
- ReHLDS
- ReGameDLL_CS
- Metamod-R
- GCC-built 32-bit Linux shared object: `scrimmod_mm_i386.so`

The current adapter requires ReHLDS API 3.3 or newer and ReGameDLL API 5.3 or newer.
It validates both interfaces when MetaMod attaches the plugin and refuses to load if
either dependency is absent or incompatible.

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
Then run the checked-in build wrapper:

```sh
./build-linux.sh
```

The `file` output must identify a 32-bit Intel 80386 ELF shared object. CMake fetches
the pinned official MetaMod-R source revision for its SDK headers during configure.
The resulting plugin is `build/linux-release/scrimmod_mm_i386.so`.

The equivalent individual preset commands are:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

## Server smoke test

Install the built plugin. By default this targets
`~/hlds/cstrike/addons/ScrimMod`:

```sh
./install-linux.sh
```

To use a different Counter-Strike directory, pass it explicitly:

```sh
./install-linux.sh /path/to/hlds/cstrike
```

Alternatively, set `SCRIMMOD_CSTRIKE_DIR`. The installed artifact is:

```text
cstrike/addons/ScrimMod/scrimmod_mm_i386.so
```

The wrapper uses CMake's standard, generator-independent install command. Its direct
equivalent is:

```sh
cmake --install build/linux-release \
  --prefix "$HOME/hlds/cstrike" \
  --component plugin
```

CMake also generates an `install` build target, but `cmake --install` is preferred
because the deployment prefix can be supplied at install time and it works equally
with Makefiles and Ninja.

Add this line to `cstrike/addons/metamod/plugins.ini`:

```text
linux addons/ScrimMod/scrimmod_mm_i386.so
```

Start the server and run `meta list`. ScrimMod should appear with status `RUN`, and
the console should contain messages similar to:

```text
[ScrimMod] ReHLDS API 3.15 and ReGameDLL API 5.30 detected.
[ScrimMod] Plugin loaded.
```

Finally, use MetaMod's normal unload and reload commands for the ScrimMod plugin and
verify that the server remains stable and the unload/load messages each appear once.

## Initial server controls

ScrimMod starts disabled. Use the server console or RCON to enable or disable it:

```text
scrim_enabled 1
scrim_enabled 0
```

Disabling resets the in-memory match state and queues `exec pregame.cfg`. Inspect the
authoritative core state with:

```text
scrim_status
```

When enabled, `scrim_status` also lists tracked players by normalized player ID and
connected/disconnected state. Human player IDs are Steam IDs. Enabling captures
players already on the server; later reconnects update the same persistent player
record rather than using the temporary client slot as identity. That initial
connected-player set is sealed as the eligible pool. Players joining afterward are
tracked but are not automatically made draft-eligible. During captain selection, an
administrator can explicitly change that fixed pool from the server console or RCON
using a normalized player ID or a unique exact player name (quote names containing
spaces):

```text
scrim_add STEAM_0:1:12345
scrim_remove "Player Name"
```

These commands only select from players ScrimMod has already tracked. If duplicate
players have the same name, use the player ID.

Select two captains from the eligible pool, inspect the pending choices, and confirm
them with:

```text
scrim_captain_a STEAM_0:1:12345
scrim_captain_b "Player Name"
scrim_status
scrim_captains_confirm
```

Use `scrim_captain_clear a` or `scrim_captain_clear b` to clear a pending choice.
Confirmation requires two different eligible players and advances the authoritative
phase to `KnifeSetup`. It randomly assigns Team A and Team B to opposite CT/T sides,
moves both captains through ReGameDLL's normal team-change path, and moves every
other connected player to spectator. These placements are idempotently reconciled
when another player connects.

Start the knife round with:

```text
scrim_knife_start
```

This advances to `KnifeLive`, queues `sv_restart 1`, strips both captains to knives
after default spawn equipment, prevents them from acquiring other weapons, and
blocks tracked players from changing teams through the normal team menu. Weapon and
team restrictions remain active throughout `KnifeSetup` and `KnifeLive`.

### Bot end-to-end testing

Bots are excluded by default. Before enabling a scrim, opt them into tracking and the
initial eligible pool with:

```text
scrim_allow_bots 1
bot_add
bot_add
scrim_enabled 1
```

Each bot receives a synthetic ID such as `BOT:42`, derived from its server-assigned
user ID. It can be added, removed, or selected as a captain exactly like a human:

```text
scrim_captain_a BOT:42
```

The ID lasts only for that bot connection. Removing and recreating the bot gives it
a new ID and intentionally does not restore its former match assignment. HLTV and
proxy clients remain excluded. Set `scrim_allow_bots` before `scrim_enabled 1` so
bots already on the server are captured in the initial eligible pool.
