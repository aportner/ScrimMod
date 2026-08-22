#include "api_probe.hpp"

#include <dlfcn.h>

#include <extdll.h>
#include <meta_api.h>
#include <rehlds_api.h>

namespace scrimmod::plugin {
namespace {

IRehldsApi* g_rehlds_api = nullptr;

} // namespace

ApiProbeResult probe_rehlds_api(void* module) noexcept {
    ApiProbeResult result{};
    auto factory = reinterpret_cast<CreateInterfaceFn>(dlsym(module, CREATEINTERFACE_PROCNAME));
    if (factory == nullptr) {
        result.error = ApiError::RehldsFactoryUnavailable;
        return result;
    }

    int factory_result = IFACE_FAILED;
    g_rehlds_api =
        reinterpret_cast<IRehldsApi*>(factory(VREHLDS_HLDS_API_VERSION, &factory_result));
    if (g_rehlds_api == nullptr) {
        result.error = ApiError::RehldsInterfaceUnavailable;
        return result;
    }

    result.major = g_rehlds_api->GetMajorVersion();
    result.minor = g_rehlds_api->GetMinorVersion();
    if (result.major != REHLDS_API_VERSION_MAJOR || result.minor < REHLDS_API_VERSION_MINOR) {
        result.error = ApiError::RehldsVersionMismatch;
        reset_rehlds_api();
        return result;
    }

    if (g_rehlds_api->GetFuncs() == nullptr || g_rehlds_api->GetHookchains() == nullptr ||
        g_rehlds_api->GetServerData() == nullptr || g_rehlds_api->GetServerStatic() == nullptr) {
        result.error = ApiError::RehldsServicesUnavailable;
        reset_rehlds_api();
    }
    return result;
}

void reset_rehlds_api() noexcept { g_rehlds_api = nullptr; }

bool add_rehlds_cvar_listener(const char* name, CvarListener listener) noexcept {
    if (g_rehlds_api == nullptr || g_rehlds_api->GetFuncs() == nullptr || name == nullptr ||
        listener == nullptr) {
        return false;
    }
    g_rehlds_api->GetFuncs()->AddCvarListener(name, listener);
    return true;
}

void remove_rehlds_cvar_listener(const char* name, CvarListener listener) noexcept {
    if (g_rehlds_api != nullptr && g_rehlds_api->GetFuncs() != nullptr && name != nullptr &&
        listener != nullptr) {
        g_rehlds_api->GetFuncs()->RemoveCvarListener(name, listener);
    }
}

} // namespace scrimmod::plugin
