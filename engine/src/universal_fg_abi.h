#pragma once
#include <cstdint>
namespace ufg033abi {
constexpr uint32_t Version=4;
struct Create {uint32_t size=sizeof(Create),version=Version;void* factory=nullptr;void* queue=nullptr;void* hwnd=nullptr;
    const void* desc=nullptr;const void* fullscreen=nullptr;const wchar_t* library=nullptr;void** output=nullptr;};
struct Status {uint32_t size=sizeof(Status),version=Version;uint32_t prepared=0,available=0,requested=0,error=0;
    uint64_t realFrames=0,generated=0,composedReal=0,composedGenerated=0,skipped=0,allocations=0,workingBytes=0;
    uint32_t reason=1,sdkReset=0;uint64_t intervalMs=0,warmup=0,busy=0,gaps=0,late=0,flowUnavailable=0;
    char flowReason[48]{};
    uint32_t bootRoute=0,selectedRoute=0,routePending=0,routeBlocked=0;
    uint32_t multiplier=2,maxMultiplier=3;uint64_t presentedReal=0,presentedGenerated=0,presentFailed=0;int32_t presentError=0;
};
struct Api {uint32_t size=sizeof(Api),version=Version;int(__cdecl* create)(const Create*)=nullptr;
    int(__cdecl* read)(Status*)=nullptr;int(__cdecl* prepare)(int)=nullptr;void(__cdecl* enable)(int)=nullptr;int(__cdecl* multiplier)(int)=nullptr;int(__cdecl* route)(int)=nullptr;};
using GetApi=const Api*(__cdecl*)(uint32_t);
}
