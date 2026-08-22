#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
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
char g_scrim_allow_bots_default[] = "0";
cvar_t g_scrim_allow_bots = {
    "scrim_allow_bots", g_scrim_allow_bots_default, FCVAR_SERVER, 0.0F, nullptr,
};

struct ServerPlayerIdentity {
    std::string player_id;
    scrimmod::core::PlayerType type;
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

void queue_live_config() {
    if (g_engfuncs.pfnServerCommand != nullptr) {
        g_engfuncs.pfnServerCommand("exec cal.cfg\n");
    }
}

bool is_steam_player_id(const char* player_id) {
    if (player_id == nullptr || std::strncmp(player_id, "STEAM_", 6) != 0) {
        return false;
    }
    return std::strcmp(player_id, "STEAM_ID_PENDING") != 0 &&
           std::strcmp(player_id, "STEAM_ID_LAN") != 0;
}

bool bot_tracking_enabled() {
    return g_engfuncs.pfnCVarGetFloat != nullptr &&
           g_engfuncs.pfnCVarGetFloat(g_scrim_allow_bots.name) != 0.0F;
}

std::optional<ServerPlayerIdentity> get_player_identity(edict_t* entity, const bool include_bots) {
    if (entity == nullptr || entity->free != FALSE || (entity->v.flags & FL_PROXY) != 0) {
        return std::nullopt;
    }

    const char* auth_id = g_engfuncs.pfnGetPlayerAuthId(entity);
    if (is_steam_player_id(auth_id)) {
        return ServerPlayerIdentity{auth_id, scrimmod::core::PlayerType::Human};
    }

    const bool is_bot_auth_id = auth_id != nullptr && std::strcmp(auth_id, "BOT") == 0;
    if (include_bots && (is_bot_auth_id || (entity->v.flags & FL_FAKECLIENT) != 0)) {
        const int user_id = g_engfuncs.pfnGetPlayerUserId(entity);
        if (user_id > 0) {
            return ServerPlayerIdentity{"BOT:" + std::to_string(user_id),
                                        scrimmod::core::PlayerType::Bot};
        }
    }
    return std::nullopt;
}

edict_t* find_connected_entity(const std::string& player_id) {
    if (gpGlobals == nullptr) {
        return nullptr;
    }
    for (int index = 1; index <= gpGlobals->maxClients; ++index) {
        edict_t* entity = g_engfuncs.pfnPEntityOfEntIndex(index);
        const auto identity = get_player_identity(entity, true);
        if (identity.has_value() && identity->player_id == player_id) {
            return entity;
        }
    }
    return nullptr;
}

scrimmod::plugin::ServerPlayerTeam
server_team(const scrimmod::core::PlayerDestination destination) {
    switch (destination) {
    case scrimmod::core::PlayerDestination::Terrorist:
        return scrimmod::plugin::ServerPlayerTeam::Terrorist;
    case scrimmod::core::PlayerDestination::CounterTerrorist:
        return scrimmod::plugin::ServerPlayerTeam::CounterTerrorist;
    case scrimmod::core::PlayerDestination::Spectator:
        return scrimmod::plugin::ServerPlayerTeam::Spectator;
    }
    return scrimmod::plugin::ServerPlayerTeam::Spectator;
}

void apply_effects(const std::vector<scrimmod::core::Effect>& effects) {
    for (const auto& effect : effects) {
        switch (effect.type) {
        case scrimmod::core::EffectType::ExecutePregameConfig:
            queue_pregame_config();
            break;
        case scrimmod::core::EffectType::ExecuteLiveConfig:
            queue_live_config();
            break;
        case scrimmod::core::EffectType::AssignPlayerTeam: {
            edict_t* entity = find_connected_entity(effect.player_id);
            if (entity == nullptr) {
                server_print("[ScrimMod] Player disconnected before team assignment; "
                             "reconciliation deferred.\n");
            } else if (!scrimmod::plugin::assign_player_team(entity,
                                                             server_team(effect.destination))) {
                server_print("[ScrimMod] ReGameDLL rejected a player team assignment.\n");
            }
            break;
        }
        case scrimmod::core::EffectType::EnsureKnifeLoadout: {
            edict_t* entity = find_connected_entity(effect.player_id);
            if (entity != nullptr && !scrimmod::plugin::ensure_knife_loadout(entity)) {
                server_print("[ScrimMod] ReGameDLL could not enforce a knife loadout.\n");
            }
            break;
        }
        case scrimmod::core::EffectType::RestartRound:
            if (g_engfuncs.pfnServerCommand != nullptr) {
                char command[32]{};
                std::snprintf(command, sizeof(command), "sv_restart %d\n",
                              effect.value > 0 ? effect.value : 1);
                g_engfuncs.pfnServerCommand(command);
            }
            break;
        }
    }
}

void on_player_spawn(edict_t* entity) {
    const auto identity = get_player_identity(entity, true);
    if (identity.has_value()) {
        apply_effects(g_match_engine.reconciliation_effects());
    }
}

bool allow_team_choice(edict_t* entity) {
    const auto identity = get_player_identity(entity, true);
    return !identity.has_value() || g_match_engine.can_player_choose_team(identity->player_id);
}

bool allow_weapon_acquisition(edict_t* entity, const bool is_knife) {
    const auto identity = get_player_identity(entity, true);
    return !identity.has_value() ||
           g_match_engine.can_player_acquire_weapon(identity->player_id, is_knife);
}

void on_player_killed(edict_t* victim, edict_t* killer) {
    const auto victim_identity = get_player_identity(victim, true);
    const auto killer_identity = get_player_identity(killer, true);
    const auto result =
        g_match_engine.player_killed(victim_identity.has_value() ? victim_identity->player_id : "",
                                     killer_identity.has_value() ? killer_identity->player_id : "");
    apply_effects(result.effects);
    switch (result.outcome) {
    case scrimmod::core::KnifeKillOutcome::Ignored:
        break;
    case scrimmod::core::KnifeKillOutcome::WinnerDecided:
        server_print("[ScrimMod] Knife winner recorded; entered KnifeComplete.\n");
        break;
    case scrimmod::core::KnifeKillOutcome::ReplayRequired:
        server_print("[ScrimMod] Ambiguous knife kill; returned to KnifeSetup for replay.\n");
        break;
    }
}

void on_round_end(const scrimmod::plugin::RoundEndType type,
                  const scrimmod::plugin::RoundWinner winner) {
    if (g_match_engine.state().phase() == scrimmod::core::Phase::RegulationFirstHalf) {
        if (type != scrimmod::plugin::RoundEndType::Gameplay) {
            return;
        }
        std::optional<scrimmod::core::Side> winning_side;
        if (winner == scrimmod::plugin::RoundWinner::Terrorist) {
            winning_side = scrimmod::core::Side::Terrorist;
        } else if (winner == scrimmod::plugin::RoundWinner::CounterTerrorist) {
            winning_side = scrimmod::core::Side::CounterTerrorist;
        }
        const auto result = g_match_engine.regulation_round_ended(winning_side);
        apply_effects(result.effects);
        if (result.outcome == scrimmod::core::RoundOutcome::Ambiguous) {
            server_print("[ScrimMod] Ambiguous live round result; score unchanged. Use "
                         "scrim_round_winner <a|b> for recovery.\n");
        } else if (result.outcome == scrimmod::core::RoundOutcome::Counted ||
                   result.outcome == scrimmod::core::RoundOutcome::HalfComplete) {
            char score[128]{};
            std::snprintf(score, sizeof(score), "[ScrimMod] Score: Team A %d - %d Team B%s\n",
                          g_match_engine.state().team(scrimmod::core::LogicalTeam::A).total_score,
                          g_match_engine.state().team(scrimmod::core::LogicalTeam::B).total_score,
                          result.outcome == scrimmod::core::RoundOutcome::HalfComplete
                              ? "; halftime reached"
                              : "");
            server_print(score);
            if (result.outcome == scrimmod::core::RoundOutcome::Counted &&
                g_match_engine.state().period_rounds_completed() + 1 ==
                    g_match_engine.state().regulation_rounds_per_half()) {
                server_print("[ScrimMod] *** LAST ROUND OF THE HALF - BUY OUT ***\n");
            }
        }
        return;
    }

    const auto result =
        g_match_engine.knife_round_ended(type != scrimmod::plugin::RoundEndType::Gameplay);
    apply_effects(result.effects);
    if (result.changed) {
        server_print("[ScrimMod] Knife round ended without a winner; returned to KnifeSetup.\n");
    }
}

void on_round_restart() {
    if (g_match_engine.state().phase() != scrimmod::core::Phase::LiveOnThree) {
        return;
    }
    const auto result = g_match_engine.live_on_three_restart_completed();
    apply_effects(result.effects);
    if (g_match_engine.state().phase() == scrimmod::core::Phase::RegulationFirstHalf) {
        server_print("[ScrimMod] Live on three complete; regulation first half is live.\n");
    } else if (g_match_engine.state().phase() == scrimmod::core::Phase::RegulationSecondHalf) {
        server_print("[ScrimMod] Live on three complete; regulation second half is live.\n");
    }
}

void track_connected_player(edict_t* entity) {
    if (!g_match_engine.state().enabled() || entity == nullptr || entity->free != FALSE) {
        return;
    }

    const auto identity = get_player_identity(entity, bot_tracking_enabled());
    if (!identity.has_value()) {
        return;
    }

    const char* name = g_engfuncs.pfnSzFromIndex(entity->v.netname);
    static_cast<void>(g_match_engine.player_connected(identity->player_id,
                                                      name != nullptr ? name : "", identity->type));
    apply_effects(g_match_engine.reconciliation_effects());
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
        const auto identity = get_player_identity(entity, true);
        if (identity.has_value()) {
            const auto previous_phase = g_match_engine.state().phase();
            const auto result = g_match_engine.player_disconnected(identity->player_id);
            apply_effects(result.effects);
            if (previous_phase == scrimmod::core::Phase::KnifeLive &&
                g_match_engine.state().phase() == scrimmod::core::Phase::KnifeSetup) {
                server_print(
                    "[ScrimMod] Captain disconnected; knife round paused at KnifeSetup.\n");
            } else if (previous_phase == scrimmod::core::Phase::LiveOnThree) {
                server_print(
                    g_match_engine.state().phase() == scrimmod::core::Phase::Halftime
                        ? "[ScrimMod] Captain disconnected during halftime LO3; paused at "
                          "Halftime.\n"
                        : "[ScrimMod] Captain disconnected during LO3; rewound to Ready.\n");
            }
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
    std::snprintf(status, sizeof(status),
                  "Period Score: Team A %d - %d Team B\nRounds Completed: %d / %d\n",
                  state.team(scrimmod::core::LogicalTeam::A).period_score,
                  state.team(scrimmod::core::LogicalTeam::B).period_score,
                  state.period_rounds_completed(), state.regulation_rounds_per_half());
    server_print(status);

    std::vector<const scrimmod::core::Player*> players;
    players.reserve(state.players().size());
    for (const auto& [player_id, player] : state.players()) {
        static_cast<void>(player_id);
        players.push_back(&player);
    }
    std::sort(players.begin(), players.end(), [](const auto* left, const auto* right) {
        return left->player_id < right->player_id;
    });

    std::snprintf(status, sizeof(status), "Tracked Players: %zu\n", players.size());
    server_print(status);
    for (const auto* player : players) {
        const bool eligible = std::binary_search(state.eligible_players().begin(),
                                                 state.eligible_players().end(), player->player_id);
        std::snprintf(status, sizeof(status), "  %s [%s] - %s%s%s\n",
                      player->last_known_name.c_str(), player->player_id.c_str(),
                      player->connected ? "connected" : "disconnected",
                      player->type == scrimmod::core::PlayerType::Bot ? ", bot" : "",
                      eligible ? ", eligible" : "");
        server_print(status);
    }

    for (const auto team : {scrimmod::core::LogicalTeam::A, scrimmod::core::LogicalTeam::B}) {
        const char team_name = team == scrimmod::core::LogicalTeam::A ? 'A' : 'B';
        const auto& captain = state.team(team).captain_player_id;
        if (!captain.has_value()) {
            std::snprintf(status, sizeof(status), "Captain %c: not selected\n", team_name);
            server_print(status);
            continue;
        }

        const auto player = state.players().find(*captain);
        const char* name =
            player != state.players().end() ? player->second.last_known_name.c_str() : "unknown";
        const char* connection = player != state.players().end() && player->second.connected
                                     ? "connected"
                                     : "disconnected";
        std::snprintf(status, sizeof(status), "Captain %c: %s [%s] - %s, %s\n", team_name, name,
                      captain->c_str(), connection,
                      state.team(team).captain_ready ? "ready" : "not ready");
        server_print(status);
    }

    if (state.team(scrimmod::core::LogicalTeam::A).current_side.has_value()) {
        const char* team_a_side = *state.team(scrimmod::core::LogicalTeam::A).current_side ==
                                          scrimmod::core::Side::Terrorist
                                      ? "T"
                                      : "CT";
        const char* team_b_side = *state.team(scrimmod::core::LogicalTeam::B).current_side ==
                                          scrimmod::core::Side::Terrorist
                                      ? "T"
                                      : "CT";
        std::snprintf(status, sizeof(status), "Sides: Team A -> %s, Team B -> %s\n", team_a_side,
                      team_b_side);
        server_print(status);
    }
    if (state.knife_winner_player_id().has_value()) {
        std::snprintf(status, sizeof(status), "Knife Winner: %s\nKnife Loser: %s\n",
                      state.knife_winner_player_id()->c_str(),
                      state.knife_loser_player_id()->c_str());
        server_print(status);
    }
    if (state.pending_knife_reward_choice().has_value()) {
        const char* pending_reward =
            *state.pending_knife_reward_choice() == scrimmod::core::KnifeRewardChoice::StartingSide
                ? "starting side"
                : "first pick";
        std::snprintf(status, sizeof(status), "Knife Reward Choice: %s%s\n", pending_reward,
                      state.confirmed_knife_reward_choice().has_value() ? " (confirmed)"
                                                                        : " (pending)");
        server_print(status);
    }
    if (state.first_picker_player_id().has_value()) {
        std::snprintf(status, sizeof(status), "First Picker: %s\nSide Chooser: %s\n",
                      state.first_picker_player_id()->c_str(),
                      state.side_chooser_player_id()->c_str());
        server_print(status);
    }
    if (state.pending_starting_side().has_value()) {
        std::snprintf(status, sizeof(status), "Pending Starting Side: %s\n",
                      *state.pending_starting_side() == scrimmod::core::Side::Terrorist ? "T"
                                                                                        : "CT");
        server_print(status);
    }
    if (state.team(scrimmod::core::LogicalTeam::A).starting_side.has_value()) {
        const char* team_a_side = *state.team(scrimmod::core::LogicalTeam::A).starting_side ==
                                          scrimmod::core::Side::Terrorist
                                      ? "T"
                                      : "CT";
        const char* team_b_side = *state.team(scrimmod::core::LogicalTeam::B).starting_side ==
                                          scrimmod::core::Side::Terrorist
                                      ? "T"
                                      : "CT";
        std::snprintf(status, sizeof(status),
                      "Regulation Starting Sides: Team A -> %s, Team B -> %s\n", team_a_side,
                      team_b_side);
        server_print(status);
    }
    std::snprintf(status, sizeof(status), "Draft Type: %s\n",
                  state.draft_type() == scrimmod::core::DraftType::AB ? "AB" : "Snake");
    server_print(status);
    if (state.current_draft_captain_player_id().has_value()) {
        std::snprintf(status, sizeof(status), "Current Draft Captain: %s\nPicks Remaining: %d\n",
                      state.current_draft_captain_player_id()->c_str(),
                      state.draft_picks_remaining_in_turn());
        server_print(status);
    }
    if (state.pending_draft_player_id().has_value()) {
        std::snprintf(status, sizeof(status), "Pending Draft Pick: %s\n",
                      state.pending_draft_player_id()->c_str());
        server_print(status);
    }
    if (!state.available_draft_players().empty()) {
        server_print("Available Draft Players:\n");
        for (const auto& player_id : state.available_draft_players()) {
            const auto& player = state.players().at(player_id);
            std::snprintf(status, sizeof(status), "  %s [%s]%s\n", player.last_known_name.c_str(),
                          player_id.c_str(), player.connected ? "" : " (disconnected)");
            server_print(status);
        }
    }
    if (!state.drafted_players().empty()) {
        server_print("Drafted Players:\n");
        for (const auto& player_id : state.drafted_players()) {
            const auto& player = state.players().at(player_id);
            std::snprintf(status, sizeof(status), "  %s [%s] -> Team %c%s\n",
                          player.last_known_name.c_str(), player_id.c_str(),
                          player.logical_team == scrimmod::core::LogicalTeam::A ? 'A' : 'B',
                          player.connected ? "" : " (disconnected)");
            server_print(status);
        }
    }
    if (state.phase() == scrimmod::core::Phase::LiveOnThree) {
        const char* target =
            state.live_on_three_target_phase() == scrimmod::core::Phase::RegulationSecondHalf
                ? "RegulationSecondHalf"
                : "RegulationFirstHalf";
        std::snprintf(status, sizeof(status), "LO3 Restarts Completed: %d / 3\nLO3 Target: %s\n",
                      state.live_on_three_restarts_completed(), target);
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
    case EligibilityError::InvalidPlayerId:
        return "the player argument is empty";
    case EligibilityError::UnknownPlayer:
        return "no tracked player matches that player ID or exact name";
    }
    return "unknown eligibility error";
}

std::string resolve_unique_player_name(const char* target, bool& ambiguous) {
    ambiguous = false;
    std::string resolved;
    if (target == nullptr) {
        return resolved;
    }

    for (const auto& [player_id, player] : g_match_engine.state().players()) {
        if (player.last_known_name != target) {
            continue;
        }
        if (!resolved.empty()) {
            ambiguous = true;
            return {};
        }
        resolved = player_id;
    }
    return resolved;
}

void update_eligible_player(const bool add) {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print(add ? "Usage: scrim_add <player ID or exact name>\n"
                         : "Usage: scrim_remove <player ID or exact name>\n");
        return;
    }

    const char* target = g_engfuncs.pfnCmd_Argv(1);
    auto result = add ? g_match_engine.add_eligible_player(target != nullptr ? target : "")
                      : g_match_engine.remove_eligible_player(target != nullptr ? target : "");
    if (result.error == scrimmod::core::EligibilityError::UnknownPlayer) {
        bool ambiguous = false;
        const std::string player_id = resolve_unique_player_name(target, ambiguous);
        if (ambiguous) {
            server_print("[ScrimMod] Player name is ambiguous; use the player ID.\n");
            return;
        }
        if (!player_id.empty()) {
            result = add ? g_match_engine.add_eligible_player(player_id)
                         : g_match_engine.remove_eligible_player(player_id);
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

const char* captain_error_message(const scrimmod::core::CaptainSelectionError error) {
    using scrimmod::core::CaptainSelectionError;
    switch (error) {
    case CaptainSelectionError::None:
        return "none";
    case CaptainSelectionError::ScrimDisabled:
        return "ScrimMod is disabled";
    case CaptainSelectionError::WrongPhase:
        return "captains can only be changed during captain selection";
    case CaptainSelectionError::PoolNotCaptured:
        return "the eligible pool has not been captured";
    case CaptainSelectionError::InvalidPlayerId:
        return "the player argument is empty";
    case CaptainSelectionError::UnknownPlayer:
        return "no tracked player matches that player ID or exact name";
    case CaptainSelectionError::IneligiblePlayer:
        return "that player is not in the eligible pool";
    case CaptainSelectionError::DuplicateCaptain:
        return "that player is already the other team's captain";
    }
    return "unknown captain selection error";
}

void select_captain(const scrimmod::core::LogicalTeam team) {
    const char team_name = team == scrimmod::core::LogicalTeam::A ? 'A' : 'B';
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print(team == scrimmod::core::LogicalTeam::A
                         ? "Usage: scrim_captain_a <player ID or exact name>\n"
                         : "Usage: scrim_captain_b <player ID or exact name>\n");
        return;
    }

    const char* target = g_engfuncs.pfnCmd_Argv(1);
    auto result = g_match_engine.select_captain(team, target != nullptr ? target : "");
    if (result.error == scrimmod::core::CaptainSelectionError::UnknownPlayer) {
        bool ambiguous = false;
        const std::string player_id = resolve_unique_player_name(target, ambiguous);
        if (ambiguous) {
            server_print("[ScrimMod] Player name is ambiguous; use the player ID.\n");
            return;
        }
        if (!player_id.empty()) {
            result = g_match_engine.select_captain(team, player_id);
        }
    }

    if (!result.ok()) {
        server_print("[ScrimMod] Cannot select captain: ");
        server_print(captain_error_message(result.error));
        server_print(".\n");
        return;
    }

    char message[96]{};
    std::snprintf(message, sizeof(message),
                  result.changed ? "[ScrimMod] Team %c captain selected.\n"
                                 : "[ScrimMod] Team %c captain is already selected.\n",
                  team_name);
    server_print(message);
}

void select_captain_a() { select_captain(scrimmod::core::LogicalTeam::A); }

void select_captain_b() { select_captain(scrimmod::core::LogicalTeam::B); }

void clear_captain() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_captain_clear <a|b>\n");
        return;
    }

    const char* target = g_engfuncs.pfnCmd_Argv(1);
    scrimmod::core::LogicalTeam team{};
    if (target != nullptr && (std::strcmp(target, "a") == 0 || std::strcmp(target, "A") == 0)) {
        team = scrimmod::core::LogicalTeam::A;
    } else if (target != nullptr &&
               (std::strcmp(target, "b") == 0 || std::strcmp(target, "B") == 0)) {
        team = scrimmod::core::LogicalTeam::B;
    } else {
        server_print("Usage: scrim_captain_clear <a|b>\n");
        return;
    }

    const auto result = g_match_engine.clear_captain(team);
    if (!result.ok()) {
        server_print("[ScrimMod] Cannot clear captain: ");
        server_print(captain_error_message(result.error));
        server_print(".\n");
        return;
    }
    server_print(result.changed ? "[ScrimMod] Captain selection cleared.\n"
                                : "[ScrimMod] That captain is already clear.\n");
}

void confirm_captains() {
    if (g_engfuncs.pfnCmd_Argc() != 1) {
        server_print("Usage: scrim_captains_confirm\n");
        return;
    }

    const bool team_a_starts_ct =
        g_engfuncs.pfnRandomLong != nullptr && g_engfuncs.pfnRandomLong(0, 1) != 0;
    const auto result =
        g_match_engine.confirm_captains(team_a_starts_ct ? scrimmod::core::Side::CounterTerrorist
                                                         : scrimmod::core::Side::Terrorist);
    if (!result.ok()) {
        switch (result.error) {
        case scrimmod::core::TransitionError::ScrimDisabled:
            server_print("[ScrimMod] Cannot confirm captains: ScrimMod is disabled.\n");
            break;
        case scrimmod::core::TransitionError::IllegalTransition:
            server_print("[ScrimMod] Cannot confirm captains outside captain selection.\n");
            break;
        case scrimmod::core::TransitionError::PrerequisiteNotMet:
            server_print("[ScrimMod] Cannot confirm captains until both teams have one.\n");
            break;
        case scrimmod::core::TransitionError::None:
            break;
        }
        return;
    }

    apply_effects(result.effects);
    server_print("[ScrimMod] Captains confirmed; entered KnifeSetup.\n");
}

void start_knife_round() {
    if (g_engfuncs.pfnCmd_Argc() != 1) {
        server_print("Usage: scrim_knife_start\n");
        return;
    }

    const auto result = g_match_engine.transition_to(scrimmod::core::Phase::KnifeLive);
    if (!result.ok()) {
        server_print("[ScrimMod] Cannot start knife round outside KnifeSetup.\n");
        return;
    }
    if (!result.changed) {
        server_print("[ScrimMod] Knife round is already live.\n");
        return;
    }
    apply_effects(result.effects);
    server_print("[ScrimMod] Knife round starting; queued sv_restart 1.\n");
}

void force_knife_winner() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_knife_winner <player ID or exact name>\n");
        return;
    }

    const char* target = g_engfuncs.pfnCmd_Argv(1);
    auto result = g_match_engine.force_knife_winner(target != nullptr ? target : "");
    if (result.outcome == scrimmod::core::KnifeKillOutcome::Ignored) {
        bool ambiguous = false;
        const std::string player_id = resolve_unique_player_name(target, ambiguous);
        if (ambiguous) {
            server_print("[ScrimMod] Player name is ambiguous; use the player ID.\n");
            return;
        }
        if (!player_id.empty()) {
            result = g_match_engine.force_knife_winner(player_id);
        }
    }

    if (result.outcome != scrimmod::core::KnifeKillOutcome::WinnerDecided) {
        server_print(
            "[ScrimMod] Knife winner must be a selected captain during the knife phase.\n");
        return;
    }
    apply_effects(result.effects);
    server_print("[ScrimMod] Admin set the knife winner; entered KnifeComplete.\n");
}

