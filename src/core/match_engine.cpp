#include "scrimmod/core/match_engine.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace scrimmod::core {

const MatchState& MatchEngine::state() const noexcept { return state_; }

TransitionResult MatchEngine::set_enabled(const bool enabled) {
    TransitionResult result{};
    if (enabled) {
        if (!state_.enabled_) {
            state_.reset();
            state_.enabled_ = true;
            state_.phase_ = Phase::CaptainSelection;
            result.changed = true;
        }
        return result;
    }

    result.changed = state_.enabled_ || state_.phase_ != Phase::Disabled;
    state_.reset();
    result.effects.push_back({EffectType::ExecutePregameConfig, {}});
    return result;
}

TransitionResult MatchEngine::transition_to(const Phase target) {
    TransitionResult result{};
    if (!state_.enabled_) {
        result.error = TransitionError::ScrimDisabled;
        return result;
    }
    if (target == state_.phase_) {
        return result;
    }
    if (!is_legal_transition(state_.phase_, target)) {
        result.error = TransitionError::IllegalTransition;
        return result;
    }
    if (target == Phase::KnifeComplete && (!state_.knife_winner_player_id_.has_value() ||
                                           !state_.knife_loser_player_id_.has_value())) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::CaptainSelection && target == Phase::KnifeSetup &&
        (!state_.team_a_.captain_player_id.has_value() ||
         !state_.team_b_.captain_player_id.has_value() ||
         !state_.team_a_.current_side.has_value() || !state_.team_b_.current_side.has_value())) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::SideOrPick && target == Phase::Draft &&
        (!state_.first_picker_player_id_.has_value() || !state_.team_a_.starting_side.has_value() ||
         !state_.team_b_.starting_side.has_value())) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::Draft && target == Phase::Ready &&
        (!state_.available_draft_players_.empty() || state_.pending_draft_player_id_.has_value())) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::Ready && target == Phase::LiveOnThree &&
        (!state_.team_a_.captain_ready || !state_.team_b_.captain_ready)) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (target == Phase::LiveOnThree && !state_.live_on_three_target_phase_.has_value()) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::LiveOnThree &&
        (target == Phase::RegulationFirstHalf || target == Phase::RegulationSecondHalf) &&
        (state_.live_on_three_restarts_completed_ < 3 ||
         state_.live_on_three_target_phase_ != target)) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::RegulationFirstHalf && target == Phase::Halftime &&
        state_.period_rounds_completed_ < state_.regulation_rounds_per_half_) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::RegulationSecondHalf && target == Phase::MatchComplete &&
        state_.team_a_.total_score < state_.regulation_rounds_per_half_ + 1 &&
        state_.team_b_.total_score < state_.regulation_rounds_per_half_ + 1) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    if (state_.phase_ == Phase::RegulationSecondHalf && target == Phase::OvertimeSetup &&
        (state_.period_rounds_completed_ < state_.regulation_rounds_per_half_ ||
         state_.team_a_.total_score != state_.team_b_.total_score)) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }

    const Phase previous_phase = state_.phase_;
    state_.phase_ = target;
    if (target == Phase::KnifeSetup) {
        commit_captains();
        append_knife_reconciliation_effects(result);
    } else if (target == Phase::KnifeLive) {
        append_knife_reconciliation_effects(result);
        result.effects.push_back({EffectType::RestartRound, {}});
    } else if (target == Phase::Draft) {
        initialize_draft();
    } else if (target == Phase::Ready) {
        state_.current_draft_captain_player_id_.reset();
        state_.draft_picks_remaining_in_turn_ = 0;
        state_.team_a_.captain_ready = false;
        state_.team_b_.captain_ready = false;
        state_.live_on_three_restarts_completed_ = 0;
        state_.live_on_three_target_phase_.reset();
        if (previous_phase == Phase::LiveOnThree) {
            result.effects.push_back({EffectType::ExecutePregameConfig, {}});
            append_draft_reconciliation_effects(result.effects);
        }
    } else if (target == Phase::LiveOnThree) {
        state_.live_on_three_restarts_completed_ = 0;
        result.effects.push_back({EffectType::ExecuteLiveConfig, {}});
        result.effects.push_back({EffectType::RestartRound, {}, PlayerDestination::Spectator, 1});
    } else if (target == Phase::Halftime) {
        state_.team_a_.current_side = *state_.team_a_.starting_side == Side::Terrorist
                                          ? Side::CounterTerrorist
                                          : Side::Terrorist;
        state_.team_b_.current_side = *state_.team_b_.starting_side == Side::Terrorist
                                          ? Side::CounterTerrorist
                                          : Side::Terrorist;
        if (previous_phase == Phase::LiveOnThree) {
            result.effects.push_back({EffectType::ExecutePregameConfig, {}});
        }
        append_draft_reconciliation_effects(result.effects);
        state_.live_on_three_restarts_completed_ = 0;
        state_.live_on_three_target_phase_.reset();
    } else if (target == Phase::RegulationFirstHalf || target == Phase::RegulationSecondHalf) {
        state_.team_a_.period_score = 0;
        state_.team_b_.period_score = 0;
        state_.team_a_.captain_ready = false;
        state_.team_b_.captain_ready = false;
        state_.period_rounds_completed_ = 0;
        state_.live_on_three_target_phase_.reset();
    } else if (target == Phase::OvertimeSetup) {
        result.effects.push_back({EffectType::ExecutePregameConfig, {}});
        append_draft_reconciliation_effects(result.effects);
    }
    result.changed = true;
    return result;
}

