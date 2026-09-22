#pragma once
#include "render_core_abi.h"
#include <atomic>
namespace rendercore {
inline std::atomic<const k033core::Api*> api_ptr{nullptr};
inline const k033core::Api* Api(){return api_ptr.load();}
inline thread_local bool in_callback=false;
inline bool Detect(){
    if(Api())return true;
    // Only inspect already loaded modules. Never load a proxy inside DllMain.
    const wchar_t* names[]={L"033-engine.dll",L"033-render-core.dll",L"OptiScaler.dll",L"winmm.dll",L"version.dll",L"dinput8.dll",L"dxgi.dll",L"wininet.dll",L"dbghelp.dll",L"OptiScaler.asi"};
    for(auto name:names){auto mod=GetModuleHandleW(name);if(!mod)continue;
        auto get=reinterpret_cast<k033core::GetApi>(GetProcAddress(mod,"K033_GetRenderCore"));
        if(!get)continue;const auto* candidate=get(k033core::Version);
        if(!candidate || candidate->version!=k033core::Version || candidate->size!=sizeof(k033core::Api)
            || !candidate->claim || !candidate->status || !candidate->showMenu || !candidate->setEnabled || !candidate->capabilities)continue;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(get),&pinned))return false;
        api_ptr.store(candidate);return true;
    }return false;
}
inline bool Integrated(){return Detect();}
inline bool AllowHost(){return !Detect() || (in_callback && Api()->claim(k033core::Host033));}
inline k033core::Status Status(){k033core::Status s;if(Detect())Api()->status(&s);return s;}
}
