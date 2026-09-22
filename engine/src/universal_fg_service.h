#pragma once
#include <fstream>
#include <sstream>
#include "universal_fg_swapchain.h"
#include "config_store.h"
#ifdef K033_BETA2_RESHADE_HOST
#include "beta2_fg_intent.h"
#endif
namespace ufg033 {
inline std::string ConfigPath(){return fgroute033::ConfigPath();}
inline bool Prepared(){return fgroute033::UniversalBoot();}
inline std::once_flag loadMultiplier;
inline void LoadMultiplier(){std::call_once(loadMultiplier,[]{
#ifdef K033_BETA2_RESHADE_HOST
    K033_Beta2FgSettings s;if(k033beta2::ReadFg(s))multiplier=s.universal_multiplier;
#else
    std::ifstream in(ConfigPath());std::string line;
    while(std::getline(in,line))if(line.rfind("imagefgmultiplier=",0)==0)multiplier=ufgpolicy033::Multiplier(std::atoi(line.c_str()+18));
#endif
});}
inline int __cdecl SetMultiplier(int value){if(value!=2&&value!=3)return 0;LoadMultiplier();std::lock_guard<std::mutex> lock(fgroute033::settingsMutex);
#ifdef K033_BETA2_RESHADE_HOST
    if(!k033beta2::SetUniversalMultiplier(unsigned(value)))return 0;
#else
    const char* keys[]={"imagefgmultiplier"};char file[32768]{};GetModuleFileNameA(nullptr,file,32768);std::string path=file;path.resize(path.find_last_of("\\/")+1);path+="dlss5-033.cfg";
    if(!configstore::Update(path,keys,1,[&](FILE* f){std::fprintf(f,"imagefgmultiplier=%d\n",value);}))return 0;
#endif
    multiplier=value;return 1;}
inline int __cdecl Prepare(int on){return fgroute033::Select(on?1:0);}
inline int __cdecl SelectRoute(int route){return fgroute033::Select(route);}
inline std::wstring Library(){HMODULE owner=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Library),&owner);
    wchar_t file[32768]{};GetModuleFileNameW(owner,file,32768);std::wstring path=file;path.resize(path.find_last_of(L"\\/")+1);
#ifdef K033_BETA2_RESHADE_HOST
return path+L"033-framegen-provider.dll";
#else
return path+L"OptiScaler\\033-framegen-provider.dll";
#endif
}
inline int __cdecl Create(const ufg033abi::Create* p){if(!p||p->size!=sizeof(*p)||p->version!=ufg033abi::Version||!p->output)return E_INVALIDARG;
    if(!fgroute033::CanGenerate()){*p->output=nullptr;return E_NOINTERFACE;}
    LoadMultiplier();try{auto path=p->library?std::wstring(p->library):Library();const auto result=Swapchain::Create(static_cast<IDXGIFactory*>(p->factory),static_cast<ID3D12CommandQueue*>(p->queue),static_cast<HWND>(p->hwnd),
        static_cast<const DXGI_SWAP_CHAIN_DESC1*>(p->desc),static_cast<const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*>(p->fullscreen),path.c_str(),reinterpret_cast<IDXGISwapChain4**>(p->output));
#ifdef K033_BETA2_RESHADE_HOST
        // Restore saved intent only after the real swapchain owner exists.
        // A failed creation must never persist an automatic 'off' over it.
        {
            K033_Beta2FgSettings settings;
            if(k033beta2::ReadFg(settings)&&k033beta2::RestoreUniversalIntent(settings,
                SUCCEEDED(result)&&*p->output,owners.load(),errorCode.load(),fgroute033::UniversalBoot(),fgroute033::CanGenerate())){
                multiplier=settings.universal_multiplier;wanted=settings.universal_enabled!=0;
            }
        }
#endif
        return result;
    }catch(...){*p->output=nullptr;return E_FAIL;}}
inline int __cdecl Read(ufg033abi::Status* s){if(!s||s->size!=sizeof(*s)||s->version!=ufg033abi::Version)return 0;
    s->bootRoute=fgroute033::State().Boot();s->selectedRoute=fgroute033::State().Selected();
    s->routePending=fgroute033::State().Pending();s->routeBlocked=fgroute033::Blocked();
    LoadMultiplier();s->multiplier=multiplier.load();s->maxMultiplier=3;s->presentedReal=presentedReal.load();s->presentedGenerated=presentedGenerated.load();s->presentFailed=presentFailed.load();s->presentError=presentError.load();
    s->prepared=Prepared();s->available=owners.load();s->requested=wanted.load();s->error=errorCode.load();s->realFrames=realCount.load();s->generated=generatedCount.load();
    s->composedReal=composedReal.load();s->composedGenerated=composedGenerated.load();s->skipped=skipped.load();s->allocations=allocations.load();s->workingBytes=workingBytes.load();
    s->reason=lastReason.load();s->sdkReset=lastSdkReset.load();s->intervalMs=lastInterval.load();
    s->warmup=recordReasons[1].load();s->busy=recordReasons[2].load();s->gaps=recordReasons[3].load();s->late=recordReasons[4].load();s->flowUnavailable=recordReasons[5].load();
    std::snprintf(s->flowReason,sizeof(s->flowReason),"%s",lastFlowReason.load());return 1;}
inline void __cdecl Enable(int enabled){const bool accepted=enabled!=0&&owners.load()!=0&&errorCode.load()==0&&fgroute033::CanGenerate();
#ifdef K033_BETA2_RESHADE_HOST
    // Turning off takes effect even if persistence is momentarily busy.
    if(!enabled)wanted=false;
    if(!k033beta2::SetUniversalEnabled(accepted))return;
#endif
    wanted=accepted;
}
inline const ufg033abi::Api api{sizeof(ufg033abi::Api),ufg033abi::Version,Create,Read,Prepare,Enable,SetMultiplier,SelectRoute};
}
extern "C" __declspec(dllexport) const ufg033abi::Api* __cdecl K033_GetUniversalFramegen(uint32_t version){return version==ufg033abi::Version?&ufg033::api:nullptr;}
