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
    LiveOnThree,
    RegulationFirstHalf,
    Halftime,
    RegulationSecondHalf,
    OvertimeSetup,
    OvertimeFirstHalf,
    OvertimeHalftime,
    OvertimeSecondHalf,
    MatchComplete,
};

enum class LogicalTeam : std::uint8_t { A, B };
enum class Side : std::uint8_t { Terrorist, CounterTerrorist };
enum class PlayerType : std::uint8_t { Human, Bot };
enum class KnifeRewardChoice : std::uint8_t { StartingSide, FirstPick };
enum class DraftType : std::uint8_t { AB, Snake };

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
    std::optional<Side> starting_side;
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
    [[nodiscard]] const std::optional<KnifeRewardChoice>&
    pending_knife_reward_choice() const noexcept;
    [[nodiscard]] const std::optional<KnifeRewardChoice>&
    confirmed_knife_reward_choice() const noexcept;
    [[nodiscard]] const std::optional<std::string>& first_picker_player_id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& side_chooser_player_id() const noexcept;
    [[nodiscard]] const std::optional<Side>& pending_starting_side() const noexcept;
    [[nodiscard]] DraftType draft_type() const noexcept;
    [[nodiscard]] const std::optional<std::string>&
    current_draft_captain_player_id() const noexcept;
    [[nodiscard]] int draft_picks_remaining_in_turn() const noexcept;
    [[nodiscard]] const std::vector<std::string>& available_draft_players() const noexcept;
    [[nodiscard]] const std::vector<std::string>& drafted_players() const noexcept;
    [[nodiscard]] const std::optional<std::string>& pending_draft_player_id() const noexcept;
    [[nodiscard]] int live_on_three_restarts_completed() const noexcept;
    [[nodiscard]] const std::optional<Phase>& live_on_three_target_phase() const noexcept;
    [[nodiscard]] int regulation_rounds_per_half() const noexcept;
    [[nodiscard]] int period_rounds_completed() const noexcept;

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
    std::optional<KnifeRewardChoice> pending_knife_reward_choice_{};
    std::optional<KnifeRewardChoice> confirmed_knife_reward_choice_{};
    std::optional<std::string> first_picker_player_id_{};
    std::optional<std::string> side_chooser_player_id_{};
    std::optional<Side> pending_starting_side_{};
    DraftType draft_type_{DraftType::Snake};
    std::optional<std::string> current_draft_captain_player_id_{};
    int draft_picks_remaining_in_turn_{0};
    std::vector<std::string> available_draft_players_{};
    std::vector<std::string> drafted_players_{};
    std::optional<std::string> pending_draft_player_id_{};
    int live_on_three_restarts_completed_{0};
    std::optional<Phase> live_on_three_target_phase_{};
    int regulation_rounds_per_half_{12};
    int period_rounds_completed_{0};
};

} // namespace scrimmod::core
