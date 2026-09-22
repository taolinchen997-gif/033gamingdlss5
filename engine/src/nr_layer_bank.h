#pragma once
#include "nr_layer_settings.h"
namespace nrlayers {
inline bool InitializationComplete(uint64_t completed,uint64_t target){return target!=0 && completed!=UINT64_MAX && completed>=target;}
inline bool BorrowStillValid(bool reused,uint64_t saved,uint64_t current){return !reused || saved==current;}
template<class Bank> void ExcludeBorrowed(Bank& retired,const Bank& live){
    ExcludeShared(retired.feat,live.feat);
    ExcludeShared(retired.full,live.full);ExcludeShared(retired.model_input,live.model_input);
    ExcludeShared(retired.out,live.out);ExcludeShared(retired.res,live.res);
    ExcludeShared(retired.spare_full,live.spare_full);ExcludeShared(retired.spare_res,live.spare_res);
    ExcludeShared(retired.extra_out,live.extra_out);ExcludeShared(retired.extra_alt,live.extra_alt);
    ExcludeShared(retired.pass_input,live.pass_input);ExcludeShared(retired.pass_input3,live.pass_input3);
    ExcludeShared(retired.hold_depth,live.hold_depth);ExcludeShared(retired.hold_motion,live.hold_motion);ExcludeShared(retired.hold_white,live.hold_white);
    for(int i=0;i<nrfeatures::MaxPasses-1;++i)ExcludeShared(retired.extra_feat[i],live.extra_feat[i]);
    for(int i=0;i<2;++i){ExcludeShared(retired.hist[i],live.hist[i]);ExcludeShared(retired.refined[i],live.refined[i]);}
}
// Candidate is a private pointer snapshot. The callback may only initialize new
// features, never write the borrowed frame textures. Caller owns publication,
// initialization fence and lease-gated retirement; failure leaves live intact.
template<class Bank,class Config,class Create> bool ReplaceFeatures(Bank& candidate,const Bank& live,const Config& requested,Create create){
    const int count=nrfeatures::ClampPasses(requested.passes);
    for(int i=0;i<count;++i){
        auto& feature=i==0?candidate.feat:candidate.extra_feat[i-1];
        const auto tune=Get(requested,i);
        if(!feature || !Equal(Get(live.model_cfg,i),tune)){
            feature=nullptr; // Old pointer remains owned by live.
            if(!create(i,tune,feature))return false;
        }
    }
    candidate.model_cfg=requested;
    // Disabled cached features still describe their actual create-time request.
    for(int i=count;i<nrfeatures::MaxPasses;++i)candidate.model_cfg.extra[i-1]=live.model_cfg.extra[i-1];
    candidate.selected_passes=count;
    if(candidate.built_passes<count)candidate.built_passes=count;
    return true;
}
}
