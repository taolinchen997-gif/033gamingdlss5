#pragma once
#include <windows.h>
namespace mfgunlock::identity {
inline HMODULE Owner(const void* code, HMODULE fallback) {
    HMODULE owner=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(code),&owner) ? owner : fallback;
}
inline bool IsPlugin(HMODULE candidate,HMODULE implementation) {
    // Markers also occur in 033's own diagnostics. A Streamline plugin must
    // expose its function table as well as containing the DLSS-G marker.
    return candidate && candidate!=implementation &&
        GetProcAddress(candidate,"slGetPluginFunction")!=nullptr;
}
}
