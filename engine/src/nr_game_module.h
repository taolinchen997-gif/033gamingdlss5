#pragma once
#include <Windows.h>
#include <string>
#include "nr_game_discovery.h"
namespace nrgame033 {
inline HMODULE FindGameModule(const wchar_t* name){
    // Both the proxy and System32 can have the same basename. Select the
    // already loaded module by its complete game-local path; never LoadLibrary.
    wchar_t exe[512]{};const DWORD size=GetModuleFileNameW(nullptr,exe,512);
    if(!size||size>=512)return nullptr;
    std::wstring path(exe,size);const auto slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return nullptr;
    path.resize(slash+1);path+=name;return GetModuleHandleW(path.c_str());
}
inline ExportMatch FindGameExport(const char* symbol,const char* companion){
    return UniqueExport([&](const wchar_t* name)->ExportMatch{
        auto module=FindGameModule(name);if(!module)return {};
        auto address=GetProcAddress(module,symbol),other=GetProcAddress(module,companion);
        if(!address||!other)return {};
        // Reject forwarded exports and System32 lookalikes. Neither exports nor
        // basename alone prove that this is the intended game-local component.
        HMODULE owner=nullptr,otherOwner=nullptr;
        const auto flags=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if(!GetModuleHandleExW(flags,reinterpret_cast<LPCWSTR>(address),&owner)||owner!=module||
           !GetModuleHandleExW(flags,reinterpret_cast<LPCWSTR>(other),&otherOwner)||otherOwner!=module)return {};
        return {uintptr_t(module),uintptr_t(address),false};
    });
}
inline bool PinGameExport(const ExportMatch& match){
    HMODULE pinned=nullptr;
    return match.module&&match.address&&!match.ambiguous&&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(match.address),&pinned)&&uintptr_t(pinned)==match.module;
}
}
