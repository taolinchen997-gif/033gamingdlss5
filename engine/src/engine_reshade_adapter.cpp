// Registration adapter only. Rendering, configuration, hooks and all controls
// reside in the one 033 engine DLL. This module owns no renderer or state.
#include <Windows.h>
extern "C" __declspec(dllexport) const char* NAME="热心网友033";
extern "C" __declspec(dllexport) const char* DESCRIPTION="033 引擎的 ReShade 接入器";
using Entry=BOOL(WINAPI*)(HMODULE,DWORD,LPVOID);
static Entry entry=nullptr;
static bool attached=false;
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID reserved) {
    if(reason==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(module);
        // Never guess/load a second engine. The early game proxy already owns it.
        const wchar_t* names[]={L"winmm.dll",L"version.dll",L"dinput8.dll",L"wininet.dll",L"dbghelp.dll",L"033-engine.dll"};
        for(auto name:names){
            auto engine=GetModuleHandleW(name);if(!engine)continue;
            entry=reinterpret_cast<Entry>(GetProcAddress(engine,"K033_ReShadeEntry"));
            if(entry){attached=entry(module,reason,reserved)!=FALSE;return attached?TRUE:FALSE;}
        }
        return FALSE;
    }
    if(reason==DLL_PROCESS_DETACH && attached && entry){entry(module,reason,reserved);attached=false;}
    return TRUE;
}
