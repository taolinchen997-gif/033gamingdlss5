#pragma once
#include <cstdint>
#include <cmath>
namespace k033 {
// S48 companion proposal. Uses the owner's existing NrConfig.direct_o fields;
// does not introduce a second projection configuration or derive either gain.
struct NrDirectOFrameKey {
    uint64_t producer=0,epoch=0,sequence=0,input_ticket=0;
    uint64_t feature_generation=0,feature_revision=0,control_epoch=0,color_lease=0;
};
inline bool nr_direct_o_key(const NrDirectOFrameKey& k){
    return k.producer&&k.epoch&&k.sequence&&k.input_ticket&&
        k.feature_generation&&k.control_epoch&&k.color_lease;
}
inline bool nr_direct_o_same_key(const NrDirectOFrameKey& a,const NrDirectOFrameKey& b){
    return a.producer==b.producer&&a.epoch==b.epoch&&a.sequence==b.sequence&&
        a.input_ticket==b.input_ticket&&a.feature_generation==b.feature_generation&&
        a.feature_revision==b.feature_revision&&a.control_epoch==b.control_epoch&&a.color_lease==b.color_lease;
}
// current/captured are precisely NrConfig.direct_o, not the original C semantics.
// Rules is an INTERNAL owner of a reviewed, actually-applied conversion receipt.
// Numeric wire fields, an SDK success, IsHDR or a texture format cannot be Rules.
template<class DirectO,class Rules>
bool nr_direct_o_bound(const NrDirectOFrameKey& current_key,const NrDirectOFrameKey& captured_key,
                      const DirectO& current,const DirectO& captured,const Rules& rules){
    if(!nr_direct_o_key(current_key)||!nr_direct_o_key(captured_key)||
       !nr_direct_o_same_key(current_key,captured_key)||
       !current.version||!current.rule_version||!current.rule||
       current.version!=captured.version||current.rule_version!=captured.rule_version||current.rule!=captured.rule||
       !std::isfinite(current.encode_gain)||current.encode_gain<=0||
       !std::isfinite(current.return_gain)||current.return_gain<=0||
       current.encode_gain!=captured.encode_gain||current.return_gain!=captured.return_gain)return false;
    // Do not assume return_gain == 1/encode_gain: the owner deliberately keeps
    // encoding and return domains independent. Their relation belongs to Rules.
    return rules.matches_applied_conversion(captured_key,captured);
}
// Current generic NGX capture performs a byte copy only. No registered rule can
// qualify it yet. This class must not be replaced by an always-true predicate.
struct NrUnknownGameOutputRule {
    template<class DirectO>bool matches_applied_conversion(const NrDirectOFrameKey&,const DirectO&)const{return false;}
};
}