void choose_knife_reward() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_knife_choice <side|pick>\n");
        return;
    }
    const char* choice = g_engfuncs.pfnCmd_Argv(1);
    scrimmod::core::KnifeRewardChoice reward;
    if (choice != nullptr && std::strcmp(choice, "side") == 0) {
        reward = scrimmod::core::KnifeRewardChoice::StartingSide;
    } else if (choice != nullptr && std::strcmp(choice, "pick") == 0) {
        reward = scrimmod::core::KnifeRewardChoice::FirstPick;
    } else {
        server_print("Usage: scrim_knife_choice <side|pick>\n");
        return;
    }

    const auto result = g_match_engine.choose_knife_reward(reward);
    if (!result.ok()) {
        server_print("[ScrimMod] Knife reward can only be chosen after a knife winner exists.\n");
        return;
    }
    server_print(reward == scrimmod::core::KnifeRewardChoice::StartingSide
                     ? "[ScrimMod] Pending choice: starting side. Confirm it explicitly.\n"
                     : "[ScrimMod] Pending choice: first draft pick. Confirm it explicitly.\n");
}

void confirm_knife_reward() {
    if (g_engfuncs.pfnCmd_Argc() != 1) {
        server_print("Usage: scrim_knife_choice_confirm\n");
        return;
    }
    const auto result = g_match_engine.confirm_knife_reward();
    if (!result.ok()) {
        server_print("[ScrimMod] Select a knife reward before confirming it.\n");
        return;
    }
    server_print("[ScrimMod] Knife reward confirmed; the other captain must choose a starting side "
                 "as applicable.\n");
}

