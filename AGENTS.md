# ScrimMod Agent Guidance

## Source of truth

- `PLAN.md` defines product behavior and supported scope.
- ScrimMod's logical match state is authoritative.
- Never infer persistent identity from client slots, entity indexes, names, CT/T
  assignment, or the engine scoreboard.
- Normalize and store players by an explicit player ID. Human IDs are Steam IDs;
  opt-in test bots use connection-scoped `BOT:<userid>` IDs supplied by the adapter.

## Architecture

- Keep the match engine independent of Metamod, ReHLDS, ReGameDLL, and engine APIs.
- Translate engine hooks into typed core events in the plugin adapter.
- Express server mutations as explicit effects applied by the adapter.
- Route every phase change through the central transition mechanism.
- Make state entry and server reconciliation idempotent.
- Do not introduce flags that duplicate state-machine authority.
- Keep platform-specific code out of `src/core` and `include/scrimmod/core`.

## Safety and correctness

- Disabled means no match behavior and fully reset internal state.
- Never count ambiguous, duplicate, knife, warmup, or restart-generated rounds.
- Config execution, restart sequences, and forced team changes must not advance state.
- Validate command permissions and arguments at the adapter boundary.
- Preserve active human roster assignments across reconnects by Steam ID. Never
  claim reconnect persistence for connection-scoped bot IDs.
- Prefer pausing or explicit admin recovery over guessing after ambiguous events.

## Development

- Use C++17 and keep the core portable between macOS development and Linux builds.
- GCC targeting 32-bit Linux is the production plugin toolchain. AppleClang is only
  for macOS core development and tests.
- Add unit tests for every state transition and scoring boundary.
- Add a regression test for every fixed state-machine bug.
- Do not modify vendored or fetched dependencies.
- Keep commits focused and preserve unrelated user changes.
- Before completing work, run formatting as applicable, configure CMake, build, and
  run `ctest --output-on-failure`.

## Compatibility

- The initial target is a GCC-built 32-bit Linux `.so` for ReHLDS + ReGameDLL_CS +
  Metamod-R.
- Pin dependency revisions; do not silently track moving branches.
- macOS only needs to build and test the platform-independent core.
- Document any new runtime dependency or supported-platform change in `README.md`
  and `PLAN.md`.
