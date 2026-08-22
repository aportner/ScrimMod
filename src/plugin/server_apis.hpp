#pragma once

namespace scrimmod::plugin {

enum class ApiError {
    None,
    MissingGameDllPath,
    RehldsModuleUnavailable,
    RehldsFactoryUnavailable,
    RehldsInterfaceUnavailable,
    RehldsVersionMismatch,
    RehldsServicesUnavailable,
    RegamedllModuleUnavailable,
    RegamedllFactoryUnavailable,
    RegamedllInterfaceUnavailable,
    RegamedllVersionMismatch,
    RegamedllServicesUnavailable,
};

struct ApiStatus {
    ApiError error{ApiError::None};
    int rehlds_major{0};
    int rehlds_minor{0};
    int regamedll_major{0};
    int regamedll_minor{0};

    [[nodiscard]] bool ok() const noexcept { return error == ApiError::None; }
};

using CvarListener = void (*)(const char* new_value);

[[nodiscard]] ApiStatus initialize_server_apis(const char* game_dll_path) noexcept;
void shutdown_server_apis() noexcept;
[[nodiscard]] const char* api_error_message(ApiError error) noexcept;
[[nodiscard]] bool add_cvar_listener(const char* name, CvarListener listener) noexcept;
void remove_cvar_listener(const char* name, CvarListener listener) noexcept;

} // namespace scrimmod::plugin
