// Internal 033 image-generation API. No swapchain hooks and no Present calls.
// All calls for a handle are serialized by its rendering owner. The caller
// retains the engine module until destroy succeeds and executes all recordings
// in order on one queue. The fence belongs exclusively to that queue.
#pragma once
#include <cstdint>
namespace fgflow033abi {
constexpr uint32_t Version=1;
struct Ticket {uint32_t index=3,reserved=0;uint64_t generation=0;};
struct Record {
    uint32_t size=sizeof(Record),version=Version;
    void* list=nullptr;void* previous=nullptr;void* current=nullptr;void* output=nullptr;void* protection=nullptr;
    float phase=.5f;uint32_t flowWidth=0,flowHeight=0,radius=4,reset=0;
};
struct Status {uint32_t size=sizeof(Status),version=Version;uint64_t allocationBatches=0,workingBytes=0;};
struct Api {
    uint32_t size=sizeof(Api),version=Version;
    void*(__cdecl* createDx12)(void* device,void* completionFence)=nullptr;
    int(__cdecl* recordDx12)(void* handle,const Record*,Ticket*)=nullptr;
    int(__cdecl* submitted)(void* handle,Ticket,uint64_t signalValue)=nullptr;
    // The command list must be discarded (never executed) before this call.
    int(__cdecl* discarded)(void* handle,Ticket)=nullptr;
    int(__cdecl* status)(void* handle,Status*)=nullptr;
    // Returns 0 while pending; the caller retains and retries the same handle.
    int(__cdecl* destroy)(void* handle)=nullptr;
};
using GetApi=const Api*(__cdecl*)(uint32_t);
}
