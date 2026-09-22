// Nonblocking GPU timing. Reuse/readback requires completion on the actual queue.
#pragma once
#include <mutex>
#include <cstdint>
#include "diagnostic_policy.h"
#if !defined(K033_TIMING_STANDALONE) || defined(K033_TIMING_SUBMIT_TEST)
#include "gputime_submit.h"
#endif
namespace gputime {
static const int kRing=64, kStamps=8, kSegs=kStamps-1;
enum Phase { Free, Recording, Recorded, Submitted };
struct Slot {
    Phase phase=Free;
    ID3D12GraphicsCommandList *cmd=nullptr; // identity, never dereferenced later
    uint64_t native=0;
    ID3D12CommandQueue *queue=nullptr;
    ID3D12Fence *fence=nullptr;
    UINT64 value=0, frequency=0;
    DWORD submit_thread=0;
    int count=0;
    bool valid=false;
    bool reset=false,pinned=false;
};
static Slot s_slots[kRing];
static ID3D12QueryHeap *s_heap=nullptr;
static ID3D12Resource *s_read=nullptr;
static bool s_ready=false;
static int cfg_enabled=1;
static bool Enabled(){return diagnostic033::TimingAllowed(cfg_enabled!=0);}
static std::mutex s_mutex;
static bool s_track_reset=false;
static thread_local int s_active=-1;
static double s_seg[kSegs]={}, s_total=0;
static UINT64 s_samples=0, s_dropped=0;
static ULONGLONG s_sample_tick=0;
static void FreeSlot(Slot &s) {
    if(s.queue){s.queue->Release();s.queue=nullptr;}
    s.phase=Free;s.cmd=nullptr;s.native=0;s.count=0;s.valid=false;s.submit_thread=0;s.frequency=0;s.reset=false;s.pinned=false;
}
static bool Matches(const Slot& s,uint64_t list){return reinterpret_cast<uint64_t>(s.cmd)==list || (s.native && s.native==list);}
static void TrackReset(){if(!Enabled())return;std::lock_guard<std::mutex> lock(s_mutex);s_track_reset=true;}
static void ResetCommand(uint64_t list){if(!Enabled())return;std::lock_guard<std::mutex> lock(s_mutex);
    for(auto& s:s_slots)if(s.phase!=Free && Matches(s,list))s.reset=true;}
static void Destroy() {
    std::lock_guard<std::mutex> lock(s_mutex);
    for(auto &s:s_slots){FreeSlot(s);if(s.fence)s.fence->Release();s=Slot{};}
    if(s_read)s_read->Release();if(s_heap)s_heap->Release();
    s_read=nullptr;s_heap=nullptr;s_ready=false;s_active=-1;s_total=0;s_samples=s_dropped=0;s_sample_tick=0;
    for(double &v:s_seg)v=0;
}
static bool Create(ID3D12Device *dev, ID3D12CommandQueue *queue) {
    if(!Enabled())return false;
    if(s_ready)return true;if(!dev)return false;
#if !defined(K033_TIMING_STANDALONE) || defined(K033_TIMING_SUBMIT_TEST)
    // Timing must never delay rendering if instrumentation cannot be installed.
    submit::Install(queue);
#endif
    D3D12_QUERY_HEAP_DESC qd={};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=kRing*kStamps;
    if(FAILED(dev->CreateQueryHeap(&qd,IID_PPV_ARGS(&s_heap))))return false;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC d={};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width=sizeof(UINT64)*kRing*kStamps;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s_read)))){Destroy();return false;}
    for(auto &s:s_slots)if(FAILED(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.fence)))){Destroy();return false;}
    s_ready=true;return true;
}
// Teardown is owned by destroy_device; a proxy pointer is not a different GPU.
// Frequency is obtained from the actual submitting queue, never a build queue.
static void EnsureDevice(ID3D12Device *){}
static void SetFrequencyFrom(ID3D12CommandQueue *){}
static void PollLocked() {
    if(!s_ready)return;
    for(int index=0;index<kRing;++index){
        Slot &s=s_slots[index];if(s.phase!=Submitted || s.pinned || (s_track_reset && !s.reset))continue;
        const UINT64 done=s.fence->GetCompletedValue();
        if(done==UINT64_MAX||done<s.value)continue;
        if(s.valid&&s.count>=2&&s.frequency){
            const SIZE_T offset=sizeof(UINT64)*index*kStamps;
            D3D12_RANGE range={offset,offset+sizeof(UINT64)*s.count};void *data=nullptr;
            if(SUCCEEDED(s_read->Map(0,&range,&data))&&data){
                const UINT64 *t=reinterpret_cast<const UINT64 *>(data)+index*kStamps;
                bool ordered=t[0]!=0;
                for(int i=1;i<s.count;++i)ordered=ordered&&t[i]>=t[i-1];
                if(ordered){
                    const double scale=1000.0/static_cast<double>(s.frequency);
                    for(double &v:s_seg)v=0;
                    for(int i=1;i<s.count;++i)s_seg[i-1]=double(t[i]-t[i-1])*scale;
                    s_total=double(t[s.count-1]-t[0])*scale;++s_samples;s_sample_tick=GetTickCount64();
                }
                D3D12_RANGE none={0,0};s_read->Unmap(0,&none);
            }
        }
        FreeSlot(s);
    }
}
static void Poll(){if(!Enabled())return;std::lock_guard<std::mutex> lock(s_mutex);PollLocked();}
static void Stamp(ID3D12GraphicsCommandList *cl){
    if(!Enabled()||s_active<0||!cl)return;
    std::lock_guard<std::mutex> lock(s_mutex);Slot &s=s_slots[s_active];
    if(s.phase!=Recording||s.cmd!=cl||s.count>=kStamps)return;
    cl->EndQuery(s_heap,D3D12_QUERY_TYPE_TIMESTAMP,s_active*kStamps+s.count++);
}
static void Begin(ID3D12GraphicsCommandList *cl){
    if(!Enabled()||!s_ready||!cl||s_active>=0)return;
    {std::lock_guard<std::mutex> lock(s_mutex);PollLocked();
        for(int i=0;i<kRing;++i)if(s_slots[i].phase==Free){
            Slot &s=s_slots[i];s.phase=Recording;s.cmd=cl;s_active=i;break;
        }
        if(s_active<0){++s_dropped;return;}
    }
    Stamp(cl);
}
static void Abort(){
    if(!Enabled()||s_active<0)return;
    std::lock_guard<std::mutex> lock(s_mutex);Slot &s=s_slots[s_active];
    // Partial timestamp commands may still execute. Do not reuse before a fence.
    s.valid=false;s.phase=Recorded;s_active=-1;
}
static void End(ID3D12GraphicsCommandList *cl){
    if(!Enabled()||s_active<0||!cl||s_slots[s_active].cmd!=cl)return;
    Stamp(cl);std::lock_guard<std::mutex> lock(s_mutex);Slot &s=s_slots[s_active];
    if(s.count>=2)cl->ResolveQueryData(s_heap,D3D12_QUERY_TYPE_TIMESTAMP,s_active*kStamps,s.count,s_read,sizeof(UINT64)*s_active*kStamps);
    s.valid=s.count>=2;s.phase=Recorded;s_active=-1;
}
static void SignalSlot(Slot &s,ID3D12CommandQueue *q){
    // A replay on an unrelated queue has no single-fence completion proof.
    // Retain this optional measurement rather than reuse query memory too soon.
    if(s.queue && s.queue!=q){s.valid=false;s.pinned=true;return;}
    if(!s.queue && q){s.queue=q;q->AddRef();}
    UINT64 frequency=0;if(!q||FAILED(q->GetTimestampFrequency(&frequency))||!frequency)return;
    const UINT64 value=s.value+1;if(FAILED(q->Signal(s.fence,value)))return;
    s.value=value;s.frequency=frequency;s.phase=Submitted;
}
// Only call after the owner's ExecuteCommandLists has returned.
static void AfterSubmit(ID3D12GraphicsCommandList *cl,ID3D12CommandQueue *q,bool nativeSubmission=false){
    if(!Enabled()||!s_ready)return;
    std::lock_guard<std::mutex> lock(s_mutex);
    for(auto &s:s_slots)if(!s.reset && Matches(s,reinterpret_cast<uint64_t>(cl))){
        // Re-execution overwrites the same query range. Retire after the latest
        // submission and discard the ambiguous sample. Explicit owner callbacks
        // following this observer are idempotent and must not signal again.
        if(nativeSubmission&&s.phase==Submitted){s.valid=false;s.phase=Recorded;}
        if(s.phase==Recorded)SignalSlot(s,q);
    }
}
#ifndef K033_TIMING_STANDALONE
static void OnBindPipeline(reshade::api::command_list *cl,reshade::api::pipeline_stage,reshade::api::pipeline){
    if(!Enabled()||s_active<0||!cl)return;
    std::lock_guard<std::mutex> lock(s_mutex);Slot &s=s_slots[s_active];
    if(!s.native)s.native=cl->get_native();
}
static void OnExecute(reshade::api::command_queue *queue,reshade::api::command_list *cl){
    if(!Enabled()||!s_ready||!cl||!queue||queue->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    const uint64_t native=cl->get_native();
    auto *q=reinterpret_cast<ID3D12CommandQueue *>(queue->get_native());
    std::lock_guard<std::mutex> lock(s_mutex);
    for(auto &s:s_slots)if(!s.reset && (s.phase==Recorded||s.phase==Submitted)&&(s.native==native||reinterpret_cast<uint64_t>(s.cmd)==native)){
        // BEFORE submission, sometimes once per list in a batch. No fence here.
        if(s.phase==Submitted){s.valid=false;s.phase=Recorded;}
        if(!s.queue){s.queue=q;q->AddRef();s.submit_thread=GetCurrentThreadId();}
        else if(s.queue!=q||s.submit_thread!=GetCurrentThreadId())s.valid=false;
    }
}
static void OnFinishPresent(reshade::api::command_queue *queue,reshade::api::swapchain *){
    if(!Enabled()||!s_ready||!queue||queue->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    auto *q=reinterpret_cast<ID3D12CommandQueue *>(queue->get_native());
    std::lock_guard<std::mutex> lock(s_mutex);
    for(auto &s:s_slots)if(s.phase==Recorded&&s.queue==q&&s.submit_thread==GetCurrentThreadId())SignalSlot(s,q);
    // Same queue AND submission thread: its Execute returned before this callback.
    // Cross-thread cases remain unmeasured, with no guessed readback or slot reuse.
    PollLocked();
}
static void RegisterEvents(){
    if(!Enabled())return;
    reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
    reshade::register_event<reshade::addon_event::execute_command_list>(OnExecute);
    reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent);
}
static void UnregisterEvents(){
    submit::Uninstall();
    reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
    reshade::unregister_event<reshade::addon_event::execute_command_list>(OnExecute);
    reshade::unregister_event<reshade::addon_event::finish_present>(OnFinishPresent);
}
#endif
static double seg(int i){std::lock_guard<std::mutex> lock(s_mutex);return i>=0&&i<kSegs?s_seg[i]:0;}
static double total(){std::lock_guard<std::mutex> lock(s_mutex);return s_total;}
static bool ready(){return s_ready;}
static bool freq_ok(){std::lock_guard<std::mutex> lock(s_mutex);return s_samples>0;}
static UINT64 samples(){std::lock_guard<std::mutex> lock(s_mutex);return s_samples;}
static UINT64 dropped(){std::lock_guard<std::mutex> lock(s_mutex);return s_dropped;}
struct Snapshot {
    UINT64 samples=0, age_ms=UINT64_MAX;
    double segments[kSegs]={}, total=0;
    bool fresh() const { return samples>0 && age_ms<=1000; }
};
static Snapshot snapshot(){
    std::lock_guard<std::mutex> lock(s_mutex);
    Snapshot result;result.samples=s_samples;result.total=s_total;
    result.age_ms=s_samples?GetTickCount64()-s_sample_tick:UINT64_MAX;
    for(int i=0;i<kSegs;++i)result.segments[i]=s_seg[i];
    return result;
}
static bool fresh(){return snapshot().fresh();}
} // namespace gputime