PlayerUpdateResult MatchEngine::player_connected(std::string player_id, std::string name,
                                                 const PlayerType type) {
    PlayerUpdateResult result{};
    if (!state_.enabled_) {
        result.error = PlayerUpdateError::ScrimDisabled;
        return result;
    }

    player_id = normalize_player_id(std::move(player_id));
    if (player_id.empty()) {
        result.error = PlayerUpdateError::InvalidPlayerId;
        return result;
    }

    auto [player_it, inserted] = state_.players_.try_emplace(player_id);
    if (inserted) {
        player_it->second.player_id = player_id;
        player_it->second.last_known_name = std::move(name);
        player_it->second.type = type;
        player_it->second.connected = true;
        result.changed = true;
        return result;
    }

    Player& player = player_it->second;
    if (player.type != type) {
        player.type = type;
        result.changed = true;
    }
    if (!name.empty() && player.last_known_name != name) {
        player.last_known_name = std::move(name);
        result.changed = true;
    }
    if (!player.connected) {
        player.connected = true;
        result.changed = true;
    }
    return result;
}

PlayerUpdateResult MatchEngine::player_disconnected(std::string player_id) {
    PlayerUpdateResult result{};
    if (!state_.enabled_) {
        result.error = PlayerUpdateError::ScrimDisabled;
        return result;
    }

    player_id = normalize_player_id(std::move(player_id));
    if (player_id.empty()) {
        result.error = PlayerUpdateError::InvalidPlayerId;
        return result;
    }

    const auto player_it = state_.players_.find(player_id);
    if (player_it == state_.players_.end()) {
        result.error = PlayerUpdateError::UnknownPlayer;
        return result;
    }
    if (player_it->second.connected) {
        player_it->second.connected = false;
        result.changed = true;
    }
    if (state_.phase_ == Phase::KnifeLive && (state_.team_a_.captain_player_id == player_id ||
                                              state_.team_b_.captain_player_id == player_id)) {
        const auto replay = require_knife_replay();
        result.effects = replay.effects;
    } else if (state_.phase_ == Phase::Ready) {
        if (state_.team_a_.captain_player_id == player_id) {
            state_.team_a_.captain_ready = false;
        } else if (state_.team_b_.captain_player_id == player_id) {
            state_.team_b_.captain_ready = false;
        }
    } else if (state_.phase_ == Phase::LiveOnThree &&
               (state_.team_a_.captain_player_id == player_id ||
                state_.team_b_.captain_player_id == player_id)) {
        const Phase recovery_phase =
            state_.live_on_three_target_phase_ == Phase::RegulationSecondHalf ? Phase::Halftime
                                                                              : Phase::Ready;
        const auto recovered = transition_to(recovery_phase);
        result.effects = recovered.effects;
    }
    return result;
}

EligibilityResult MatchEngine::capture_eligible_players() {
    EligibilityResult result{};
    if (!state_.enabled_) {
        result.error = EligibilityError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = EligibilityError::WrongPhase;
        return result;
    }
    if (state_.eligible_pool_captured_) {
        return result;
    }

    state_.eligible_players_.reserve(state_.players_.size());
    for (const auto& [player_id, player] : state_.players_) {
        if (player.connected) {
            state_.eligible_players_.push_back(player_id);
        }
    }
    std::sort(state_.eligible_players_.begin(), state_.eligible_players_.end());
    state_.eligible_pool_captured_ = true;
    result.changed = true;
    return result;
}

EligibilityResult MatchEngine::add_eligible_player(std::string player_id) {
    EligibilityResult result{};
    if (!state_.enabled_) {
        result.error = EligibilityError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = EligibilityError::WrongPhase;
        return result;
    }
    if (!state_.eligible_pool_captured_) {
        result.error = EligibilityError::PoolNotCaptured;
        return result;
    }

    player_id = normalize_player_id(std::move(player_id));
    if (player_id.empty()) {
        result.error = EligibilityError::InvalidPlayerId;
        return result;
    }
    if (state_.players_.find(player_id) == state_.players_.end()) {
        result.error = EligibilityError::UnknownPlayer;
        return result;
    }

    const auto position = std::lower_bound(state_.eligible_players_.begin(),
                                           state_.eligible_players_.end(), player_id);
    if (position == state_.eligible_players_.end() || *position != player_id) {
        state_.eligible_players_.insert(position, std::move(player_id));
        result.changed = true;
    }
    return result;
}

EligibilityResult MatchEngine::remove_eligible_player(std::string player_id) {
    EligibilityResult result{};
    if (!state_.enabled_) {
        result.error = EligibilityError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = EligibilityError::WrongPhase;
        return result;
    }
    if (!state_.eligible_pool_captured_) {
        result.error = EligibilityError::PoolNotCaptured;
        return result;
    }

    player_id = normalize_player_id(std::move(player_id));
    if (player_id.empty()) {
        result.error = EligibilityError::InvalidPlayerId;
        return result;
    }
    if (state_.players_.find(player_id) == state_.players_.end()) {
        result.error = EligibilityError::UnknownPlayer;
        return result;
    }

    const auto position = std::lower_bound(state_.eligible_players_.begin(),
                                           state_.eligible_players_.end(), player_id);
    if (position != state_.eligible_players_.end() && *position == player_id) {
        state_.eligible_players_.erase(position);
        if (state_.team_a_.captain_player_id == player_id) {
            state_.team_a_.captain_player_id.reset();
        }
        if (state_.team_b_.captain_player_id == player_id) {
            state_.team_b_.captain_player_id.reset();
        }
        result.changed = true;
    }
    return result;
}

