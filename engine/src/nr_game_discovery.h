#pragma once
#include <cstdint>
namespace nrgame033 {
// Finite game-local mount inventory, shared by host and semantic adapters.
// A filename is only a lookup location: both required exports identify a role.
inline constexpr const wchar_t* ModuleNames[]={L"winmm.dll",L"version.dll",L"dinput8.dll",
    L"wininet.dll",L"dbghelp.dll",L"dxgi.dll",L"033-render-core.dll",L"OptiScaler.dll",L"OptiScaler.asi"};
struct ExportMatch {uintptr_t module=0,address=0;bool ambiguous=false;};
template<class Lookup> ExportMatch UniqueExport(Lookup lookup){
    ExportMatch found;
    for(auto name:ModuleNames){
        const auto candidate=lookup(name);
        if(!candidate.module||!candidate.address)continue;
        if(found.module&&found.module!=candidate.module)return {0,0,true};
        found=candidate;
    }
    return found;
}
}
