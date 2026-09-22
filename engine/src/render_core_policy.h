#pragma once
#include "render_core_abi.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
namespace k033core {
// Select the same renderer before any backend can claim process ownership.
// Native game SR/FG calls continue; only the competing NR renderer is excluded.
inline bool OwnerAllowed(uint32_t provider,uint32_t owner) {
    return provider==0 ? owner==Host033 : provider==1 && (owner==CoreDx12 || owner==NativeVulkan);
}
// Ownership is process-lifetime: a backend change cannot create a second NR
// device behind a still-live feature. Changing APIs after NR starts needs restart.
class Ownership {
    std::atomic<uint32_t> value{Unclaimed};
public:
    bool claim(uint32_t owner){if(owner<Host033||owner>CoreDx12)return false;
        uint32_t expected=Unclaimed;return value.compare_exchange_strong(expected,owner)||expected==owner;}
    uint32_t get()const{return value.load();}
};
struct FeatureEpoch {uint64_t generation=0;std::atomic<uint64_t> evaluation{0};std::atomic<bool> live{true};};
struct Metadata {int32_t feature=-1;uint32_t flags=0,haveFlags=0,outputW=0,outputH=0;std::shared_ptr<FeatureEpoch> epoch;};
class Ledger {
    std::mutex mutex;
    std::unordered_map<uintptr_t,Metadata> entries;
public:
    void created(uintptr_t handle,bool success,const Metadata& m){
        if(!handle||!success)return;std::lock_guard<std::mutex> lock(mutex);
        if(auto old=entries.find(handle);old!=entries.end()&&old->second.epoch)old->second.epoch->live=false;
        static std::atomic<uint64_t> next{0};auto copy=m;copy.epoch=std::make_shared<FeatureEpoch>();
        copy.epoch->generation=++next;entries[handle]=std::move(copy);
    }
    Metadata get(uintptr_t handle){std::lock_guard<std::mutex> lock(mutex);auto it=entries.find(handle);return it==entries.end()?Metadata{}:it->second;}
    void released(uintptr_t handle,bool success){if(!success)return;std::lock_guard<std::mutex> lock(mutex);
        auto it=entries.find(handle);if(it!=entries.end()&&it->second.epoch)it->second.epoch->live=false;entries.erase(handle);}
};
struct Context {Metadata info;uintptr_t stream=0;uint32_t source=Dx12;unsigned depth=0;bool dispatched=false,completed=false;uint64_t evaluation=0;};
inline thread_local Context context;
struct Scope {
    Context previous;
    Scope(Metadata info,uintptr_t stream,uint32_t source):previous(context){
        context={info,stream,source,previous.depth+1,false};
        if(info.epoch)context.evaluation=++info.epoch->evaluation;
    }
    ~Scope(){context=previous;}
};
inline bool FirstOffer(){if(context.depth!=1 || context.dispatched || !context.completed || !Image(context.info.feature))return false;context.dispatched=true;return true;}
#ifdef K033_BETA2_RESHADE_HOST
inline int __cdecl CurrentFrame(const Frame* f){
    const auto& c=context;const auto& e=c.info.epoch;
    return f&&c.depth==1&&c.completed&&e&&e->live.load()&&f->stream==c.stream&&
        f->featureGeneration==e->generation&&f->evaluateEpoch==c.evaluation&&
        e->evaluation.load()==c.evaluation;
}
#endif
}
