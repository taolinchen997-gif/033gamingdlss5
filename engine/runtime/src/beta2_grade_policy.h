#pragma once
#include "inline_grade_program.h"
namespace beta2grade {
// GPU completion proves retirement, never an interrupted O resource state.
// Only a different successful feature generation resets output uncertainty.
struct FaultState {
    uint64_t generation=0;bool output_uncertain=false;
    void feature(uint64_t next){if(next&&next!=generation){generation=next;output_uncertain=false;}}
    void outcome(int result,bool armed){if(result!=K033_OK&&armed)output_uncertain=true;}
    bool admissible()const{return generation&&!output_uncertain;}
    bool uncertain_for(uint64_t current)const{return generation==current&&output_uncertain;}
};
inline bool recyclable(bool idle,const FaultState& state){return idle&&!state.output_uncertain;}
inline bool may_retire(bool armed,bool ticket,bool completed){return ticket?completed:!armed;}
}