void choose_starting_side() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_starting_side <ct|t>\n");
        return;
    }
    const char* choice = g_engfuncs.pfnCmd_Argv(1);
    scrimmod::core::Side side;
    if (choice != nullptr && std::strcmp(choice, "ct") == 0) {
        side = scrimmod::core::Side::CounterTerrorist;
    } else if (choice != nullptr && std::strcmp(choice, "t") == 0) {
        side = scrimmod::core::Side::Terrorist;
    } else {
        server_print("Usage: scrim_starting_side <ct|t>\n");
        return;
    }

    const auto result = g_match_engine.choose_starting_side(side);
    if (!result.ok()) {
        server_print("[ScrimMod] Confirm the knife reward before choosing a starting side.\n");
        return;
    }
    server_print(side == scrimmod::core::Side::CounterTerrorist
                     ? "[ScrimMod] Pending starting side: CT. Confirm it explicitly.\n"
                     : "[ScrimMod] Pending starting side: T. Confirm it explicitly.\n");
}

void confirm_starting_side() {
    if (g_engfuncs.pfnCmd_Argc() != 1) {
        server_print("Usage: scrim_starting_side_confirm\n");
        return;
    }
    const auto result = g_match_engine.confirm_starting_side();
    if (!result.ok()) {
        server_print("[ScrimMod] Select a starting side before confirming it.\n");
        return;
    }
    apply_effects(result.effects);
    server_print(g_match_engine.state().phase() == scrimmod::core::Phase::Ready
                     ? "[ScrimMod] Starting side confirmed; no draft picks remain, entered Ready.\n"
                     : "[ScrimMod] Starting side confirmed; captains placed and Draft entered.\n");
}

