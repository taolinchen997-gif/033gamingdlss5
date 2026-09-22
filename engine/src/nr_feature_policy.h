#pragma once
namespace nrfeatures {
inline constexpr int MaxPasses=3;
inline int ClampPasses(int value){return value<1?1:(value>MaxPasses?MaxPasses:value);}
template<class Config> inline void Restrict(Config& cfg){
    cfg.passes=ClampPasses(cfg.passes);
    cfg.faceboost=0.f;cfg.portrait_enabled=0;cfg.portrait_strength=0.f;
    cfg.pre.skinProtection=0.f;
}
}
