#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <extdll.h>
#include <meta_api.h>

#include "scrimmod/core/match_engine.hpp"
#include "server_apis.hpp"

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals = nullptr;
meta_globals_t* gpMetaGlobals = nullptr;
gamedll_funcs_t* gpGamedllFuncs = nullptr;
mutil_funcs_t* gpMetaUtilFuncs = nullptr;

plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,  // ifvers
    "ScrimMod",              // name
    "0.1.0",                 // version
    __DATE__,                // date
    "ScrimMod contributors", // author
    "N/A",                   // url
    "SCRIMMOD",              // logtag
    PT_ANYTIME,              // loadable
    PT_ANYTIME,              // unloadable
};

namespace {

DLL_FUNCTIONS g_dll_functions{};
META_FUNCTIONS g_meta_functions{};
scrimmod::core::MatchEngine g_match_engine{};
char g_scrim_enabled_default[] = "0";
cvar_t g_scrim_enabled = {
    "scrim_enabled", g_scrim_enabled_default, FCVAR_SERVER, 0.0F, nullptr,
};

void server_print(const char* message) {
    if (g_engfuncs.pfnServerPrint != nullptr) {
        g_engfuncs.pfnServerPrint(message);
    }
}

void print_api_error(const scrimmod::plugin::ApiStatus& status) {
    server_print("[ScrimMod] Cannot load: ");
    server_print(scrimmod::plugin::api_error_message(status.error));
    server_print("\n");
}

void queue_pregame_config() {
    if (g_engfuncs.pfnServerCommand != nullptr) {
        g_engfuncs.pfnServerCommand("exec pregame.cfg\n");
    }
}

void apply_effects(const std::vector<scrimmod::core::Effect>& effects) {
    for (const auto& effect : effects) {
        switch (effect.type) {
        case scrimmod::core::EffectType::ExecutePregameConfig:
            queue_pregame_config();
            break;
        }
    }
}

bool is_trackable_steam_id(const char* steam_id) {
    if (steam_id == nullptr || std::strncmp(steam_id, "STEAM_", 6) != 0) {
        return false;
    }
    return std::strcmp(steam_id, "STEAM_ID_PENDING") != 0 &&
           std::strcmp(steam_id, "STEAM_ID_LAN") != 0;
}

void track_connected_player(edict_t* entity) {
    if (!g_match_engine.state().enabled() || entity == nullptr || entity->free != FALSE) {
        return;
    }

    const char* steam_id = g_engfuncs.pfnGetPlayerAuthId(entity);
    if (!is_trackable_steam_id(steam_id)) {
        return;
    }

    const char* name = g_engfuncs.pfnSzFromIndex(entity->v.netname);
    static_cast<void>(g_match_engine.player_connected(steam_id, name != nullptr ? name : ""));
}

void capture_connected_players() {
    if (gpGlobals == nullptr) {
        return;
    }
    for (int index = 1; index <= gpGlobals->maxClients; ++index) {
        track_connected_player(g_engfuncs.pfnPEntityOfEntIndex(index));
    }
    static_cast<void>(g_match_engine.capture_eligible_players());
}

void on_client_put_in_server(edict_t* entity) {
    track_connected_player(entity);
    RETURN_META(MRES_IGNORED);
}

void on_client_disconnect(edict_t* entity) {
    if (g_match_engine.state().enabled() && entity != nullptr) {
        const char* steam_id = g_engfuncs.pfnGetPlayerAuthId(entity);
        if (is_trackable_steam_id(steam_id)) {
            static_cast<void>(g_match_engine.player_disconnected(steam_id));
        }
    }
    RETURN_META(MRES_IGNORED);
}

void apply_enabled_value(const char* new_value) {
    const bool enable = new_value != nullptr && std::strtof(new_value, nullptr) != 0.0F;
    const auto result = g_match_engine.set_enabled(enable);
    apply_effects(result.effects);
    if (enable && result.changed) {
        capture_connected_players();
        server_print("[ScrimMod] Enabled.\n");
    } else if (!enable) {
        server_print("[ScrimMod] Disabled; match state reset and pregame.cfg queued.\n");
    }
}

void print_status() {
    const auto& state = g_match_engine.state();
    char status[256]{};
    std::snprintf(
        status, sizeof(status), "Scrim Mod: %s\nPhase: %s\nTeam A Score: %d\nTeam B Score: %d\n",
        state.enabled() ? "ENABLED" : "DISABLED", scrimmod::core::phase_name(state.phase()),
        state.team(scrimmod::core::LogicalTeam::A).total_score,
        state.team(scrimmod::core::LogicalTeam::B).total_score);
    server_print(status);

    std::vector<const scrimmod::core::Player*> players;
    players.reserve(state.players().size());
    for (const auto& [steam_id, player] : state.players()) {
        static_cast<void>(steam_id);
        players.push_back(&player);
    }
    std::sort(players.begin(), players.end(),
              [](const auto* left, const auto* right) { return left->steam_id < right->steam_id; });

    std::snprintf(status, sizeof(status), "Tracked Players: %zu\n", players.size());
    server_print(status);
    for (const auto* player : players) {
        const bool eligible = std::binary_search(state.eligible_players().begin(),
                                                 state.eligible_players().end(), player->steam_id);
        std::snprintf(status, sizeof(status), "  %s [%s] - %s%s\n", player->last_known_name.c_str(),
                      player->steam_id.c_str(), player->connected ? "connected" : "disconnected",
                      eligible ? ", eligible" : "");
        server_print(status);
    }
}

const char* eligibility_error_message(const scrimmod::core::EligibilityError error) {
    using scrimmod::core::EligibilityError;
    switch (error) {
    case EligibilityError::None:
        return "none";
    case EligibilityError::ScrimDisabled:
        return "ScrimMod is disabled";
    case EligibilityError::WrongPhase:
        return "the eligible pool can only be changed during captain selection";
    case EligibilityError::PoolNotCaptured:
        return "the eligible pool has not been captured";
    case EligibilityError::InvalidSteamId:
        return "the player argument is empty";
    case EligibilityError::UnknownPlayer:
        return "no tracked player matches that Steam ID or exact name";
    }
    return "unknown eligibility error";
}

std::string resolve_unique_player_name(const char* target, bool& ambiguous) {
    ambiguous = false;
    std::string resolved;
    if (target == nullptr) {
        return resolved;
    }

    for (const auto& [steam_id, player] : g_match_engine.state().players()) {
        if (player.last_known_name != target) {
            continue;
        }
        if (!resolved.empty()) {
            ambiguous = true;
            return {};
        }
        resolved = steam_id;
    }
    return resolved;
}

void update_eligible_player(const bool add) {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print(add ? "Usage: scrim_add <Steam ID or exact name>\n"
                         : "Usage: scrim_remove <Steam ID or exact name>\n");
        return;
    }

