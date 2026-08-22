#pragma once

struct edict_s;

namespace scrimmod::plugin {

enum class ApiError {
    None,
    MissingGameDllPath,
    RehldsModuleUnavailable,
    RehldsFactoryUnavailable,
    RehldsInterfaceUnavailable,
    RehldsVersionMismatch,
    RehldsServicesUnavailable,
    RegamedllModuleUnavailable,
    RegamedllFactoryUnavailable,
    RegamedllInterfaceUnavailable,
    RegamedllVersionMismatch,
    RegamedllServicesUnavailable,
};

struct ApiStatus {
    ApiError error{ApiError::None};
    int rehlds_major{0};
    int rehlds_minor{0};
    int regamedll_major{0};
    int regamedll_minor{0};

    [[nodiscard]] bool ok() const noexcept { return error == ApiError::None; }
};

using CvarListener = void (*)(const char* new_value);
using PlayerSpawnListener = void (*)(edict_s* entity);
using TeamChoiceListener = bool (*)(edict_s* entity);
using WeaponAcquireListener = bool (*)(edict_s* entity, bool is_knife);
using PlayerKilledListener = void (*)(edict_s* victim, edict_s* killer);
enum class RoundEndType { Gameplay, Restart, Commence };
enum class RoundWinner { None, Terrorist, CounterTerrorist };
using RoundEndListener = void (*)(RoundEndType type, RoundWinner winner);
using RoundRestartListener = void (*)();
enum class ServerPlayerTeam { Unassigned, Terrorist, CounterTerrorist, Spectator, Unknown };

struct TeamAssignmentResult {
    ServerPlayerTeam previous_team{ServerPlayerTeam::Unknown};
    ServerPlayerTeam current_team{ServerPlayerTeam::Unknown};
    bool join_accepted{false};

    [[nodiscard]] bool ok(const ServerPlayerTeam requested_team) const noexcept {
        return current_team == requested_team;
    }
};

[[nodiscard]] ApiStatus initialize_server_apis(const char* game_dll_path) noexcept;
void shutdown_server_apis() noexcept;
[[nodiscard]] const char* api_error_message(ApiError error) noexcept;
[[nodiscard]] bool add_cvar_listener(const char* name, CvarListener listener) noexcept;
void remove_cvar_listener(const char* name, CvarListener listener) noexcept;
[[nodiscard]] ServerPlayerTeam player_team(edict_s* entity) noexcept;
[[nodiscard]] TeamAssignmentResult assign_player_team(edict_s* entity,
                                                      ServerPlayerTeam team) noexcept;
[[nodiscard]] bool ensure_knife_loadout(edict_s* entity) noexcept;
[[nodiscard]] bool install_gameplay_hooks(PlayerSpawnListener spawn_listener,
                                          TeamChoiceListener team_choice_listener,
                                          WeaponAcquireListener weapon_acquire_listener,
                                          PlayerKilledListener player_killed_listener,
                                          RoundEndListener round_end_listener,
                                          RoundRestartListener round_restart_listener) noexcept;
void remove_gameplay_hooks() noexcept;

} // namespace scrimmod::plugin
