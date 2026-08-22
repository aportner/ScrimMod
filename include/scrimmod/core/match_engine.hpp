#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "scrimmod/core/match_state.hpp"

namespace scrimmod::core {

enum class EffectType : std::uint8_t {
    ExecutePregameConfig,
    AssignPlayerTeam,
    EnsureKnifeLoadout,
    RestartRound,
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
    std::vector<Effect> effects;

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

enum class KnifeKillOutcome : std::uint8_t { Ignored, WinnerDecided, ReplayRequired };

struct KnifeKillResult {
    KnifeKillOutcome outcome{KnifeKillOutcome::Ignored};
    std::vector<Effect> effects;
};

enum class DecisionError : std::uint8_t {
    None,
    ScrimDisabled,
    WrongPhase,
    ChoiceNotSelected,
    RewardNotConfirmed,
    UnknownPlayer,
};

struct DecisionResult {
    DecisionError error{DecisionError::None};
    bool changed{false};
    std::vector<Effect> effects;

    [[nodiscard]] bool ok() const noexcept { return error == DecisionError::None; }
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
    [[nodiscard]] bool can_player_choose_team(std::string player_id) const;
    [[nodiscard]] bool can_player_acquire_weapon(std::string player_id, bool is_knife) const;
    [[nodiscard]] KnifeKillResult player_killed(std::string victim_player_id,
                                                std::string killer_player_id);
    [[nodiscard]] KnifeKillResult force_knife_winner(std::string winner_player_id);
    [[nodiscard]] TransitionResult knife_round_ended(bool generated_restart);
    [[nodiscard]] DecisionResult choose_knife_reward(KnifeRewardChoice choice);
    [[nodiscard]] DecisionResult confirm_knife_reward();
    [[nodiscard]] DecisionResult choose_starting_side(Side side);
    [[nodiscard]] DecisionResult confirm_starting_side();

  private:
    [[nodiscard]] static bool is_legal_transition(Phase from, Phase to) noexcept;
    [[nodiscard]] static std::string normalize_player_id(std::string player_id);
    [[nodiscard]] TeamState& mutable_team(LogicalTeam team) noexcept;
    void commit_captains();
    void append_knife_reconciliation_effects(TransitionResult& result) const;
    void append_draft_reconciliation_effects(std::vector<Effect>& effects) const;
    [[nodiscard]] bool is_knife_phase() const noexcept;
    [[nodiscard]] bool are_team_changes_locked() const noexcept;
    [[nodiscard]] TransitionResult require_knife_replay();

    MatchState state_{};
};

} // namespace scrimmod::core