    const char* target = g_engfuncs.pfnCmd_Argv(1);
    auto result = add ? g_match_engine.add_eligible_player(target != nullptr ? target : "")
                      : g_match_engine.remove_eligible_player(target != nullptr ? target : "");
    if (result.error == scrimmod::core::EligibilityError::UnknownPlayer) {
        bool ambiguous = false;
        const std::string steam_id = resolve_unique_player_name(target, ambiguous);
        if (ambiguous) {
            server_print("[ScrimMod] Player name is ambiguous; use the Steam ID.\n");
            return;
        }
        if (!steam_id.empty()) {
            result = add ? g_match_engine.add_eligible_player(steam_id)
                         : g_match_engine.remove_eligible_player(steam_id);
        }
    }

    if (!result.ok()) {
        server_print("[ScrimMod] Cannot update eligible pool: ");
        server_print(eligibility_error_message(result.error));
        server_print(".\n");
        return;
    }

    server_print(result.changed ? (add ? "[ScrimMod] Player added to eligible pool.\n"
                                       : "[ScrimMod] Player removed from eligible pool.\n")
                                : "[ScrimMod] Eligible pool already has the requested state.\n");
}

void add_eligible_player() { update_eligible_player(true); }

void remove_eligible_player() { update_eligible_player(false); }

} // namespace

