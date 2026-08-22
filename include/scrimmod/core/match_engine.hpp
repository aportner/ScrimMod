#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "scrimmod/core/match_state.hpp"

namespace scrimmod::core {

enum class EffectType : std::uint8_t {
    ExecutePregameConfig,
};

struct Effect {
    EffectType type;
};

enum class TransitionError : std::uint8_t {
    None,
    ScrimDisabled,
    IllegalTransition,
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
    InvalidSteamId,
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
    InvalidSteamId,
    UnknownPlayer,
};

struct EligibilityResult {
    EligibilityError error{EligibilityError::None};
    bool changed{false};

    [[nodiscard]] bool ok() const noexcept { return error == EligibilityError::None; }
};

class MatchEngine final {
  public:
    [[nodiscard]] const MatchState& state() const noexcept;

    [[nodiscard]] TransitionResult set_enabled(bool enabled);
    [[nodiscard]] TransitionResult transition_to(Phase target);
    [[nodiscard]] PlayerUpdateResult player_connected(std::string steam_id, std::string name);
    [[nodiscard]] PlayerUpdateResult player_disconnected(std::string steam_id);
    [[nodiscard]] EligibilityResult capture_eligible_players();
    [[nodiscard]] EligibilityResult add_eligible_player(std::string steam_id);
    [[nodiscard]] EligibilityResult remove_eligible_player(std::string steam_id);

  private:
    [[nodiscard]] static bool is_legal_transition(Phase from, Phase to) noexcept;
    [[nodiscard]] static std::string normalize_steam_id(std::string steam_id);

    MatchState state_{};
};

} // namespace scrimmod::core