CaptainSelectionResult MatchEngine::select_captain(const LogicalTeam team, std::string player_id) {
    CaptainSelectionResult result{};
    if (!state_.enabled_) {
        result.error = CaptainSelectionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = CaptainSelectionError::WrongPhase;
        return result;
    }
    if (!state_.eligible_pool_captured_) {
        result.error = CaptainSelectionError::PoolNotCaptured;
        return result;
    }

    player_id = normalize_player_id(std::move(player_id));
    if (player_id.empty()) {
        result.error = CaptainSelectionError::InvalidPlayerId;
        return result;
    }
    if (state_.players_.find(player_id) == state_.players_.end()) {
        result.error = CaptainSelectionError::UnknownPlayer;
        return result;
    }
    if (!std::binary_search(state_.eligible_players_.begin(), state_.eligible_players_.end(),
                            player_id)) {
        result.error = CaptainSelectionError::IneligiblePlayer;
        return result;
    }

    const LogicalTeam other_team = team == LogicalTeam::A ? LogicalTeam::B : LogicalTeam::A;
    if (state_.team(other_team).captain_player_id == player_id) {
        result.error = CaptainSelectionError::DuplicateCaptain;
        return result;
    }

    auto& captain = mutable_team(team).captain_player_id;
    if (captain != player_id) {
        captain = std::move(player_id);
        result.changed = true;
    }
    return result;
}

CaptainSelectionResult MatchEngine::clear_captain(const LogicalTeam team) {
    CaptainSelectionResult result{};
    if (!state_.enabled_) {
        result.error = CaptainSelectionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = CaptainSelectionError::WrongPhase;
        return result;
    }

    auto& captain = mutable_team(team).captain_player_id;
    if (captain.has_value()) {
        captain.reset();
        result.changed = true;
    }
    return result;
}

TransitionResult MatchEngine::confirm_captains(const Side team_a_knife_side) {
    TransitionResult rejected{};
    if (!state_.enabled_) {
        rejected.error = TransitionError::ScrimDisabled;
        return rejected;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        rejected.error = TransitionError::IllegalTransition;
        return rejected;
    }
    if (!state_.team_a_.captain_player_id.has_value() ||
        !state_.team_b_.captain_player_id.has_value()) {
        rejected.error = TransitionError::PrerequisiteNotMet;
        return rejected;
    }

    state_.team_a_.current_side = team_a_knife_side;
    state_.team_b_.current_side =
        team_a_knife_side == Side::Terrorist ? Side::CounterTerrorist : Side::Terrorist;
    auto result = transition_to(Phase::KnifeSetup);
    if (!result.ok()) {
        state_.team_a_.current_side.reset();
        state_.team_b_.current_side.reset();
        return result;
    }
    return result;
}

std::vector<Effect> MatchEngine::reconciliation_effects() const {
    TransitionResult result{};
    if (is_knife_phase()) {
        append_knife_reconciliation_effects(result);
    } else if (state_.phase_ == Phase::Draft || state_.phase_ == Phase::Ready ||
               state_.phase_ == Phase::LiveOnThree || state_.phase_ == Phase::RegulationFirstHalf ||
               state_.phase_ == Phase::Halftime || state_.phase_ == Phase::RegulationSecondHalf ||
               state_.phase_ == Phase::OvertimeSetup) {
        append_draft_reconciliation_effects(result.effects);
    }
    return result.effects;
}

bool MatchEngine::can_player_choose_team(std::string player_id) const {
    player_id = normalize_player_id(std::move(player_id));
    return !are_team_changes_locked() || state_.players_.find(player_id) == state_.players_.end();
}

bool MatchEngine::can_player_acquire_weapon(std::string player_id, const bool is_knife) const {
    player_id = normalize_player_id(std::move(player_id));
    if (!is_knife_phase()) {
        return true;
    }

    const auto player = state_.players_.find(player_id);
    if (player == state_.players_.end()) {
        return false;
    }
    const bool is_captain = state_.team_a_.captain_player_id == player_id ||
                            state_.team_b_.captain_player_id == player_id;
    return is_captain && is_knife;
}

KnifeKillResult MatchEngine::player_killed(std::string victim_player_id,
                                           std::string killer_player_id) {
    KnifeKillResult result{};
    if (state_.phase_ != Phase::KnifeLive) {
        return result;
    }

    victim_player_id = normalize_player_id(std::move(victim_player_id));
    killer_player_id = normalize_player_id(std::move(killer_player_id));
    const bool victim_is_a = state_.team_a_.captain_player_id == victim_player_id;
    const bool victim_is_b = state_.team_b_.captain_player_id == victim_player_id;
    const bool clean_a_kill = victim_is_b && state_.team_a_.captain_player_id == killer_player_id;
    const bool clean_b_kill = victim_is_a && state_.team_b_.captain_player_id == killer_player_id;
    if (!clean_a_kill && !clean_b_kill) {
        auto replay = require_knife_replay();
        result.outcome = KnifeKillOutcome::ReplayRequired;
        result.effects = std::move(replay.effects);
        return result;
    }

    state_.knife_winner_player_id_ = killer_player_id;
    state_.knife_loser_player_id_ = victim_player_id;
    const auto completed = transition_to(Phase::KnifeComplete);
    result.outcome =
        completed.ok() ? KnifeKillOutcome::WinnerDecided : KnifeKillOutcome::ReplayRequired;
    result.effects = completed.effects;
    return result;
}

