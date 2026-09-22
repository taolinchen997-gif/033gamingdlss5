#pragma once
#include "../include/033_nr.h"
#include <cmath>
#include <cstddef>
#include <initializer_list>
namespace k033 {
using NrLayer=K033_NrLayer;
static_assert(sizeof(K033_NrLayer)==32 && offsetof(K033_NrSettings,skin_structure)==136 && offsetof(K033_NrSettings,natural_look)==156 && offsetof(K033_NrSettings,final_clarity)==152 && sizeof(K033_NrSettings)==160,"NR settings v6 must append without shifting legacy fields");
inline NrLayer nr_default_layer(){return {0,0,1,0,1,1,1,1};}
inline K033_NrSettings nr_defaults(bool enabled=true){return {sizeof(K033_NrSettings),6,uint32_t(enabled),1,1,{nr_default_layer(),nr_default_layer(),nr_default_layer()},{0,0,0},0,{-1,-1,-1},.35f,0.f,0.f};}
inline bool nr_sr_work(unsigned layer,uint32_t value){return layer<3 && (!value || (value>=(layer?50u:25u) && value<=(layer?100u:200u)));}
inline bool nr_layer(const NrLayer& p){
    if(p.style>2||p.preset>3||p.auto_mask>1||p.ui_correction>1)return false;
    for(float v:{p.intensity,p.local_structure,p.local_tone,p.global_tone})
        if(!std::isfinite(v)||v<0||v>2)return false;
    return true;
}
inline bool nr_settings(const K033_NrSettings& c){
    if(c.size!=sizeof(c)||c.version!=6||c.enabled>1||!c.layers||c.layers>3||!c.appearance_epoch||c.reserved)return false;
    if(!std::isfinite(c.natural_look)||c.natural_look<0||c.natural_look>1)return false;
    if(!std::isfinite(c.final_clarity)||c.final_clarity<0||c.final_clarity>1)return false;
    if(!std::isfinite(c.skin_lift)||c.skin_lift<0||c.skin_lift>1)return false;
    for(unsigned i=0;i<3;++i)if(!nr_layer(c.layer[i])||!nr_sr_work(i,c.sr_work[i])||
        !std::isfinite(c.skin_structure[i])||c.skin_structure[i]<-1||c.skin_structure[i]>2)return false;return true;
}
inline bool nr_same_appearance(const K033_NrSettings& a,const K033_NrSettings& b){
    if(a.enabled!=b.enabled||a.layers!=b.layers||a.skin_lift!=b.skin_lift||a.final_clarity!=b.final_clarity||a.natural_look!=b.natural_look)return false;
    for(unsigned i=0;i<3;++i)if(a.skin_structure[i]!=b.skin_structure[i])return false;
    for(unsigned i=0;i<3;++i)if(a.sr_work[i]!=b.sr_work[i])return false;
    for(unsigned i=0;i<3;++i){const auto& x=a.layer[i];const auto& y=b.layer[i];
        if(x.style!=y.style||x.preset!=y.preset||x.auto_mask!=y.auto_mask||x.ui_correction!=y.ui_correction||x.intensity!=y.intensity||
           x.local_structure!=y.local_structure||x.local_tone!=y.local_tone||x.global_tone!=y.global_tone)return false;}
    return true;
}
// Only content travels through the common file. Runtime epochs stay local.
inline bool nr_equal(const K033_NrSettings& a,const K033_NrSettings& b){return a.appearance_epoch==b.appearance_epoch&&nr_same_appearance(a,b);}
// Never mutate an aligned aggregate passed by value: the observed x86 object
// wrote through those parameter pointers. Keep inputs const and copy explicitly.
inline K033_NrSettings nr_persistent(const K033_NrSettings& source){auto value=source;value.appearance_epoch=1;return value;}
inline int nr_apply(K033_NrSettings& current,const K033_NrSettings& requested){
    if(!nr_settings(requested))return K033_INVALID;
    if(nr_same_appearance(current,requested))return K033_OK;
    if(current.appearance_epoch==UINT64_MAX)return K033_UNSUPPORTED;
    auto next=requested;next.appearance_epoch=current.appearance_epoch+1;current=next;return K033_OK;
}
}
