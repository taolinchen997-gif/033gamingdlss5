#include "pch.h"
#include "Core033.h"
#include "../../../../src/nr_fault.h"
#include <Config.h>
#include <proxies/NVNGX_Proxy.h>
#include <fstream>

namespace Core033 {
static k033core::Ownership ownership;
static std::atomic<uint64_t> offered{0},processed{0},withheld{0};
static std::atomic<bool> menuRequested{false};
static uint32_t Provider(){
    // The integrated product selects one DX12 renderer. The upstream renderer
    // remains in source for comparison; an old config must not start it beside
    // the 033 controls or reactivate frame-count-based retirement/capture.
    return 0u;
}
uint32_t Owner(){return ownership.get();}
static HMODULE HostModule(){
#ifdef K033_MONOLITHIC_ENGINE
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(&Owner),&module);
    return module;
#else
    return GetModuleHandleW(L"dlss5-033.addon64");
#endif
}
bool EmbeddedUiOwned(){
    auto mod=HostModule();
    auto query=mod?reinterpret_cast<int(__cdecl*)()>(GetProcAddress(mod,"K033_HasEmbeddedUi")):nullptr;
    return query && query()!=0;
}
bool RequestCapture(){
    auto module=HostModule();
    auto request=module?reinterpret_cast<void(__cdecl*)()>(GetProcAddress(module,"K033_RequestCapture")):nullptr;
    if(!request || Owner()!=k033core::Host033)return false;
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(request),&pin))return false;
    request();return true;
}
bool Claim(uint32_t owner){if(nrfault033::Blocked())return false;if(!k033core::OwnerAllowed(Provider(),owner))return false;return ownership.claim(owner);}
bool UsesHost(){return Provider()==0;}
bool ConsumeMenuRequest(){return menuRequested.exchange(false);}
const nrcontrolsabi::Api* Controls(){
    static std::atomic<const nrcontrolsabi::Api*> cache{nullptr};
    if(auto api=cache.load())return api;
    auto module=HostModule();
    auto get=module?reinterpret_cast<nrcontrolsabi::GetApi>(GetProcAddress(module,"K033_GetNrControls")):nullptr;
    auto api=get?get(nrcontrolsabi::Version):nullptr;
    if(!api || api->size!=sizeof(*api) || api->version!=nrcontrolsabi::Version || !api->read || !api->set || !api->action || !api->setMfg)return nullptr;
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(get),&pin))return nullptr;
    cache.store(api);return api;
}
static int Invoke(k033core::AfterUpscale cb,const k033core::Frame* frame){
    if(nrfault033::Blocked())return 0;
    __try{return cb(frame);}__except(EXCEPTION_EXECUTE_HANDLER){
        nrfault033::Record(GetExceptionCode(),nrfault033::Site::Callback);return 0;}
}
bool OfferDx12(ID3D12GraphicsCommandList* command,NVSDK_NGX_Parameter* params,ID3D12CommandQueue* queue){
    if(nrfault033::Blocked()){++withheld;return true;}
    if(!k033core::FirstOffer()){++withheld;return true;}
    if(Provider()==1){if(!Claim(k033core::CoreDx12)){++withheld;return true;}return false;}
    ++offered;
    // The add-on may be loaded after this core. Retry on following real frames;
    // never silently start a competing model while waiting for it.
    static std::atomic<k033core::AfterUpscale> callback{nullptr};
    auto cb=callback.load();
    if(!cb){auto addon=HostModule();
        if(addon){cb=reinterpret_cast<k033core::AfterUpscale>(GetProcAddress(addon,"K033_AfterUpscale"));
            HMODULE pinned=nullptr;if(cb && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(cb),&pinned))callback.store(cb);else cb=nullptr;}}
    if(!cb){++withheld;return true;}
    const auto& c=k033core::context;k033core::Frame frame;
    frame.command=command;frame.parameters=params;frame.queue=queue;frame.stream=c.stream;
    frame.feature=c.info.feature;frame.flags=c.info.flags;frame.haveFlags=c.info.haveFlags;
    frame.outputW=c.info.outputW;frame.outputH=c.info.outputH;frame.source=c.source;
#ifdef K033_BETA2_RESHADE_HOST
    frame.featureGeneration=c.info.epoch?c.info.epoch->generation:0;
    frame.evaluateEpoch=c.evaluation;frame.current=k033core::CurrentFrame;
#endif
    if(k033core::Valid(&frame) && Invoke(cb,&frame))++processed;else ++withheld;
    return true;
}
static int __cdecl ApiClaim(uint32_t owner){return Claim(owner)?1:0;}
static void __cdecl ApiStatus(k033core::Status* s){if(!s || s->size!=sizeof(*s)||s->version!=k033core::Version)return;
    s->owner=ownership.get();s->provider=Provider();s->offered=offered.load();s->processed=processed.load();s->withheld=withheld.load();}
static void __cdecl ApiMenu(){menuRequested=true;}
static void __cdecl ApiEnabled(int enabled){Config::Instance()->DlssNrEnabled.set_volatile_value(enabled!=0);}
static void* CapabilitiesImpl(void* rawDevice){
    if(nrfault033::Blocked())return nullptr;
    if(!rawDevice || ownership.get()!=k033core::Host033)return nullptr;
    static std::mutex mutex;
    static std::unordered_map<ID3D12Device*,NVSDK_NGX_Parameter*> capabilities;
    std::lock_guard<std::mutex> lock(mutex);
    if(nrfault033::Blocked())return nullptr;
    auto device=static_cast<ID3D12Device*>(rawDevice);
    auto it=capabilities.find(device);if(it!=capabilities.end())return it->second;
    // The NR owner is fixed; retain this small capability object and its device
    // until process exit rather than exposing a dangling cross-module pointer.
    if(capabilities.size()>=4 || !NVNGXProxy::InitDx12(device))return nullptr;
    auto get=NVNGXProxy::D3D12_GetCapabilityParameters();NVSDK_NGX_Parameter* p=nullptr;
    if(!get || get(&p)!=NVSDK_NGX_Result_Success || !p)return nullptr;
    device->AddRef();capabilities.emplace(device,p);return p;
}
static void* __cdecl ApiCapabilities(void* rawDevice){
    if(nrfault033::Blocked())return nullptr;
    __try{return CapabilitiesImpl(rawDevice);}__except(EXCEPTION_EXECUTE_HANDLER){
        nrfault033::Record(GetExceptionCode(),nrfault033::Site::Capabilities);return nullptr;}
}
}
extern "C" __declspec(dllexport) const k033core::Api* __cdecl K033_GetRenderCore(uint32_t version){
    static const k033core::Api api{sizeof(k033core::Api),k033core::Version,Core033::ApiClaim,Core033::ApiStatus,Core033::ApiMenu,Core033::ApiEnabled,Core033::ApiCapabilities};
    return version==k033core::Version?&api:nullptr;
}
extern "C" __declspec(dllexport) const nrcontrolsabi::Api* __cdecl K033_GetControlEndpoint(){return Core033::Controls();}