KnifeKillResult MatchEngine::force_knife_winner(std::string winner_player_id) {
    KnifeKillResult result{};
    if (state_.phase_ != Phase::KnifeSetup && state_.phase_ != Phase::KnifeLive) {
        return result;
    }

    winner_player_id = normalize_player_id(std::move(winner_player_id));
    std::string loser_player_id;
    if (state_.team_a_.captain_player_id == winner_player_id) {
        loser_player_id = *state_.team_b_.captain_player_id;
    } else if (state_.team_b_.captain_player_id == winner_player_id) {
        loser_player_id = *state_.team_a_.captain_player_id;
    } else {
        return result;
    }

    state_.knife_winner_player_id_ = winner_player_id;
    state_.knife_loser_player_id_ = std::move(loser_player_id);
    const auto completed = transition_to(Phase::KnifeComplete);
    if (!completed.ok()) {
        state_.knife_winner_player_id_.reset();
        state_.knife_loser_player_id_.reset();
        result.outcome = KnifeKillOutcome::ReplayRequired;
        return result;
    }
    result.outcome = KnifeKillOutcome::WinnerDecided;
    result.effects = completed.effects;
    return result;
}

TransitionResult MatchEngine::knife_round_ended(const bool generated_restart) {
    if (generated_restart || state_.phase_ != Phase::KnifeLive) {
        return {};
    }
    return require_knife_replay();
}

DecisionResult MatchEngine::choose_knife_reward(const KnifeRewardChoice choice) {
    DecisionResult result{};
    if (!state_.enabled_) {
        result.error = DecisionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ == Phase::KnifeComplete) {
        const auto transition = transition_to(Phase::SideOrPick);
        if (!transition.ok()) {
            result.error = DecisionError::WrongPhase;
            return result;
        }
        result.changed = transition.changed;
    } else if (state_.phase_ != Phase::SideOrPick) {
        result.error = DecisionError::WrongPhase;
        return result;
    }

    if (state_.pending_knife_reward_choice_ != choice) {
        state_.pending_knife_reward_choice_ = choice;
        state_.confirmed_knife_reward_choice_.reset();
        state_.first_picker_player_id_.reset();
        state_.side_chooser_player_id_.reset();
        state_.pending_starting_side_.reset();
        state_.team_a_.starting_side.reset();
        state_.team_b_.starting_side.reset();
        result.changed = true;
    }
    return result;
}

DecisionResult MatchEngine::confirm_knife_reward() {
    DecisionResult result{};
    if (!state_.enabled_) {
        result.error = DecisionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::SideOrPick) {
        result.error = DecisionError::WrongPhase;
        return result;
    }
    if (!state_.pending_knife_reward_choice_.has_value()) {
        result.error = DecisionError::ChoiceNotSelected;
        return result;
    }
    if (state_.confirmed_knife_reward_choice_ == state_.pending_knife_reward_choice_ &&
        state_.first_picker_player_id_.has_value() && state_.side_chooser_player_id_.has_value()) {
        return result;
    }

    state_.confirmed_knife_reward_choice_ = state_.pending_knife_reward_choice_;
    if (*state_.confirmed_knife_reward_choice_ == KnifeRewardChoice::StartingSide) {
        state_.side_chooser_player_id_ = state_.knife_winner_player_id_;
        state_.first_picker_player_id_ = state_.knife_loser_player_id_;
    } else {
        state_.first_picker_player_id_ = state_.knife_winner_player_id_;
        state_.side_chooser_player_id_ = state_.knife_loser_player_id_;
    }
    state_.pending_starting_side_.reset();
    result.changed = true;
    return result;
}

DecisionResult MatchEngine::choose_starting_side(const Side side) {
    DecisionResult result{};
    if (!state_.enabled_) {
        result.error = DecisionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::SideOrPick) {
        result.error = DecisionError::WrongPhase;
        return result;
    }
    if (!state_.confirmed_knife_reward_choice_.has_value() ||
        !state_.side_chooser_player_id_.has_value()) {
        result.error = DecisionError::RewardNotConfirmed;
        return result;
    }
    if (state_.pending_starting_side_ != side) {
        state_.pending_starting_side_ = side;
        result.changed = true;
    }
    return result;
}

