#pragma once
#include <cstdint>
#include <limits>

// Recover ordinary, returned failures only. A driver/runtime exception is not
// cleared here. State is owned by the existing NR writer; no timers or threads.
namespace nrrecovery033 {
struct Retry {
    uint64_t next=0;
    unsigned failures=0;
    bool Due(uint64_t now) const { return now>=next; }
    void Failed(uint64_t now) {
        const unsigned shift=failures<4?failures:4;
        const uint64_t delay=250ull<<shift;
        if(failures<32)++failures;
        next=now>UINT64_MAX-delay?UINT64_MAX:now+delay;
    }
    void Success(){next=0;failures=0;}
};

struct Evaluation {
    unsigned streak=0,pass=0,mask=0;
    uint64_t cooldown=0;
    // Three consecutive ordinary failures in the same layer request a fresh
    // feature through the existing candidate bank. One successful frame ends
    // the streak; settings, input checks and opaque fault state are untouched.
    bool Failed(unsigned failedPass,uint64_t now) {
        if(failedPass>=3)return false;
        if(streak && pass!=failedPass)streak=0;
        pass=failedPass;if(streak<3)++streak;
        if(streak<3 || now<cooldown)return false;
        const unsigned before=mask;mask|=1u<<failedPass;return mask!=before;
    }
    void Success(){streak=0;}
    bool Pending(unsigned count=3) const {return (mask&((1u<<(count<3?count:3))-1u))!=0;}
    // Called only after a signaled GPU initialization has completed and the
    // new bank was adopted. Borrowed/unreplaced models do not count as healed.
    void Adopted(unsigned replaced,uint64_t now) {
        const unsigned before=mask;mask&=~replaced;
        if(before && !mask){streak=0;cooldown=now>UINT64_MAX-5000?UINT64_MAX:now+5000;}
    }
};

template<class Bank> unsigned Replaced(const Bank& old,const Bank& fresh) {
    unsigned result=0;
    if(fresh.feat && fresh.feat!=old.feat)result|=1;
    for(unsigned i=0;i<2;++i)
        if(fresh.extra_feat[i] && fresh.extra_feat[i]!=old.extra_feat[i])result|=2u<<i;
    return result;
}
template<class Bank> void RequestFresh(Bank& candidate,unsigned mask) {
    if(mask&1)candidate.feat=nullptr;
    for(unsigned i=0;i<2;++i)if(mask&(2u<<i))candidate.extra_feat[i]=nullptr;
}
}