void set_draft_type() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_draft_type <ab|snake>\n");
        return;
    }
    const char* value = g_engfuncs.pfnCmd_Argv(1);
    scrimmod::core::DraftType type;
    if (value != nullptr && std::strcmp(value, "ab") == 0) {
        type = scrimmod::core::DraftType::AB;
    } else if (value != nullptr && std::strcmp(value, "snake") == 0) {
        type = scrimmod::core::DraftType::Snake;
    } else {
        server_print("Usage: scrim_draft_type <ab|snake>\n");
        return;
    }
    const auto result = g_match_engine.set_draft_type(type);
    if (!result.ok()) {
        server_print("[ScrimMod] Draft type cannot be changed after Draft begins.\n");
        return;
    }
    server_print(type == scrimmod::core::DraftType::AB ? "[ScrimMod] Draft type set to AB.\n"
                                                       : "[ScrimMod] Draft type set to Snake.\n");
}

const char* draft_error_message(const scrimmod::core::DraftError error) {
    using scrimmod::core::DraftError;
    switch (error) {
    case DraftError::None:
        return "none";
    case DraftError::ScrimDisabled:
        return "ScrimMod is disabled";
    case DraftError::WrongPhase:
        return "draft picks can only be made during Draft";
    case DraftError::InvalidPlayerId:
        return "the player argument is empty";
    case DraftError::UnknownPlayer:
        return "no tracked player matches that player ID or exact name";
    case DraftError::IneligiblePlayer:
        return "that player is not available to draft";
    case DraftError::AlreadyDrafted:
        return "that player has already been drafted";
    case DraftError::ChoiceNotSelected:
        return "select a draft pick before confirming it";
    case DraftError::CaptainDisconnected:
        return "the current draft captain is disconnected";
    }
    return "unknown draft error";
}

