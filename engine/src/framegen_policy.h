// 033 frame-generation scheduling primitives. No sleeps, GPU calls, or native
// Present calls live here. The presentation owner supplies its real deadline.
#pragma once
#include <array>
#include <cstdint>
#include <cmath>

namespace framegen033 {
class CallbackHistory {
    uint64_t last=0;bool have=false;
public:
    bool Duplicate(uint64_t id)const{return have&&id==last;}
    void Record(uint64_t id){last=id;have=true;}
    void Reset(){last=0;have=false;}
};
enum class Origin : uint32_t { Real, Generated };
struct Frame {
    uint64_t id=0, epoch=0, timeNs=0;
    uint32_t width=0,height=0,format=0;
    Origin origin=Origin::Real;
};
inline bool Pair(const Frame& a,const Frame& b) {
    return a.origin==Origin::Real && b.origin==Origin::Real && a.id && b.id>a.id &&
        a.epoch==b.epoch && a.width && a.height && a.width==b.width && a.height==b.height &&
        a.format==b.format && b.timeNs>a.timeNs && b.timeNs-a.timeNs<=100000000;
}
struct Slot {uint64_t generation=0,fence=0;bool reserved=false;};
struct Ticket {uint32_t index=3;uint64_t generation=0;explicit operator bool()const{return index<3;}};
// Fixed capacity. Completion is a queue fence, never an elapsed-frame estimate.
class Pool {
    std::array<Slot,3> slots{};
    uint64_t next=0,lastFence=0;
public:
    Ticket Acquire(uint64_t completed) {
        if(completed==UINT64_MAX)return {}; // D3D12 device removal sentinel
        for(uint32_t i=0;i<slots.size();++i){auto& s=slots[i];
            if(!s.reserved && (!s.fence || s.fence<=completed)){
                s={++next,0,true};return {i,s.generation};}}
        return {};
    }
    bool Submit(Ticket t,uint64_t fence) {
        if(!t || !fence || fence==UINT64_MAX || fence<=lastFence)return false;
        auto& s=slots[t.index];if(!s.reserved||s.generation!=t.generation)return false;
        s.reserved=false;s.fence=fence;lastFence=fence;return true;
    }
    bool Cancel(Ticket t) {
        if(!t)return false;auto& s=slots[t.index];
        if(!s.reserved||s.generation!=t.generation)return false;
        s.reserved=false;return true;
    }
    bool Idle(uint64_t completed)const {
        if(completed==UINT64_MAX)return false;
        for(const auto& s:slots)if(s.reserved || s.fence>completed)return false;
        return true;
    }
};
struct Presentation {float phase=1;uint64_t dueNs=0;bool real=true;};
struct Schedule {std::array<Presentation,6> frames{};uint32_t count=0,dropped=0;};
inline Schedule MakeSchedule(uint64_t intervalStart,uint64_t realDeadline,uint64_t now,
                             uint32_t multiplier,bool pairValid) {
    Schedule out;
    if(realDeadline>intervalStart && pairValid && multiplier>=2 && multiplier<=6){
        const uint64_t duration=realDeadline-intervalStart;
        for(uint32_t i=1;i<multiplier;++i){
            // Avoid overflow in duration*i; deadlines stay within the real window.
            const uint64_t due=intervalStart+(duration/multiplier)*i+(duration%multiplier)*i/multiplier;
            if(due<=now){++out.dropped;continue;}
            out.frames[out.count++]={float(i)/float(multiplier),due,false};
        }
    }
    // A late synthetic frame must never push back a real frame's deadline.
    out.frames[out.count++]={1.f,realDeadline>now?realDeadline:now,true};return out;
}
// A copied ready flag from the previous scene is insufficient. Every resource
// must belong to the exact real frame being submitted by the caller.
inline bool InputsMatch(uint64_t frame,uint64_t depth,uint64_t motion,uint64_t hudless) {
    return frame && depth==frame && motion==frame && hudless==frame;
}
}