DecisionResult MatchEngine::confirm_starting_side() {
    DecisionResult result{};
    if (!state_.enabled_) {
        result.error = DecisionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::SideOrPick) {
        result.error = DecisionError::WrongPhase;
        return result;
    }
    if (!state_.confirmed_knife_reward_choice_.has_value() ||
        !state_.side_chooser_player_id_.has_value()) {
        result.error = DecisionError::RewardNotConfirmed;
        return result;
    }
    if (!state_.pending_starting_side_.has_value()) {
        result.error = DecisionError::ChoiceNotSelected;
        return result;
    }

    const auto player = state_.players_.find(*state_.side_chooser_player_id_);
    if (player == state_.players_.end() || !player->second.logical_team.has_value()) {
        result.error = DecisionError::UnknownPlayer;
        return result;
    }
    const LogicalTeam chooser_team = *player->second.logical_team;
    const Side chosen_side = *state_.pending_starting_side_;
    const Side other_side =
        chosen_side == Side::Terrorist ? Side::CounterTerrorist : Side::Terrorist;
    mutable_team(chooser_team).starting_side = chosen_side;
    mutable_team(chooser_team).current_side = chosen_side;
    const LogicalTeam other_team = chooser_team == LogicalTeam::A ? LogicalTeam::B : LogicalTeam::A;
    mutable_team(other_team).starting_side = other_side;
    mutable_team(other_team).current_side = other_side;

    const auto transition = transition_to(Phase::Draft);
    if (!transition.ok()) {
        result.error = DecisionError::RewardNotConfirmed;
        return result;
    }
    if (state_.available_draft_players_.empty()) {
        const auto ready = transition_to(Phase::Ready);
        if (!ready.ok()) {
            result.error = DecisionError::RewardNotConfirmed;
            return result;
        }
    }
    result.changed = true;
    append_draft_reconciliation_effects(result.effects);
    return result;
}

DraftResult MatchEngine::set_draft_type(const DraftType type) {
    DraftResult result{};
    if (!state_.enabled_) {
        result.error = DraftError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ == Phase::Draft || state_.phase_ == Phase::Ready ||
        state_.phase_ == Phase::LiveOnThree || state_.phase_ == Phase::RegulationFirstHalf ||
        state_.phase_ == Phase::Halftime || state_.phase_ == Phase::RegulationSecondHalf ||
        state_.phase_ == Phase::OvertimeFirstHalf || state_.phase_ == Phase::OvertimeHalftime ||
        state_.phase_ == Phase::OvertimeSecondHalf || state_.phase_ == Phase::MatchComplete) {
        result.error = DraftError::WrongPhase;
        return result;
    }
    if (state_.draft_type_ != type) {
        state_.draft_type_ = type;
        result.changed = true;
    }
    return result;
}

DraftResult MatchEngine::choose_draft_player(std::string player_id, const bool admin_override) {
    DraftResult result{};
    if (!state_.enabled_) {
        result.error = DraftError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::Draft) {
        result.error = DraftError::WrongPhase;
        return result;
    }
    const auto captain = state_.players_.find(*state_.current_draft_captain_player_id_);
    if (!admin_override && (captain == state_.players_.end() || !captain->second.connected)) {
        result.error = DraftError::CaptainDisconnected;
        return result;
    }

    player_id = normalize_player_id(std::move(player_id));
    if (player_id.empty()) {
        result.error = DraftError::InvalidPlayerId;
        return result;
    }
    if (state_.players_.find(player_id) == state_.players_.end()) {
        result.error = DraftError::UnknownPlayer;
        return result;
    }
    if (std::find(state_.drafted_players_.begin(), state_.drafted_players_.end(), player_id) !=
        state_.drafted_players_.end()) {
        result.error = DraftError::AlreadyDrafted;
        return result;
    }
    if (!std::binary_search(state_.available_draft_players_.begin(),
                            state_.available_draft_players_.end(), player_id)) {
        result.error = DraftError::IneligiblePlayer;
        return result;
    }
    if (state_.pending_draft_player_id_ != player_id) {
        state_.pending_draft_player_id_ = std::move(player_id);
        result.changed = true;
    }
    return result;
}

DraftResult MatchEngine::confirm_draft_player(const bool admin_override) {
    DraftResult result{};
    if (!state_.enabled_) {
        result.error = DraftError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::Draft) {
        result.error = DraftError::WrongPhase;
        return result;
    }
    const auto captain = state_.players_.find(*state_.current_draft_captain_player_id_);
    if (!admin_override && (captain == state_.players_.end() || !captain->second.connected)) {
        result.error = DraftError::CaptainDisconnected;
        return result;
    }
    if (!state_.pending_draft_player_id_.has_value()) {
        result.error = DraftError::ChoiceNotSelected;
        return result;
    }

    const std::string player_id = *state_.pending_draft_player_id_;
    const auto available = std::lower_bound(state_.available_draft_players_.begin(),
                                            state_.available_draft_players_.end(), player_id);
    if (available == state_.available_draft_players_.end() || *available != player_id) {
        result.error = DraftError::IneligiblePlayer;
        return result;
    }

    LogicalTeam drafting_team;
    if (state_.team_a_.captain_player_id == state_.current_draft_captain_player_id_) {
        drafting_team = LogicalTeam::A;
    } else if (state_.team_b_.captain_player_id == state_.current_draft_captain_player_id_) {
        drafting_team = LogicalTeam::B;
    } else {
        result.error = DraftError::WrongPhase;
        return result;
    }

    state_.available_draft_players_.erase(available);
    state_.drafted_players_.push_back(player_id);
    state_.pending_draft_player_id_.reset();
    state_.players_.at(player_id).logical_team = drafting_team;
    mutable_team(drafting_team).roster.push_back(player_id);
    if (state_.players_.at(player_id).connected) {
        const Side side = *mutable_team(drafting_team).current_side;
        const PlayerDestination destination = side == Side::Terrorist
                                                  ? PlayerDestination::Terrorist
                                                  : PlayerDestination::CounterTerrorist;
        result.effects.push_back({EffectType::AssignPlayerTeam, player_id, destination});
    }
    result.changed = true;

    if (state_.available_draft_players_.empty()) {
        const auto ready = transition_to(Phase::Ready);
        result.effects.insert(result.effects.end(), ready.effects.begin(), ready.effects.end());
    } else {
        advance_draft_turn();
    }
    return result;
}

