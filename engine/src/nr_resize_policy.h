#pragma once
#include <algorithm>
#include <cstdint>
namespace nrresize033 {
struct Geometry {
    const void* device=nullptr;
    uint32_t width=0,height=0,guideWidth=0,guideHeight=0;
    int format=0;
};
inline bool GeometryChanged(const Geometry& current,const Geometry& wanted) {
    return current.device!=wanted.device ||
        current.width!=wanted.width || current.height!=wanted.height ||
        current.guideWidth!=wanted.guideWidth || current.guideHeight!=wanted.guideHeight ||
        current.format!=wanted.format;
}
// Upper bound for one new model bank: up to RGBA32F; the first NR dimensions
// may be 200% per axis. An unknown budget (0) never blocks a creation.
inline uint64_t CreationBytes(uint32_t w,uint32_t h,uint32_t gw,uint32_t gh,int passes) {
    const uint64_t thirdW=(std::max)(uint64_t(64),uint64_t((std::max)(w,gw))*2u);
    const uint64_t thirdH=(std::max)(uint64_t(64),uint64_t((std::max)(h,gh))*2u);
    return uint64_t(w)*h*(64u+128u*uint64_t((std::max)(1,passes)))+(passes>2?thirdW*thirdH*16u:0u);
}
inline bool OverlapFits(uint64_t usage,uint64_t budget,uint64_t bytes) {
    return !budget || (usage<budget && bytes<=budget-usage);
}
// A model built for another geometry renders nothing (Stage skips NR until the
// wanted geometry is built); it only holds memory. The presentation route
// always retires it first. The upscale route keeps the overlap, so the old
// model resumes at once if the game returns to its size, unless the new model
// cannot fit beside it: waiting for memory would keep NR off for as long as the
// budget stays short (YanYun S33: 57 s after an SR model change).
inline bool DrainBeforeBuild(bool presentation,bool hasModel,const Geometry& current,const Geometry& wanted,bool overlapFits) {
    return hasModel && (presentation || !overlapFits) && GeometryChanged(current,wanted);
}
// The caller supplies actual lease/fence retirement, not an elapsed-frame
// approximation. This does not block the CPU or record additional GPU work.
template<class Retire,class Collect,class Pending>
bool Prepare(bool drain,Retire&& retire,Collect&& collect,Pending&& pending) {
    if(drain){retire();collect();}
    return !pending();
}
}
