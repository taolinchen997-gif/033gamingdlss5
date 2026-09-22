#pragma once
#if defined(K033_MONOLITHIC_ENGINE) && !defined(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
#error The combined renderer must use the same legacy-runtime mutex ABI as the core.
#endif
#include <Windows.h>
#include <atomic>
#include <string>
namespace engine033 {
inline std::atomic<bool> host_ready{false};
inline HMODULE Module() {
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(&Module), &module);
    return module;
}
inline std::wstring Directory() {
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(Module(),path,DWORD(sizeof(path)/sizeof(path[0])));
    if(!length||length>=sizeof(path)/sizeof(path[0]))return {};
    std::wstring result(path,length);const auto cut=result.find_last_of(L"\\/");
    if(cut==std::wstring::npos)return {};result.resize(cut);return result;
}
}
