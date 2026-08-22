#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace scrimmod::core {

class MatchEngine;

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
enum class PlayerType : std::uint8_t { Human, Bot };

[[nodiscard]] const char* phase_name(Phase phase) noexcept;

struct Player {
    std::string player_id;
    std::string last_known_name;
    std::optional<LogicalTeam> logical_team;
    PlayerType type{PlayerType::Human};
    bool connected{false};
};

struct TeamState {
    std::optional<std::string> captain_player_id;
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
    [[nodiscard]] bool eligible_pool_captured() const noexcept;
    [[nodiscard]] const std::vector<std::string>& eligible_players() const noexcept;
    [[nodiscard]] const std::optional<std::string>& knife_winner_player_id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& knife_loser_player_id() const noexcept;

  private:
    friend class MatchEngine;

    void reset() noexcept;

    bool enabled_{false};
    Phase phase_{Phase::Disabled};
    TeamState team_a_{};
    TeamState team_b_{};
    std::unordered_map<std::string, Player> players_{};
    bool eligible_pool_captured_{false};
    std::vector<std::string> eligible_players_{};
    std::optional<std::string> knife_winner_player_id_{};
    std::optional<std::string> knife_loser_player_id_{};
};

} // namespace scrimmod::core
