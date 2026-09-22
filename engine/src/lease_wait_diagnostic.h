#pragma once
#include <atomic>
#include <cstdint>
// Y1: bounded metadata only. No driver calls, hooks, waits or admission changes.
// Y4 appends GradePresent after NR so every previously recorded owner index is
// unchanged; array bounds now follow the enum instead of a literal.
namespace leasewait033 {
enum class Owner : unsigned { Other, Grade, NR, GradePresent, Count };
enum class Reason : unsigned { Ready, Invalid, SubmitMissing, ResetMissing, ResetDifferent, HeapFailed, PoolFull, Count };
constexpr unsigned Owners=unsigned(Owner::Count),Reasons=unsigned(Reason::Count);
inline Reason Admission(bool submitReady,bool resetReady,uintptr_t target,uintptr_t entry) {
    if(!submitReady)return Reason::SubmitMissing;
    if(!resetReady)return Reason::ResetMissing;
    return target==entry?Reason::Ready:Reason::ResetDifferent;
}
struct Sample {
    std::atomic<unsigned> published{0};
    uintptr_t command=0,target=0,entry=0;
    uint32_t result=0;
};
struct Recorder {
    std::atomic<unsigned long long> counts[Owners][Reasons]{};
    // Only one mismatch and one actual descriptor-allocation error per owner.
    // The Pump reader acquires publication; writers never overwrite a sample.
    Sample samples[Owners][2];
    void Record(Owner owner,Reason reason,uintptr_t command=0,uintptr_t target=0,uintptr_t entry=0,uint32_t result=0) {
        const auto o=unsigned(owner),r=unsigned(reason);
        if(o>=Owners||r==0||r>=Reasons)return;
        counts[o][r].fetch_add(1,std::memory_order_relaxed);
        if(reason!=Reason::ResetDifferent&&reason!=Reason::HeapFailed)return;
        auto& s=samples[o][reason==Reason::HeapFailed?1:0];unsigned empty=0;
        if(s.published.compare_exchange_strong(empty,1,std::memory_order_acquire)) {
            s.command=command;s.target=target;s.entry=entry;s.result=result;
            s.published.store(2,std::memory_order_release);
        }
    }
    unsigned long long Count(Owner owner,Reason reason)const{return counts[unsigned(owner)][unsigned(reason)].load(std::memory_order_relaxed);}
};
}
