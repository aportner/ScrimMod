#include "server_apis.hpp"

#include <dlfcn.h>

#include "api_probe.hpp"

namespace scrimmod::plugin {
namespace {

void* g_engine_module = nullptr;
void* g_game_module = nullptr;

} // namespace

ApiStatus initialize_server_apis(const char* game_dll_path) noexcept {
    shutdown_server_apis();
    ApiStatus status{};

    if (game_dll_path == nullptr || game_dll_path[0] == '\0') {
        status.error = ApiError::MissingGameDllPath;
        return status;
    }

    g_engine_module = dlopen("engine_i486.so", RTLD_NOW);
    if (g_engine_module == nullptr) {
        status.error = ApiError::RehldsModuleUnavailable;
        return status;
    }
    const ApiProbeResult rehlds = probe_rehlds_api(g_engine_module);
    status.rehlds_major = rehlds.major;
    status.rehlds_minor = rehlds.minor;
    if (rehlds.error != ApiError::None) {
        status.error = rehlds.error;
        shutdown_server_apis();
        return status;
    }

    g_game_module = dlopen(game_dll_path, RTLD_NOW);
    if (g_game_module == nullptr) {
        status.error = ApiError::RegamedllModuleUnavailable;
        shutdown_server_apis();
        return status;
    }
    const ApiProbeResult regamedll = probe_regamedll_api(g_game_module);
    status.regamedll_major = regamedll.major;
    status.regamedll_minor = regamedll.minor;
    if (regamedll.error != ApiError::None) {
        status.error = regamedll.error;
        shutdown_server_apis();
        return status;
    }

    return status;
}

void shutdown_server_apis() noexcept {
    reset_regamedll_api();
    reset_rehlds_api();

    if (g_game_module != nullptr) {
        dlclose(g_game_module);
        g_game_module = nullptr;
    }
    if (g_engine_module != nullptr) {
        dlclose(g_engine_module);
        g_engine_module = nullptr;
    }
}

const char* api_error_message(const ApiError error) noexcept {
    switch (error) {
    case ApiError::None:
        return "no error";
    case ApiError::MissingGameDllPath:
        return "MetaMod did not provide the GameDLL path";
    case ApiError::RehldsModuleUnavailable:
        return "engine_i486.so could not be opened";
    case ApiError::RehldsFactoryUnavailable:
        return "the ReHLDS CreateInterface factory is missing";
    case ApiError::RehldsInterfaceUnavailable:
        return "the required ReHLDS API interface is unavailable";
    case ApiError::RehldsVersionMismatch:
        return "the ReHLDS API version is incompatible (requires 3.3 or newer)";
    case ApiError::RehldsServicesUnavailable:
        return "one or more required ReHLDS services are unavailable";
    case ApiError::RegamedllModuleUnavailable:
        return "the ReGameDLL shared object could not be opened";
    case ApiError::RegamedllFactoryUnavailable:
        return "the ReGameDLL CreateInterface factory is missing";
    case ApiError::RegamedllInterfaceUnavailable:
        return "the required ReGameDLL API interface is unavailable";
    case ApiError::RegamedllVersionMismatch:
        return "the ReGameDLL API version is incompatible (requires 5.3 or newer)";
    case ApiError::RegamedllServicesUnavailable:
        return "one or more required ReGameDLL services are unavailable";
    }
    return "unknown API initialization error";
}

bool add_cvar_listener(const char* name, CvarListener listener) noexcept {
    return add_rehlds_cvar_listener(name, listener);
}

void remove_cvar_listener(const char* name, CvarListener listener) noexcept {
    remove_rehlds_cvar_listener(name, listener);
}

bool assign_player_team(edict_s* entity, const ServerPlayerTeam team) noexcept {
    return assign_regamedll_player_team(entity, team);
}

} // namespace scrimmod::plugin
