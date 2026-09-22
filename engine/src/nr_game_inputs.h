#pragma once
#include "nr_game_input_policy.h"
#include "nr_input_policy.h"
#include "nr_guide_normalize_policy.h"
#include "d3d12_identity.h"
#include "nr_game_module.h"
#include "nr_native_input_observer.h"
#include <mutex>
#include <atomic>
namespace nrgame033 {
using Microsoft::WRL::ComPtr;
inline StateBook stateBook;
inline std::mutex stateMutex;
inline std::array<std::atomic<uintptr_t>,StateBook::Resources> watched{};
inline std::atomic<bool> watchActive{false},poisoned{false},wanted{false};
inline std::atomic<Result> status{Result::Missing};
inline nrdiag033::DisplayState contractDisplay;
// Read's existing AfterScope serializes this fixed storage. No UI reader
// accesses the signatures or descriptors; it reads only atomic display state.
inline nrdiag033::LogBook contractLogs;
inline void RejectContract(nrdiag033::Reason reason,uint32_t frameMask=0,const Frame& f=Frame{},
        const Frame2& geometry=Frame2{},const D3D12_RESOURCE_DESC& depth={},const D3D12_RESOURCE_DESC& motion={},
        uint32_t depthMask=0,uint32_t motionMask=0,bool v2=false){
    contractDisplay.Reject(reason);
    status.store(Result::Contract);
    if(!contractLogs.New(nrdiag033::Key(reason,frameMask,depthMask,motionMask,f,geometry,depth,motion)))return;
    // Existing logger performs synchronous I/O. At most 16 distinct extra
    // records per process; no frame serial or resource pointer is a log key.
    Log("[033 native contract] reason=%u frameMask=0x%X depthMask=0x%X motionMask=0x%X v2=%u "
        "outer=%u/%u frame=%u/%u source=%u flags=0x%X serial=%llu streamValid=%u scale=%.7g,%.7g jitter=%.7g,%.7g "
        "depth={format=%u dim=%u size=%llux%u array=%u mips=%u samples=%u flags=0x%X} "
        "motion={format=%u dim=%u size=%llux%u array=%u mips=%u samples=%u flags=0x%X}",
        unsigned(reason),frameMask,depthMask,motionMask,unsigned(v2),geometry.size,geometry.version,
        f.size,f.version,f.source,f.flags,f.serial,unsigned(f.stream!=0),double(f.scaleX),double(f.scaleY),double(f.jitterX),double(f.jitterY),
        unsigned(depth.Format),unsigned(depth.Dimension),depth.Width,depth.Height,unsigned(depth.DepthOrArraySize),unsigned(depth.MipLevels),depth.SampleDesc.Count,unsigned(depth.Flags),
        unsigned(motion.Format),unsigned(motion.Dimension),motion.Width,motion.Height,unsigned(motion.DepthOrArraySize),unsigned(motion.MipLevels),motion.SampleDesc.Count,unsigned(motion.Flags));
}

inline std::atomic<bool> jitterKnown{false};
inline std::atomic<bool> ngxJitterKnown{false},ngxMvJittered{false},ngxExposure{false},ngxPreExposure{false};
inline uint64_t consumed=0,previousStream=0;
inline Acquire acquire=nullptr;
inline Acquire2 acquire2=nullptr;
inline std::atomic<int> sceneAcquireStatus{SceneNotPublished};
inline Describe describe=nullptr;
inline std::atomic<bool> automaticAdapter{false};
inline std::atomic<bool> waitingForRenderer{false};
inline std::atomic<unsigned> discoveryState{0}; // missing, legacy, identified, incompatible, ambiguous
inline void Discover(){
    static ULONGLONG nextLookup=0;
    if(!acquire){
        if(GetTickCount64()<nextLookup)return;nextLookup=GetTickCount64()+1000;
        const auto match=FindGameExport("K033_AcquireReEngineInput","K033_AcquireReEngineInput");
        const auto v2=FindGameExport("K033_AcquireReEngineInputV2","K033_AcquireReEngineInput");
        const auto descMatch=FindGameExport("K033_DescribeGameInput","K033_AcquireReEngineInput");
        InputExports selected;
        if(!SelectInputExports(match,v2,descMatch,selected)||!PinGameExport(match)){
            contractDisplay.Clear();
            acquire=nullptr;acquire2=nullptr;describe=nullptr;
            automaticAdapter.store(false);waitingForRenderer.store(false);
            discoveryState.store(match.module||match.ambiguous||v2.ambiguous||descMatch.ambiguous?4:0);return;
        }
        // Publish no callable pointer until all optional exports have passed
        // the same-owner/ambiguity checks and the owner is pinned.
        acquire=reinterpret_cast<Acquire>(selected.acquire);
        acquire2=reinterpret_cast<Acquire2>(selected.acquire2);
        describe=reinterpret_cast<Describe>(selected.describe);
        discoveryState.store(describe?3:1);
    }
    if(describe){
        // Once discovered, the descriptor reads only adapter atomics. Refresh
        // every frame so a resize/reset is not hidden by the module scan timer.
        Adapter a;bool read=describe(&a)!=0;
        if(!read){a=Adapter{};a.version=1;read=describe(&a)!=0;}
        const bool ok=read&&Supported(a);
        if(!ok)contractDisplay.Clear();
        waitingForRenderer.store(read&&WaitingForRenderer(a));
        automaticAdapter.store(ok);discoveryState.store(ok?2:3);
    }
}
inline const char* DiscoveryNote(){
    if(nrinput033::ownership.Get()==nrinput033::Route::Upscale)return "自动接入超分接口 · 共用数据通道";
    switch(discoveryState.load()){
    case 1:return "已发现旧场景适配 · 保留现有接入设置";
    case 2:return "已自动识别 RE4 场景适配 · 数据有效性另行校验";
    case 3:return waitingForRenderer.load()?"正在等待游戏渲染器就绪":"场景适配尚未确认兼容 · 保留现有通道";
    case 4:return "发现多个场景适配 · 不自动选择";
    default:return "等待通用接口或兼容场景适配 · 保留现有通道";
    }
}
inline void Pause(){contractDisplay.Clear();wanted.store(false);watchActive.store(false);poisoned.store(true);status.store(Result::Missing);}
inline bool IsWatched(uintptr_t id){for(auto& slot:watched)if(id&&slot.load(std::memory_order_relaxed)==id)return true;return false;}
inline void OnBarrier(uint64_t list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    if(!watchActive.load(std::memory_order_relaxed)||!list||!barriers)return;
    bool relevant=false;
    for(UINT i=0;i<count;++i){const auto& b=barriers[i];
        if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)relevant|=IsWatched(uintptr_t(b.Transition.pResource));
        if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING)relevant|=IsWatched(uintptr_t(b.Aliasing.pResourceBefore))||IsWatched(uintptr_t(b.Aliasing.pResourceAfter));
    }
    if(!relevant)return;
    std::unique_lock lock(stateMutex,std::try_to_lock);
    if(!lock.owns_lock()){poisoned.store(true);return;}
    for(UINT i=0;i<count;++i){const auto& b=barriers[i];
        if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION){
            const auto usage=b.Transition.StateAfter==D3D12_RESOURCE_STATE_COMMON?0x80000000u:uint32_t(b.Transition.StateAfter);
            stateBook.Barrier(list,uintptr_t(b.Transition.pResource),usage,uint32_t(b.Flags),b.Transition.Subresource);
        }else if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING){
            // Aliasing does not transition either resource to COMMON.
            stateBook.Barrier(list,uintptr_t(b.Aliasing.pResourceBefore),0);
            stateBook.Barrier(list,uintptr_t(b.Aliasing.pResourceAfter),0);
        }
        // UAV ordering alone does not establish a resource's current state.
    }
}
inline void OnSubmit(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists){
    if(!watchActive.load(std::memory_order_relaxed)||!queue||!lists)return;
    std::unique_lock lock(stateMutex,std::try_to_lock);
    if(!lock.owns_lock()){poisoned.store(true);return;}
    for(UINT i=0;i<count;++i)stateBook.Submit(uintptr_t(lists[i]),uintptr_t(queue));
}
inline void OnReset(uint64_t list){
    if(!watchActive.load(std::memory_order_relaxed)||!list)return;
    std::unique_lock lock(stateMutex,std::try_to_lock);
    if(!lock.owns_lock()){poisoned.store(true);return;}
    stateBook.Reset(list);
}
inline void OnDestroyList(reshade::api::command_list* cl){if(cl)OnReset(cl->get_native());}
inline void OnDestroy(reshade::api::device*,reshade::api::resource resource){
    if(!IsWatched(resource.handle))return;
    std::unique_lock lock(stateMutex,std::try_to_lock);
    if(!lock.owns_lock()){poisoned.store(true);return;}
    stateBook.Forget(resource.handle);
    for(auto& slot:watched)if(slot.load()==resource.handle)slot.store(0);
}
inline void Register(){
    using reshade::addon_event;
    nrnative033::barrierNotice.store(OnBarrier);nrnative033::submitNotice.store(OnSubmit);nrnative033::resetNotice.store(OnReset);
    reshade::register_event<addon_event::destroy_command_list>(OnDestroyList);
    reshade::register_event<addon_event::destroy_resource>(OnDestroy);
}
inline void Unregister(){
    contractDisplay.Clear();
    using reshade::addon_event;
    watchActive.store(false);wanted.store(false);
    nrnative033::Uninstall();
    reshade::unregister_event<addon_event::destroy_command_list>(OnDestroyList);
    reshade::unregister_event<addon_event::destroy_resource>(OnDestroy);
}
inline ComPtr<ID3D12Resource> NativeResource(void* ptr){
    auto identity=identity033::Canonical(static_cast<ID3D12Resource*>(ptr));
    ComPtr<ID3D12Resource> result;
    if(identity)identity.As(&result);
    return result;
}
struct Input {
    Frame frame;ComPtr<ID3D12Resource> depth,motion;ComPtr<ID3D12CommandQueue> queue;
    GuideSubresource depthPlan=GuideSubresource::Whole;
    uint32_t motionWidth=0,motionHeight=0;
    uint32_t depthState=0,motionState=0;bool ready=false,normalizeRe4=false;int reset=1;
};
inline Input Read(ID3D12Device* device,ID3D12CommandQueue* queue,ID3D12GraphicsCommandList* list,uint32_t w,uint32_t h){
    Input input;
    wanted.store(true);
    Discover();
    if(describe&&!automaticAdapter.load()){RejectContract(nrdiag033::Reason::Adapter);return input;}
    Frame2 geometry;
    if(acquire2){
        const int acquired=acquire2(&geometry);sceneAcquireStatus.store(acquired);
        if(acquired!=SceneAvailable){
            if(acquired==SceneNotPublished)contractDisplay.Unpublished();else contractDisplay.Clear();
            status.store(Result::Missing);return input;
        }
        input.frame=geometry.frame;
    }else if(!acquire||!acquire(&input.frame)){contractDisplay.Clear();status.store(Result::Missing);return input;}
    auto& f=input.frame;
    // Take ownership immediately, including every validation failure path.
    ComPtr<ID3D12Resource> suppliedDepth,suppliedMotion;
    suppliedDepth.Attach(static_cast<ID3D12Resource*>(f.depth));suppliedMotion.Attach(static_cast<ID3D12Resource*>(f.motion));
    input.queue.Attach(static_cast<ID3D12CommandQueue*>(f.queue));
    input.depth=NativeResource(f.depth);input.motion=NativeResource(f.motion);
    auto identity=identity033::Canonical(device);
    const bool deviceOk=identity&&identity033::Child(identity.Get(),input.depth.Get()).equal&&
        identity033::Child(identity.Get(),input.motion.Get()).equal&&identity033::Child(identity.Get(),input.queue.Get()).equal;
    const bool queueOk=identity033::Equal(queue,input.queue.Get());
    auto result=acquire2?Check(geometry,w,h,deviceOk,queueOk,true,consumed):Check(f,w,h,deviceOk,queueOk,true,consumed);
    if(result==Result::Contract){
        auto mask=acquire2?nrdiag033::OuterIssues(geometry):0u;
        if(!mask)mask=nrdiag033::FrameIssues(f);
        RejectContract(nrdiag033::FrameReason(mask),mask,f,geometry,{},{},0,0,acquire2!=nullptr);return input;
    }
    if(result==Result::Ready){
        // Actual current descriptors, once per resource. Reuse them for both
        // the unchanged acceptance predicates and the extent comparison.
        const auto d=input.depth->GetDesc(),m=input.motion->GetDesc();
        input.depthPlan=DepthReadPlan(d);
        input.normalizeRe4=bool(nrnormalize033::Select(describe&&automaticAdapter.load(),d,m));
        auto dm=nrdiag033::TextureIssues(d,true),mm=nrdiag033::TextureIssues(m,false);
        // Exact identified RE4 pair will be converted before NR. All other
        // descriptor, frame, queue, extent and state rejection gates remain.
        if(input.normalizeRe4){dm&=~uint32_t(nrdiag033::Format);mm&=~uint32_t(nrdiag033::Format);}
        if(dm||mm){RejectContract(nrdiag033::TextureReason(dm,mm),0,f,geometry,d,m,dm,mm,acquire2!=nullptr);return input;}
        if(acquire2?!ResourceExtents(geometry,d.Width,d.Height,m.Width,m.Height):
            (d.Width!=f.width||d.Height!=f.height||m.Width!=f.width||m.Height!=f.height))result=Result::Extent;
    }
    // A different concrete failure, successful admission, or state-observer
    // phase supersedes the previous Contract rejection.
    contractDisplay.Clear();
    if(result!=Result::Ready){status.store(result);return input;}
    if(!nrnative033::Request(list)){status.store(Result::State);return input;}
    // No COM/D3D operation is performed while holding this bounded CPU lock.
    const auto dep=uintptr_t(input.depth.Get()),mv=uintptr_t(input.motion.Get()),q=uintptr_t(queue);
    {
        std::unique_lock lock(stateMutex,std::try_to_lock);
        if(!lock.owns_lock()){poisoned.store(true);status.store(Result::State);return input;}
        if(poisoned.exchange(false))stateBook.Invalidate();
        stateBook.Watch(dep,q,input.depthPlan);stateBook.Watch(mv,q);
        // The CPU book can retain eight rotating textures. Export precisely
        // those IDs for the lock-free barrier prefilter.
        const auto ids=stateBook.Ids();
        for(size_t i=0;i<ids.size();++i)watched[i].store(ids[i]);
        watchActive.store(true);
        const bool known=stateBook.Read(dep,input.depthState)&&stateBook.Read(mv,input.motionState);
        if(!known){status.store(Result::State);return input;}
    }
    if(poisoned.load()){status.store(Result::State);return input;}
    input.motionWidth=acquire2?geometry.motionWidth:f.width;
    input.motionHeight=acquire2?geometry.motionHeight:f.height;
    input.ready=true;input.reset=(f.flags&SceneReset)||f.stream!=previousStream||f.serial!=consumed+1;
    status.store(Result::Ready);jitterKnown.store((f.flags&JitterKnown)!=0);return input;
}
inline void Consume(const Input& input){if(input.ready){consumed=input.frame.serial;previousStream=input.frame.stream;}}
inline const char* JitterNote(){
    if(nrinput033::ownership.Get()==nrinput033::Route::Presentation)
        return status.load()==Result::Ready?(jitterKnown.load()?"相机抖动已读取供核对 · NR 使用去抖运动语义":"相机抖动未取得 · 运动按引擎语义接入"):"等待有效场景输入";
    if(ngxMvJittered.load())return "接口报告运动包含相机抖动 · 尚未归一化";
    return ngxJitterKnown.load()?"接口抖动参数已读取供核对":"接口未报告抖动参数";
}
// Construct only after the existing submission lease has retained both game
// resources. Restore exact arrival states on all exits, including NR failure.
struct ReadScope {
    ID3D12GraphicsCommandList* list=nullptr;D3D12_RESOURCE_BARRIER reverse[2]{};UINT count=0;
    ReadScope(ID3D12GraphicsCommandList* cl,ID3D12Resource* depth,ID3D12Resource* motion,bool frozen=false,
              GuideSubresource depthPlan=GuideSubresource::Whole){
        if(!nrinput033::context.presentation||!nrinput033::context.nativeGuides||frozen)return;
        list=cl;
        D3D12_RESOURCE_BARRIER before[2]{};
        count=BuildReadTransitions(depth,motion,nrinput033::context.depthState,nrinput033::context.motionState,depthPlan,before,reverse);
        if(count)list->ResourceBarrier(count,before);
    }
    ~ReadScope(){if(count)list->ResourceBarrier(count,reverse);}
};
inline const char* Note(){switch(status.load()){
    case Result::Ready:return "RE 场景深度 / 运动已接入 · 内容待游戏验证";
    case Result::Contract:if(auto text=contractDisplay.Note())return text;return "原生输入格式或语义不匹配 · NR 等待";
    case Result::Extent:return "原生输入区域与资源或输出不匹配 · NR 等待";
    case Result::MotionExtent:return "原生深度与运动尺寸不同，换算尚未验证 · NR 等待";
    case Result::Device:return "原生输入设备不匹配 · NR 等待";
    case Result::Queue:return "原生输入跨队列 · NR 等待";
    case Result::State:
        if(nrnative033::enhancedSeen.load())return "增强资源屏障尚不支持 · NR 等待";
        if(nrnative033::failed.load())return "原生资源保护未接通 · NR 等待";
        return "原生输入资源状态尚未确认 · NR 等待";
    case Result::Stale:return "没有新的场景原生输入 · NR 等待";
    default:
        switch(sceneAcquireStatus.load()){
        case SceneNotPublished:if(auto text=contractDisplay.Note())return text;return "场景适配未提供本帧深度 / 运动 · NR 等待";
        case SceneOutputMissing:return "场景最终输出目标未取得 · NR 等待";
        case SceneMissing:return "没有唯一已渲染主场景 · NR 等待";
        case SceneDepthMissing:return "场景原生深度未取得 · NR 等待";
        case SceneMotionMissing:return "场景原生运动未取得 · NR 等待";
        case SceneQueueMissing:return "场景原生提交队列未取得 · NR 等待";
        case SceneCoverageInvalid:return "场景资源不是已确认的完整区域 · NR 等待";
        case SceneUnsupported:return "场景适配版本不兼容或已停用 · NR 等待";
        case SceneNotReady:return "场景渲染器尚未就绪 · NR 等待";
        default:return "场景适配未提供本帧深度 / 运动 · NR 等待";
        }
}}
}
