#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "scrimmod/core/match_state.hpp"

namespace scrimmod::core {

enum class EffectType : std::uint8_t {
    ExecutePregameConfig,
    ExecuteLiveConfig,
    AssignPlayerTeam,
    EnsureKnifeLoadout,
    RestartRound,
};

enum class PlayerDestination : std::uint8_t { Terrorist, CounterTerrorist, Spectator };

struct Effect {
    EffectType type;
    std::string player_id;
    PlayerDestination destination{PlayerDestination::Spectator};
    int value{0};
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

enum class DraftError : std::uint8_t {
    None,
    ScrimDisabled,
    WrongPhase,
    InvalidPlayerId,
    UnknownPlayer,
    IneligiblePlayer,
    AlreadyDrafted,
    ChoiceNotSelected,
    CaptainDisconnected,
};

struct DraftResult {
    DraftError error{DraftError::None};
    bool changed{false};
    std::vector<Effect> effects;

    [[nodiscard]] bool ok() const noexcept { return error == DraftError::None; }
};

enum class ReadyError : std::uint8_t {
    None,
    ScrimDisabled,
    WrongPhase,
    InvalidPlayerId,
    UnknownPlayer,
    NotCaptain,
    CaptainDisconnected,
};

struct ReadyResult {
    ReadyError error{ReadyError::None};
    bool changed{false};
    std::vector<Effect> effects;

    [[nodiscard]] bool ok() const noexcept { return error == ReadyError::None; }
};

enum class MatchConfigurationError : std::uint8_t {
    None,
    ScrimDisabled,
    WrongPhase,
    InvalidValue,
};

struct MatchConfigurationResult {
    MatchConfigurationError error{MatchConfigurationError::None};
    bool changed{false};

    [[nodiscard]] bool ok() const noexcept { return error == MatchConfigurationError::None; }
};

enum class RoundOutcome : std::uint8_t { Ignored, Counted, HalfComplete, Ambiguous };

struct RoundResult {
    RoundOutcome outcome{RoundOutcome::Ignored};
    std::optional<LogicalTeam> winning_team;
    std::vector<Effect> effects;
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
    [[nodiscard]] DraftResult set_draft_type(DraftType type);
    [[nodiscard]] DraftResult choose_draft_player(std::string player_id,
                                                  bool admin_override = false);
    [[nodiscard]] DraftResult confirm_draft_player(bool admin_override = false);
    [[nodiscard]] ReadyResult set_captain_ready(std::string captain_player_id, bool ready,
                                                bool admin_override = false);
    [[nodiscard]] TransitionResult live_on_three_restart_completed();
    [[nodiscard]] TransitionResult start_halftime_live_on_three(bool admin_override = false);
    [[nodiscard]] MatchConfigurationResult set_regulation_rounds_per_half(int rounds);
    [[nodiscard]] RoundResult regulation_round_ended(std::optional<Side> winning_side);
    [[nodiscard]] RoundResult force_regulation_round_winner(LogicalTeam winning_team);

  private:
    [[nodiscard]] static bool is_legal_transition(Phase from, Phase to) noexcept;
    [[nodiscard]] static std::string normalize_player_id(std::string player_id);
    [[nodiscard]] TeamState& mutable_team(LogicalTeam team) noexcept;
    void commit_captains();
    void append_knife_reconciliation_effects(TransitionResult& result) const;
    void append_draft_reconciliation_effects(std::vector<Effect>& effects) const;
    void initialize_draft();
    void advance_draft_turn();
    [[nodiscard]] RoundResult count_regulation_round(LogicalTeam winning_team);
    [[nodiscard]] TransitionResult begin_live_on_three(Phase target);
    [[nodiscard]] bool is_knife_phase() const noexcept;
    [[nodiscard]] bool are_team_changes_locked() const noexcept;
    [[nodiscard]] TransitionResult require_knife_replay();

    MatchState state_{};
};

} // namespace scrimmod::core
