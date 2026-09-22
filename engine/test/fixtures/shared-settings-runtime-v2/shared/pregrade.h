// Small deterministic pre-NR grade, shared by model input and composition base.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
namespace pregrade {
struct Settings {
    uint32_t enabled=0;
    float exposure=0,contrast=1,saturation=1,warmth=0,tint=0,highlights=0,skinProtection=0;
    uint32_t style=0;
    float styleStrength=1;
};
static_assert(sizeof(Settings)==40,"pregrade constant layout");
static bool Valid(const Settings& p) {
    return p.enabled<=1 && std::isfinite(p.exposure) && std::abs(p.exposure)<=2 &&
        std::isfinite(p.contrast) && p.contrast>=0.75f && p.contrast<=1.25f &&
        std::isfinite(p.saturation) && p.saturation>=0 && p.saturation<=1.5f &&
        std::isfinite(p.warmth) && std::abs(p.warmth)<=0.25f &&
        std::isfinite(p.tint) && std::abs(p.tint)<=0.25f &&
        std::isfinite(p.highlights) && p.highlights>=0 && p.highlights<=0.25f && std::isfinite(p.skinProtection) && p.skinProtection>=0 && p.skinProtection<=1 && p.style<=3 && std::isfinite(p.styleStrength) && p.styleStrength>=0 && p.styleStrength<=1;
}
static Settings Checked(const Settings* p) {return p && Valid(*p) ? *p : Settings{};}
static unsigned Signature(const Settings& p) {
    if(!p.enabled)return 0;
    uint32_t words[10];std::memcpy(words,&p,sizeof(words));unsigned h=2166136261u;
    for(unsigned i=0;i<10;++i)h=(h^words[i])*16777619u;
    return h;
}
}
