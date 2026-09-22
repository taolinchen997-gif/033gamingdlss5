#pragma once
#include <windows.h>
#include <cwchar>

namespace diagnostic033 {
// The user stopped the escalating diagnostic builds after a dump located
// contention in forced DRED instrumentation. No new allocation tracer is used.
inline constexpr bool StartupTraces=false;
inline constexpr bool FaultCollection=false;
// Optional query heaps, timestamp commands and readbacks are diagnostic work,
// not NR/FG resource-lifetime fences. Apply the same default to every game.
inline constexpr bool TimingCollection=false;
inline bool IsRe4(){
    static const bool result=[] {
        wchar_t path[1024]{};const auto length=GetModuleFileNameW(nullptr,path,1024);
        if(!length||length>=1024)return false;
        const auto* name=wcsrchr(path,L'\\');return _wcsicmp(name?name+1:path,L"re4.exe")==0;
    }();return result;
}
inline bool TimingAllowed(bool requested,bool /*re4*/){return requested&&TimingCollection;}
inline bool TimingAllowed(bool requested){return requested&&TimingCollection;}
}
