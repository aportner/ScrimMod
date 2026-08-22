#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <extdll.h>
#include <meta_api.h>

#include "scrimmod/core/match_state.hpp"
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
scrimmod::core::MatchState g_match_state{};
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

void apply_enabled_value(const char* new_value) {
    const bool enable = new_value != nullptr && std::strtof(new_value, nullptr) != 0.0F;
    if (enable) {
        if (!g_match_state.enabled()) {
            g_match_state.enable();
            server_print("[ScrimMod] Enabled.\n");
        }
        return;
    }

    g_match_state.disable();
    queue_pregame_config();
    server_print("[ScrimMod] Disabled; match state reset and pregame.cfg queued.\n");
}

void print_status() {
    char status[256]{};
    std::snprintf(status, sizeof(status),
                  "Scrim Mod: %s\nPhase: %s\nTeam A Score: %d\nTeam B Score: %d\n",
                  g_match_state.enabled() ? "ENABLED" : "DISABLED",
                  scrimmod::core::phase_name(g_match_state.phase()),
                  g_match_state.team(scrimmod::core::LogicalTeam::A).total_score,
                  g_match_state.team(scrimmod::core::LogicalTeam::B).total_score);
    server_print(status);
}

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
    const cvar_t* registered_cvar = g_engfuncs.pfnCVarGetPointer(g_scrim_enabled.name);
    apply_enabled_value(registered_cvar != nullptr ? registered_cvar->string
                                                   : g_scrim_enabled.string);

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
    g_match_state.disable();
    queue_pregame_config();
    scrimmod::plugin::shutdown_server_apis();
    server_print("[ScrimMod] Plugin unloaded.\n");
    gpMetaGlobals = nullptr;
    gpGamedllFuncs = nullptr;
    return TRUE;
}