void choose_draft_player() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_pick <player ID or exact name>\n");
        return;
    }
    const char* target = g_engfuncs.pfnCmd_Argv(1);
    auto result = g_match_engine.choose_draft_player(target != nullptr ? target : "", true);
    if (result.error == scrimmod::core::DraftError::UnknownPlayer) {
        bool ambiguous = false;
        const std::string player_id = resolve_unique_player_name(target, ambiguous);
        if (ambiguous) {
            server_print("[ScrimMod] Player name is ambiguous; use the player ID.\n");
            return;
        }
        if (!player_id.empty()) {
            result = g_match_engine.choose_draft_player(player_id, true);
        }
    }
    if (!result.ok()) {
        server_print("[ScrimMod] Cannot select draft pick: ");
        server_print(draft_error_message(result.error));
        server_print(".\n");
        return;
    }
    server_print("[ScrimMod] Pending draft pick recorded; confirm it explicitly.\n");
}

void confirm_draft_player() {
    if (g_engfuncs.pfnCmd_Argc() != 1) {
        server_print("Usage: scrim_pick_confirm\n");
        return;
    }
    const auto result = g_match_engine.confirm_draft_player(true);
    if (!result.ok()) {
        server_print("[ScrimMod] Cannot confirm draft pick: ");
        server_print(draft_error_message(result.error));
        server_print(".\n");
        return;
    }
    apply_effects(result.effects);
    server_print(g_match_engine.state().phase() == scrimmod::core::Phase::Ready
                     ? "[ScrimMod] Draft complete; entered Ready.\n"
                     : "[ScrimMod] Draft pick confirmed.\n");
}

