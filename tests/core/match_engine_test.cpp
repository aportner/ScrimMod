#include "scrimmod/core/match_engine.hpp"

#include <algorithm>
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
    using scrimmod::core::DecisionError;
    using scrimmod::core::DraftError;
    using scrimmod::core::DraftType;
    using scrimmod::core::EffectType;
    using scrimmod::core::EligibilityError;
    using scrimmod::core::KnifeKillOutcome;
    using scrimmod::core::KnifeRewardChoice;
    using scrimmod::core::MatchConfigurationError;
    using scrimmod::core::MatchEngine;
    using scrimmod::core::Phase;
    using scrimmod::core::PlayerType;
    using scrimmod::core::PlayerUpdateError;
    using scrimmod::core::ReadyError;
    using scrimmod::core::RoundOutcome;
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
    const auto incomplete_captains = engine.confirm_captains(scrimmod::core::Side::Terrorist);
    require(incomplete_captains.error == TransitionError::PrerequisiteNotMet,
            "knife setup requires two selected captains");
    require(engine.state().phase() == Phase::CaptainSelection,
            "failed captain confirmation leaves phase unchanged");
    require(engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:0:20").ok(),
            "second captain can be restored before confirmation");
    const auto missing_side_choice = engine.transition_to(Phase::KnifeSetup);
    require(missing_side_choice.error == TransitionError::PrerequisiteNotMet,
            "generic transition cannot bypass randomized starting-side selection");
    require(engine.player_connected("STEAM_0:1:10", "Second Name").ok(),
            "disconnected captain can reconnect before knife setup");
    require(engine.player_connected("STEAM_0:0:30", "Spectator").ok(),
            "non-captain can be tracked before knife setup");
    require(engine.player_connected("STEAM_0:0:40", "Fourth Player").ok(),
            "additional draft candidate can be tracked");
    require(engine.player_connected("STEAM_0:0:50", "Fifth Player").ok(),
            "disconnection draft candidate can be tracked");
    require(engine.player_connected("STEAM_0:0:60", "Sixth Player").ok(),
            "final draft candidate can be tracked");
    for (const auto* player_id : {"STEAM_0:0:30", "STEAM_0:0:40", "STEAM_0:0:50", "STEAM_0:0:60"}) {
        require(engine.add_eligible_player(player_id).ok(),
                "admin can explicitly add a pre-knife draft candidate");
    }
    require(engine.set_draft_type(DraftType::AB).ok(), "draft type can be configured before Draft");

    const auto knife_setup = engine.confirm_captains(scrimmod::core::Side::CounterTerrorist);
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
    require(engine.state().team(scrimmod::core::LogicalTeam::A).current_side ==
                scrimmod::core::Side::CounterTerrorist,
            "confirmation stores Team A knife side");
    require(engine.state().team(scrimmod::core::LogicalTeam::B).current_side ==
                scrimmod::core::Side::Terrorist,
            "confirmation stores the opposite Team B knife side");
    require(knife_setup.effects.size() == 8,
            "knife setup emits placements plus both captain loadouts");
    const auto has_assignment =
        [&knife_setup](const std::string& player_id,
                       const scrimmod::core::PlayerDestination destination) {
            return std::any_of(
                knife_setup.effects.begin(), knife_setup.effects.end(), [&](const auto& effect) {
                    return effect.type == EffectType::AssignPlayerTeam &&
                           effect.player_id == player_id && effect.destination == destination;
                });
        };
    require(has_assignment("STEAM_0:1:10", scrimmod::core::PlayerDestination::CounterTerrorist),
            "Team A captain is assigned to randomized CT side");
    require(has_assignment("STEAM_0:0:20", scrimmod::core::PlayerDestination::Terrorist),
            "Team B captain is assigned to opposite T side");
    require(has_assignment("STEAM_0:0:30", scrimmod::core::PlayerDestination::Spectator),
            "non-captain is assigned to spectator");
    const auto knife_loadout_count = static_cast<int>(std::count_if(
        knife_setup.effects.begin(), knife_setup.effects.end(),
        [](const auto& effect) { return effect.type == EffectType::EnsureKnifeLoadout; }));
    require(knife_loadout_count == 2, "knife setup equips exactly both captains");
    const auto reconciliation = engine.reconciliation_effects();
    require(reconciliation.size() == knife_setup.effects.size(),
            "knife setup reconciliation reproduces every placement idempotently");
    require(engine.add_eligible_player("STEAM_0:1:10").error == EligibilityError::WrongPhase,
            "eligible roster cannot be changed after captain selection");
    require(engine.clear_captain(scrimmod::core::LogicalTeam::A).error ==
                CaptainSelectionError::WrongPhase,
            "confirmed captains cannot be changed outside captain selection");

    const auto same_phase = engine.transition_to(Phase::KnifeSetup);
    require(same_phase.ok() && !same_phase.changed, "same-phase transition is idempotent");
    require(!engine.can_player_choose_team("STEAM_0:1:10"),
            "captain team choices are locked during knife setup");
    require(!engine.can_player_choose_team("STEAM_0:0:30"),
            "non-captain team choices are locked during knife setup");
    require(engine.can_player_acquire_weapon("STEAM_0:1:10", true),
            "captain may acquire a knife during knife setup");
    require(!engine.can_player_acquire_weapon("STEAM_0:1:10", false),
            "captain may not acquire another weapon during knife setup");
    require(!engine.can_player_acquire_weapon("STEAM_0:0:30", true),
            "spectating non-captain may not acquire a weapon during knife setup");

    const auto knife_live = engine.transition_to(Phase::KnifeLive);
    require(knife_live.ok() && knife_live.changed, "knife setup can advance to knife live");
    require(engine.state().phase() == Phase::KnifeLive, "knife start updates central phase");
    require(std::count_if(
                knife_live.effects.begin(), knife_live.effects.end(),
                [](const auto& effect) { return effect.type == EffectType::RestartRound; }) == 1,
            "knife start emits exactly one round restart");
    require(!engine.can_player_acquire_weapon("STEAM_0:1:10", false),
            "knife-only acquisition remains active during knife live");
    const auto generated_restart_end = engine.knife_round_ended(true);
    require(generated_restart_end.ok() && !generated_restart_end.changed,
            "restart-generated round end is ignored");
    require(engine.state().phase() == Phase::KnifeLive,
            "generated restart does not alter knife phase");

    const auto unexpected_round_end = engine.knife_round_ended(false);
    require(unexpected_round_end.ok() && unexpected_round_end.changed,
            "round ending without a kill requires replay");
    require(engine.state().phase() == Phase::KnifeSetup,
            "unexpected round end returns to knife setup");
    require(engine.transition_to(Phase::KnifeLive).ok(),
            "admin can restart knife after unexpected round end");

    const auto ambiguous_kill = engine.player_killed("STEAM_0:1:10", "STEAM_0:1:10");
    require(ambiguous_kill.outcome == KnifeKillOutcome::ReplayRequired,
            "suicide requires an explicit knife replay");
    require(engine.state().phase() == Phase::KnifeSetup,
            "ambiguous knife result returns to knife setup");
    require(!engine.state().knife_winner_player_id().has_value(),
            "ambiguous knife result does not guess a winner");

    require(engine.transition_to(Phase::KnifeLive).ok(),
            "admin can restart knife round after ambiguity");
    const auto clean_kill = engine.player_killed("STEAM_0:1:10", "STEAM_0:0:20");
    require(clean_kill.outcome == KnifeKillOutcome::WinnerDecided,
            "opposing captain kill decides knife winner");
    require(engine.state().phase() == Phase::KnifeComplete,
            "clean captain kill enters knife complete");
    require(engine.state().knife_winner_player_id() == "STEAM_0:0:20",
            "knife winner is stored by player ID");
    require(engine.state().knife_loser_player_id() == "STEAM_0:1:10",
            "knife loser is stored by player ID");
    require(!engine.knife_round_ended(false).changed,
            "round end after recorded winner does not invalidate result");
    require(!engine.can_player_choose_team("STEAM_0:0:20"),
            "team choices remain locked after the knife result");
    require(engine.confirm_knife_reward().error == DecisionError::WrongPhase,
            "knife reward cannot be confirmed before entering its decision phase");
    require(engine.transition_to(Phase::Draft).error == TransitionError::IllegalTransition,
            "knife complete cannot skip the side-or-pick checkpoint");

    const auto pending_side_reward = engine.choose_knife_reward(KnifeRewardChoice::StartingSide);
    require(pending_side_reward.ok() && pending_side_reward.changed,
            "knife winner can make a pending reward choice");
    require(engine.state().phase() == Phase::SideOrPick,
            "first reward choice enters the side-or-pick phase centrally");
    require(engine.state().pending_knife_reward_choice() == KnifeRewardChoice::StartingSide,
            "pending starting-side reward is stored explicitly");
    require(engine.choose_starting_side(scrimmod::core::Side::CounterTerrorist).error ==
                DecisionError::RewardNotConfirmed,
            "starting side cannot be selected before reward confirmation");
    require(engine.confirm_starting_side().error == DecisionError::RewardNotConfirmed,
            "starting side cannot be confirmed before reward confirmation");
    require(engine.transition_to(Phase::Draft).error == TransitionError::PrerequisiteNotMet,
            "generic transition cannot bypass side-or-pick decisions");

    const auto pending_first_pick = engine.choose_knife_reward(KnifeRewardChoice::FirstPick);
    require(pending_first_pick.ok() && pending_first_pick.changed,
            "pending reward may be changed before confirmation");
    require(!engine.state().first_picker_player_id().has_value(),
            "changing a pending reward clears dependent first-picker state");
    const auto confirmed_reward = engine.confirm_knife_reward();
    require(confirmed_reward.ok() && confirmed_reward.changed,
            "pending knife reward requires explicit confirmation");
    require(engine.state().confirmed_knife_reward_choice() == KnifeRewardChoice::FirstPick,
            "confirmed first-pick reward is stored explicitly");
    require(engine.state().first_picker_player_id() == "STEAM_0:0:20",
            "knife winner receives first pick when that reward is chosen");
    require(engine.state().side_chooser_player_id() == "STEAM_0:1:10",
            "knife loser receives the remaining starting-side decision");
    const auto same_confirmed_reward = engine.choose_knife_reward(KnifeRewardChoice::FirstPick);
    require(same_confirmed_reward.ok() && !same_confirmed_reward.changed,
            "repeating the confirmed reward choice is idempotent");
    const auto confirmed_reward_again = engine.confirm_knife_reward();
    require(confirmed_reward_again.ok() && !confirmed_reward_again.changed,
            "reward confirmation is idempotent");

    const auto pending_starting_side =
        engine.choose_starting_side(scrimmod::core::Side::CounterTerrorist);
    require(pending_starting_side.ok() && pending_starting_side.changed,
            "side chooser can make a pending starting-side choice");
    require(engine.state().pending_starting_side() == scrimmod::core::Side::CounterTerrorist,
            "pending regulation side is stored for confirmation");
    const auto starting_side = engine.confirm_starting_side();
    require(starting_side.ok() && starting_side.changed,
            "starting-side confirmation completes the decision checkpoint");
    require(engine.state().phase() == Phase::Draft,
            "confirmed starting side advances centrally into Draft");
    require(engine.state().draft_type() == DraftType::AB,
            "configured draft type is retained on Draft entry");
    require(engine.state().current_draft_captain_player_id() == "STEAM_0:0:20",
            "knife reward determines the explicit first draft captain");
    require(engine.state().draft_picks_remaining_in_turn() == 1,
            "AB draft begins with one pick in the turn");
    require(engine.state().available_draft_players().size() == 4,
            "Draft entry explicitly captures eligible non-captains as available");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).starting_side ==
                scrimmod::core::Side::CounterTerrorist,
            "side chooser's logical team explicitly stores its regulation starting side");
    require(engine.state().team(scrimmod::core::LogicalTeam::B).starting_side ==
                scrimmod::core::Side::Terrorist,
            "other logical team explicitly stores the opposite regulation starting side");
    const auto has_draft_assignment = [&starting_side](
                                          const std::string& player_id,
                                          const scrimmod::core::PlayerDestination destination) {
        return std::any_of(
            starting_side.effects.begin(), starting_side.effects.end(), [&](const auto& effect) {
                return effect.type == EffectType::AssignPlayerTeam &&
                       effect.player_id == player_id && effect.destination == destination;
            });
    };
    require(
        has_draft_assignment("STEAM_0:1:10", scrimmod::core::PlayerDestination::CounterTerrorist),
        "Team A captain is moved to the confirmed regulation side");
    require(has_draft_assignment("STEAM_0:0:20", scrimmod::core::PlayerDestination::Terrorist),
            "Team B captain is moved to the opposite regulation side");
    require(has_draft_assignment("STEAM_0:0:30", scrimmod::core::PlayerDestination::Spectator),
            "undrafted player remains spectator on Draft entry");
    require(!engine.can_player_choose_team("STEAM_0:0:30"),
            "tracked players cannot bypass authoritative Draft placement");
    require(engine.reconciliation_effects().size() == starting_side.effects.size(),
            "Draft entry placement can be reconciled idempotently");

    require(engine.set_draft_type(DraftType::Snake).error == DraftError::WrongPhase,
            "draft type cannot change after Draft begins");
    require(engine.confirm_draft_player().error == DraftError::ChoiceNotSelected,
            "draft pick requires an explicit pending selection");
    require(engine.choose_draft_player("STEAM_0:1:999").error == DraftError::UnknownPlayer,
            "unknown player cannot be drafted");
    require(engine.choose_draft_player("STEAM_0:1:10").error == DraftError::IneligiblePlayer,
            "captain cannot be selected from the available draft pool");
    const auto pending_pick = engine.choose_draft_player("STEAM_0:0:30");
    require(pending_pick.ok() && pending_pick.changed,
            "available player can be selected as a pending pick");
    require(engine.state().pending_draft_player_id() == "STEAM_0:0:30",
            "pending draft pick is stored by stable player ID");
    require(!engine.choose_draft_player("STEAM_0:0:30").changed,
            "repeating a pending pick is idempotent");
    const auto first_pick = engine.confirm_draft_player();
    require(first_pick.ok() && first_pick.changed, "pending draft pick can be confirmed");
    require(engine.state().players().at("STEAM_0:0:30").logical_team ==
                scrimmod::core::LogicalTeam::B,
            "first pick joins the first captain's authoritative logical team");
    require(engine.state().current_draft_captain_player_id() == "STEAM_0:1:10",
            "AB draft alternates to the other captain after one pick");
    require(first_pick.effects.size() == 1 && first_pick.effects.front().destination ==
                                                  scrimmod::core::PlayerDestination::Terrorist,
            "connected drafted player is moved to the drafting team's current side");
    require(engine.choose_draft_player("STEAM_0:0:30").error == DraftError::AlreadyDrafted,
            "confirmed player cannot be drafted twice");

    require(engine.player_disconnected("STEAM_0:1:10").ok(),
            "current captain may disconnect during Draft");
    require(engine.choose_draft_player("STEAM_0:0:40").error == DraftError::CaptainDisconnected,
            "ordinary draft selection pauses while the current captain is disconnected");
    require(engine.choose_draft_player("STEAM_0:0:40", true).ok(),
            "explicit administrator override can recover a disconnected captain's turn");
    require(engine.confirm_draft_player().error == DraftError::CaptainDisconnected,
            "ordinary draft confirmation also pauses for a disconnected captain");
    require(engine.player_connected("STEAM_0:1:10", "Second Name").ok(),
            "current captain can reconnect before pick confirmation");

    require(engine.player_disconnected("STEAM_0:0:50").ok(),
            "unpicked candidate may disconnect during Draft");
    require(std::binary_search(engine.state().available_draft_players().begin(),
                               engine.state().available_draft_players().end(),
                               std::string("STEAM_0:0:50")),
            "disconnected unpicked player remains available");
    require(engine.confirm_draft_player().ok(), "Team A AB pick confirms");
    require(engine.state().current_draft_captain_player_id() == "STEAM_0:0:20",
            "AB draft returns to Team B after Team A's pick");
    require(engine.choose_draft_player("STEAM_0:0:50").ok(),
            "disconnected available player can still be selected");
    const auto disconnected_pick = engine.confirm_draft_player();
    require(disconnected_pick.ok() && disconnected_pick.effects.empty(),
            "disconnected drafted player retains assignment without a server move");
    require(engine.state().players().at("STEAM_0:0:50").logical_team ==
                scrimmod::core::LogicalTeam::B,
            "disconnected drafted player receives a persistent logical team");
    require(engine.choose_draft_player("STEAM_0:0:60").ok(),
            "last available player can be selected");
    const auto final_pick = engine.confirm_draft_player();
    require(final_pick.ok() && engine.state().phase() == Phase::Ready,
            "final confirmed pick automatically advances to Ready");
    require(engine.state().available_draft_players().empty(),
            "completed draft has no available players");
    require(engine.state().drafted_players().size() == 4,
            "completed draft explicitly records every drafted player");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).roster.size() == 3 &&
                engine.state().team(scrimmod::core::LogicalTeam::B).roster.size() == 3,
            "AB draft produces the expected balanced logical rosters");
    require(!engine.can_player_choose_team("STEAM_0:0:60"),
            "Ready keeps authoritative team placement locked");
    require(engine.set_regulation_rounds_per_half(0).error == MatchConfigurationError::InvalidValue,
            "regulation half length must be positive");
    require(engine.set_regulation_rounds_per_half(101).error ==
                MatchConfigurationError::InvalidValue,
            "regulation half length has a guarded upper bound");
    require(engine.set_regulation_rounds_per_half(3).ok(),
            "regulation half length can be configured before LO3");
    require(engine.state().regulation_rounds_per_half() == 3,
            "configured regulation half length is explicit state");

    require(engine.live_on_three_restart_completed().error == TransitionError::IllegalTransition,
            "restart completion cannot advance state outside LO3");
    require(engine.transition_to(Phase::RegulationFirstHalf).error ==
                TransitionError::IllegalTransition,
            "Ready cannot bypass the explicit LO3 phase");
    require(engine.transition_to(Phase::LiveOnThree).error == TransitionError::PrerequisiteNotMet,
            "generic transition cannot bypass both captain ready confirmations");
    require(engine.set_captain_ready("STEAM_0:0:60", true).error == ReadyError::NotCaptain,
            "non-captain cannot ready a logical team");
    const auto captain_a_ready = engine.set_captain_ready("STEAM_0:1:10", true);
    require(captain_a_ready.ok() && captain_a_ready.changed,
            "connected Team A captain can become ready");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).captain_ready &&
                !engine.state().team(scrimmod::core::LogicalTeam::B).captain_ready,
            "captain ready states are tracked separately");
    require(!engine.set_captain_ready("STEAM_0:1:10", true).changed,
            "repeating a ready state is idempotent");
    require(engine.set_captain_ready("STEAM_0:1:10", false).changed,
            "captain can become unready before LO3");
    require(engine.set_captain_ready("STEAM_0:1:10", true).ok(), "Team A captain can ready again");
    require(engine.player_disconnected("STEAM_0:0:20").ok(),
            "Team B captain can disconnect at Ready");
    require(engine.set_captain_ready("STEAM_0:0:20", true).error == ReadyError::CaptainDisconnected,
            "ordinary ready confirmation rejects a disconnected captain");
    require(engine.player_connected("STEAM_0:0:20", "Late Player").ok(),
            "Team B captain can reconnect before readying");
    const auto captain_b_ready = engine.set_captain_ready("STEAM_0:0:20", true);
    require(captain_b_ready.ok() && captain_b_ready.changed, "second captain ready starts LO3");
    require(engine.state().phase() == Phase::LiveOnThree,
            "both ready confirmations enter explicit LO3 phase");
    require(engine.transition_to(Phase::RegulationFirstHalf).error ==
                TransitionError::PrerequisiteNotMet,
            "LO3 cannot enter regulation before three completed restarts");
    require(captain_b_ready.effects.size() == 2 &&
                captain_b_ready.effects.front().type == EffectType::ExecuteLiveConfig &&
                captain_b_ready.effects.back().type == EffectType::RestartRound &&
                captain_b_ready.effects.back().value == 1,
            "LO3 entry executes cal.cfg and requests the first one-second restart");
    const auto admin_lo3_rewind = engine.set_captain_ready("STEAM_0:1:10", false, true);
    require(admin_lo3_rewind.ok() && engine.state().phase() == Phase::Ready &&
                !admin_lo3_rewind.effects.empty() &&
                admin_lo3_rewind.effects.front().type == EffectType::ExecutePregameConfig,
            "administrator unready explicitly rewinds LO3 and restores pregame config");
    require(engine.set_captain_ready("STEAM_0:1:10", true).ok() &&
                engine.set_captain_ready("STEAM_0:0:20", true).ok(),
            "captains can restart LO3 after an administrator rewind");
    const auto first_lo3_restart = engine.live_on_three_restart_completed();
    require(first_lo3_restart.ok() && first_lo3_restart.effects.size() == 1 &&
                first_lo3_restart.effects.front().value == 1,
            "first completed restart requests the second one-second restart");
    require(engine.state().live_on_three_restarts_completed() == 1,
            "LO3 explicitly tracks completed restart count");
    const auto second_lo3_restart = engine.live_on_three_restart_completed();
    require(second_lo3_restart.ok() && second_lo3_restart.effects.size() == 1 &&
                second_lo3_restart.effects.front().value == 3,
            "second completed restart requests the three-second final restart");
    require(engine.player_disconnected("STEAM_0:1:10").ok(), "captain can disconnect during LO3");
    require(engine.state().phase() == Phase::Ready,
            "captain disconnect rewinds incomplete LO3 to Ready");
    require(!engine.state().team(scrimmod::core::LogicalTeam::A).captain_ready &&
                !engine.state().team(scrimmod::core::LogicalTeam::B).captain_ready,
            "LO3 rewind clears both captain ready states");
    require(engine.state().live_on_three_restarts_completed() == 0,
            "LO3 rewind clears restart progress");
    require(engine.player_connected("STEAM_0:1:10", "Second Name").ok(),
            "disconnected LO3 captain can reconnect");
    require(engine.set_captain_ready("STEAM_0:1:10", true).ok() &&
                engine.set_captain_ready("STEAM_0:0:20", true).ok(),
            "both captains can restart LO3 after rewind");
    require(engine.live_on_three_restart_completed().ok() &&
                engine.live_on_three_restart_completed().ok(),
            "restarted LO3 completes its first two restarts");
    const auto third_lo3_restart = engine.live_on_three_restart_completed();
    require(third_lo3_restart.ok() && engine.state().phase() == Phase::RegulationFirstHalf,
            "third completed restart makes regulation first half live");
    require(!engine.state().team(scrimmod::core::LogicalTeam::A).captain_ready &&
                !engine.state().team(scrimmod::core::LogicalTeam::B).captain_ready,
            "entering live regulation clears checkpoint-only ready state");
    require(!engine.can_player_choose_team("STEAM_0:0:60"),
            "live regulation retains authoritative team lock");
    require(engine.set_regulation_rounds_per_half(12).error == MatchConfigurationError::WrongPhase,
            "regulation length is frozen after LO3 begins");
    require(engine.transition_to(Phase::Halftime).error == TransitionError::PrerequisiteNotMet,
            "first half cannot end before its configured round count");
    const auto ambiguous_live_round = engine.regulation_round_ended(std::nullopt);
    require(ambiguous_live_round.outcome == RoundOutcome::Ambiguous,
            "ambiguous live round result is not guessed");
    require(engine.state().period_rounds_completed() == 0,
            "ambiguous live round does not advance completed-round state");
    const auto ct_round = engine.regulation_round_ended(scrimmod::core::Side::CounterTerrorist);
    require(ct_round.outcome == RoundOutcome::Counted &&
                ct_round.winning_team == scrimmod::core::LogicalTeam::A,
            "physical CT win maps to logical Team A on its current side");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).total_score == 1 &&
                engine.state().team(scrimmod::core::LogicalTeam::A).period_score == 1,
            "counted round updates both total and current-period Team A score");
    const auto t_round = engine.regulation_round_ended(scrimmod::core::Side::Terrorist);
    require(t_round.outcome == RoundOutcome::Counted &&
                t_round.winning_team == scrimmod::core::LogicalTeam::B,
            "physical T win maps to logical Team B on its current side");
    require(engine.state().period_rounds_completed() == 2,
            "valid gameplay results explicitly advance completed rounds");
    const auto recovered_round =
        engine.force_regulation_round_winner(scrimmod::core::LogicalTeam::A);
    require(recovered_round.outcome == RoundOutcome::HalfComplete,
            "explicit admin recovery can count an unambiguous logical winner");
    require(engine.state().phase() == Phase::LiveOnThree,
            "configured final first-half round switches sides and starts halftime LO3");
    require(engine.state().live_on_three_target_phase() == Phase::RegulationSecondHalf,
            "halftime LO3 explicitly targets the regulation second half");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).current_side ==
                    scrimmod::core::Side::Terrorist &&
                engine.state().team(scrimmod::core::LogicalTeam::B).current_side ==
                    scrimmod::core::Side::CounterTerrorist,
            "halftime derives and stores sides opposite the regulation starting sides");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).starting_side ==
                scrimmod::core::Side::CounterTerrorist,
            "halftime does not overwrite the explicit regulation starting side");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).total_score == 2 &&
                engine.state().team(scrimmod::core::LogicalTeam::B).total_score == 1,
            "logical total score remains authoritative at halftime");
    const auto halftime_assignment =
        [&recovered_round](const std::string& player_id,
                           const scrimmod::core::PlayerDestination destination) {
            return std::any_of(recovered_round.effects.begin(), recovered_round.effects.end(),
                               [&](const auto& effect) {
                                   return effect.type == EffectType::AssignPlayerTeam &&
                                          effect.player_id == player_id &&
                                          effect.destination == destination;
                               });
        };
    require(halftime_assignment("STEAM_0:1:10", scrimmod::core::PlayerDestination::Terrorist) &&
                halftime_assignment("STEAM_0:0:20",
                                    scrimmod::core::PlayerDestination::CounterTerrorist),
            "halftime effects move connected captains to their second-half sides");
    require(std::count_if(recovered_round.effects.begin(), recovered_round.effects.end(),
                          [](const auto& effect) {
                              return effect.type == EffectType::ExecuteLiveConfig;
                          }) == 1,
            "halftime automatically queues a fresh live config");
    require(engine.regulation_round_ended(scrimmod::core::Side::Terrorist).outcome ==
                RoundOutcome::Ignored,
            "round result cannot count during halftime LO3");
    require(!engine.can_player_choose_team("STEAM_0:0:60"),
            "halftime retains authoritative team lock");

    require(engine.player_disconnected("STEAM_0:1:10").ok(),
            "captain can disconnect during halftime LO3");
    require(engine.state().phase() == Phase::Halftime,
            "halftime LO3 captain disconnect pauses at Halftime");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).current_side ==
                scrimmod::core::Side::Terrorist,
            "idempotent Halftime re-entry does not swap sides a second time");
    require(engine.start_halftime_live_on_three().error == TransitionError::PrerequisiteNotMet,
            "ordinary halftime restart waits for both captains to reconnect");
    require(engine.transition_to(Phase::LiveOnThree).error == TransitionError::PrerequisiteNotMet,
            "generic transition cannot bypass the explicit halftime LO3 target");
    require(engine.player_connected("STEAM_0:1:10", "Second Name").ok(),
            "halftime captain can reconnect");
    const auto restarted_halftime = engine.start_halftime_live_on_three();
    require(restarted_halftime.ok() && engine.state().phase() == Phase::LiveOnThree,
            "halftime LO3 can restart after captain reconnects");
    require(engine.live_on_three_restart_completed().ok() &&
                engine.live_on_three_restart_completed().ok(),
            "halftime LO3 completes its first two restarts");
    const auto halftime_third_restart = engine.live_on_three_restart_completed();
    require(halftime_third_restart.ok() && engine.state().phase() == Phase::RegulationSecondHalf,
            "third halftime restart enters regulation second half");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).total_score == 2 &&
                engine.state().team(scrimmod::core::LogicalTeam::B).total_score == 1,
            "second-half entry preserves total logical scores");
    require(engine.state().team(scrimmod::core::LogicalTeam::A).period_score == 0 &&
                engine.state().team(scrimmod::core::LogicalTeam::B).period_score == 0 &&
                engine.state().period_rounds_completed() == 0,
            "second-half entry resets only the current-period score and round count");

    const auto disabled = engine.set_enabled(false);
    require(disabled.ok() && disabled.changed, "disabling active engine changes state");
    require(!engine.state().enabled(), "disable clears enabled state");
    require(engine.state().phase() == Phase::Disabled, "disable restores Disabled phase");
    require(engine.state().players().empty(), "disable clears tracked players");
    require(!engine.state().eligible_pool_captured(), "disable clears eligible capture state");
    require(engine.state().eligible_players().empty(), "disable clears eligible players");
    require(!engine.state().first_picker_player_id().has_value(),
            "disable clears first-picker decision state");
    require(!engine.state().side_chooser_player_id().has_value(),
            "disable clears side-chooser decision state");
    require(!engine.state().team(scrimmod::core::LogicalTeam::A).starting_side.has_value(),
            "disable clears regulation starting sides");
    require(engine.state().available_draft_players().empty(),
            "disable clears available draft state");
    require(engine.state().drafted_players().empty(), "disable clears drafted-player history");
    require(engine.state().regulation_rounds_per_half() == 12,
            "disable restores default regulation half length");
    require(engine.state().period_rounds_completed() == 0, "disable clears completed-round state");
    require(engine.can_player_choose_team("STEAM_0:1:10"),
            "disable removes team-choice restrictions");
    require(engine.can_player_acquire_weapon("STEAM_0:1:10", false),
            "disable removes weapon restrictions");
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
    require(bot_engine.player_connected("BOT:43", "Draft Bot One", PlayerType::Bot).ok(),
            "first bot draft candidate can be tracked");
    require(bot_engine.player_connected("BOT:44", "Draft Bot Two", PlayerType::Bot).ok(),
            "second bot draft candidate can be tracked");
    require(bot_engine.player_connected("STEAM_0:1:88", "Draft Human").ok(),
            "human draft candidate can be tracked");
    require(bot_engine.capture_eligible_players().ok(), "mixed eligible pool is captured");
    require(bot_engine.state().players().at("BOT:42").type == PlayerType::Bot,
            "core preserves explicit bot player type");
    require(bot_engine.select_captain(scrimmod::core::LogicalTeam::A, "BOT:42").ok(),
            "eligible bot can be selected as captain");
    require(bot_engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:1:77").ok(),
            "eligible human can captain the other team");
    require(bot_engine.confirm_captains(scrimmod::core::Side::Terrorist).ok(),
            "bot captain can be confirmed through normal transition rules");
    require(bot_engine.transition_to(Phase::KnifeLive).ok(), "bot knife round can start");
    const auto bot_disconnect = bot_engine.player_disconnected("BOT:42");
    require(bot_disconnect.ok() && bot_disconnect.changed,
            "disconnecting bot captain updates player state");
    require(bot_engine.state().phase() == Phase::KnifeSetup,
            "captain disconnect pauses live knife round at setup");
    const auto forced_bot_result = bot_engine.force_knife_winner("STEAM_0:1:77");
    require(forced_bot_result.outcome == KnifeKillOutcome::WinnerDecided,
            "admin can resolve disconnected bot captain from knife setup");
    require(bot_engine.state().phase() == Phase::KnifeComplete,
            "forced knife winner enters knife complete through central transition");
    require(bot_engine.choose_knife_reward(KnifeRewardChoice::StartingSide).ok(),
            "knife winner can choose the starting-side reward");
    require(bot_engine.confirm_knife_reward().ok(), "starting-side reward can be confirmed");
    require(bot_engine.state().side_chooser_player_id() == "STEAM_0:1:77",
            "knife winner becomes side chooser for the starting-side reward");
    require(bot_engine.state().first_picker_player_id() == "BOT:42",
            "knife loser receives first pick for the starting-side reward");
    require(bot_engine.player_connected("BOT:42", "Test Bot", PlayerType::Bot).ok(),
            "bot captain can reconnect before side confirmation");
    require(bot_engine.choose_starting_side(scrimmod::core::Side::Terrorist).ok(),
            "winning captain can choose a starting side");
    require(bot_engine.confirm_starting_side().ok(),
            "starting-side confirmation enters bot-capable Draft");
    require(bot_engine.state().draft_type() == DraftType::Snake, "Snake is the default draft mode");
    require(bot_engine.state().current_draft_captain_player_id() == "BOT:42" &&
                bot_engine.state().draft_picks_remaining_in_turn() == 1,
            "Snake starts with one pick for the first captain");
    require(bot_engine.choose_draft_player("BOT:43").ok(),
            "first captain can select the opening Snake pick");
    require(bot_engine.confirm_draft_player().ok(), "opening Snake pick confirms");
    require(bot_engine.state().current_draft_captain_player_id() == "STEAM_0:1:77" &&
                bot_engine.state().draft_picks_remaining_in_turn() == 2,
            "Snake switches to a two-pick turn for the other captain");
    require(bot_engine.choose_draft_player("BOT:44").ok(),
            "second captain can select the first pick of a pair");
    require(bot_engine.confirm_draft_player().ok(), "first paired Snake pick confirms");
    require(bot_engine.state().current_draft_captain_player_id() == "STEAM_0:1:77" &&
                bot_engine.state().draft_picks_remaining_in_turn() == 1,
            "Snake retains the same captain for the second paired pick");
    require(bot_engine.choose_draft_player("STEAM_0:1:88").ok(),
            "second captain can select the final paired pick");
    require(bot_engine.confirm_draft_player().ok() && bot_engine.state().phase() == Phase::Ready,
            "final Snake pick completes Draft and enters Ready");

    MatchEngine empty_draft_engine;
    require(empty_draft_engine.set_enabled(true).ok(), "empty-draft engine enables");
    require(empty_draft_engine.player_connected("STEAM_0:1:91", "One").ok() &&
                empty_draft_engine.player_connected("STEAM_0:1:92", "Two").ok(),
            "empty-draft captains can be tracked");
    require(empty_draft_engine.capture_eligible_players().ok(),
            "empty-draft eligible pool is captured");
    require(
        empty_draft_engine.select_captain(scrimmod::core::LogicalTeam::A, "STEAM_0:1:91").ok() &&
            empty_draft_engine.select_captain(scrimmod::core::LogicalTeam::B, "STEAM_0:1:92").ok(),
        "empty-draft captains are selected");
    require(empty_draft_engine.confirm_captains(scrimmod::core::Side::Terrorist).ok(),
            "empty-draft captains confirm");
    require(empty_draft_engine.force_knife_winner("STEAM_0:1:91").outcome ==
                KnifeKillOutcome::WinnerDecided,
            "empty-draft knife winner can be resolved");
    require(
        empty_draft_engine.choose_knife_reward(KnifeRewardChoice::StartingSide).ok() &&
            empty_draft_engine.confirm_knife_reward().ok() &&
            empty_draft_engine.choose_starting_side(scrimmod::core::Side::CounterTerrorist).ok(),
        "empty-draft decisions can be completed");
    require(empty_draft_engine.confirm_starting_side().ok() &&
                empty_draft_engine.state().phase() == Phase::Ready,
            "a captain-only roster skips empty Draft work and enters Ready");

    std::cout << "All ScrimMod engine tests passed\n";
    return EXIT_SUCCESS;
}
