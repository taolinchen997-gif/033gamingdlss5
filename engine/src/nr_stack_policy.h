#pragma once
#include "nr_feature_policy.h"
namespace nrstack {
struct Tune {float intensity,structure,tone,skin,globalTone;int style,preset;};
inline Tune ForPass(int pass,float intensity,float structure,float tone,float skin,float globalTone=1.f,int style=0,int preset=0){
    if(pass<=0)return {intensity,structure,tone,skin,globalTone,style,preset};
    // Later layers refine an already styled image. Standard mode is the known
    // runtime default (not a documented style-free neural model). Do not inherit
    // first-layer cinema, preset or tone/skin strength. Fixed per-index budgets
    // keep layer-count cache switches deterministic.
    const float refinement=1.f/(1.f+float(pass));
    return {refinement,refinement,0.f,refinement,0.f,0,0};
}
}