const char* ready_error_message(const scrimmod::core::ReadyError error) {
    using scrimmod::core::ReadyError;
    switch (error) {
    case ReadyError::None:
        return "none";
    case ReadyError::ScrimDisabled:
        return "ScrimMod is disabled";
    case ReadyError::WrongPhase:
        return "captain readiness can only change during Ready";
    case ReadyError::InvalidPlayerId:
        return "the captain argument is empty";
    case ReadyError::UnknownPlayer:
        return "the selected captain is not tracked";
    case ReadyError::NotCaptain:
        return "the selected player is not a captain";
    case ReadyError::CaptainDisconnected:
        return "the captain is disconnected";
    }
    return "unknown ready error";
}

void set_team_ready(const bool ready) {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print(ready ? "Usage: scrim_ready <a|b>\n" : "Usage: scrim_unready <a|b>\n");
        return;
    }
    const char* team_argument = g_engfuncs.pfnCmd_Argv(1);
    scrimmod::core::LogicalTeam team;
    if (team_argument != nullptr && std::strcmp(team_argument, "a") == 0) {
        team = scrimmod::core::LogicalTeam::A;
    } else if (team_argument != nullptr && std::strcmp(team_argument, "b") == 0) {
        team = scrimmod::core::LogicalTeam::B;
    } else {
        server_print(ready ? "Usage: scrim_ready <a|b>\n" : "Usage: scrim_unready <a|b>\n");
        return;
    }

    const auto& captain = g_match_engine.state().team(team).captain_player_id;
    if (!captain.has_value()) {
        server_print("[ScrimMod] That logical team does not have a captain.\n");
        return;
    }
    const auto result = g_match_engine.set_captain_ready(*captain, ready, true);
    if (!result.ok()) {
        server_print("[ScrimMod] Cannot change captain readiness: ");
        server_print(ready_error_message(result.error));
        server_print(".\n");
        return;
    }
    apply_effects(result.effects);
    if (g_match_engine.state().phase() == scrimmod::core::Phase::LiveOnThree) {
        server_print("[ScrimMod] Both captains ready; cal.cfg queued and LO3 started.\n");
    } else if (ready) {
        server_print("[ScrimMod] Captain marked ready.\n");
    } else {
        server_print(g_match_engine.state().phase() == scrimmod::core::Phase::Halftime
                         ? "[ScrimMod] Halftime LO3 paused; run scrim_halftime_start to retry.\n"
                         : "[ScrimMod] Captain marked unready; Ready checkpoint restored.\n");
    }
}

