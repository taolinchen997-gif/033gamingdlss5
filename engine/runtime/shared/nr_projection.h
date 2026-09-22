// Identical scalar math is compiled by CPU tests and the two NR compute shaders.
#ifdef __cplusplus
#pragma once
#include <algorithm>
#include <cmath>
namespace k033nr {
using std::isfinite;using std::pow;using std::abs;using std::min;using std::max;using std::sqrt;
#define NR_INLINE inline
#else
#define NR_INLINE
#endif
struct Rgb {float r,g,b;};
NR_INLINE Rgb rgb(float r,float g,float b){Rgb v;v.r=r;v.g=g;v.b=b;return v;}
NR_INLINE float peak(Rgb c){return max(c.r,max(c.g,c.b));}
NR_INLINE bool finite_rgb(Rgb c){return isfinite(c.r)&&isfinite(c.g)&&isfinite(c.b);}
NR_INLINE float to_srgb(float x){return x<=0.0031308f?12.92f*x:1.055f*pow(max(x,0.f),1.f/2.4f)-0.055f;}
NR_INLINE float to_linear(float x){return x<=0.04045f?x/12.92f:pow(max((x+0.055f)/1.055f,0.f),2.4f);}
// Reuse old 033 replica=1's peak-coupled Neutwo colour bridge from
// engine/src/scale.h (NeutwoEncode/Decode) and nr_color_hlsl.inl (NrBoundEdit).
// Upstream RenoDX/OptiScaler attribution and licenses remain in the package.
// One scalar preserves RGB proportions before the sRGB transfer; the previous
// per-channel x/(1+x) curve changed hue and was not the old working bridge.
// Gains are explicit 033 model-projection units, not inferred physical nits or
// a claim about an intermediate game's O exposure. Old fixed replica policy is
// encode_gain=1/3.16 and return_gain=3.16. Gamma wraps the entire NR stack once.
NR_INLINE bool encodable(Rgb c,float gain){
    return finite_rgb(c)&&isfinite(gain)&&gain>0&&isfinite(max(0.f,peak(c))*gain);
}
NR_INLINE Rgb neutwo_encode(Rgb v){
    v=rgb(max(v.r,0.f),max(v.g,0.f),max(v.b,0.f));float m=peak(v);
    if(m<=1e-6f)return v;
    // The gain range and finite FP16 source fit m*m in float. Keep the exact
    // old scalar curve rather than substituting per-channel compression.
    float scale=1.f/sqrt(m*m+1.f);return rgb(v.r*scale,v.g*scale,v.b*scale);
}
NR_INLINE Rgb neutwo_decode(Rgb v){
    v=rgb(max(v.r,0.f),max(v.g,0.f),max(v.b,0.f));
    float m=min(peak(v),.999999f);if(m<=1e-6f)return v;
    float scale=1.f/sqrt(max(1.f-m*m,1e-8f));return rgb(v.r*scale,v.g*scale,v.b*scale);
}
NR_INLINE Rgb encode(Rgb c,float gain){
    Rgb v=neutwo_encode(rgb(c.r*gain,c.g*gain,c.b*gain));
    return rgb(to_srgb(min(v.r,1.f)),to_srgb(min(v.g,1.f)),to_srgb(min(v.b,1.f)));
}
NR_INLINE Rgb resolve_gains(Rgb original,Rgb proxy,Rgb model,float gain,float return_gain){
    if(!encodable(original,gain)||!isfinite(return_gain)||return_gain<=0||!finite_rgb(proxy)||!finite_rgb(model))return original;
    // Preserve exact source identity, including negative channels and values
    // above the model domain, before inverse-curve floating-point arithmetic.
    if(proxy.r==model.r&&proxy.g==model.g&&proxy.b==model.b)return original;
    if(min(proxy.r,min(proxy.g,proxy.b))<0||peak(proxy)>1||min(model.r,min(model.g,model.b))<0||peak(model)>1)return original;
    Rgb p=rgb(to_linear(proxy.r),to_linear(proxy.g),to_linear(proxy.b));
    Rgb m=rgb(to_linear(model.r),to_linear(model.g),to_linear(model.b));
    Rgb baseline=neutwo_decode(p),answer=neutwo_decode(m);
    Rgb d=rgb((answer.r-baseline.r)*return_gain,(answer.g-baseline.g)*return_gain,(answer.b-baseline.b)*return_gain);
    if(!finite_rgb(d))return original;
    float span=max(abs(d.r),max(abs(d.g),abs(d.b)));if(span==0)return original;
    float scene=max(0.f,peak(original));if(scene==0)return original;
    float change=max(abs(m.r-p.r),max(abs(m.g-p.g),abs(m.b-p.b)));
    // One source-relative scalar bounds inverse-curve amplification while
    // preserving residual direction and all unchanged original HDR values.
    float budget=scene*3.f*change/max(peak(p),1.f/512.f);
    float a=min(1.f,budget/span);
    if(d.r<0)a=min(a,max(0.f,original.r)/-d.r);
    if(d.g<0)a=min(a,max(0.f,original.g)/-d.g);
    if(d.b<0)a=min(a,max(0.f,original.b)/-d.b);
    if(d.r>0)a=min(a,max(0.f,scene*3.f-original.r)/d.r);
    if(d.g>0)a=min(a,max(0.f,scene*3.f-original.g)/d.g);
    if(d.b>0)a=min(a,max(0.f,scene*3.f-original.b)/d.b);
    a=max(0.f,a);Rgb result=rgb(original.r+d.r*a,original.g+d.g*a,original.b+d.b*a);
    if(finite_rgb(result))return result;return original;
}
NR_INLINE Rgb resolve(Rgb original,Rgb proxy,Rgb model,float gain){return resolve_gains(original,proxy,model,gain,1.f/gain);}
#undef NR_INLINE
#ifdef __cplusplus
}
#endif
