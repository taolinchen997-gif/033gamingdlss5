// Immutable bindings for the complete host NR pass. Reuse requires BOTH a
// command-list reset and completion on every observed native execution queue.
// Missing observations exhaust a bounded pool and bypass NR, never wait.
#pragma once
#include <mutex>
#include "nr_stack_layout.h"
#include "command_lifetime.h"
#include "gputime_submit.h"
#include "nr_native_input_events.h"
#include "nr_fault.h"
#include "lease_wait_diagnostic.h"
#include "reset_entry_owner_policy.h"
namespace resolveleases {
// A game may retain dozens of closed lists after GPU completion. These are
// descriptor/resource-reference leases, NOT queued frames or extra NR images.
// Sixteen was exhausted in YanYun (116 real NR frames bypassed on 2026-09-06).
#ifdef K033_TEST_LEASE_CAPACITY
constexpr unsigned Capacity=K033_TEST_LEASE_CAPACITY, QueueCapacity=4;
#else
constexpr unsigned Capacity=128, QueueCapacity=4;
#endif
struct Use { ID3D12CommandQueue* queue=nullptr; ID3D12Fence* fence=nullptr; UINT64 value=0; };
struct Slot {
    std::shared_ptr<commandlife::State> life;
    commandlife::AllocatorUse allocator;
    ULONGLONG began=0;
    ID3D12DescriptorHeap* heap=nullptr;
    IUnknown* refs[48]={};
    void* tag=nullptr;
    unsigned long long generation=0;
    ID3D12GraphicsCommandList* cmd=nullptr;
    uint64_t native=0;
    Use uses[QueueCapacity];
    unsigned pending=0;
    bool active=false,reset=false,pinned=false,recording=false,submitted=false;
};
static Slot slots[Capacity];
// One monotonic fence per observed queue, retained for this process. Resource
// leases take references to it instead of creating a D3D object every frame.
static Use queueFences[QueueCapacity];
static std::mutex mutex;
static std::mutex install_mutex;
static thread_local Slot* recording=nullptr;
static unsigned long long admitted=0,skipped=0,retired=0,submitted=0;
static unsigned high_water=0;
static std::atomic<unsigned long long> unavailable{0};
static leasewait033::Recorder waitDiagnostic;
// Reserved until the per-frame control pump reports the real switch:
// a presentation list must not take the entry before NR is known.
static std::atomic_bool nr_enabled{true};
static void NrEnabled(bool on){nr_enabled.store(on,std::memory_order_relaxed);}
using NativeReset=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
static NativeReset original_reset=nullptr;
static std::atomic<void*> requested_reset{nullptr};
static void* reset_entry=nullptr;
static std::atomic_bool reset_installed{false};
static std::atomic<ID3D12Device*> requested_device{nullptr};
static ID3D12CommandQueue* observation_queue=nullptr;
using NativeAllocatorReset=HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandAllocator*);
static NativeAllocatorReset original_allocator_reset=nullptr;
static std::atomic<void*> requested_allocator_reset{nullptr};
static std::atomic_bool allocator_reset_installed{false};
static unsigned long long allocator_retired=0;
// Binding-only host seam: the native Reset hook alone invalidates old slots.
// Initial Create or a Reset before the first NR recording can now establish
// the exact allocator association. Existing Slot snapshots are never changed.
static int BindCommandAllocator(uint32_t version,ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator) {
    if(version!=1 || !list || !allocator)return -1;
    if(!commandlife::BindObservedAllocator(list,allocator))return -2;
    requested_allocator_reset.store((*reinterpret_cast<void***>(allocator))[8],std::memory_order_release);
    return 0;
}
static bool Discarded(const Slot& s){
    return (s.life && s.life->discarded.load(std::memory_order_acquire)) || s.allocator.Invalidated();
}
static bool Matches(const Slot& s,uint64_t list) {
    return s.active && (reinterpret_cast<uint64_t>(s.cmd)==list || (s.native && s.native==list));
}
static void CollectLocked() {
    if(nrfault033::Blocked())return; // Retain existing NR leases; do not infer faulted work completed.
    for(auto& s:slots) {
        const bool destroyed=Discarded(s);
        if(!s.active || (!s.reset&&!destroyed) || s.recording || s.pending || s.pinned)continue;
        bool complete=true,observed=false;
        for(auto& u:s.uses)if(u.queue){observed=true;const auto done=u.fence?u.fence->GetCompletedValue():0;
            if(!u.fence || done==UINT64_MAX || done<u.value)complete=false;}
        // A reset without any Execute observation might be abandoned recording
        // OR an uncovered submission entry. Keep it pinned rather than guess.
        if(!commandlife::MayRetire(s.reset,destroyed,s.recording,s.pending,s.pinned,observed,complete))continue;
        if(!s.reset && s.allocator.Invalidated())++allocator_retired;
        for(auto& p:s.refs)if(p){p->Release();p=nullptr;}
        for(auto& u:s.uses){if(u.fence)u.fence->Release();if(u.queue)u.queue->Release();u={};}
        // Reuse the now-idle descriptor heap; avoid allocating one every frame.
        auto heap=s.heap;s={};s.heap=heap;++retired;
    }
}
static void Reset(uint64_t list) {
    gputime::ResetCommand(list);
    std::lock_guard<std::mutex> lock(mutex);
    for(auto& s:slots)if(Matches(s,list))s.reset=true;
    CollectLocked();
}
// The existing real-frame diagnostics opt in only that thread. Do not add
// global driver hooks or file I/O on arbitrary game/driver worker threads.
static thread_local unsigned resetTraceCalls=0;
static HRESULT STDMETHODCALLTYPE ResetHook(ID3D12GraphicsCommandList* cl,ID3D12CommandAllocator* alloc,ID3D12PipelineState* pso) {
    const bool trace=gputime::submit::trace && resetTraceCalls<16;
    const unsigned call=trace?++resetTraceCalls:0;
    auto phase=[&](const char* name){if(trace)Log("[033 startup reset] tid=%lu call=%u list=%p allocator=%p phase=%s",GetCurrentThreadId(),call,cl,alloc,name);};
    phase("native-reset-begin");
    const HRESULT hr=original_reset(cl,alloc,pso);
    if(trace)Log("[033 startup reset] tid=%lu call=%u list=%p phase=native-reset-returned hr=%08X",GetCurrentThreadId(),call,cl,unsigned(hr));
    if(SUCCEEDED(hr)){
        if(auto notify=nrnative033::resetNotice.load(std::memory_order_acquire))notify(uint64_t(cl));
        phase("retirement-begin");
        Reset(reinterpret_cast<uint64_t>(cl));
        phase("retirement-returned");
        // Only already observed NR lists get an allocator association. Never
        // attach watches to every unrelated game list reset by this entry.
        phase("list-lifetime-lookup-begin");
        auto life=commandlife::Existing(cl);
        phase("list-lifetime-lookup-returned");
        if(life){
            phase("allocator-observe-begin");
            auto allocator=commandlife::Observe(alloc);
            phase("allocator-observe-returned");
            commandlife::BindAllocator(life,allocator);
            phase("allocator-bind-returned");
            if(alloc)requested_allocator_reset.store((*reinterpret_cast<void***>(alloc))[8]);
        }
    }
    phase("hook-return");
    return hr;
}
static HRESULT STDMETHODCALLTYPE AllocatorResetHook(ID3D12CommandAllocator* allocator) {
    auto life=commandlife::Existing(allocator);
    const auto result=original_allocator_reset(allocator);
    commandlife::AllocatorReset(life,result);return result;
}
static void InstallAllocatorReset() {
    std::unique_lock<std::mutex> install(install_mutex,std::try_to_lock);if(!install.owns_lock())return;
    void* target=requested_allocator_reset.load();if(!target || allocator_reset_installed.load())return;
    auto threads=mfgunlock::hook::internal::OpenOtherThreads();
    LONG error=DetourTransactionBegin();
    if(error==NO_ERROR){
        error=DetourUpdateThread(GetCurrentThread());
        for(HANDLE t:threads)if(error==NO_ERROR)error=DetourUpdateThread(t);
        original_allocator_reset=reinterpret_cast<NativeAllocatorReset>(target);
        if(error==NO_ERROR)error=DetourAttach(reinterpret_cast<PVOID*>(&original_allocator_reset),reinterpret_cast<PVOID>(&AllocatorResetHook));
        if(error==NO_ERROR)error=DetourTransactionCommit();else DetourTransactionAbort();
        if(error==NO_ERROR)allocator_reset_installed.store(true);else original_allocator_reset=nullptr;
    }
    for(HANDLE t:threads)CloseHandle(t);
}
static void InstallReset() {
    std::unique_lock<std::mutex> install(install_mutex,std::try_to_lock);if(!install.owns_lock())return;
    void* target=requested_reset.load();if(!target || reset_installed.load())return;
    auto threads=mfgunlock::hook::internal::OpenOtherThreads();
    LONG error=DetourTransactionBegin();
    if(error==NO_ERROR) {
        error=DetourUpdateThread(GetCurrentThread());
        for(HANDLE t:threads)if(error==NO_ERROR)error=DetourUpdateThread(t);
        original_reset=reinterpret_cast<NativeReset>(target);
        if(error==NO_ERROR)error=DetourAttach(reinterpret_cast<PVOID*>(&original_reset),reinterpret_cast<PVOID>(&ResetHook));
        if(error==NO_ERROR)error=DetourTransactionCommit();else DetourTransactionAbort();
        if(error==NO_ERROR){reset_entry=target;gputime::TrackReset();reset_installed.store(true);}else original_reset=nullptr;
    }
    for(HANDLE t:threads)CloseHandle(t);
}
static void UninstallReset() {
    if(!reset_installed.load() && !allocator_reset_installed.load())return;
    auto threads=mfgunlock::hook::internal::OpenOtherThreads();
    LONG error=DetourTransactionBegin();
    if(error==NO_ERROR) {
        error=DetourUpdateThread(GetCurrentThread());
        for(HANDLE t:threads)if(error==NO_ERROR)error=DetourUpdateThread(t);
        if(error==NO_ERROR && reset_installed.load())error=DetourDetach(reinterpret_cast<PVOID*>(&original_reset),reinterpret_cast<PVOID>(&ResetHook));
        if(error==NO_ERROR && allocator_reset_installed.load())error=DetourDetach(reinterpret_cast<PVOID*>(&original_allocator_reset),reinterpret_cast<PVOID>(&AllocatorResetHook));
        if(error==NO_ERROR)error=DetourTransactionCommit();else DetourTransactionAbort();
        if(error==NO_ERROR){reset_installed.store(false);allocator_reset_installed.store(false);}
    }
    for(HANDLE t:threads)CloseHandle(t);
}
static void Submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists,bool after) {
    if(!lists)return;
    if(after)if(auto notify=nrnative033::submitNotice.load(std::memory_order_acquire))notify(queue,count,lists);
    std::lock_guard<std::mutex> lock(mutex);
    for(UINT i=0;i<count;++i)for(auto& s:slots)if(!s.reset && Matches(s,reinterpret_cast<uint64_t>(lists[i]))) {
        if(!after){++s.pending;continue;}
        if(!s.pending){s.pinned=true;continue;}
        --s.pending;
        Use* use=nullptr;
        for(auto& u:s.uses)if(u.queue==queue){use=&u;break;}
        if(!use)for(auto& u:s.uses)if(!u.queue){use=&u;u.queue=queue;queue->AddRef();break;}
        if(!use){s.pinned=true;continue;}
        Use* shared=nullptr;
        for(auto& q:queueFences)if(q.queue==queue){shared=&q;break;}
        if(!shared)for(auto& q:queueFences)if(!q.queue){shared=&q;q.queue=queue;queue->AddRef();break;}
        if(!shared){s.pinned=true;continue;}
        if(!shared->fence) {
            ID3D12Device* dev=nullptr;
            if(SUCCEEDED(queue->GetDevice(__uuidof(ID3D12Device),reinterpret_cast<void**>(&dev)))) {
                dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),reinterpret_cast<void**>(&shared->fence));
                dev->Release();
            }
        }
        if(!use->fence && shared->fence){use->fence=shared->fence;use->fence->AddRef();}
        use->value=++shared->value;
        if(!use->fence || FAILED(queue->Signal(use->fence,use->value)))s.pinned=true;
        else if(!s.submitted){s.submitted=true;++submitted;}
    }
}
static bool Pending(void* tag) {
    if(!tag)return false;std::lock_guard<std::mutex> lock(mutex);CollectLocked();
    for(const auto& s:slots)if(s.active && s.tag==tag)return true;return false;
}
static bool PendingRange(uintptr_t first,uintptr_t last) {
    std::lock_guard<std::mutex> lock(mutex);CollectLocked();
    for(const auto& s:slots)if(s.active && reinterpret_cast<uintptr_t>(s.tag)>=first && reinterpret_cast<uintptr_t>(s.tag)<=last)return true;
    return false;
}
struct RangeStatus {unsigned active=0,gpu=0,replayable=0,unknownAllocator=0;ULONGLONG oldestMs=0;};
static RangeStatus InspectRange(uintptr_t first,uintptr_t last){
    RangeStatus result;std::lock_guard<std::mutex> lock(mutex);const auto now=GetTickCount64();
    for(const auto& s:slots)if(s.active && reinterpret_cast<uintptr_t>(s.tag)>=first && reinterpret_cast<uintptr_t>(s.tag)<=last){
        ++result.active;if(!s.reset&&!Discarded(s))++result.replayable;
        if(!s.allocator.state)++result.unknownAllocator;
        result.oldestMs=(std::max)(result.oldestMs,now-s.began);
        bool pending=s.pending||s.recording||s.pinned||!s.submitted;
        for(const auto& u:s.uses)if(u.queue){const auto done=u.fence?u.fence->GetCompletedValue():0;
            pending|=!u.fence||done==UINT64_MAX||done<u.value;}
        if(pending)++result.gpu;
    }return result;
}
struct Ticket {Slot* slot=nullptr;unsigned long long generation=0;};
static Ticket GetTicket(Slot* slot){return {slot,slot?slot->generation:0};}
#ifdef K033_BETA2_RESHADE_HOST
// Only before the grade arming path records its first GPU command.
static void CancelUnrecorded(Ticket ticket){
    IUnknown* refs[48]={};
    {
        std::lock_guard<std::mutex> lock(mutex);auto* s=ticket.slot;
        if(!s||!s->active||s->generation!=ticket.generation||!s->recording||s->pending||s->submitted||s->pinned)return;
        for(unsigned i=0;i<48;++i){refs[i]=s->refs[i];s->refs[i]=nullptr;}
        auto heap=s->heap;if(recording==s)recording=nullptr;*s={};s->heap=heap;++retired;
    }
    for(auto* ref:refs)if(ref)ref->Release();
}
#endif
static bool Completed(Ticket ticket){
    if(!ticket.slot || !ticket.generation)return false;
    std::lock_guard<std::mutex> lock(mutex);CollectLocked();
    return !ticket.slot->active || ticket.slot->generation!=ticket.generation;
}
static Slot* Begin(ID3D12Device* dev,ID3D12GraphicsCommandList* cmd,IUnknown* const* refs,size_t count,void* tag=nullptr,
        leasewait033::Owner owner=leasewait033::Owner::Other) {
    if(!dev || !cmd || count>48){waitDiagnostic.Record(owner,leasewait033::Reason::Invalid);return nullptr;}
    if(!requested_device.load()){
        dev->AddRef();ID3D12Device* empty=nullptr;
        if(!requested_device.compare_exchange_strong(empty,dev))dev->Release();
    }
    void* target=(*reinterpret_cast<void***>(cmd))[10];
    // Only owners recording on the game's own render lists may claim the
    // single entry. Admission itself is unchanged: the same submit/reset/
    // target comparison decides, and a refused claimer whose list already
    // matches the installed entry is still admitted.
    if(resetentryowner033::MayClaim(owner,nr_enabled.load(std::memory_order_relaxed)))requested_reset.store(target);
    const auto refusal=leasewait033::Admission(gputime::submit::installed.load(),reset_installed.load(),
        reinterpret_cast<uintptr_t>(target),reinterpret_cast<uintptr_t>(reset_entry));
    if(refusal!=leasewait033::Reason::Ready){
        waitDiagnostic.Record(owner,refusal,reinterpret_cast<uintptr_t>(cmd),
            reinterpret_cast<uintptr_t>(target),reinterpret_cast<uintptr_t>(reset_entry));
        ++unavailable;return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    CollectLocked();
    bool heapFailed=false;
    for(auto& s:slots)if(!s.active) {
        D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors=nrstack::DescriptorCount;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(s.heap){ID3D12Device* previous=nullptr;s.heap->GetDevice(__uuidof(ID3D12Device),reinterpret_cast<void**>(&previous));
            const bool same=previous==dev;if(previous)previous->Release();if(!same){s.heap->Release();s.heap=nullptr;}}
        if(!s.heap){
            const auto heapResult=dev->CreateDescriptorHeap(&hd,__uuidof(ID3D12DescriptorHeap),reinterpret_cast<void**>(&s.heap));
            if(FAILED(heapResult)){
                heapFailed=true;waitDiagnostic.Record(owner,leasewait033::Reason::HeapFailed,reinterpret_cast<uintptr_t>(cmd),
                    reinterpret_cast<uintptr_t>(target),reinterpret_cast<uintptr_t>(reset_entry),uint32_t(heapResult));break;
            }
        }
        for(size_t i=0;i<count;++i){s.refs[i]=refs[i];if(s.refs[i])s.refs[i]->AddRef();}
        s.life=commandlife::Observe(cmd);s.allocator=commandlife::AllocatorFor(s.life);s.began=GetTickCount64();
        s.tag=tag;s.cmd=cmd;s.active=true;s.recording=true;recording=&s;s.generation=++admitted;
        unsigned active=0;for(const auto& item:slots)if(item.active)++active;
        if(active>high_water)high_water=active;
        return &s;
    }
    if(!heapFailed)waitDiagnostic.Record(owner,leasewait033::Reason::PoolFull);
    ++skipped;return nullptr;
}
static bool Hold(Slot* slot,IUnknown* value) {
    if(!slot || !value)return true;std::lock_guard<std::mutex> lock(mutex);
    for(auto ref:slot->refs)if(ref==value)return true;
    for(auto& ref:slot->refs)if(!ref){ref=value;value->AddRef();return true;}
    slot->pinned=true;return false;
}
static void End() {
    std::lock_guard<std::mutex> lock(mutex);
    if(recording)recording->recording=false;
    recording=nullptr;
}
static void Alias(uint64_t native) {
    if(!recording)return;
    std::lock_guard<std::mutex> lock(mutex);recording->native=native;
}
static void Pump(ID3D12CommandQueue* q,bool enabled) {
    if(nrfault033::Blocked())return;
    if(enabled) {
        gputime::submit::observer.store(Submitted,std::memory_order_release);
        // A DX11/Vulkan present cannot supply a native D3D12 queue although a
        // Feeder/interop callback does supply its D3D12 device. Use an idle
        // queue only to discover the native Execute entry; actual completion
        // still comes exclusively from the queue that submits each lease.
        if(!q && !gputime::submit::installed.load()) {
            std::lock_guard<std::mutex> lock(mutex);
            if(!observation_queue && requested_device.load()) {
                D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
                requested_device.load()->CreateCommandQueue(&desc,__uuidof(ID3D12CommandQueue),reinterpret_cast<void**>(&observation_queue));
            }
            q=observation_queue;
        }
        // Log outside the Detours transaction: logging while another thread
        // is suspended could itself wait on that thread's CRT lock.
        static unsigned submitTraces=0,resetTraces=0,allocatorTraces=0;
        const bool traceSubmit=diagnostic033::StartupTraces && q&&!gputime::submit::installed.load()&&submitTraces++<3;
        if(traceSubmit)Log("[033 startup] submission observer install begin");
        if(q)gputime::submit::Install(q);
        if(traceSubmit)Log("[033 startup] submission observer install returned ready=%d",gputime::submit::installed.load());
        const bool traceReset=diagnostic033::StartupTraces && requested_reset.load()&&!reset_installed.load()&&resetTraces++<3;
        if(traceReset)Log("[033 startup] command reset observer install begin");
        InstallReset();
        if(traceReset)Log("[033 startup] command reset observer install returned ready=%d",reset_installed.load());
        const bool traceAllocator=diagnostic033::StartupTraces && requested_allocator_reset.load()&&!allocator_reset_installed.load()&&allocatorTraces++<3;
        if(traceAllocator)Log("[033 startup] allocator reset observer install begin");
        InstallAllocatorReset();
        if(traceAllocator)Log("[033 startup] allocator reset observer install returned ready=%d",allocator_reset_installed.load());
    }
    std::lock_guard<std::mutex> lock(mutex);CollectLocked();
    static ULONGLONG reported=0;
    const auto now=GetTickCount64();
    if(enabled && now-reported>=5000) {
        reported=now;unsigned active=0,pinned=0,awaitReset=0,awaitGpu=0,unobserved=0,lifetimes=0,allocators=0;
        for(const auto& s:slots)if(s.active){++active;if(s.pinned)++pinned;
            if(!s.reset)++awaitReset;if(!s.submitted)++unobserved;if(s.life)++lifetimes;if(s.allocator.state)++allocators;
            bool pending=false;for(const auto& u:s.uses)if(u.queue && (!u.fence || u.fence->GetCompletedValue()<u.value || u.fence->GetCompletedValue()==UINT64_MAX))pending=true;
            if(pending)++awaitGpu;
        }
        using O=leasewait033::Owner;using Rn=leasewait033::Reason;
        auto W=[](O o,Rn r){return waitDiagnostic.Count(o,r);};
        Log("[033 GPU leases] recorded=%llu bypass=%llu retired=%llu active=%u retained=%u reset_hook=%d capacity=%u high_water=%u await_reset=%u await_gpu=%u unobserved=%u native_lifetimes=%u allocator_known=%u allocator_hook=%d allocator_retired=%llu diag_v=2 reset_reserved=%d grade_wait=%llu/%llu/%llu/%llu/%llu/%llu gradepresent_wait=%llu/%llu/%llu/%llu/%llu/%llu nr_wait=%llu/%llu/%llu/%llu/%llu/%llu",
            admitted,skipped+unavailable.load(),retired,active,pinned,reset_installed.load()?1:0,
            Capacity,high_water,awaitReset,awaitGpu,unobserved,lifetimes,allocators,allocator_reset_installed.load()?1:0,allocator_retired,
            nr_enabled.load(std::memory_order_relaxed)?1:0,
            W(O::Grade,Rn::Invalid),W(O::Grade,Rn::SubmitMissing),W(O::Grade,Rn::ResetMissing),W(O::Grade,Rn::ResetDifferent),W(O::Grade,Rn::HeapFailed),W(O::Grade,Rn::PoolFull),
            W(O::GradePresent,Rn::Invalid),W(O::GradePresent,Rn::SubmitMissing),W(O::GradePresent,Rn::ResetMissing),W(O::GradePresent,Rn::ResetDifferent),W(O::GradePresent,Rn::HeapFailed),W(O::GradePresent,Rn::PoolFull),
            W(O::NR,Rn::Invalid),W(O::NR,Rn::SubmitMissing),W(O::NR,Rn::ResetMissing),W(O::NR,Rn::ResetDifferent),W(O::NR,Rn::HeapFailed),W(O::NR,Rn::PoolFull));
        // Existing low-frequency control pump only; at most eight extra
        // lines per process, not per-frame I/O and never from a hook.
        static bool detailsReported[leasewait033::Owners][2]{};
        for(unsigned who=0;who<leasewait033::Owners;++who)for(unsigned kind=0;kind<2;++kind){
            const auto& sample=waitDiagnostic.samples[who][kind];
            if(!detailsReported[who][kind]&&sample.published.load(std::memory_order_acquire)==2){
                detailsReported[who][kind]=true;
                Log("[033 lease refusal] v=2 owner=%u reason=%s list=%p reset_target=%p installed_entry=%p hr=%08X; CPU metadata only",
                    who,kind?"heap-failed":"reset-entry-different",reinterpret_cast<void*>(sample.command),
                    reinterpret_cast<void*>(sample.target),reinterpret_cast<void*>(sample.entry),unsigned(sample.result));
            }
        }
    }
}
static void Counts(unsigned long long& processed,unsigned long long& bypass) {
    std::lock_guard<std::mutex> lock(mutex);processed=submitted;bypass=skipped+unavailable.load();
}
#ifndef K033_LEASE_STANDALONE
static void OnBind(reshade::api::command_list* cl,reshade::api::pipeline_stage,reshade::api::pipeline) {
    if(recording && cl)Alias(cl->get_native());
}
static void OnDestroy(reshade::api::command_list* cl) {if(cl)Reset(cl->get_native());}
static void RegisterEvents() {
    reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroy);
    reshade::register_event<reshade::addon_event::bind_pipeline>(OnBind);
}
static void UnregisterEvents() {
    reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnDestroy);
    reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBind);
    UninstallReset();
    // Unsubmitted/unobserved work remains pinned until process teardown.
    // Detaching the observer early would destroy the proof of GPU completion.
}
#endif
}
