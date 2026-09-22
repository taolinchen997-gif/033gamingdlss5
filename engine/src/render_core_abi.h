#pragma once
#include <cstdint>
// Stable, borrowed-only C boundary. No STL, ImGui contexts or allocation crosses it.
namespace k033core {
#ifdef K033_BETA2_RESHADE_HOST
constexpr uint32_t Version=2;
#else
constexpr uint32_t Version=1;
#endif
enum Owner:uint32_t { Unclaimed=0, Host033=1, NativeVulkan=2, CoreDx12=3 };
enum Source:uint32_t { Dx12=1, Dx11Bridge=2, VulkanBridge=3, Vulkan=4 };
struct Frame {
    uint32_t size=sizeof(Frame),version=Version;
    void* command=nullptr;
    void* parameters=nullptr;
    void* queue=nullptr;
    uintptr_t stream=0;
    int32_t feature=-1;
    uint32_t flags=0,haveFlags=0,outputW=0,outputH=0,source=Dx12;
#ifdef K033_BETA2_RESHADE_HOST
    uint64_t featureGeneration=0,evaluateEpoch=0;
    int(__cdecl* current)(const Frame*)=nullptr;
#endif
};
struct Status {
    uint32_t size=sizeof(Status),version=Version,owner=0,provider=0;
    uint64_t offered=0,processed=0,withheld=0;
};
using AfterUpscale=int(__cdecl*)(const Frame*);
struct Api {
    uint32_t size=sizeof(Api),version=Version;
    int(__cdecl* claim)(uint32_t)=nullptr;
    void(__cdecl* status)(Status*)=nullptr;
    void(__cdecl* showMenu)()=nullptr;
    void(__cdecl* setEnabled)(int)=nullptr;
    // Borrow a real NVIDIA capability block owned by this core, never an input
    // shim's emulated parameter object. Called during deferred feature creation.
    void*(__cdecl* capabilities)(void* device)=nullptr;
};
using GetApi=const Api*(__cdecl*)(uint32_t);
// NGX features whose output receives NR: 1 = DLSS super resolution, 13 = ray reconstruction
// (as ShortFuse's SF 0.54 does). 12 is DeepDVC, a colour filter, not an upscaler.
// 2026-09-13: the RE-engine cores still listed 12, so RE9 with ray reconstruction never got NR.
inline bool Image(int32_t feature){return feature==1 || feature==13;}
inline bool Valid(const Frame* f){return f && f->size==sizeof(Frame) && f->version==Version && Image(f->feature)
    && f->command && f->parameters && f->stream && f->haveFlags<=1 && f->source>=Dx12 && f->source<=VulkanBridge;}
}
