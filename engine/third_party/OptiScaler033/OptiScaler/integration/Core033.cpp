#include "pch.h"
#include "Core033.h"
#include "../../../../src/nr_fault.h"
#include "../../../../src/sr_model_abi.h"
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
// S32 (owner 2026-09-24): 超分模型 on the 033 SR page. The choice rides on the
// upstream preset override (DLSSFeature::ProcessInitParams, RenderPresetForAll for
// every quality mode) as volatile values, so no OptiScaler.ini is ever written.
// A change recreates the DLSS feature once on the evaluate thread that owns it.
static std::atomic<uint32_t> srRequested{srmodelabi::GameDefault},srApplied{srmodelabi::None},srCreations{0},srPending{0},
    srMajor{0},srMinor{0},srPatch{0},srExternal{0};
static std::atomic<bool> srRecreate{false};
void RecordSrCreation(uint32_t appliedPreset,bool external){
    srApplied=appliedPreset;srExternal=external?1u:0u;++srCreations;srPending=0;
}
void RecordSrVersion(uint32_t major,uint32_t minor,uint32_t patch){srMajor=major;srMinor=minor;srPatch=patch;}
bool ConsumeSrRecreate(){return srRecreate.exchange(false);}
// S33 (owner: 「设L，就自动将游戏设置成超级性能」): M and L also force the render size the
// game is told for every quality mode (upstream UpscaleRatioOverride, volatile). The game
// takes it the next time it asks for its render size (NVSDK_NGX_DLSS_GetOptimalSettingsCallback);
// an answer computed before the latest change keeps the size "pending".
static std::atomic<uint32_t> srQueries{0},srOutW{0},srOutH{0},srRenderW{0},srRenderH{0},srGameMode{0},
    srForcedMilli{0},srRatioSeq{0},srAnsweredSeq{0};
uint32_t SrRatioSequence(){return srRatioSeq.load();}
void RecordSrQuery(uint32_t outputW,uint32_t outputH,uint32_t renderW,uint32_t renderH,uint32_t gameMode,uint32_t ratioSequence){
    srOutW=outputW;srOutH=outputH;srRenderW=renderW;srRenderH=renderH;srGameMode=gameMode;srAnsweredSeq=ratioSequence;++srQueries;
}
}
extern "C" __declspec(dllexport) int __cdecl K033_SetSrPreset(uint32_t preset,uint32_t recreate){
    if(!srmodelabi::Allowed(preset))return 0;
    auto* config=Config::Instance();
    config->RenderPresetOverride.set_volatile_value(preset!=srmodelabi::GameDefault);
    config->RenderPresetForAll.set_volatile_value(preset);
    const float ratio=srmodelabi::LinkedRatio(preset);const uint32_t milli=uint32_t(ratio*1000.0f+0.5f);
    config->UpscaleRatioOverrideEnabled.set_volatile_value(ratio>0.0f);
    if(ratio>0.0f)config->UpscaleRatioOverrideValue.set_volatile_value(ratio);
    // Bumped after the new values are in place: an answer computed from the old ones stays pending.
    if(Core033::srForcedMilli.exchange(milli)!=milli)++Core033::srRatioSeq;
    const bool changed=Core033::srRequested.exchange(preset)!=preset;
    // Before the game's first DLSS creation the values simply wait for it.
    if(recreate && changed && Core033::srCreations.load()){Core033::srPending=1;Core033::srRecreate=true;}
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl K033_GetSrPresetStatus(srmodelabi::Status* out){
    if(!out || out->size!=sizeof(srmodelabi::Status) || out->version!=2)return 0;
    out->requested=Core033::srRequested.load();out->applied=Core033::srApplied.load();out->creations=Core033::srCreations.load();
    out->pending=Core033::srPending.load();out->major=Core033::srMajor.load();out->minor=Core033::srMinor.load();out->patch=Core033::srPatch.load();
    out->external=Core033::srExternal.load();
    out->queries=Core033::srQueries.load();out->outputW=Core033::srOutW.load();out->outputH=Core033::srOutH.load();
    out->renderW=Core033::srRenderW.load();out->renderH=Core033::srRenderH.load();out->gameMode=Core033::srGameMode.load();
    out->forcedMilli=Core033::srForcedMilli.load();out->sizePending=Core033::srRatioSeq.load()!=Core033::srAnsweredSeq.load()?1u:0u;
    return 1;
}
extern "C" __declspec(dllexport) const k033core::Api* __cdecl K033_GetRenderCore(uint32_t version){
    static const k033core::Api api{sizeof(k033core::Api),k033core::Version,Core033::ApiClaim,Core033::ApiStatus,Core033::ApiMenu,Core033::ApiEnabled,Core033::ApiCapabilities};
    return version==k033core::Version?&api:nullptr;
}
extern "C" __declspec(dllexport) const nrcontrolsabi::Api* __cdecl K033_GetControlEndpoint(){return Core033::Controls();}