ReadyResult MatchEngine::set_captain_ready(std::string captain_player_id, const bool ready,
                                           const bool admin_override) {
    ReadyResult result{};
    if (!state_.enabled_) {
        result.error = ReadyError::ScrimDisabled;
        return result;
    }
    const bool live_on_three_recovery =
        state_.phase_ == Phase::LiveOnThree && !ready && admin_override;
    if (state_.phase_ != Phase::Ready && !live_on_three_recovery) {
        result.error = ReadyError::WrongPhase;
        return result;
    }

    captain_player_id = normalize_player_id(std::move(captain_player_id));
    if (captain_player_id.empty()) {
        result.error = ReadyError::InvalidPlayerId;
        return result;
    }
    const auto player = state_.players_.find(captain_player_id);
    if (player == state_.players_.end()) {
        result.error = ReadyError::UnknownPlayer;
        return result;
    }

    TeamState* team = nullptr;
    if (state_.team_a_.captain_player_id == captain_player_id) {
        team = &state_.team_a_;
    } else if (state_.team_b_.captain_player_id == captain_player_id) {
        team = &state_.team_b_;
    } else {
        result.error = ReadyError::NotCaptain;
        return result;
    }
    if (ready && !admin_override && !player->second.connected) {
        result.error = ReadyError::CaptainDisconnected;
        return result;
    }

    if (live_on_three_recovery) {
        const Phase recovery_phase =
            state_.live_on_three_target_phase_ == Phase::RegulationSecondHalf ? Phase::Halftime
                                                                              : Phase::Ready;
        const auto rewound = transition_to(recovery_phase);
        if (!rewound.ok()) {
            result.error = ReadyError::WrongPhase;
            return result;
        }
        result.changed = true;
        result.effects = rewound.effects;
        return result;
    }

    if (team->captain_ready != ready) {
        team->captain_ready = ready;
        result.changed = true;
    }
    if (state_.team_a_.captain_ready && state_.team_b_.captain_ready) {
        auto live_on_three = begin_live_on_three(Phase::RegulationFirstHalf);
        if (!live_on_three.ok()) {
            result.error = ReadyError::WrongPhase;
            return result;
        }
        result.changed = true;
        result.effects = std::move(live_on_three.effects);
    }
    return result;
}

TransitionResult MatchEngine::live_on_three_restart_completed() {
    TransitionResult result{};
    if (!state_.enabled_) {
        result.error = TransitionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::LiveOnThree) {
        result.error = TransitionError::IllegalTransition;
        return result;
    }

    ++state_.live_on_three_restarts_completed_;
    result.changed = true;
    if (state_.live_on_three_restarts_completed_ == 1) {
        result.effects.push_back({EffectType::RestartRound, {}, PlayerDestination::Spectator, 1});
    } else if (state_.live_on_three_restarts_completed_ == 2) {
        result.effects.push_back({EffectType::RestartRound, {}, PlayerDestination::Spectator, 3});
    } else {
        const Phase target = *state_.live_on_three_target_phase_;
        const auto live = transition_to(target);
        result.effects.insert(result.effects.end(), live.effects.begin(), live.effects.end());
    }
    return result;
}

TransitionResult MatchEngine::start_halftime_live_on_three(const bool admin_override) {
    TransitionResult result{};
    if (!state_.enabled_) {
        result.error = TransitionError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::Halftime) {
        result.error = TransitionError::IllegalTransition;
        return result;
    }
    const auto captain_a = state_.players_.find(*state_.team_a_.captain_player_id);
    const auto captain_b = state_.players_.find(*state_.team_b_.captain_player_id);
    if (!admin_override && (captain_a == state_.players_.end() || !captain_a->second.connected ||
                            captain_b == state_.players_.end() || !captain_b->second.connected)) {
        result.error = TransitionError::PrerequisiteNotMet;
        return result;
    }
    return begin_live_on_three(Phase::RegulationSecondHalf);
}

MatchConfigurationResult MatchEngine::set_regulation_rounds_per_half(const int rounds) {
    MatchConfigurationResult result{};
    if (!state_.enabled_) {
        result.error = MatchConfigurationError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ == Phase::LiveOnThree || state_.phase_ == Phase::RegulationFirstHalf ||
        state_.phase_ == Phase::Halftime || state_.phase_ == Phase::RegulationSecondHalf ||
        state_.phase_ == Phase::OvertimeSetup || state_.phase_ == Phase::OvertimeFirstHalf ||
        state_.phase_ == Phase::OvertimeHalftime || state_.phase_ == Phase::OvertimeSecondHalf ||
        state_.phase_ == Phase::MatchComplete) {
        result.error = MatchConfigurationError::WrongPhase;
        return result;
    }
    if (rounds <= 0 || rounds > 100) {
        result.error = MatchConfigurationError::InvalidValue;
        return result;
    }
    if (state_.regulation_rounds_per_half_ != rounds) {
        state_.regulation_rounds_per_half_ = rounds;
        result.changed = true;
    }
    return result;
}

