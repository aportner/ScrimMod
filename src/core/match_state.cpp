#include "scrimmod/core/match_state.hpp"

namespace scrimmod::core {

const char* phase_name(const Phase phase) noexcept {
    switch (phase) {
    case Phase::Disabled:
        return "Disabled";
    case Phase::CaptainSelection:
        return "CaptainSelection";
    case Phase::KnifeSetup:
        return "KnifeSetup";
    case Phase::KnifeLive:
        return "KnifeLive";
    case Phase::KnifeComplete:
        return "KnifeComplete";
    case Phase::SideOrPick:
        return "SideOrPick";
    case Phase::Draft:
        return "Draft";
    case Phase::Ready:
        return "Ready";
    case Phase::RegulationFirstHalf:
        return "RegulationFirstHalf";
    case Phase::Halftime:
        return "Halftime";
    case Phase::RegulationSecondHalf:
        return "RegulationSecondHalf";
    case Phase::OvertimeFirstHalf:
        return "OvertimeFirstHalf";
    case Phase::OvertimeHalftime:
        return "OvertimeHalftime";
    case Phase::OvertimeSecondHalf:
        return "OvertimeSecondHalf";
    case Phase::MatchComplete:
        return "MatchComplete";
    }
    return "Unknown";
}

bool MatchState::enabled() const noexcept { return enabled_; }

Phase MatchState::phase() const noexcept { return phase_; }

const TeamState& MatchState::team(const LogicalTeam team) const noexcept {
    return team == LogicalTeam::A ? team_a_ : team_b_;
}

const std::unordered_map<std::string, Player>& MatchState::players() const noexcept {
    return players_;
}

bool MatchState::eligible_pool_captured() const noexcept { return eligible_pool_captured_; }

const std::vector<std::string>& MatchState::eligible_players() const noexcept {
    return eligible_players_;
}

const std::optional<std::string>& MatchState::knife_winner_player_id() const noexcept {
    return knife_winner_player_id_;
}

const std::optional<std::string>& MatchState::knife_loser_player_id() const noexcept {
    return knife_loser_player_id_;
}

const std::optional<KnifeRewardChoice>& MatchState::pending_knife_reward_choice() const noexcept {
    return pending_knife_reward_choice_;
}

const std::optional<KnifeRewardChoice>& MatchState::confirmed_knife_reward_choice() const noexcept {
    return confirmed_knife_reward_choice_;
}

const std::optional<std::string>& MatchState::first_picker_player_id() const noexcept {
    return first_picker_player_id_;
}

const std::optional<std::string>& MatchState::side_chooser_player_id() const noexcept {
    return side_chooser_player_id_;
}

const std::optional<Side>& MatchState::pending_starting_side() const noexcept {
    return pending_starting_side_;
}

DraftType MatchState::draft_type() const noexcept { return draft_type_; }

const std::optional<std::string>& MatchState::current_draft_captain_player_id() const noexcept {
    return current_draft_captain_player_id_;
}

int MatchState::draft_picks_remaining_in_turn() const noexcept {
    return draft_picks_remaining_in_turn_;
}

const std::vector<std::string>& MatchState::available_draft_players() const noexcept {
    return available_draft_players_;
}

const std::vector<std::string>& MatchState::drafted_players() const noexcept {
    return drafted_players_;
}

const std::optional<std::string>& MatchState::pending_draft_player_id() const noexcept {
    return pending_draft_player_id_;
}

void MatchState::reset() noexcept {
    enabled_ = false;
    phase_ = Phase::Disabled;
    team_a_ = TeamState{};
    team_b_ = TeamState{};
    players_.clear();
    eligible_pool_captured_ = false;
    eligible_players_.clear();
    knife_winner_player_id_.reset();
    knife_loser_player_id_.reset();
    pending_knife_reward_choice_.reset();
    confirmed_knife_reward_choice_.reset();
    first_picker_player_id_.reset();
    side_chooser_player_id_.reset();
    pending_starting_side_.reset();
    draft_type_ = DraftType::Snake;
    current_draft_captain_player_id_.reset();
    draft_picks_remaining_in_turn_ = 0;
    available_draft_players_.clear();
    drafted_players_.clear();
    pending_draft_player_id_.reset();
}

} // namespace scrimmod::core
