#pragma once
#include "nr_native_input_events.h"
#include <wrl/client.h>
#include <mutex>
namespace nrnative033 {
using BarrierFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
using EnhancedFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*,UINT,const D3D12_BARRIER_GROUP*);
using BarrierNotice=void(*)(uint64_t,UINT,const D3D12_RESOURCE_BARRIER*);
inline std::atomic<BarrierNotice> barrierNotice{nullptr};
inline std::atomic<void*> requested{nullptr},requestedEnhanced{nullptr};
inline std::atomic<bool> installed{false},failed{false},enhancedSeen{false};
inline void* entry=nullptr;
inline BarrierFn original=nullptr;
inline EnhancedFn originalEnhanced=nullptr;
inline std::mutex installMutex;
inline void STDMETHODCALLTYPE BarrierHook(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    original(list,count,barriers);
    if(auto notify=barrierNotice.load(std::memory_order_acquire))notify(uint64_t(list),count,barriers);
}
inline void STDMETHODCALLTYPE EnhancedHook(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups){
    if(count)enhancedSeen.store(true,std::memory_order_release);
    originalEnhanced(list,count,groups);
}
inline bool Request(ID3D12GraphicsCommandList* list){
    if(!list||failed.load()||enhancedSeen.load())return false;
    void* target=(*reinterpret_cast<void***>(list))[ResourceBarrierSlot];
    if(installed.load())return target==entry;
    if(!requested.load()){
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> extended;
        const auto hr=list->QueryInterface(IID_PPV_ARGS(&extended));
        if(FAILED(hr)&&hr!=E_NOINTERFACE){failed.store(true);return false;}
        if(extended)requestedEnhanced.store((*reinterpret_cast<void***>(extended.Get()))[EnhancedBarrierSlot]);
        requested.store(target,std::memory_order_release);
    }
    return false;
}
// Called only by the real-frame pump, outside NR recording and resource locks.
// Reuse the project's reviewed Detours transaction discipline. These callbacks
// only keep states of the eight semantic input resources; no DRED, GPU queries,
// disk logging, resource scanning, or per-frame CPU/GPU waits are added.
inline void Pump(){
    std::unique_lock lock(installMutex,std::try_to_lock);if(!lock.owns_lock())return;
    void* target=requested.load(std::memory_order_acquire);
    if(!target||installed.load()||failed.load())return;
    auto threads=mfgunlock::hook::internal::OpenOtherThreads();
    LONG error=DetourTransactionBegin();
    if(error==NO_ERROR){
        error=DetourUpdateThread(GetCurrentThread());
        for(HANDLE thread:threads)if(error==NO_ERROR)error=DetourUpdateThread(thread);
        original=reinterpret_cast<BarrierFn>(target);
        originalEnhanced=reinterpret_cast<EnhancedFn>(requestedEnhanced.load());
        if(error==NO_ERROR)error=DetourAttach(reinterpret_cast<PVOID*>(&original),reinterpret_cast<PVOID>(&BarrierHook));
        if(error==NO_ERROR&&originalEnhanced)error=DetourAttach(reinterpret_cast<PVOID*>(&originalEnhanced),reinterpret_cast<PVOID>(&EnhancedHook));
        if(error==NO_ERROR)error=DetourTransactionCommit();else DetourTransactionAbort();
    }
    for(HANDLE thread:threads)CloseHandle(thread);
    if(error==NO_ERROR){entry=target;installed.store(true,std::memory_order_release);}
    else{original=nullptr;originalEnhanced=nullptr;failed.store(true);}
}
inline void Uninstall(){
    barrierNotice.store(nullptr);resetNotice.store(nullptr);submitNotice.store(nullptr);
    std::unique_lock lock(installMutex,std::try_to_lock);if(!lock.owns_lock())return;
    if(!installed.load())return;
    auto threads=mfgunlock::hook::internal::OpenOtherThreads();
    LONG error=DetourTransactionBegin();
    if(error==NO_ERROR){
        error=DetourUpdateThread(GetCurrentThread());
        for(HANDLE thread:threads)if(error==NO_ERROR)error=DetourUpdateThread(thread);
        if(error==NO_ERROR)error=DetourDetach(reinterpret_cast<PVOID*>(&original),reinterpret_cast<PVOID>(&BarrierHook));
        if(error==NO_ERROR&&originalEnhanced)error=DetourDetach(reinterpret_cast<PVOID*>(&originalEnhanced),reinterpret_cast<PVOID>(&EnhancedHook));
        if(error==NO_ERROR)error=DetourTransactionCommit();else DetourTransactionAbort();
    }
    for(HANDLE thread:threads)CloseHandle(thread);
    if(error==NO_ERROR)installed.store(false);
}
}
