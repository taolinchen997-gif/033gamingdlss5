#pragma once
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdio>
#include "universal_fg_images.h"
#include "framegen_scene.h"
#include "universal_fg_abi.h"
#include "universal_fg_policy.h"
#include "fg_route.h"
#include "nr_scene_queue.h"
#include "ufg_provider_abi.h"
#include "../third_party/OptiScaler033/external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h"
#include "../third_party/OptiScaler033/external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.h"
namespace ufg033 {
inline std::atomic<bool> wanted{false};
inline std::atomic<uint32_t> multiplier{2};
inline std::atomic<uint64_t> presentedReal{0},presentedGenerated{0},presentFailed{0};
inline std::atomic<int32_t> presentError{0};
inline std::atomic<uint32_t> owners{0},errorCode{0};
inline std::atomic<uint64_t> realCount{0},generatedCount{0},composedReal{0},composedGenerated{0},skipped{0},allocations{0},workingBytes{0};
inline std::atomic<uint64_t> resetColourCount{0},sameColourCount{0},lastFrameId{0},lastInterval{0};
inline std::atomic<uint32_t> lastReason{1},lastSdkReset{0};
inline std::atomic<const char*> lastFlowReason{"none"};
inline std::atomic<uint64_t> recordReasons[8]{};
inline std::atomic<uint64_t> normalizedDevices{0},identityExpected{0},identityActual{0};
inline std::atomic<long> identityQuery{0};
// ReShade 6.8 public COM escape hatch, verified against the release source:
// source/com_utils.hpp and d3d12/d3d12_command_queue.cpp. Every returned
// interface is owned and queried for its real type; no wrapper-layout casts.
template<class T> ComPtr<T> Native(T* object){ComPtr<T> current=object;
    constexpr GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    for(int i=0;current&&i<4;++i){ComPtr<IUnknown> raw;ComPtr<T> next;
        if(FAILED(current->QueryInterface(unwrapped,reinterpret_cast<void**>(raw.GetAddressOf())))||!raw||FAILED(raw.As(&next))||next.Get()==current.Get())break;current=next;}
    return current;}
inline void Trace(const char* event){static std::mutex logMutex;std::lock_guard<std::mutex> lock(logMutex);
    wchar_t path[32768]{};if(!GetModuleFileNameW(nullptr,path,32768))return;auto slash=wcsrchr(path,L'\\');if(!slash)return;
    if(wcscpy_s(slash+1,size_t(path+32768-slash-1),L"033-framegen.log"))return;FILE* f=nullptr;if(_wfopen_s(&f,path,L"ab")||!f)return;
    const char* reasons[]={"output","warmup","busy","frame-gap","late-frame","flow-unavailable","incompatible","lost-device"};
    std::fprintf(f,"tick=%llu %s enabled=%d error=%u real=%llu generated=%llu composedReal=%llu composedGenerated=%llu bytes=%llu reason=%s flowReason=%s id=%llu interval_ms=%llu sdkReset=%u colourChanges=%llu colourRepeats=%llu warmup=%llu busy=%llu gaps=%llu late=%llu flowUnavailable=%llu multiplier=%u presentReal=%llu presentGenerated=%llu presentFailed=%llu presentError=%ld deviceAliases=%llu deviceExpected=%llX deviceActual=%llX deviceQuery=%lX\n",
        GetTickCount64(),event,int(wanted.load()),errorCode.load(),realCount.load(),generatedCount.load(),composedReal.load(),composedGenerated.load(),workingBytes.load(),
        reasons[std::min(lastReason.load(),7u)],lastFlowReason.load(),lastFrameId.load(),lastInterval.load(),lastSdkReset.load(),resetColourCount.load(),sameColourCount.load(),
        recordReasons[1].load(),recordReasons[2].load(),recordReasons[3].load(),recordReasons[4].load(),recordReasons[5].load(),multiplier.load(),presentedReal.load(),presentedGenerated.load(),presentFailed.load(),long(presentError.load()),normalizedDevices.load(),identityExpected.load(),identityActual.load(),identityQuery.load());std::fclose(f);}
inline D3D12_RESOURCE_STATES State(uint32_t s){switch(s){
    case FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case FFX_API_RESOURCE_STATE_COMPUTE_READ:return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_READ:return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_COPY_SRC:return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case FFX_API_RESOURCE_STATE_COPY_DEST:return D3D12_RESOURCE_STATE_COPY_DEST;
    case FFX_API_RESOURCE_STATE_RENDER_TARGET:return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case FFX_API_RESOURCE_STATE_GENERIC_READ:return D3D12_RESOURCE_STATE_GENERIC_READ;
    default:return D3D12_RESOURCE_STATE_COMMON;}}
inline void Transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER d{};d.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;d.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&d);}
}
#include "universal_fg_reshade.h"
namespace ufg033 {
// Owns one SDK swapchain and its callback state. The game sees this proxy;
// internal SDK presentation goes directly to the real DXGI swapchain.
class Swapchain final:public IDXGISwapChain4 {
    std::atomic<ULONG> refs{1};std::recursive_mutex mutex;
    HMODULE provider=nullptr;const ufgprovider033::Api* providerApi=nullptr;
    IDXGISwapChain4* sdk=nullptr;ComPtr<IFrameInterpolationSwapChainDX12> stable;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    ReShadeSurface reshadeSurface;
    reshade::api::color_space colourSpace=reshade::api::color_space::srgb;
    DXGI_COLOR_SPACE_TYPE outputSpace=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    std::unique_ptr<Images> images;uint64_t serial=0,frame=0,sceneRevision=0;bool active=false,fault=false,registered=false,sawSdkReset=false;
    FfxFrameGenerationConfig Config(bool enabled){FfxFrameGenerationConfig c{};c.header.type=FFX_API_FRAME_GENERATION_CONFIG;c.swapChain=sdk;
        c.frameGenerationCallback=Generate;c.frameGenerationCallbackContext=this;c.presentCallback=Compose;c.presentCallbackContext=this;
        c.frameGenerationEnabled=enabled;c.allowAsyncWorkloads=false;c.frameID=frame;
        DXGI_SWAP_CHAIN_DESC1 d{};sdk->GetDesc1(&d);c.interpolationRect={0,0,int32_t(d.Width),int32_t(d.Height)};return c;}
    static ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* p,void* ctx){auto& self=*static_cast<Swapchain*>(ctx);
        if(self.fault||!wanted.load()||!fgroute033::CanGenerate()){p->numGeneratedFrames=0;return FFX_API_RETURN_OK;}
        // SDK holds reset high until a frame is generated. Reset history on the
        // rising edge; resetting on every callback would prevent warmup forever.
        if(p->reset&&!self.sawSdkReset)self.images->ResetHistory();self.sawSdkReset=p->reset;
        const auto revision=fgscene033::Revision();
        if(revision!=self.sceneRevision){self.images->ResetHistory();self.sceneRevision=revision;}
        const UINT count=ufgpolicy033::Count(multiplier.load(),p->numGeneratedFrames);
        if(!count){p->numGeneratedFrames=0;return FFX_API_RETURN_OK;}
        ID3D12Resource* outputs[2]={static_cast<ID3D12Resource*>(p->outputs[0].resource),static_cast<ID3D12Resource*>(p->outputs[1].resource)};
        D3D12_RESOURCE_STATES states[2]={State(p->outputs[0].state),State(p->outputs[1].state)};
        auto result=self.images->RecordBatch(static_cast<ID3D12GraphicsCommandList*>(p->commandList),static_cast<ID3D12Resource*>(p->presentColor.resource),
            outputs,states,count,State(p->presentColor.state),p->backbufferTransferFunction,p->frameID,false);
        lastReason=uint32_t(self.images->LastReason());++recordReasons[lastReason.load()];lastFrameId=p->frameID;lastInterval=self.images->Interval();lastSdkReset=p->reset;lastFlowReason=self.images->FlowRejection();
        normalizedDevices=self.images->NormalizedDevices();const auto& identity=self.images->LastDeviceCheck();
        identityExpected=identity.expected;identityActual=identity.actual;identityQuery=identity.query;
        p->numGeneratedFrames=result>0?UINT(result):0;if(result>0)generatedCount+=result;else ++skipped;
        if(result<0){self.fault=true;errorCode=5;wanted=false;Trace("unsupported-input-real-frames-only");}
        allocations=self.images->Allocations();workingBytes=self.images->Bytes();return FFX_API_RETURN_OK;}
    static ffxReturnCode_t Compose(ffxCallbackDescFrameGenerationPresent* p,void*){
        auto* l=static_cast<ID3D12GraphicsCommandList*>(p->commandList);auto* src=static_cast<ID3D12Resource*>(p->currentBackBuffer.resource);
        auto* dst=static_cast<ID3D12Resource*>(p->outputSwapChainBuffer.resource);if(!l||!src||!dst)return FFX_API_RETURN_ERROR_PARAMETER;
        auto a=src->GetDesc(),b=dst->GetDesc();if(a.Width!=b.Width||a.Height!=b.Height||a.Format!=b.Format)return FFX_API_RETURN_ERROR_PARAMETER;
        if(src!=dst){Transition(l,src,State(p->currentBackBuffer.state),D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(l,dst,State(p->outputSwapChainBuffer.state),D3D12_RESOURCE_STATE_COPY_DEST);
            l->CopyResource(dst,src);Transition(l,src,D3D12_RESOURCE_STATE_COPY_SOURCE,State(p->currentBackBuffer.state));Transition(l,dst,D3D12_RESOURCE_STATE_COPY_DEST,State(p->outputSwapChainBuffer.state));}
        if(p->isGeneratedFrame)++composedGenerated;else ++composedReal;return FFX_API_RETURN_OK;}
    bool ObserveProvider(){if(!sdk||!providerApi)return true;ufgprovider033::Stats s;
        if(!providerApi->read(sdk,&s))return false;presentedReal=s.real;presentedGenerated=s.generated;presentFailed=s.failed;presentError=s.error;workingBytes=s.workingBytes+(images?images->Bytes():0);
        if(FAILED(s.error)){fault=true;wanted=false;errorCode=6;return false;}return true;}
    bool Signal(){if(FAILED(queue->Signal(fence.Get(),++serial))){fault=true;wanted=false;errorCode=4;return false;}
        if(images&&!images->Submitted(serial)){fault=true;wanted=false;errorCode=4;return false;}return true;}
    bool Drain(){if(!Signal())return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;
        auto hr=fence->SetEventOnCompletion(serial,e);auto result=SUCCEEDED(hr)?WaitForSingleObject(e,5000):WAIT_FAILED;CloseHandle(e);
        const auto completed=fence->GetCompletedValue();
        if(result!=WAIT_OBJECT_0||completed==UINT64_MAX||completed<serial){errorCode=3;fault=true;wanted=false;return false;}return true;}
    HRESULT PresentCommon(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* params){std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!ObserveProvider())return presentError.load()?HRESULT(presentError.load()):E_FAIL;
        if(flags&DXGI_PRESENT_TEST)return params?sdk->Present1(sync,flags,params):sdk->Present(sync,flags);
        if(reshadeSurface.Attach(device.Get(),queue.Get(),sdk))reshadeSurface.Present(colourSpace);
        const bool enable=wanted.load()&&!fault&&fgroute033::CanGenerate();if(enable!=active){images->ResetHistory();active=enable;Trace(enable?"enabled":"disabled");}
        ++frame;++realCount;auto c=Config(enable);stable->setFrameGenerationConfig(&c);
        if(!ObserveProvider())return presentError.load()?HRESULT(presentError.load()):E_FAIL;
        // UI/NR drawing by the outer owner is already complete. The SDK captures
        // this real backbuffer in its generation callback before presenting it.
        auto hr=params?sdk->Present1(sync,flags,params):sdk->Present(sync,flags);
        if(images->Pending()&&!Signal())return E_FAIL;
        ObserveProvider();
        if(!enable&&images->Retire())ObserveProvider();
        if(enable&&(frame<4||frame%300==0))Trace("present");
        if(FAILED(hr)){images->ResetHistory();if(hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET){fault=true;wanted=false;errorCode=4;}}
        return hr;}
    bool BeforeResize(){auto c=Config(false);stable->setFrameGenerationConfig(&c);if(!ObserveProvider())return false;active=false;images->ResetHistory();
        // Resource destruction is allowed only at an explicit resize/shutdown
        // boundary after the queue fence; never a frame-count approximation.
        if(!Drain())return false;reshadeSurface.Reset();return images->Retire();}
    bool ReadyToDestroy(){
        if(!ObserveProvider())return false;
        if(stable){auto c=Config(false);c.frameGenerationCallback=nullptr;c.frameGenerationCallbackContext=nullptr;
            c.presentCallback=nullptr;c.presentCallbackContext=nullptr;stable->setFrameGenerationConfig(&c);}
        if(!ObserveProvider()||(fence&&!Drain()))return false;return true;
    }
    ~Swapchain(){reshadeSurface.Reset();stable.Reset();if(sdk){sdk->Release();sdk=nullptr;}
        if(provider)FreeLibrary(provider);if(registered)--owners;workingBytes=0;Trace("closed");}

public:
    static HRESULT Create(IDXGIFactory* factory,ID3D12CommandQueue* queue,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc,
                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,const wchar_t* path,IDXGISwapChain4** out){
        if(!out)return E_POINTER;*out=nullptr;if(!factory||!queue||!window||!desc||!path||desc->SampleDesc.Count!=1||desc->BufferCount<2)return E_INVALIDARG;
        auto nativeQueue=Native(queue);auto nativeFactory=Native(factory);queue=nativeQueue.Get();factory=nativeFactory.Get();
        auto module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module){errorCode=1;return HRESULT_FROM_WIN32(GetLastError());}
        auto get=reinterpret_cast<ufgprovider033::GetApi>(GetProcAddress(module,"K033_GetImageSwapchainApi"));
        auto* api=get?get(ufgprovider033::Version):nullptr;
        if(!api||api->size!=sizeof(*api)||api->version!=ufgprovider033::Version||api->maxGenerated!=2||!api->create||!api->read){FreeLibrary(module);errorCode=2;return E_NOINTERFACE;}
        // Hold exactly one provider reference for this owner and release it
        // after callbacks, SDK COM references and context have drained.
        auto* self=new(std::nothrow)Swapchain;if(!self){FreeLibrary(module);return E_OUTOFMEMORY;}self->provider=module;self->queue=queue;self->providerApi=api;
        if(FAILED(queue->GetDevice(IID_PPV_ARGS(&self->device)))||FAILED(self->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&self->fence)))){delete self;return E_FAIL;}
        try{self->images=std::make_unique<Images>();}catch(...){delete self;return E_OUTOFMEMORY;}if(!self->images->Initialize(self->device.Get(),self->fence.Get())){delete self;return E_FAIL;}
        auto result=api->create(window,desc,full,queue,factory,&self->sdk);
        if(FAILED(result)||!self->sdk||FAILED(self->sdk->QueryInterface(IID_PPV_ARGS(&self->stable)))){errorCode=2;delete self;return E_FAIL;}
        if(desc->Format==DXGI_FORMAT_R16G16B16A16_FLOAT){self->colourSpace=reshade::api::color_space::scrgb;self->outputSpace=DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;}
