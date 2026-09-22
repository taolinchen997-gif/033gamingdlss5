// Observe the native D3D12 submission RETURN. ReShade's execute event is before
// submission and may miss NGX's unwrapped lists, so it cannot prove completion.
#pragma once
#include <atomic>
#include "mfg/ngx_hook.hpp"
namespace gputime {
static void AfterSubmit(ID3D12GraphicsCommandList*, ID3D12CommandQueue*, bool);
namespace submit {
using Execute = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
static Execute original = nullptr;
static void* entry = nullptr;
static std::atomic_bool installed{false};
static SRWLOCK install_lock = SRWLOCK_INIT;
static std::atomic<unsigned long long> calls{0};
using Observer=void(*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*,bool);
static std::atomic<Observer> observer{nullptr};
using TraceObserver=void(*)(const char*,unsigned,ID3D12CommandQueue*,UINT);
static thread_local TraceObserver trace=nullptr;
static thread_local unsigned traceCalls=0;
static void STDMETHODCALLTYPE Hook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    const auto notify=observer.load(std::memory_order_acquire);
    const auto sink=trace && traceCalls<32?trace:nullptr;
    const unsigned sequence=sink?++traceCalls:0;
    if(sink)sink("lease-before-begin",sequence,queue,count);
    if(notify)notify(queue,count,lists,false);
    if(sink)sink("native-execute-begin",sequence,queue,count);
    original(queue, count, lists);
    if(sink)sink("native-execute-returned",sequence,queue,count);
    if(notify)notify(queue,count,lists,true);
    if(sink)sink("lease-after-returned",sequence,queue,count);
    calls.fetch_add(1, std::memory_order_relaxed);
    // The caller owns this array until we return. AfterSubmit only compares list
    // identities; it does not change, reset or execute them a second time.
    if (lists) for (UINT i=0;i<count;++i)
        AfterSubmit(reinterpret_cast<ID3D12GraphicsCommandList*>(lists[i]), queue, true);
    if(sink)sink("timing-returned",sequence,queue,count);
}
static bool Install(ID3D12CommandQueue* queue) {
    if (!queue) return false;
    auto* target = (*reinterpret_cast<void***>(queue))[10]; // ID3D12CommandQueue::ExecuteCommandLists
    if (installed.load(std::memory_order_acquire)) return target == entry;
    if (!TryAcquireSRWLockExclusive(&install_lock)) return false;
    bool result=false;
    if (!installed.load()) {
        auto threads=mfgunlock::hook::internal::OpenOtherThreads();
        LONG error=DetourTransactionBegin();
        if (error==NO_ERROR) {
            error=DetourUpdateThread(GetCurrentThread());
            for (HANDLE t:threads) if(error==NO_ERROR) error=DetourUpdateThread(t);
            original=reinterpret_cast<Execute>(target);
            if(error==NO_ERROR) error=DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(&Hook));
            if(error==NO_ERROR) error=DetourTransactionCommit(); else DetourTransactionAbort();
            if(error==NO_ERROR) {entry=target;installed.store(true,std::memory_order_release);result=true;}
            else original=nullptr;
        }
        for(HANDLE t:threads) CloseHandle(t);
    } else result=target==entry;
    ReleaseSRWLockExclusive(&install_lock);
    return result;
}
static void Uninstall() {
    if(!installed.load()) return;
    AcquireSRWLockExclusive(&install_lock);
    auto threads=mfgunlock::hook::internal::OpenOtherThreads();
    LONG error=DetourTransactionBegin();
    if(error==NO_ERROR){
        error=DetourUpdateThread(GetCurrentThread());
        for(HANDLE t:threads) if(error==NO_ERROR)error=DetourUpdateThread(t);
        if(error==NO_ERROR)error=DetourDetach(reinterpret_cast<PVOID*>(&original),reinterpret_cast<PVOID>(&Hook));
        if(error==NO_ERROR)error=DetourTransactionCommit();else DetourTransactionAbort();
        if(error==NO_ERROR)installed.store(false);
    }
    for(HANDLE t:threads)CloseHandle(t);
    ReleaseSRWLockExclusive(&install_lock);
}
} // namespace submit
} // namespace gputime