void ready_team() { set_team_ready(true); }

void unready_team() { set_team_ready(false); }

void start_halftime() {
    if (g_engfuncs.pfnCmd_Argc() != 1) {
        server_print("Usage: scrim_halftime_start\n");
        return;
    }
    const auto result = g_match_engine.start_halftime_live_on_three(true);
    if (!result.ok()) {
        server_print("[ScrimMod] Halftime LO3 can only start while paused at Halftime.\n");
        return;
    }
    apply_effects(result.effects);
    server_print("[ScrimMod] Halftime LO3 restarted.\n");
}

void set_regulation_rounds() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_rounds <1-100>\n");
        return;
    }
    const char* value = g_engfuncs.pfnCmd_Argv(1);
    char* end = nullptr;
    const long rounds = value != nullptr ? std::strtol(value, &end, 10) : 0;
    if (value == nullptr || end == value || *end != '\0' || rounds < 1 || rounds > 100) {
        server_print("Usage: scrim_rounds <1-100>\n");
        return;
    }
    const auto result = g_match_engine.set_regulation_rounds_per_half(static_cast<int>(rounds));
    if (!result.ok()) {
        server_print("[ScrimMod] Regulation length cannot change after LO3 begins.\n");
        return;
    }
    server_print("[ScrimMod] Regulation half length updated.\n");
}

