#include "scrimmod/core/match_engine.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace scrimmod::core {

const MatchState& MatchEngine::state() const noexcept { return state_; }

TransitionResult MatchEngine::set_enabled(const bool enabled) {
    TransitionResult result{};
    if (enabled) {
        if (!state_.enabled_) {
            state_.reset();
            state_.enabled_ = true;
            state_.phase_ = Phase::CaptainSelection;
            result.changed = true;
        }
        return result;
    }

    result.changed = state_.enabled_ || state_.phase_ != Phase::Disabled;
    state_.reset();
    result.effects.push_back({EffectType::ExecutePregameConfig});
    return result;
}

TransitionResult MatchEngine::transition_to(const Phase target) {
    TransitionResult result{};
    if (!state_.enabled_) {
        result.error = TransitionError::ScrimDisabled;
        return result;
    }
    if (target == state_.phase_) {
        return result;
    }
    if (!is_legal_transition(state_.phase_, target)) {
        result.error = TransitionError::IllegalTransition;
        return result;
    }

    state_.phase_ = target;
    result.changed = true;
    return result;
}

PlayerUpdateResult MatchEngine::player_connected(std::string steam_id, std::string name) {
    PlayerUpdateResult result{};
    if (!state_.enabled_) {
        result.error = PlayerUpdateError::ScrimDisabled;
        return result;
    }

    steam_id = normalize_steam_id(std::move(steam_id));
    if (steam_id.empty()) {
        result.error = PlayerUpdateError::InvalidSteamId;
        return result;
    }

    auto [player_it, inserted] = state_.players_.try_emplace(steam_id);
    if (inserted) {
        player_it->second.steam_id = steam_id;
        player_it->second.last_known_name = std::move(name);
        player_it->second.connected = true;
        result.changed = true;
        return result;
    }

    Player& player = player_it->second;
    if (!name.empty() && player.last_known_name != name) {
        player.last_known_name = std::move(name);
        result.changed = true;
    }
    if (!player.connected) {
        player.connected = true;
        result.changed = true;
    }
    return result;
}

PlayerUpdateResult MatchEngine::player_disconnected(std::string steam_id) {
    PlayerUpdateResult result{};
    if (!state_.enabled_) {
        result.error = PlayerUpdateError::ScrimDisabled;
        return result;
    }

    steam_id = normalize_steam_id(std::move(steam_id));
    if (steam_id.empty()) {
        result.error = PlayerUpdateError::InvalidSteamId;
        return result;
    }

    const auto player_it = state_.players_.find(steam_id);
    if (player_it == state_.players_.end()) {
        result.error = PlayerUpdateError::UnknownPlayer;
        return result;
    }
    if (player_it->second.connected) {
        player_it->second.connected = false;
        result.changed = true;
    }
    return result;
}

EligibilityResult MatchEngine::capture_eligible_players() {
    EligibilityResult result{};
    if (!state_.enabled_) {
        result.error = EligibilityError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = EligibilityError::WrongPhase;
        return result;
    }
    if (state_.eligible_pool_captured_) {
        return result;
    }

    state_.eligible_players_.reserve(state_.players_.size());
    for (const auto& [steam_id, player] : state_.players_) {
        if (player.connected) {
            state_.eligible_players_.push_back(steam_id);
        }
    }
    std::sort(state_.eligible_players_.begin(), state_.eligible_players_.end());
    state_.eligible_pool_captured_ = true;
    result.changed = true;
    return result;
}

EligibilityResult MatchEngine::add_eligible_player(std::string steam_id) {
    EligibilityResult result{};
    if (!state_.enabled_) {
        result.error = EligibilityError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = EligibilityError::WrongPhase;
        return result;
    }
    if (!state_.eligible_pool_captured_) {
        result.error = EligibilityError::PoolNotCaptured;
        return result;
    }

    steam_id = normalize_steam_id(std::move(steam_id));
    if (steam_id.empty()) {
        result.error = EligibilityError::InvalidSteamId;
        return result;
    }
    if (state_.players_.find(steam_id) == state_.players_.end()) {
        result.error = EligibilityError::UnknownPlayer;
        return result;
    }

    const auto position = std::lower_bound(state_.eligible_players_.begin(),
                                           state_.eligible_players_.end(), steam_id);
    if (position == state_.eligible_players_.end() || *position != steam_id) {
        state_.eligible_players_.insert(position, std::move(steam_id));
        result.changed = true;
    }
    return result;
}

EligibilityResult MatchEngine::remove_eligible_player(std::string steam_id) {
    EligibilityResult result{};
    if (!state_.enabled_) {
        result.error = EligibilityError::ScrimDisabled;
        return result;
    }
    if (state_.phase_ != Phase::CaptainSelection) {
        result.error = EligibilityError::WrongPhase;
        return result;
    }
    if (!state_.eligible_pool_captured_) {
        result.error = EligibilityError::PoolNotCaptured;
        return result;
    }

    steam_id = normalize_steam_id(std::move(steam_id));
    if (steam_id.empty()) {
        result.error = EligibilityError::InvalidSteamId;
        return result;
    }
    if (state_.players_.find(steam_id) == state_.players_.end()) {
        result.error = EligibilityError::UnknownPlayer;
        return result;
    }

    const auto position = std::lower_bound(state_.eligible_players_.begin(),
                                           state_.eligible_players_.end(), steam_id);
    if (position != state_.eligible_players_.end() && *position == steam_id) {
        state_.eligible_players_.erase(position);
        result.changed = true;
    }
    return result;
}

bool MatchEngine::is_legal_transition(const Phase from, const Phase to) noexcept {
    switch (from) {
    case Phase::Disabled:
        return false;
    case Phase::CaptainSelection:
        return to == Phase::KnifeSetup;
    case Phase::KnifeSetup:
        return to == Phase::KnifeLive;
    case Phase::KnifeLive:
        return to == Phase::KnifeComplete || to == Phase::KnifeSetup;
    case Phase::KnifeComplete:
        return to == Phase::SideOrPick;
    case Phase::SideOrPick:
        return to == Phase::Draft;
    case Phase::Draft:
        return to == Phase::Ready;
    case Phase::Ready:
        return to == Phase::RegulationFirstHalf;
    case Phase::RegulationFirstHalf:
        return to == Phase::Halftime;
    case Phase::Halftime:
        return to == Phase::RegulationSecondHalf;
    case Phase::RegulationSecondHalf:
        return to == Phase::OvertimeFirstHalf || to == Phase::MatchComplete;
    case Phase::OvertimeFirstHalf:
        return to == Phase::OvertimeHalftime;
    case Phase::OvertimeHalftime:
        return to == Phase::OvertimeSecondHalf;
    case Phase::OvertimeSecondHalf:
        return to == Phase::OvertimeFirstHalf || to == Phase::MatchComplete;
    case Phase::MatchComplete:
        return false;
    }
    return false;
}

std::string MatchEngine::normalize_steam_id(std::string steam_id) {
    const auto is_space = [](const unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto first = std::find_if_not(steam_id.begin(), steam_id.end(), is_space);
    const auto last = std::find_if_not(steam_id.rbegin(), steam_id.rend(), is_space).base();
    if (first >= last) {
        return {};
    }

    std::string normalized(first, last);
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
    return normalized;
}

} // namespace scrimmod::core
