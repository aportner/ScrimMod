#pragma once

#include "server_apis.hpp"

namespace scrimmod::plugin {

struct ApiProbeResult {
    ApiError error{ApiError::None};
    int major{0};
    int minor{0};
};

[[nodiscard]] ApiProbeResult probe_rehlds_api(void* module) noexcept;
[[nodiscard]] ApiProbeResult probe_regamedll_api(void* module) noexcept;
void reset_rehlds_api() noexcept;
void reset_regamedll_api() noexcept;
[[nodiscard]] bool add_rehlds_cvar_listener(const char* name, CvarListener listener) noexcept;
void remove_rehlds_cvar_listener(const char* name, CvarListener listener) noexcept;
[[nodiscard]] bool assign_regamedll_player_team(edict_s* entity, ServerPlayerTeam team) noexcept;
[[nodiscard]] bool ensure_regamedll_knife_loadout(edict_s* entity) noexcept;
[[nodiscard]] bool install_regamedll_gameplay_hooks(
    PlayerSpawnListener spawn_listener, TeamChoiceListener team_choice_listener,
    WeaponAcquireListener weapon_acquire_listener, PlayerKilledListener player_killed_listener,
    RoundEndListener round_end_listener, RoundRestartListener round_restart_listener) noexcept;
void remove_regamedll_gameplay_hooks() noexcept;

} // namespace scrimmod::plugin
