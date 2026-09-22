#pragma once
#include <cstdint>

// Dimensions are NR-internal. The game's SR input/output and guide rectangles
// never change. Both later layers scale independently from the first NR layer.
namespace nrlayersr {
struct Extent {uint32_t width=0,height=0;};
inline bool Equal(Extent a,Extent b){return a.width==b.width&&a.height==b.height;}
inline int ClampWork(int value){return value<50?50:(value>100?100:value);}
inline Extent Scale(Extent first,int work){
    const auto percent=uint32_t(ClampWork(work));
    const auto side=[&](uint32_t value){const uint64_t scaled=(uint64_t(value)*percent/100u)&~uint64_t(1);return uint32_t(scaled<64?64:scaled);};
    return {side(first.width),side(first.height)};
}
struct Layout {Extent layer[3];};
inline Layout Make(Extent first,int second,int third){return {{first,Scale(first,second),Scale(first,third)}};}
inline bool AnyScaled(const Layout& layout,int count){
    return (count>1&&!Equal(layout.layer[0],layout.layer[1]))||(count>2&&!Equal(layout.layer[0],layout.layer[2]));
}
template<class Bank> Layout Of(const Bank& b){return {{{b.sw,b.sh},{b.ew,b.eh},{b.tw,b.th}}};}
// Candidate is a pointer snapshot, never the live bank. Detach only changed
// layers; their old resources remain owned by live until publication + leases.
// A null feature forces the existing ReplaceFeatures path to recreate it even
// when the appearance controls did not change.
template<class Bank> unsigned ResizeCandidate(Bank& candidate,const Bank& live,int second,int third){
    const auto layout=Make({live.sw,live.sh},second,third);
    unsigned changed=0;
    if(!Equal(layout.layer[1],{live.ew,live.eh})){
        candidate.extra_out=nullptr;candidate.pass_input=nullptr;candidate.extra_feat[0]=nullptr;changed|=2;
        candidate.built_passes=1;
    }
    if(!Equal(layout.layer[2],{live.tw,live.th})){
        candidate.extra_alt=nullptr;candidate.pass_input3=nullptr;candidate.extra_feat[1]=nullptr;changed|=4;
        if(candidate.built_passes>2)candidate.built_passes=2;
    }
    candidate.ew=layout.layer[1].width;candidate.eh=layout.layer[1].height;
    candidate.tw=layout.layer[2].width;candidate.th=layout.layer[2].height;
    candidate.built_passwork=ClampWork(second);candidate.built_passwork3=ClampWork(third);
    return changed;
}
}
