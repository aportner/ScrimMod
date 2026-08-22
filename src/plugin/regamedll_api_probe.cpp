#include "api_probe.hpp"

#include <dlfcn.h>

#include <extdll.h>
#include <meta_api.h>

#include <cbase.h>
#include <entity_state.h>
#include <pm_defs.h>
#include <regamedll_api.h>

namespace scrimmod::plugin {
namespace {

IReGameApi* g_regamedll_api = nullptr;

} // namespace

ApiProbeResult probe_regamedll_api(void* module) noexcept {
    ApiProbeResult result{};
    auto factory = reinterpret_cast<CreateInterfaceFn>(dlsym(module, CREATEINTERFACE_PROCNAME));
    if (factory == nullptr) {
        result.error = ApiError::RegamedllFactoryUnavailable;
        return result;
    }

    int factory_result = IFACE_FAILED;
    g_regamedll_api =
        reinterpret_cast<IReGameApi*>(factory(VRE_GAMEDLL_API_VERSION, &factory_result));
    if (g_regamedll_api == nullptr) {
        result.error = ApiError::RegamedllInterfaceUnavailable;
        return result;
    }

    result.major = g_regamedll_api->GetMajorVersion();
    result.minor = g_regamedll_api->GetMinorVersion();
    if (result.major != REGAMEDLL_API_VERSION_MAJOR || result.minor < REGAMEDLL_API_VERSION_MINOR) {
        result.error = ApiError::RegamedllVersionMismatch;
        reset_regamedll_api();
        return result;
    }

    if (g_regamedll_api->GetFuncs() == nullptr || g_regamedll_api->GetHookchains() == nullptr) {
        result.error = ApiError::RegamedllServicesUnavailable;
        reset_regamedll_api();
    }
    return result;
}

void reset_regamedll_api() noexcept { g_regamedll_api = nullptr; }

bool assign_regamedll_player_team(edict_s* entity, const ServerPlayerTeam team) noexcept {
    if (g_regamedll_api == nullptr || entity == nullptr) {
        return false;
    }

    CBasePlayer* player = CBasePlayer::Instance(entity);
    if (player == nullptr || player->CSPlayer() == nullptr) {
        return false;
    }

    TeamName destination = SPECTATOR;
    switch (team) {
    case ServerPlayerTeam::Terrorist:
        destination = TERRORIST;
        break;
    case ServerPlayerTeam::CounterTerrorist:
        destination = CT;
        break;
    case ServerPlayerTeam::Spectator:
        destination = SPECTATOR;
        break;
    }
    if (player->m_iTeam == destination) {
        return true;
    }
    return player->CSPlayer()->JoinTeam(destination);
}

} // namespace scrimmod::plugin
