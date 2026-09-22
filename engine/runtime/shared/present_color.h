// Pure CPU/HLSL colour math for a separately admitted FINAL swapchain lease.
// This file cannot establish source ownership, declaration success, generation,
// completion or qualification. The native owner must prove those independently.
// Never apply these modes to NGX O merely because it has a float texture format.
#ifdef __cplusplus
#pragma once
#endif
#include "nr_projection.h"
#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
namespace k033present {
using k033nr::Rgb;using k033nr::rgb;
using std::min;using std::max;using std::pow;using std::abs;using std::isfinite;
#define PC_INLINE inline
using PcUint=uint32_t;
#else
#define PC_INLINE
#define PcUint uint
#endif

// Values describe the actual sampled values, not just the resource's storage.
// SDR requires a raw UNORM view: a typed _SRGB SRV already performs a decode.
// scRGB is signed linear BT.709, with exactly 80 nits per stored unit.
// HDR10 is raw PQ-coded BT.2020. A PQ code outside [0,1] is left unchanged.
static const PcUint PcUnknown=0;
static const PcUint PcSdrSrgbUnorm=1;
static const PcUint PcScRgbLinear709=2;
static const PcUint PcHdr10Pq2020=3;

// Internal shader constants only; not a public ABI or a qualification receipt.
// The owner supplies white/gains from its actually applied unit-conversion
// receipt. Zero/unset is invalid; nothing here derives them from C exposure.
struct PresentColorConstants {
    PcUint width,height,mode,reserved;
    float source_white_nits,working_unit_nits,encode_gain_per_unit,return_units;
};
#ifdef __cplusplus
static_assert(sizeof(PresentColorConstants)==32&&
              offsetof(PresentColorConstants,source_white_nits)==16,
              "present colour cbuffer layout");
#endif

PC_INLINE bool pc_finite(Rgb v){return isfinite(v.r)&&isfinite(v.g)&&isfinite(v.b);}
PC_INLINE Rgb pc_add(Rgb a,Rgb b){return rgb(a.r+b.r,a.g+b.g,a.b+b.b);}
PC_INLINE Rgb pc_sub(Rgb a,Rgb b){return rgb(a.r-b.r,a.g-b.g,a.b-b.b);}
PC_INLINE Rgb pc_scale(Rgb a,float b){return rgb(a.r*b,a.g*b,a.b*b);}
PC_INLINE bool pc_same(Rgb a,Rgb b){return a.r==b.r&&a.g==b.g&&a.b==b.b;}
PC_INLINE float pc_sat(float v){return min(1.f,max(0.f,v));}
PC_INLINE bool pc_mode(PcUint mode,float sdr_white_nits){
    if(mode==PcSdrSrgbUnorm)return isfinite(sdr_white_nits)&&sdr_white_nits>0.f;
    return mode==PcScRgbLinear709||mode==PcHdr10Pq2020;
}
PC_INLINE bool pc_source(Rgb source,PcUint mode,float sdr_white_nits){
    if(!pc_mode(mode,sdr_white_nits)||!pc_finite(source))return false;
    if(mode==PcHdr10Pq2020)
        return min(source.r,min(source.g,source.b))>=0.f&&
               max(source.r,max(source.g,source.b))<=1.f;
    return true;
}
PC_INLINE bool pc_projection_units(float encode_gain_per_unit,float return_units){
    return isfinite(encode_gain_per_unit)&&encode_gain_per_unit>0.f&&
           isfinite(return_units)&&return_units>0.f;
}
PC_INLINE bool pc_work_units(float source_white_nits,float working_unit_nits){
    return isfinite(source_white_nits)&&source_white_nits>0.f&&
           isfinite(working_unit_nits)&&working_unit_nits>0.f;
}
// Native private work textures are explicitly R16G16B16A16_FLOAT. Out-of-range
// RGB is bypassed back to raw, never silently clipped into a different scene.
PC_INLINE bool pc_half_work(Rgb v){
    return pc_finite(v)&&max(abs(v.r),max(abs(v.g),abs(v.b)))<=65504.f;
}

// Scalar transcription of the existing nr_color_hlsl.inl ST.2084 and matrices.
// The same expression is now compiled by CPU and HLSL; no new tone mapper.
PC_INLINE float pc_pq_to_nits(float v){
    const float m1=2610.f/16384.f,m2=2523.f/32.f;
    const float c1=3424.f/4096.f,c2=2413.f/128.f,c3=2392.f/128.f;
    float p=pow(pc_sat(v),1.f/m2);
    return 10000.f*pow(max(p-c1,0.f)/max(c2-c3*p,1e-7f),1.f/m1);
}
PC_INLINE float pc_nits_to_pq(float v){
    const float m1=2610.f/16384.f,m2=2523.f/32.f;
    const float c1=3424.f/4096.f,c2=2413.f/128.f,c3=2392.f/128.f;
    float p=pow(pc_sat(v/10000.f),m1);
    return pow((c1+c2*p)/(1.f+c3*p),m2);
}
PC_INLINE Rgb pc_2020_to_709(Rgb v){
    return rgb(1.6604910f*v.r-0.5876411f*v.g-0.0728499f*v.b,
              -0.1245505f*v.r+1.1328999f*v.g-0.0083494f*v.b,
              -0.0181508f*v.r-0.1005789f*v.g+1.1187297f*v.b);
}
PC_INLINE Rgb pc_709_to_2020(Rgb v){
    return rgb(0.6274039f*v.r+0.3292830f*v.g+0.0433131f*v.b,
               0.0690973f*v.r+0.9195404f*v.g+0.0113623f*v.b,
               0.0163914f*v.r+0.0880133f*v.g+0.8955953f*v.b);
}
// Same knee, low-luminance branch and inverse guard as NrGamut. This mapping is
// only for the HDR model working domain; it is NOT absolute linear709 storage.
PC_INLINE Rgb pc_nr_gamut(Rgb v,bool inverse_mapping){
    float y=0.2126f*v.r+0.7152f*v.g+0.0722f*v.b;
    if(y<=1e-7f)return rgb(max(v.r,0.f),max(v.g,0.f),max(v.b,0.f));
    float radius=max(0.f,1.f-min(v.r,min(v.g,v.b))/y);
    const float knee=0.8f;
    if(radius<=knee)return v;
    float mapped=radius;
    if(inverse_mapping){
        float u=min((radius-knee)/(1.f-knee),0.99999f);
        mapped=knee+(1.f-knee)*u/(1.f-u);
    }else{
        float u=(radius-knee)/(1.f-knee);
        mapped=knee+(1.f-knee)*u/(1.f+u);
    }
    return rgb(y+(v.r-y)*(mapped/radius),y+(v.g-y)*(mapped/radius),
               y+(v.b-y)*(mapped/radius));
}

// Mathematical decode only: caller must first check pc_source and retain the
// original texel. Signed/subblack values are not clipped in the linear storage.
PC_INLINE Rgb pc_decode_nits(Rgb source,PcUint mode,float sdr_white_nits){
    if(mode==PcSdrSrgbUnorm){
#ifdef __cplusplus
        return rgb(k033nr::to_linear(source.r)*sdr_white_nits,
                   k033nr::to_linear(source.g)*sdr_white_nits,
                   k033nr::to_linear(source.b)*sdr_white_nits);
#else
        return rgb(to_linear(source.r)*sdr_white_nits,to_linear(source.g)*sdr_white_nits,
                   to_linear(source.b)*sdr_white_nits);
#endif
    }
    if(mode==PcScRgbLinear709)return pc_scale(source,80.f);
    if(mode==PcHdr10Pq2020)return pc_2020_to_709(rgb(pc_pq_to_nits(source.r),
        pc_pq_to_nits(source.g),pc_pq_to_nits(source.b)));
    return rgb(0.f,0.f,0.f);
}
PC_INLINE Rgb pc_model_nits(Rgb absolute_nits,PcUint mode){
    if(mode==PcHdr10Pq2020)return pc_nr_gamut(absolute_nits,false);
    return absolute_nits;
}
PC_INLINE Rgb pc_unmap_model_nits(Rgb model_nits,PcUint mode){
    if(mode==PcHdr10Pq2020)return pc_nr_gamut(model_nits,true);
    return model_nits;
}
PC_INLINE Rgb pc_decode_work(Rgb source,PcUint mode,float source_white_nits,float working_unit_nits){
    // Pure conversion primitive: caller checks pc_work_units/pc_source and the
    // finite/half-range result, as both actual decode shader entries do.
    return pc_scale(pc_decode_nits(source,mode,source_white_nits),1.f/working_unit_nits);
}
PC_INLINE Rgb pc_prepare_work(Rgb source,PcUint mode,float source_white_nits,float working_unit_nits){
    return pc_scale(pc_model_nits(pc_decode_nits(source,mode,source_white_nits),mode),1.f/working_unit_nits);
}
PC_INLINE Rgb pc_store_nits(Rgb nits,PcUint mode,float sdr_white_nits){
    if(mode==PcScRgbLinear709)return pc_scale(nits,1.f/80.f);
    if(mode==PcHdr10Pq2020){
        Rgb wide=pc_709_to_2020(nits);
        return rgb(pc_nits_to_pq(wide.r),pc_nits_to_pq(wide.g),pc_nits_to_pq(wide.b));
    }
#ifdef __cplusplus
    return rgb(k033nr::to_srgb(nits.r/sdr_white_nits),
               k033nr::to_srgb(nits.g/sdr_white_nits),k033nr::to_srgb(nits.b/sdr_white_nits));
#else
    return rgb(to_srgb(nits.r/sdr_white_nits),to_srgb(nits.g/sdr_white_nits),
               to_srgb(nits.b/sdr_white_nits));
#endif
}

// Baseline is the exact private pc_model_nits(pc_decode_nits(original)) image
// BEFORE any pregrade/model edit. The owner binds both images to the same lease.
// Returning a residual around the original absolute base also preserves signed
// components lost by NrGamut's low-y branch and matrix/transfer roundoff.
// No edit: bypass every transfer/matrix and return the original loaded values.
PC_INLINE Rgb pc_return_model_nits(Rgb original,Rgb baseline,Rgb edited,
                                  PcUint mode,float sdr_white_nits){
    if(pc_same(baseline,edited))return original;
    if(!pc_source(original,mode,sdr_white_nits)||!pc_finite(baseline)||!pc_finite(edited))return original;
    Rgb decoded=pc_decode_nits(original,mode,sdr_white_nits);
    Rgb base_unmapped=pc_unmap_model_nits(baseline,mode);
    Rgb edit_unmapped=pc_unmap_model_nits(edited,mode);
    Rgb delta=pc_sub(edit_unmapped,base_unmapped);
    if(!pc_finite(decoded)||!pc_finite(delta)||pc_same(delta,rgb(0.f,0.f,0.f)))return original;
    Rgb target=pc_add(decoded,delta);
    if(!pc_finite(target))return original;
    Rgb stored=pc_store_nits(target,mode,sdr_white_nits);
    if(!pc_finite(stored))return original;
    return stored;
}

// Native Return uses normalized MODEL work from PrepareModel, including the
// same HDR-only mapping. Decode's absolute linear work is a distinct image.
// Actual R16 quantization belongs to the retained baseline; do not recompute a
// mathematically ideal baseline instead of that actual private texture.
PC_INLINE Rgb pc_return_work(Rgb original,Rgb baseline_work,Rgb edited_work,PcUint mode,
                            float source_white_nits,float working_unit_nits){
    if(pc_same(baseline_work,edited_work))return original;
    if(!pc_work_units(source_white_nits,working_unit_nits)||
       !pc_source(original,mode,source_white_nits)||!pc_half_work(baseline_work)||
       !pc_half_work(edited_work))return original;
    Rgb expected=pc_prepare_work(original,mode,source_white_nits,working_unit_nits);
    if(!pc_half_work(expected))return original;
    return pc_return_model_nits(original,pc_scale(baseline_work,working_unit_nits),
        pc_scale(edited_work,working_unit_nits),mode,source_white_nits);
}

// Optional pure helpers: reuse the EXACT existing NR projection, no second
// gamma/NR algorithm. Native integration may instead call its existing backend.
PC_INLINE Rgb pc_encode_projection(Rgb model_work,float encode_gain_per_unit){
#ifdef __cplusplus
    if(!k033nr::encodable(model_work,encode_gain_per_unit))return rgb(0.f,0.f,0.f);
    return k033nr::encode(model_work,encode_gain_per_unit);
#else
    if(!encodable(model_work,encode_gain_per_unit))return rgb(0.f,0.f,0.f);
    return encode(model_work,encode_gain_per_unit);
#endif
}
PC_INLINE Rgb pc_resolve_projection(Rgb original,Rgb proxy,Rgb model,PcUint mode,
        float source_white_nits,float working_unit_nits,float encode_gain_per_unit,float return_units){
    if(pc_same(proxy,model))return original;
    if(!pc_work_units(source_white_nits,working_unit_nits)||!pc_source(original,mode,source_white_nits)||
       !pc_projection_units(encode_gain_per_unit,return_units))return original;
    Rgb baseline=pc_prepare_work(original,mode,source_white_nits,working_unit_nits);
    if(!pc_half_work(baseline))return original;
    Rgb edited;
#ifdef __cplusplus
    edited=k033nr::resolve_gains(baseline,proxy,model,encode_gain_per_unit,return_units);
#else
    edited=resolve_gains(baseline,proxy,model,encode_gain_per_unit,return_units);
#endif
    return pc_return_work(original,baseline,edited,mode,source_white_nits,working_unit_nits);
}

#undef PC_INLINE
#ifdef __cplusplus
}
#else
#undef PcUint
#endif