C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t* engine_functions, globalvars_t* globals) {
    if (engine_functions != nullptr) {
        std::memcpy(&g_engfuncs, engine_functions, sizeof(g_engfuncs));
    }
    gpGlobals = globals;
}

C_DLLEXPORT int Meta_Query(char*, plugin_info_t** plugin_info, mutil_funcs_t* meta_util_functions) {
    if (plugin_info == nullptr || meta_util_functions == nullptr) {
        return FALSE;
    }

    *plugin_info = &Plugin_info;
    gpMetaUtilFuncs = meta_util_functions;
    return TRUE;
}

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS* function_table, int* interface_version) {
    if (function_table == nullptr || interface_version == nullptr) {
        return FALSE;
    }
    if (*interface_version != INTERFACE_VERSION) {
        *interface_version = INTERFACE_VERSION;
        return FALSE;
    }

    std::memcpy(function_table, &g_dll_functions, sizeof(g_dll_functions));
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS* function_table,
                            meta_globals_t* meta_globals, gamedll_funcs_t* gamedll_functions) {
    if (now > Plugin_info.loadable || function_table == nullptr || meta_globals == nullptr ||
        gamedll_functions == nullptr) {
        return FALSE;
    }

    gpMetaGlobals = meta_globals;
    gpGamedllFuncs = gamedll_functions;

    const char* game_dll_path = GET_GAME_INFO(PLID, GINFO_DLL_FULLPATH);
    const scrimmod::plugin::ApiStatus api_status =
        scrimmod::plugin::initialize_server_apis(game_dll_path);
    if (!api_status.ok()) {
        print_api_error(api_status);
        gpMetaGlobals = nullptr;
        gpGamedllFuncs = nullptr;
        return FALSE;
    }

    g_engfuncs.pfnCVarRegister(&g_scrim_enabled);
    if (!scrimmod::plugin::add_cvar_listener(g_scrim_enabled.name, apply_enabled_value)) {
        server_print("[ScrimMod] Cannot load: failed to register scrim_enabled listener.\n");
        scrimmod::plugin::shutdown_server_apis();
        gpMetaGlobals = nullptr;
        gpGamedllFuncs = nullptr;
        return FALSE;
    }

    g_engfuncs.pfnAddServerCommand("scrim_status", print_status);
    g_engfuncs.pfnAddServerCommand("scrim_add", add_eligible_player);
    g_engfuncs.pfnAddServerCommand("scrim_remove", remove_eligible_player);
    const cvar_t* registered_cvar = g_engfuncs.pfnCVarGetPointer(g_scrim_enabled.name);
    apply_enabled_value(registered_cvar != nullptr ? registered_cvar->string
                                                   : g_scrim_enabled.string);

    g_dll_functions.pfnClientDisconnect = on_client_disconnect;
    g_dll_functions.pfnClientPutInServer = on_client_put_in_server;
    g_meta_functions.pfnGetEntityAPI2 = GetEntityAPI2;
    std::memcpy(function_table, &g_meta_functions, sizeof(g_meta_functions));

    char api_message[192]{};
    std::snprintf(api_message, sizeof(api_message),
                  "[ScrimMod] ReHLDS API %d.%d and ReGameDLL API %d.%d detected.\n",
                  api_status.rehlds_major, api_status.rehlds_minor, api_status.regamedll_major,
                  api_status.regamedll_minor);
    server_print(api_message);
    server_print("[ScrimMod] Plugin loaded.\n");
    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason) {
    if (now > Plugin_info.unloadable && reason != PNL_CMD_FORCED) {
        return FALSE;
    }

    scrimmod::plugin::remove_cvar_listener(g_scrim_enabled.name, apply_enabled_value);
    apply_effects(g_match_engine.set_enabled(false).effects);
    scrimmod::plugin::shutdown_server_apis();
    server_print("[ScrimMod] Plugin unloaded.\n");
    gpMetaGlobals = nullptr;
    gpGamedllFuncs = nullptr;
    return TRUE;
}
