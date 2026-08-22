#pragma once

#include "server_apis.hpp"

namespace scrimmod::plugin {

struct ApiProbeResult {
    ApiError error{ApiError::None};
    int major{0};
    int minor{0};
};

[[nodiscard]] ApiProbeResult probe_rehlds_api(void* module) noexcept;
[[nodiscard]] ApiProbeResult probe_regamedll_api(void* module) noexcept;
void reset_rehlds_api() noexcept;
void reset_regamedll_api() noexcept;
[[nodiscard]] bool add_rehlds_cvar_listener(const char* name, CvarListener listener) noexcept;
void remove_rehlds_cvar_listener(const char* name, CvarListener listener) noexcept;

} // namespace scrimmod::plugin