#ifdef K033_BETA2_RESHADE_HOST
        // Versioned private-data contract with the fork host: this virtual
        // swapchain owns its explicit 033 runtime. All outer wrappers forward
        // GetPrivateData, so the host does not create a second runtime/pump.
        // This optional association records the real game queue for scene
        // inputs. A metadata failure keeps FG available and NR unadmitted.
        const auto sceneMetadata=nrgame033::PublishSceneQueue(self->sdk,self->queue.Get());
        if(FAILED(sceneMetadata.owner)){
            errorCode=2;delete self;return E_FAIL;
        }
#endif
        auto c=self->Config(false);self->stable->setFrameGenerationConfig(&c);self->registered=true;++owners;errorCode=0;*out=self;Trace("swapchain-ready-starts-off");return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIDeviceSubObject)||id==__uuidof(IDXGISwapChain)||
           id==__uuidof(IDXGISwapChain1)||id==__uuidof(IDXGISwapChain2)||id==__uuidof(IDXGISwapChain3)||id==__uuidof(IDXGISwapChain4)){*out=static_cast<IDXGISwapChain4*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n){
        // Preserve the entire owner, callbacks and GPU resources on failed
        // drainage. A timeout is never permission to destroy in-flight work.
        if(!ReadyToDestroy()){refs=1;wanted=false;Trace("queue-fault-owner-quarantined");return 0;}delete this;}return n;}
    HRESULT STDMETHODCALLTYPE Present(UINT s,UINT f)override{return PresentCommon(s,f,nullptr);}
    HRESULT STDMETHODCALLTYPE Present1(UINT s,UINT f,const DXGI_PRESENT_PARAMETERS* p)override{return PresentCommon(s,f,p);}
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags)override{std::lock_guard<std::recursive_mutex> lock(mutex);if(!BeforeResize())return DXGI_ERROR_INVALID_CALL;return sdk->ResizeBuffers(n,w,h,f,flags);}
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags,const UINT* masks,IUnknown*const* queues)override{std::lock_guard<std::recursive_mutex> lock(mutex);
        if(queues)for(UINT i=0;i<n;++i)if(queues[i]&&queues[i]!=queue.Get())return DXGI_ERROR_INVALID_CALL;
        if(!BeforeResize())return DXGI_ERROR_INVALID_CALL;return sdk->ResizeBuffers1(n,w,h,f,flags,masks,queues);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c)override{return sdk->SetPrivateData(a,b,c);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b)override{return sdk->SetPrivateDataInterface(a,b);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c)override{return sdk->GetPrivateData(a,b,c);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID a,void** b)override{return sdk->GetParent(a,b);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID a,void** b)override{return sdk->GetDevice(a,b);}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT a,REFIID b,void** c)override{return sdk->GetBuffer(a,b,c);}
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL a,IDXGIOutput* b)override{return sdk->SetFullscreenState(a,b);}
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* a,IDXGIOutput** b)override{return sdk->GetFullscreenState(a,b);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* a)override{return sdk->GetDesc(a);}
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* a)override{return sdk->ResizeTarget(a);}
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** a)override{return sdk->GetContainingOutput(a);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a)override{return sdk->GetFrameStatistics(a);}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* a)override{return sdk->GetLastPresentCount(a);}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* a)override{return sdk->GetDesc1(a);}
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a)override{return sdk->GetFullscreenDesc(a);}
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* a)override{return sdk->GetHwnd(a);}
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID a,void** b)override{return sdk->GetCoreWindow(a,b);}
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported()override{return sdk->IsTemporaryMonoSupported();}
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** a)override{return sdk->GetRestrictToOutput(a);}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* a)override{return sdk->SetBackgroundColor(a);}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* a)override{return sdk->GetBackgroundColor(a);}
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION a)override{return sdk->SetRotation(a);}
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* a)override{return sdk->GetRotation(a);}
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT a,UINT b)override{return sdk->SetSourceSize(a,b);}
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* a,UINT* b)override{return sdk->GetSourceSize(a,b);}
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT a)override{return sdk->SetMaximumFrameLatency(a);}
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* a)override{return sdk->GetMaximumFrameLatency(a);}
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject()override{return sdk->GetFrameLatencyWaitableObject();}
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* a)override{return sdk->SetMatrixTransform(a);}
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* a)override{return sdk->GetMatrixTransform(a);}
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex()override{return sdk->GetCurrentBackBufferIndex();}
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE a,UINT* b)override{return sdk->CheckColorSpaceSupport(a,b);}
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE a)override{std::lock_guard<std::recursive_mutex> lock(mutex);auto hr=sdk->SetColorSpace1(a);
        // Reasserting metadata is common and must not invalidate the two-frame
        // history. Failed requests likewise leave the current contract intact.
        if(SUCCEEDED(hr)){if(outputSpace!=a){images->ResetHistory();outputSpace=a;++resetColourCount;Trace("colour-space-changed");}else ++sameColourCount;
            colourSpace=a==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709?reshade::api::color_space::srgb:
            a==DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709?reshade::api::color_space::scrgb:
            a==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020?reshade::api::color_space::hdr10_pq:reshade::api::color_space::unknown;}
        return hr;}
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE a,UINT b,void* c)override{return sdk->SetHDRMetaData(a,b,c);}
};
}
