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
