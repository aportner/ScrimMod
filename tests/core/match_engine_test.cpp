#include "scrimmod/core/match_engine.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using scrimmod::core::CaptainSelectionError;
    using scrimmod::core::EffectType;
    using scrimmod::core::EligibilityError;
    using scrimmod::core::MatchEngine;
    using scrimmod::core::Phase;
    using scrimmod::core::PlayerType;
    using scrimmod::core::PlayerUpdateError;
    using scrimmod::core::TransitionError;

    MatchEngine engine;
    const auto disabled_transition = engine.transition_to(Phase::CaptainSelection);
    require(disabled_transition.error == TransitionError::ScrimDisabled,
            "disabled engine rejects phase transitions");
    require(engine.player_connected("STEAM_0:1:10", "player").error ==
                PlayerUpdateError::ScrimDisabled,
            "disabled engine rejects player updates");
    require(engine.select_captain(scrimmod::core::LogicalTeam::A, "STEAM_0:1:10").error ==
                CaptainSelectionError::ScrimDisabled,
            "disabled engine rejects captain selection");

    const auto enabled = engine.set_enabled(true);
    require(enabled.ok() && enabled.changed, "enabling succeeds and changes state");
    require(enabled.effects.empty(), "enabling has no server effects yet");
    require(engine.state().enabled(), "engine state is enabled");
    require(engine.state().phase() == Phase::CaptainSelection, "enabling enters captain selection");
    require(engine.add_eligible_player("STEAM_0:1:10").error == EligibilityError::PoolNotCaptured,
            "eligible pool cannot be edited before capture");

    const auto connected = engine.player_connected("  steam_0:1:10  ", "First Name");
    require(connected.ok() && connected.changed, "new player connection is recorded");
    require(engine.state().players().size() == 1, "player is keyed once by player ID");
    const auto& player = engine.state().players().at("STEAM_0:1:10");
    require(player.connected, "new player is connected");
    require(player.last_known_name == "First Name", "new player name is recorded");

    const auto reconnected = engine.player_connected("STEAM_0:1:10", "Second Name");
    require(reconnected.ok() && reconnected.changed, "reconnect updates changed player data");
    require(engine.state().players().size() == 1, "reconnect does not duplicate player");
    require(engine.state().players().at("STEAM_0:1:10").last_known_name == "Second Name",
            "reconnect refreshes last known name");

    const auto eligibility = engine.capture_eligible_players();
    require(eligibility.ok() && eligibility.changed, "eligible pool is captured once");
    require(engine.state().eligible_pool_captured(), "eligible pool records capture state");
    require(engine.state().eligible_players().size() == 1,
            "connected tracked player is eligible at capture");
    require(engine.state().eligible_players().front() == "STEAM_0:1:10",
            "eligible pool contains stable player ID");

    require(engine.player_connected("STEAM_0:0:20", "Late Player").ok(),
            "late player remains trackable");
    require(engine.state().players().size() == 2, "late player is tracked");
    require(engine.state().eligible_players().size() == 1,
            "late player is not automatically draft eligible");
    const auto eligibility_again = engine.capture_eligible_players();
    require(eligibility_again.ok() && !eligibility_again.changed,
            "eligible pool cannot be silently recaptured");

    const auto add_late_player = engine.add_eligible_player("steam_0:0:20");
    require(add_late_player.ok() && add_late_player.changed,
            "admin operation can explicitly add a tracked late player");
    require(engine.state().eligible_players().size() == 2,
            "explicitly added player enters eligible pool");
    require(engine.state().eligible_players().front() == "STEAM_0:0:20",
            "eligible pool remains sorted after an add");
    const auto add_late_player_again = engine.add_eligible_player("STEAM_0:0:20");
    require(add_late_player_again.ok() && !add_late_player_again.changed,
            "adding an eligible player is idempotent");

    const auto remove_initial_player = engine.remove_eligible_player("steam_0:1:10");
    require(remove_initial_player.ok() && remove_initial_player.changed,
            "admin operation can explicitly remove an eligible player");
    require(engine.state().eligible_players().size() == 1, "removed player leaves eligible pool");
    const auto remove_initial_player_again = engine.remove_eligible_player("STEAM_0:1:10");
    require(remove_initial_player_again.ok() && !remove_initial_player_again.changed,
            "removing an ineligible player is idempotent");
    require(engine.add_eligible_player("STEAM_0:1:999").error == EligibilityError::UnknownPlayer,
            "unknown player cannot be made eligible");

    require(engine.select_captain(scrimmod::core::LogicalTeam::A, "STEAM_0:1:10").error ==
                CaptainSelectionError::IneligiblePlayer,
            "removed player cannot be selected as a captain");
    require(engine.add_eligible_player("STEAM_0:1:10").ok(),
            "removed player can be restored to eligibility explicitly");

    const auto captain_a = engine.select_captain(scrimmod::core::LogicalTeam::A, "steam_0:1:10");
    require(captain_a.ok() && captain_a.changed, "eligible player can be selected for Team A");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).captain_player_id == "STEAM_0:1:10",
            "Team A captain is stored by normalized player ID");
    const auto captain_a_again =
        engine.select_captain(scrimmod::core::LogicalTeam::A, "STEAM_0:1:10");
    require(captain_a_again.ok() && !captain_a_again.changed,
            "selecting the same captain is idempotent");
    require(engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:1:999").error ==
                CaptainSelectionError::UnknownPlayer,
            "unknown player cannot be selected as captain");
    require(engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:1:10").error ==
                CaptainSelectionError::DuplicateCaptain,
            "same player cannot captain both teams");

    const auto captain_b = engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:0:20");
    require(captain_b.ok() && captain_b.changed, "eligible player can be selected for Team B");
    const auto clear_captain_b = engine.clear_captain(scrimmod::core::LogicalTeam::B);
    require(clear_captain_b.ok() && clear_captain_b.changed,
            "pending captain selection can be cleared");
    require(!engine.state().team(scrimmod::core::LogicalTeam::B).captain_player_id.has_value(),
            "cleared captain is removed from pending state");
    require(engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:0:20").ok(),
            "cleared captain can be selected again");

    require(engine.remove_eligible_player("STEAM_0:0:20").changed,
            "selected captain can be removed from eligibility");
    require(!engine.state().team(scrimmod::core::LogicalTeam::B).captain_player_id.has_value(),
            "removing eligibility invalidates dependent captain selection");
    require(engine.add_eligible_player("STEAM_0:0:20").ok(),
            "removed captain can be restored to eligibility");
    require(engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:0:20").ok(),
            "restored eligible player can be selected again");

    const auto disconnected = engine.player_disconnected("steam_0:1:10");
    require(disconnected.ok() && disconnected.changed, "disconnect marks known player");
    require(!engine.state().players().at("STEAM_0:1:10").connected,
            "disconnect retains player as disconnected");
    require(engine.player_disconnected("STEAM_0:0:999").error == PlayerUpdateError::UnknownPlayer,
            "unknown disconnect is rejected explicitly");

    const auto enabled_again = engine.set_enabled(true);
    require(enabled_again.ok() && !enabled_again.changed,
            "enabling an enabled engine is idempotent");

    const auto skipped_phase = engine.transition_to(Phase::Draft);
    require(skipped_phase.error == TransitionError::IllegalTransition,
            "engine rejects skipped phases");
    require(engine.state().phase() == Phase::CaptainSelection,
            "illegal transition leaves phase unchanged");

    const auto clear_captain_b_again = engine.clear_captain(scrimmod::core::LogicalTeam::B);
    require(clear_captain_b_again.ok() && clear_captain_b_again.changed,
            "captain can be cleared before confirmation");
    const auto incomplete_captains = engine.transition_to(Phase::KnifeSetup);
    require(incomplete_captains.error == TransitionError::PrerequisiteNotMet,
            "knife setup requires two selected captains");
    require(engine.state().phase() == Phase::CaptainSelection,
            "failed captain confirmation leaves phase unchanged");
    require(engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:0:20").ok(),
            "second captain can be restored before confirmation");

    const auto knife_setup = engine.transition_to(Phase::KnifeSetup);
    require(knife_setup.ok() && knife_setup.changed, "captain confirmation enters knife setup");
    require(engine.state().phase() == Phase::KnifeSetup,
            "captain confirmation updates the central phase");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).roster.front() == "STEAM_0:1:10",
            "confirmed Team A captain begins Team A roster");
    require(engine.state().team(scrimmod::core::LogicalTeam::B).roster.front() == "STEAM_0:0:20",
            "confirmed Team B captain begins Team B roster");
    require(engine.state().players().at("STEAM_0:1:10").logical_team ==
                scrimmod::core::LogicalTeam::A,
            "confirmed Team A captain receives authoritative logical team");
    require(engine.state().players().at("STEAM_0:0:20").logical_team ==
                scrimmod::core::LogicalTeam::B,
            "confirmed Team B captain receives authoritative logical team");
    require(engine.add_eligible_player("STEAM_0:1:10").error == EligibilityError::WrongPhase,
            "eligible roster cannot be changed after captain selection");
    require(engine.clear_captain(scrimmod::core::LogicalTeam::A).error ==
                CaptainSelectionError::WrongPhase,
            "confirmed captains cannot be changed outside captain selection");

    const auto same_phase = engine.transition_to(Phase::KnifeSetup);
    require(same_phase.ok() && !same_phase.changed, "same-phase transition is idempotent");

    const auto disabled = engine.set_enabled(false);
    require(disabled.ok() && disabled.changed, "disabling active engine changes state");
    require(!engine.state().enabled(), "disable clears enabled state");
    require(engine.state().phase() == Phase::Disabled, "disable restores Disabled phase");
    require(engine.state().players().empty(), "disable clears tracked players");
    require(!engine.state().eligible_pool_captured(), "disable clears eligible capture state");
    require(engine.state().eligible_players().empty(), "disable clears eligible players");
    require(disabled.effects.size() == 1, "disable emits one reconciliation effect");
    require(disabled.effects.front().type == EffectType::ExecutePregameConfig,
            "disable emits pregame config effect");

    const auto disabled_again = engine.set_enabled(false);
    require(disabled_again.ok() && !disabled_again.changed,
            "disabling a disabled engine is state-idempotent");
    require(disabled_again.effects.size() == 1,
            "repeated disable still reconciles server configuration");

    MatchEngine bot_engine;
    require(bot_engine.set_enabled(true).ok(), "bot test engine enables");
    require(bot_engine.player_connected("bot:42", "Test Bot", PlayerType::Bot).ok(),
            "bot can be tracked with a synthetic player ID");
    require(bot_engine.player_connected("STEAM_0:1:77", "Human Captain").ok(),
            "human can be tracked alongside bot");
    require(bot_engine.capture_eligible_players().ok(), "mixed eligible pool is captured");
    require(bot_engine.state().players().at("BOT:42").type == PlayerType::Bot,
            "core preserves explicit bot player type");
    require(bot_engine.select_captain(scrimmod::core::LogicalTeam::A, "BOT:42").ok(),
            "eligible bot can be selected as captain");
    require(bot_engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:1:77").ok(),
            "eligible human can captain the other team");
    require(bot_engine.transition_to(Phase::KnifeSetup).ok(),
            "bot captain can be confirmed through normal transition rules");

    std::cout << "All ScrimMod engine tests passed\n";
    return EXIT_SUCCESS;
}
