#pragma once
#include "../include/033_runtime.h"
#include "../shared/pregrade.h"
#include <memory>
namespace k033 {
struct Backend {
    virtual ~Backend()=default;
    virtual int process(const K033_Frame&, const pregrade::Settings&)=0;
    virtual int poll()=0;
    virtual uint32_t pending() const=0;
    virtual uint64_t completed() const=0;
};
std::unique_ptr<Backend> make_backend(const K033_Attach&, int& result);
inline K033_Settings defaults() {return {sizeof(K033_Settings),1,1,0,0,1,1,0,0,0,1};}
inline pregrade::Settings grade(const K033_Settings& s) {
    return {s.enabled,s.exposure,s.contrast,s.saturation,s.warmth,s.tint,s.highlights,0,s.style,s.style_strength};
}
inline bool valid(const K033_Settings& s) {
    return s.size==sizeof(s)&&s.version==1&&pregrade::Valid(grade(s));
}
inline bool needs_grade(const pregrade::Settings& s) {
    return s.enabled&&(s.exposure!=0||s.contrast!=1||s.saturation!=1||s.warmth!=0||s.tint!=0||s.highlights!=0||(s.style!=0&&s.styleStrength!=0));
}
// Bounded ownership shared by actual backends and CPU failure-injection tests.
struct Slots {
    uint64_t values[3]={}, submitted=0, retired=0;
    bool poisoned=false;
    int free_slot() const {for(int i=0;i<3;++i)if(!values[i])return i;return -1;}
    uint32_t pending() const {return uint32_t(!!values[0])+uint32_t(!!values[1])+uint32_t(!!values[2]);}
    uint64_t issue(int i) {values[i]=++submitted;return submitted;}
    void retire(int i) {if(values[i]){values[i]=0;++retired;}}
};
}