RoundResult MatchEngine::regulation_round_ended(const std::optional<Side> winning_side) {
    if (state_.phase_ != Phase::RegulationFirstHalf &&
        state_.phase_ != Phase::RegulationSecondHalf) {
        return {};
    }
    if (!winning_side.has_value()) {
        RoundResult result{};
        result.outcome = RoundOutcome::Ambiguous;
        return result;
    }
    if (state_.team_a_.current_side == winning_side) {
        return count_regulation_round(LogicalTeam::A);
    }
    if (state_.team_b_.current_side == winning_side) {
        return count_regulation_round(LogicalTeam::B);
    }
    RoundResult result{};
    result.outcome = RoundOutcome::Ambiguous;
    return result;
}

RoundResult MatchEngine::force_regulation_round_winner(const LogicalTeam winning_team) {
    if (state_.phase_ != Phase::RegulationFirstHalf &&
        state_.phase_ != Phase::RegulationSecondHalf) {
        return {};
    }
    return count_regulation_round(winning_team);
}

TeamState& MatchEngine::mutable_team(const LogicalTeam team) noexcept {
    return team == LogicalTeam::A ? state_.team_a_ : state_.team_b_;
}

void MatchEngine::commit_captains() {
    const std::string& captain_a = *state_.team_a_.captain_player_id;
    const std::string& captain_b = *state_.team_b_.captain_player_id;
    state_.team_a_.roster = {captain_a};
    state_.team_b_.roster = {captain_b};
    state_.players_.at(captain_a).logical_team = LogicalTeam::A;
    state_.players_.at(captain_b).logical_team = LogicalTeam::B;
}

void MatchEngine::append_knife_reconciliation_effects(TransitionResult& result) const {
    const std::string& captain_a = *state_.team_a_.captain_player_id;
    const std::string& captain_b = *state_.team_b_.captain_player_id;
    std::vector<std::string> connected_player_ids;
    connected_player_ids.reserve(state_.players_.size());
    for (const auto& [player_id, player] : state_.players_) {
        if (!player.connected) {
            continue;
        }
        connected_player_ids.push_back(player_id);
    }
    std::sort(connected_player_ids.begin(), connected_player_ids.end());

    for (const auto& player_id : connected_player_ids) {
        PlayerDestination destination = PlayerDestination::Spectator;
        if (player_id == captain_a) {
            destination = *state_.team_a_.current_side == Side::Terrorist
                              ? PlayerDestination::Terrorist
                              : PlayerDestination::CounterTerrorist;
        } else if (player_id == captain_b) {
            destination = *state_.team_b_.current_side == Side::Terrorist
                              ? PlayerDestination::Terrorist
                              : PlayerDestination::CounterTerrorist;
        }
        result.effects.push_back({EffectType::AssignPlayerTeam, player_id, destination});
        if (player_id == captain_a || player_id == captain_b) {
            result.effects.push_back(
                {EffectType::EnsureKnifeLoadout, player_id, PlayerDestination::Spectator});
        }
    }
}

void MatchEngine::append_draft_reconciliation_effects(std::vector<Effect>& effects) const {
    std::vector<std::string> connected_player_ids;
    connected_player_ids.reserve(state_.players_.size());
    for (const auto& [player_id, player] : state_.players_) {
        if (player.connected) {
            connected_player_ids.push_back(player_id);
        }
    }
    std::sort(connected_player_ids.begin(), connected_player_ids.end());

    for (const auto& player_id : connected_player_ids) {
        PlayerDestination destination = PlayerDestination::Spectator;
        const auto& player = state_.players_.at(player_id);
        if (player.logical_team == LogicalTeam::A) {
            destination = *state_.team_a_.current_side == Side::Terrorist
                              ? PlayerDestination::Terrorist
                              : PlayerDestination::CounterTerrorist;
        } else if (player.logical_team == LogicalTeam::B) {
            destination = *state_.team_b_.current_side == Side::Terrorist
                              ? PlayerDestination::Terrorist
                              : PlayerDestination::CounterTerrorist;
        }
        effects.push_back({EffectType::AssignPlayerTeam, player_id, destination});
    }
}

void MatchEngine::initialize_draft() {
    state_.available_draft_players_.clear();
    state_.drafted_players_.clear();
    state_.pending_draft_player_id_.reset();
    for (const auto& player_id : state_.eligible_players_) {
        if (state_.team_a_.captain_player_id != player_id &&
            state_.team_b_.captain_player_id != player_id) {
            state_.available_draft_players_.push_back(player_id);
        }
    }
    state_.current_draft_captain_player_id_ = state_.first_picker_player_id_;
    state_.draft_picks_remaining_in_turn_ = state_.available_draft_players_.empty() ? 0 : 1;
}

void MatchEngine::advance_draft_turn() {
    if (state_.draft_type_ == DraftType::Snake && state_.draft_picks_remaining_in_turn_ > 1) {
        --state_.draft_picks_remaining_in_turn_;
        return;
    }

    if (state_.current_draft_captain_player_id_ == state_.team_a_.captain_player_id) {
        state_.current_draft_captain_player_id_ = state_.team_b_.captain_player_id;
    } else {
        state_.current_draft_captain_player_id_ = state_.team_a_.captain_player_id;
    }
    const int turn_length = state_.draft_type_ == DraftType::Snake ? 2 : 1;
    state_.draft_picks_remaining_in_turn_ =
        std::min(turn_length, static_cast<int>(state_.available_draft_players_.size()));
}

