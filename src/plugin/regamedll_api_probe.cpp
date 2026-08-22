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
PlayerSpawnListener g_spawn_listener = nullptr;
TeamChoiceListener g_team_choice_listener = nullptr;
WeaponAcquireListener g_weapon_acquire_listener = nullptr;
bool g_gameplay_hooks_installed = false;

void on_give_default_items(IReGameHook_CBasePlayer_GiveDefaultItems* chain, CBasePlayer* player) {
    chain->callNext(player);
    if (g_spawn_listener != nullptr && player != nullptr) {
        g_spawn_listener(player->edict());
    }
}

BOOL on_choose_team(IReGameHook_HandleMenu_ChooseTeam* chain, CBasePlayer* player, const int slot) {
    if (g_team_choice_listener != nullptr && player != nullptr &&
        !g_team_choice_listener(player->edict())) {
        return FALSE;
    }
    return chain->callNext(player, slot);
}

BOOL on_can_have_player_item(IReGameHook_CSGameRules_CanHavePlayerItem* chain, CBasePlayer* player,
                             CBasePlayerItem* item) {
    if (g_weapon_acquire_listener != nullptr && player != nullptr && item != nullptr &&
        !g_weapon_acquire_listener(player->edict(), item->m_iId == WEAPON_KNIFE)) {
        return FALSE;
    }
    return chain->callNext(player, item);
}

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

void reset_regamedll_api() noexcept {
    remove_regamedll_gameplay_hooks();
    g_regamedll_api = nullptr;
}

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

bool ensure_regamedll_knife_loadout(edict_s* entity) noexcept {
    if (g_regamedll_api == nullptr || entity == nullptr) {
        return false;
    }

    CBasePlayer* player = CBasePlayer::Instance(entity);
    if (player == nullptr || player->CSPlayer() == nullptr) {
        return false;
    }
    player->CSPlayer()->RemoveAllItems(false);
    return player->CSPlayer()->GiveNamedItem("weapon_knife") != nullptr;
}

bool install_regamedll_gameplay_hooks(
    const PlayerSpawnListener spawn_listener, const TeamChoiceListener team_choice_listener,
    const WeaponAcquireListener weapon_acquire_listener) noexcept {
    if (g_regamedll_api == nullptr || g_regamedll_api->GetHookchains() == nullptr) {
        return false;
    }

    g_spawn_listener = spawn_listener;
    g_team_choice_listener = team_choice_listener;
    g_weapon_acquire_listener = weapon_acquire_listener;
    if (!g_gameplay_hooks_installed) {
        auto* hooks = g_regamedll_api->GetHookchains();
        hooks->CBasePlayer_GiveDefaultItems()->registerHook(on_give_default_items);
        hooks->HandleMenu_ChooseTeam()->registerHook(on_choose_team, HC_PRIORITY_HIGH);
        hooks->CSGameRules_CanHavePlayerItem()->registerHook(on_can_have_player_item,
                                                             HC_PRIORITY_HIGH);
        g_gameplay_hooks_installed = true;
    }
    return true;
}

void remove_regamedll_gameplay_hooks() noexcept {
    if (g_gameplay_hooks_installed && g_regamedll_api != nullptr &&
        g_regamedll_api->GetHookchains() != nullptr) {
        auto* hooks = g_regamedll_api->GetHookchains();
        hooks->CBasePlayer_GiveDefaultItems()->unregisterHook(on_give_default_items);
        hooks->HandleMenu_ChooseTeam()->unregisterHook(on_choose_team);
        hooks->CSGameRules_CanHavePlayerItem()->unregisterHook(on_can_have_player_item);
    }
    g_gameplay_hooks_installed = false;
    g_spawn_listener = nullptr;
    g_team_choice_listener = nullptr;
    g_weapon_acquire_listener = nullptr;
}

} // namespace scrimmod::plugin
