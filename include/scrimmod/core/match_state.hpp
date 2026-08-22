#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace scrimmod::core {

enum class Phase : std::uint8_t {
    Disabled,
    CaptainSelection,
    KnifeSetup,
    KnifeLive,
    KnifeComplete,
    SideOrPick,
    Draft,
    Ready,
    RegulationFirstHalf,
    Halftime,
    RegulationSecondHalf,
    OvertimeFirstHalf,
    OvertimeHalftime,
    OvertimeSecondHalf,
    MatchComplete,
};

enum class LogicalTeam : std::uint8_t { A, B };
enum class Side : std::uint8_t { Terrorist, CounterTerrorist };

struct Player {
    std::string steam_id;
    std::string last_known_name;
    std::optional<LogicalTeam> logical_team;
    bool connected{false};
};

struct TeamState {
    std::optional<std::string> captain_steam_id;
    std::vector<std::string> roster;
    int total_score{0};
    int period_score{0};
    std::optional<Side> current_side;
    bool captain_ready{false};
};

class MatchState final {
  public:
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] Phase phase() const noexcept;
    [[nodiscard]] const TeamState& team(LogicalTeam team) const noexcept;
    [[nodiscard]] const std::unordered_map<std::string, Player>& players() const noexcept;

    void enable();
    void disable();

  private:
    void reset() noexcept;

    bool enabled_{false};
    Phase phase_{Phase::Disabled};
    TeamState team_a_{};
    TeamState team_b_{};
    std::unordered_map<std::string, Player> players_{};
};

} // namespace scrimmod::core