TransitionResult MatchEngine::begin_live_on_three(const Phase target) {
    TransitionResult result{};
    if (target != Phase::RegulationFirstHalf && target != Phase::RegulationSecondHalf) {
        result.error = TransitionError::IllegalTransition;
        return result;
    }
    state_.live_on_three_target_phase_ = target;
    result = transition_to(Phase::LiveOnThree);
    if (!result.ok()) {
        state_.live_on_three_target_phase_.reset();
    }
    return result;
}

RoundResult MatchEngine::count_regulation_round(const LogicalTeam winning_team) {
    RoundResult result{};
    const Phase period_phase = state_.phase_;
    TeamState& team = mutable_team(winning_team);
    ++team.total_score;
    ++team.period_score;
    ++state_.period_rounds_completed_;
    result.winning_team = winning_team;
    if (period_phase == Phase::RegulationFirstHalf &&
        state_.period_rounds_completed_ == state_.regulation_rounds_per_half_) {
        const auto halftime = transition_to(Phase::Halftime);
        result.outcome = halftime.ok() ? RoundOutcome::HalfComplete : RoundOutcome::Ambiguous;
        result.effects = halftime.effects;
        if (halftime.ok()) {
            const auto live_on_three = start_halftime_live_on_three(false);
            if (live_on_three.ok()) {
                result.effects.insert(result.effects.end(), live_on_three.effects.begin(),
                                      live_on_three.effects.end());
            }
        }
        return result;
    }
    if (period_phase == Phase::RegulationSecondHalf &&
        team.total_score >= state_.regulation_rounds_per_half_ + 1) {
        const auto complete = transition_to(Phase::MatchComplete);
        result.outcome = complete.ok() ? RoundOutcome::MatchComplete : RoundOutcome::Ambiguous;
        result.effects = complete.effects;
        return result;
    }
    if (period_phase == Phase::RegulationSecondHalf &&
        state_.period_rounds_completed_ == state_.regulation_rounds_per_half_) {
        const auto overtime = transition_to(Phase::OvertimeSetup);
        result.outcome = overtime.ok() ? RoundOutcome::RegulationTied : RoundOutcome::Ambiguous;
        result.effects = overtime.effects;
        return result;
    }
    result.outcome = RoundOutcome::Counted;
    return result;
}

bool MatchEngine::is_knife_phase() const noexcept {
    return state_.phase_ == Phase::KnifeSetup || state_.phase_ == Phase::KnifeLive;
}

bool MatchEngine::are_team_changes_locked() const noexcept {
    return is_knife_phase() || state_.phase_ == Phase::KnifeComplete ||
           state_.phase_ == Phase::SideOrPick || state_.phase_ == Phase::Draft ||
           state_.phase_ == Phase::Ready || state_.phase_ == Phase::LiveOnThree ||
           state_.phase_ == Phase::RegulationFirstHalf || state_.phase_ == Phase::Halftime ||
           state_.phase_ == Phase::RegulationSecondHalf || state_.phase_ == Phase::OvertimeSetup;
}

TransitionResult MatchEngine::require_knife_replay() {
    state_.knife_winner_player_id_.reset();
    state_.knife_loser_player_id_.reset();
    return transition_to(Phase::KnifeSetup);
}

bool MatchEngine::is_legal_transition(const Phase from, const Phase to) noexcept {
    switch (from) {
    case Phase::Disabled:
        return false;
    case Phase::CaptainSelection:
        return to == Phase::KnifeSetup;
    case Phase::KnifeSetup:
        return to == Phase::KnifeLive || to == Phase::KnifeComplete;
    case Phase::KnifeLive:
        return to == Phase::KnifeComplete || to == Phase::KnifeSetup;
    case Phase::KnifeComplete:
        return to == Phase::SideOrPick;
    case Phase::SideOrPick:
        return to == Phase::Draft;
    case Phase::Draft:
        return to == Phase::Ready;
    case Phase::Ready:
        return to == Phase::LiveOnThree;
    case Phase::LiveOnThree:
        return to == Phase::Ready || to == Phase::Halftime || to == Phase::RegulationFirstHalf ||
               to == Phase::RegulationSecondHalf;
    case Phase::RegulationFirstHalf:
        return to == Phase::Halftime;
    case Phase::Halftime:
        return to == Phase::LiveOnThree;
    case Phase::RegulationSecondHalf:
        return to == Phase::OvertimeSetup || to == Phase::MatchComplete;
    case Phase::OvertimeSetup:
        return to == Phase::OvertimeFirstHalf;
    case Phase::OvertimeFirstHalf:
        return to == Phase::OvertimeHalftime;
    case Phase::OvertimeHalftime:
        return to == Phase::OvertimeSecondHalf;
    case Phase::OvertimeSecondHalf:
        return to == Phase::OvertimeFirstHalf || to == Phase::MatchComplete;
    case Phase::MatchComplete:
        return false;
    }
    return false;
}

std::string MatchEngine::normalize_player_id(std::string player_id) {
    const auto is_space = [](const unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto first = std::find_if_not(player_id.begin(), player_id.end(), is_space);
    const auto last = std::find_if_not(player_id.rbegin(), player_id.rend(), is_space).base();
    if (first >= last) {
        return {};
    }

    std::string normalized(first, last);
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
    return normalized;
}

} // namespace scrimmod::core