void force_round_winner() {
    if (g_engfuncs.pfnCmd_Argc() != 2) {
        server_print("Usage: scrim_round_winner <a|b>\n");
        return;
    }
    const char* value = g_engfuncs.pfnCmd_Argv(1);
    scrimmod::core::LogicalTeam team;
    if (value != nullptr && std::strcmp(value, "a") == 0) {
        team = scrimmod::core::LogicalTeam::A;
    } else if (value != nullptr && std::strcmp(value, "b") == 0) {
        team = scrimmod::core::LogicalTeam::B;
    } else {
        server_print("Usage: scrim_round_winner <a|b>\n");
        return;
    }
    const auto result = g_match_engine.force_regulation_round_winner(team);
    if (result.outcome == scrimmod::core::RoundOutcome::Ignored) {
        server_print("[ScrimMod] Round recovery is only available during live regulation.\n");
        return;
    }
    apply_effects(result.effects);
    char score[128]{};
    std::snprintf(
        score, sizeof(score), "[ScrimMod] Admin score recovery: Team A %d - %d Team B%s\n",
        g_match_engine.state().team(scrimmod::core::LogicalTeam::A).total_score,
        g_match_engine.state().team(scrimmod::core::LogicalTeam::B).total_score,
        result.outcome == scrimmod::core::RoundOutcome::HalfComplete ? "; halftime reached" : "");
    server_print(score);
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
    g_engfuncs.pfnCVarRegister(&g_scrim_allow_bots);
    if (!scrimmod::plugin::add_cvar_listener(g_scrim_enabled.name, apply_enabled_value)) {
        server_print("[ScrimMod] Cannot load: failed to register scrim_enabled listener.\n");
        scrimmod::plugin::shutdown_server_apis();
        gpMetaGlobals = nullptr;
        gpGamedllFuncs = nullptr;
        return FALSE;
    }
    if (!scrimmod::plugin::install_gameplay_hooks(on_player_spawn, allow_team_choice,
                                                  allow_weapon_acquisition, on_player_killed,
                                                  on_round_end, on_round_restart)) {
        server_print("[ScrimMod] Cannot load: failed to register ReGameDLL gameplay hooks.\n");
        scrimmod::plugin::remove_cvar_listener(g_scrim_enabled.name, apply_enabled_value);
        scrimmod::plugin::shutdown_server_apis();
        gpMetaGlobals = nullptr;
        gpGamedllFuncs = nullptr;
        return FALSE;
    }

    g_engfuncs.pfnAddServerCommand("scrim_status", print_status);
    g_engfuncs.pfnAddServerCommand("scrim_add", add_eligible_player);
    g_engfuncs.pfnAddServerCommand("scrim_remove", remove_eligible_player);
    g_engfuncs.pfnAddServerCommand("scrim_captain_a", select_captain_a);
    g_engfuncs.pfnAddServerCommand("scrim_captain_b", select_captain_b);
    g_engfuncs.pfnAddServerCommand("scrim_captain_clear", clear_captain);
    g_engfuncs.pfnAddServerCommand("scrim_captains_confirm", confirm_captains);
    g_engfuncs.pfnAddServerCommand("scrim_knife_start", start_knife_round);
    g_engfuncs.pfnAddServerCommand("scrim_knife_winner", force_knife_winner);
    g_engfuncs.pfnAddServerCommand("scrim_knife_choice", choose_knife_reward);
    g_engfuncs.pfnAddServerCommand("scrim_knife_choice_confirm", confirm_knife_reward);
    g_engfuncs.pfnAddServerCommand("scrim_starting_side", choose_starting_side);
    g_engfuncs.pfnAddServerCommand("scrim_starting_side_confirm", confirm_starting_side);
    g_engfuncs.pfnAddServerCommand("scrim_draft_type", set_draft_type);
    g_engfuncs.pfnAddServerCommand("scrim_pick", choose_draft_player);
    g_engfuncs.pfnAddServerCommand("scrim_pick_confirm", confirm_draft_player);
    g_engfuncs.pfnAddServerCommand("scrim_ready", ready_team);
    g_engfuncs.pfnAddServerCommand("scrim_unready", unready_team);
    g_engfuncs.pfnAddServerCommand("scrim_halftime_start", start_halftime);
    g_engfuncs.pfnAddServerCommand("scrim_rounds", set_regulation_rounds);
    g_engfuncs.pfnAddServerCommand("scrim_round_winner", force_round_winner);
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
    scrimmod::plugin::remove_gameplay_hooks();
    apply_effects(g_match_engine.set_enabled(false).effects);
    scrimmod::plugin::shutdown_server_apis();
    server_print("[ScrimMod] Plugin unloaded.\n");
    gpMetaGlobals = nullptr;
    gpGamedllFuncs = nullptr;
    return TRUE;
}
