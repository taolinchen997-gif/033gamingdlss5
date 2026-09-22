#pragma once
#include <cstdint>
namespace gradepresentpolicy {
// This token is per real presentation, not an NGX Evaluate/feature token.
struct Once {
    uintptr_t runtime=0;uint64_t generation=0,serial=0;bool seen=false,recorded=false,uncertain=false;
    bool enter(uintptr_t r,uint64_t g,uint64_t s){
        if(!r||!g||!s)return false;
        if(runtime!=r||generation!=g){runtime=r;generation=g;serial=0;seen=false;recorded=uncertain=false;}
        if(uncertain||(seen&&serial==s))return false;
        serial=s;seen=true;recorded=false;return true;
    }
    void finish(bool didRecord,bool outputUncertain){recorded=didRecord;uncertain|=outputUncertain;}
    bool current(uintptr_t r,uint64_t g,uint64_t s)const{return runtime==r&&generation==g&&serial==s&&seen;}
};
inline bool admit(bool immediate,bool sameSurface,uint64_t ngxOffers){return immediate&&sameSurface&&ngxOffers==0;}
inline bool color(uint32_t encoding,bool rgba8,bool rgb10,bool fp16){
    return (encoding==1&&(rgba8||rgb10))||(encoding==2&&rgb10)||(encoding==3&&fp16);
}
// Input color uses this declared arrival for both capture and writeback.
// Native record calls this helper rather than assuming every source is a UAV.
template<class State>struct Transitions {State arrival,copySource,copyDest;
    State captureBefore()const{return arrival;}State captureAfter()const{return copySource;}
    State restoreCaptureBefore()const{return copySource;}State restoreCaptureAfter()const{return arrival;}
    State writeBefore()const{return arrival;}State writeAfter()const{return copyDest;}
    State restoreWriteBefore()const{return copyDest;}State restoreWriteAfter()const{return arrival;}
};
}
