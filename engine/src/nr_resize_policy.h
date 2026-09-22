#pragma once
#include <cstdint>
namespace nrresize033 {
struct Geometry {
    const void* device=nullptr;
    uint32_t width=0,height=0,guideWidth=0,guideHeight=0;
    int format=0;
};
inline bool DrainBeforeBuild(bool presentation,bool hasModel,const Geometry& current,const Geometry& wanted) {
    return presentation && hasModel && (current.device!=wanted.device ||
        current.width!=wanted.width || current.height!=wanted.height ||
        current.guideWidth!=wanted.guideWidth || current.guideHeight!=wanted.guideHeight ||
        current.format!=wanted.format);
}
// The caller supplies actual lease/fence retirement, not an elapsed-frame
// approximation. This does not block the CPU or record additional GPU work.
template<class Retire,class Collect,class Pending>
bool Prepare(bool drain,Retire&& retire,Collect&& collect,Pending&& pending) {
    if(drain){retire();collect();}
    return !pending();
}
}
