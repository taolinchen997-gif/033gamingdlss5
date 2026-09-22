// Explicit ReShade runtime for the game-facing virtual backbuffers. The
// internal AMD presentation chain deliberately uses native D3D objects and
// therefore cannot depend on ReShade's automatic DXGI proxy attachment.
#pragma once
#include <Psapi.h>
#include <string>
#include "diagnostic_policy.h"
#include "../sdk/reshade-6.8.0/include/reshade_api.hpp"
namespace ufg033 {
class ReShadeSurface {
    using Create=bool(*)(reshade::api::device_api,void*,void*,void*,const char*,reshade::api::effect_runtime**);
    using RuntimeCall=void(*)(reshade::api::effect_runtime*);
    using Pump=void(__cdecl*)(void*,uint32_t);
    reshade::api::effect_runtime* runtime=nullptr;RuntimeCall destroy=nullptr,update=nullptr;Pump pump=nullptr;
    bool attempted=false;uint64_t frames=0;
public:
    bool Attach(ID3D12Device* device,ID3D12CommandQueue* queue,IDXGISwapChain4* swap){
        if(runtime)return true;if(attempted)return false;
        HMODULE module=nullptr;
#ifdef K033_BETA2_RESHADE_HOST
        // Controlled package layout: game/dxgi.dll + game/033-runtime/core.
        // Never bind this product surface to an unrelated ReShade in memory.
        HMODULE component=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&FindOwner),&component))return false;
        wchar_t componentPath[32768]{};
        const DWORD pathLength=GetModuleFileNameW(component,componentPath,32768);
        if(!pathLength||pathLength>=32768)return false;
        std::wstring hostPath(componentPath,pathLength);
        auto slash=hostPath.find_last_of(L"\\/");if(slash==std::wstring::npos)return false;
        hostPath.resize(slash);slash=hostPath.find_last_of(L"\\/");if(slash==std::wstring::npos)return false;
        hostPath.resize(slash+1);hostPath+=L"dxgi.dll";
        module=GetModuleHandleW(hostPath.c_str());
#else
        HMODULE modules[1024]{};DWORD bytes=0;
        if(!K32EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes))return false;
        for(size_t i=0;i<std::min(size_t(bytes)/sizeof(HMODULE),size_t(1024));++i)
            if(GetProcAddress(modules[i],"ReShadeCreateEffectRuntime")){module=modules[i];break;}
#endif
        if(!module)return false;attempted=true;
        auto create=reinterpret_cast<Create>(GetProcAddress(module,"ReShadeCreateEffectRuntime"));
        destroy=reinterpret_cast<RuntimeCall>(GetProcAddress(module,"ReShadeDestroyEffectRuntime"));
        update=reinterpret_cast<RuntimeCall>(GetProcAddress(module,"ReShadeUpdateAndPresentEffectRuntime"));
        if(!create||!destroy||!update)return false;
        wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))return false;std::wstring wide=exe;
        wide.resize(wide.find_last_of(L"\\/")+1);wide+=L"ReShade.ini";
        int size=WideCharToMultiByte(CP_UTF8,0,wide.c_str(),-1,nullptr,0,nullptr,nullptr);std::string path(size,'\0');
        WideCharToMultiByte(CP_UTF8,0,wide.c_str(),-1,path.data(),size,nullptr,nullptr);
        HMODULE owner=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&FindOwner),&owner);
        pump=reinterpret_cast<Pump>(GetProcAddress(owner,"K033_UniversalPresent"));
        if(!create(reshade::api::device_api::d3d12,device,queue,swap,path.c_str(),&runtime)||!runtime){Trace("reshade-runtime-create-failed");return false;}
        Trace("reshade-runtime-attached-Home");return true;
    }
    void Present(reshade::api::color_space space){if(!runtime)return;
        runtime->set_color_space(space);
        const bool startup=diagnostic033::StartupTraces && frames<8;
        if(startup)Trace("startup-control-pump-begin");
        if(pump)pump(runtime,static_cast<uint32_t>(space));
        if(startup)Trace("startup-control-pump-end-runtime-update-begin");
        // This draws effects and the Home overlay into the current virtual real
        // frame and flushes the runtime queue. It does not call DXGI Present.
        update(runtime);if(startup)Trace("startup-runtime-update-returned");if(++frames==1)Trace("reshade-runtime-first-draw");}
    void Reset(){if(runtime){destroy(runtime);runtime=nullptr;Trace("reshade-runtime-released");}attempted=false;frames=0;}
private:
    static void FindOwner(){}
};
}
