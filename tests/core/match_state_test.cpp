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

    state.enable();
    require(state.enabled(), "enable marks state enabled");
    require(state.phase() == Phase::CaptainSelection, "enable begins at captain selection");

    state.disable();
    require(!state.enabled(), "disable marks state disabled");
    require(state.phase() == Phase::Disabled, "disable restores Disabled phase");
    require(state.players().empty(), "disable clears players");
    require(state.team(LogicalTeam::A).roster.empty(), "disable clears Team A roster");
    require(state.team(LogicalTeam::B).roster.empty(), "disable clears Team B roster");

    std::cout << "All ScrimMod core tests passed\n";
    return EXIT_SUCCESS;
}
