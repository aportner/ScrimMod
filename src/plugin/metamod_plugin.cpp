#include <cstring>

#include <extdll.h>
#include <meta_api.h>

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals = nullptr;
meta_globals_t* gpMetaGlobals = nullptr;
gamedll_funcs_t* gpGamedllFuncs = nullptr;
mutil_funcs_t* gpMetaUtilFuncs = nullptr;

plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,  // ifvers
    "ScrimMod",              // name
    "0.1.0",                 // version
    __DATE__,                // date
    "ScrimMod contributors", // author
    "N/A",                   // url
    "SCRIMMOD",              // logtag
    PT_ANYTIME,              // loadable
    PT_ANYTIME,              // unloadable
};

namespace {

DLL_FUNCTIONS g_dll_functions{};
META_FUNCTIONS g_meta_functions{};

void server_print(const char* message) {
    if (g_engfuncs.pfnServerPrint != nullptr) {
        g_engfuncs.pfnServerPrint(message);
    }
}

} // namespace

C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t* engine_functions, globalvars_t* globals) {
    if (engine_functions != nullptr) {
        std::memcpy(&g_engfuncs, engine_functions, sizeof(g_engfuncs));
    }
    gpGlobals = globals;
}

C_DLLEXPORT int Meta_Query(char*, plugin_info_t** plugin_info, mutil_funcs_t* meta_util_functions) {
    if (plugin_info == nullptr || meta_util_functions == nullptr) {
        return FALSE;
    }

    *plugin_info = &Plugin_info;
    gpMetaUtilFuncs = meta_util_functions;
    return TRUE;
}

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS* function_table, int* interface_version) {
    if (function_table == nullptr || interface_version == nullptr) {
        return FALSE;
    }
    if (*interface_version != INTERFACE_VERSION) {
        *interface_version = INTERFACE_VERSION;
        return FALSE;
    }

    std::memcpy(function_table, &g_dll_functions, sizeof(g_dll_functions));
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS* function_table,
                            meta_globals_t* meta_globals, gamedll_funcs_t* gamedll_functions) {
    if (now > Plugin_info.loadable || function_table == nullptr || meta_globals == nullptr ||
        gamedll_functions == nullptr) {
        return FALSE;
    }

    gpMetaGlobals = meta_globals;
    gpGamedllFuncs = gamedll_functions;
    g_meta_functions.pfnGetEntityAPI2 = GetEntityAPI2;
    std::memcpy(function_table, &g_meta_functions, sizeof(g_meta_functions));

    server_print("[ScrimMod] Empty plugin scaffold loaded.\n");
    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason) {
    if (now > Plugin_info.unloadable && reason != PNL_CMD_FORCED) {
        return FALSE;
    }

    server_print("[ScrimMod] Empty plugin scaffold unloaded.\n");
    gpMetaGlobals = nullptr;
    gpGamedllFuncs = nullptr;
    return TRUE;
}
