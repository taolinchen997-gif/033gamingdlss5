#pragma once
#include <cstdint>
namespace nrlifetime {
constexpr uint32_t Version=2;
struct Snapshot {
    uint32_t size=sizeof(Snapshot),version=Version;
    uint64_t created=0,released=0,releaseFailed=0,fullBuilds=0,passChanges=0;
    uint64_t usage=0,budget=0,timingSamples=0;
    uint32_t parkedFeatures=0,parkedObjects=0,activePasses=0,extraW=0,extraH=0;
    uint32_t cachedPasses=0,candidatePasses=0;
    double lastBuildMs=0,gpuMs=0;
};
using Read=int(__cdecl*)(Snapshot*);
}
