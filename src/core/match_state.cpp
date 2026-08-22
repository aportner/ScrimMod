#include "scrimmod/core/match_state.hpp"

namespace scrimmod::core {

bool MatchState::enabled() const noexcept { return enabled_; }

Phase MatchState::phase() const noexcept { return phase_; }

const TeamState& MatchState::team(const LogicalTeam team) const noexcept {
    return team == LogicalTeam::A ? team_a_ : team_b_;
}

const std::unordered_map<std::string, Player>& MatchState::players() const noexcept {
    return players_;
}

void MatchState::enable() {
    reset();
    enabled_ = true;
    phase_ = Phase::CaptainSelection;
}

void MatchState::disable() { reset(); }

void MatchState::reset() noexcept {
    enabled_ = false;
    phase_ = Phase::Disabled;
    team_a_ = TeamState{};
    team_b_ = TeamState{};
    players_.clear();
}

} // namespace scrimmod::core
