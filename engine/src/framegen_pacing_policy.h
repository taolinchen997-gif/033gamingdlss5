#pragma once
#include <algorithm>
#include <cstdint>
namespace fgpace033 {
// Set once after the first composed image is ready. Later GPU or present
// delays consume this batch's interval; they must not shift every next slot.
class Batch {
    uint64_t first=0,step=0;
    unsigned count=0;
public:
    void Begin(uint64_t previous,uint64_t ready,uint64_t interval,unsigned n){
        step=interval;count=n;
        first=std::max(ready,previous>UINT64_MAX-step?UINT64_MAX:previous+step);
    }
    uint64_t Due(unsigned index)const{
        const uint64_t distance=uint64_t(index)*step;
        return first>UINT64_MAX-distance?UINT64_MAX:first+distance;
    }
    // Discard only a synthetic presentation whose successor is already due.
    // Its composition and retirement fences still run in the caller.
    bool Stale(unsigned index,uint64_t now,bool real)const{
        return !real&&index+1<count&&step&&now>=Due(index+1);
    }
};
// The ten-frame average can over-delay frames after the user removes an NR
// layer. Cap it by the latest completed interval, retaining the existing
// variance/safety margin calculation in the caller.
inline double Interval(double average,double latest,unsigned count){
    return std::min(average,latest>0?latest:average)/double(count?count:1);
}
}
