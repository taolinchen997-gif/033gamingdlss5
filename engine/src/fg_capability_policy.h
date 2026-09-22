#pragma once
#include <atomic>
// Input capability is observed from the caller's feature protocol. Neither a
// GPU model, a loaded interposer nor a DLSS-SR call establishes DLSS-G input.
namespace fgcap033 {
enum Status : unsigned { Unknown=0, NoInput=1, OldInterface=2, Incompatible=3, Ready=4 };
class Evidence {
    std::atomic<unsigned> status{Unknown};
public:
    unsigned Read()const{return status.load();}
    bool NativeAllowed()const{return Read()==Ready;}
    void Observe(unsigned value){
        if(value>Ready)return;
        // A second (e.g. SR-only) API cannot erase a validated FG input.
        auto old=status.load();
        while(old!=Ready && old<value && !status.compare_exchange_weak(old,value)){}
    }
};
inline unsigned Streamline(bool valid,bool requestsFG,bool compatible,bool initialized){
    if(!valid)return Incompatible;
    if(!requestsFG)return NoInput;
    return compatible&&initialized?Ready:Incompatible;
}
template<class Feature> bool Requests(const Feature* features,unsigned count,Feature fg){
    if(count>64 || (count&&!features))return false;
    for(unsigned i=0;i<count;++i)if(features[i]==fg)return true;
    return false;
}
// 2026-09-12: the automatic choice stays native. Without positive DLSS-G input evidence this
// used to answer Universal, and its only caller runs while the 033 panel is open, so opening
// the panel once in a game with no frame generation wrote Universal into the shared settings -
// which are global, not per game. The next launch booted 033's own swapchain and a UE4 title
// died with "D3D device being lost". Universal is now only ever an explicit panel choice;
// Selectable still offers it, and Selection::Resolve keeps fg_automatic set so an automatic
// write can be corrected by a later one.
inline unsigned DefaultRoute(bool nativeAllowed){(void)nativeAllowed;return 0u;}
inline bool Selectable(unsigned route,bool nativeAllowed){return route==1u||(route==0u&&nativeAllowed);}
}
