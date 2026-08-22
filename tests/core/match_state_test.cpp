#include "scrimmod/core/match_state.hpp"

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
    using scrimmod::core::LogicalTeam;
    using scrimmod::core::MatchState;
    using scrimmod::core::Phase;
    using scrimmod::core::phase_name;

    MatchState state;
    require(!state.enabled(), "new match state is disabled");
    require(state.phase() == Phase::Disabled, "new match state has Disabled phase");
    require(std::string{phase_name(Phase::Disabled)} == "Disabled",
            "Disabled phase has a stable display name");
    require(std::string{phase_name(Phase::OvertimeSecondHalf)} == "OvertimeSecondHalf",
            "overtime phase has a stable display name");
    require(std::string{phase_name(Phase::LiveOnThree)} == "LiveOnThree",
            "LO3 phase has a stable display name");

    require(state.players().empty(), "new match state has no players");
    require(state.team(LogicalTeam::A).roster.empty(), "new match state has empty Team A");
    require(state.team(LogicalTeam::B).roster.empty(), "new match state has empty Team B");

    std::cout << "All ScrimMod core tests passed\n";
    return EXIT_SUCCESS;
}
