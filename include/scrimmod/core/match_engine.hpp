#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "scrimmod/core/match_state.hpp"

namespace scrimmod::core {

enum class EffectType : std::uint8_t {
    ExecutePregameConfig,
    AssignPlayerTeam,
};

enum class PlayerDestination : std::uint8_t { Terrorist, CounterTerrorist, Spectator };

struct Effect {
    EffectType type;
    std::string player_id;
    PlayerDestination destination{PlayerDestination::Spectator};
};

enum class TransitionError : std::uint8_t {
    None,
    ScrimDisabled,
    IllegalTransition,
    PrerequisiteNotMet,
};

struct TransitionResult {
    TransitionError error{TransitionError::None};
    bool changed{false};
    std::vector<Effect> effects;

    [[nodiscard]] bool ok() const noexcept { return error == TransitionError::None; }
};

enum class PlayerUpdateError : std::uint8_t {
    None,
    ScrimDisabled,
    InvalidPlayerId,
    UnknownPlayer,
};

struct PlayerUpdateResult {
    PlayerUpdateError error{PlayerUpdateError::None};
    bool changed{false};

    [[nodiscard]] bool ok() const noexcept { return error == PlayerUpdateError::None; }
};

enum class EligibilityError : std::uint8_t {
    None,
    ScrimDisabled,
    WrongPhase,
    PoolNotCaptured,
    InvalidPlayerId,
    UnknownPlayer,
};

struct EligibilityResult {
    EligibilityError error{EligibilityError::None};
    bool changed{false};

    [[nodiscard]] bool ok() const noexcept { return error == EligibilityError::None; }
};

enum class CaptainSelectionError : std::uint8_t {
    None,
    ScrimDisabled,
    WrongPhase,
    PoolNotCaptured,
    InvalidPlayerId,
    UnknownPlayer,
    IneligiblePlayer,
    DuplicateCaptain,
};

struct CaptainSelectionResult {
    CaptainSelectionError error{CaptainSelectionError::None};
    bool changed{false};

    [[nodiscard]] bool ok() const noexcept { return error == CaptainSelectionError::None; }
};

class MatchEngine final {
  public:
    [[nodiscard]] const MatchState& state() const noexcept;

    [[nodiscard]] TransitionResult set_enabled(bool enabled);
    [[nodiscard]] TransitionResult transition_to(Phase target);
    [[nodiscard]] PlayerUpdateResult player_connected(std::string player_id, std::string name,
                                                      PlayerType type = PlayerType::Human);
    [[nodiscard]] PlayerUpdateResult player_disconnected(std::string player_id);
    [[nodiscard]] EligibilityResult capture_eligible_players();
    [[nodiscard]] EligibilityResult add_eligible_player(std::string player_id);
    [[nodiscard]] EligibilityResult remove_eligible_player(std::string player_id);
    [[nodiscard]] CaptainSelectionResult select_captain(LogicalTeam team, std::string player_id);
    [[nodiscard]] CaptainSelectionResult clear_captain(LogicalTeam team);
    [[nodiscard]] TransitionResult confirm_captains(Side team_a_knife_side);
    [[nodiscard]] std::vector<Effect> reconciliation_effects() const;

  private:
    [[nodiscard]] static bool is_legal_transition(Phase from, Phase to) noexcept;
    [[nodiscard]] static std::string normalize_player_id(std::string player_id);
    [[nodiscard]] TeamState& mutable_team(LogicalTeam team) noexcept;
    void commit_captains();
    void append_knife_setup_effects(TransitionResult& result) const;

    MatchState state_{};
};

} // namespace scrimmod::core
