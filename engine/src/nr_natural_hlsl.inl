#define K033_NATURAL_MATH(...) #__VA_ARGS__
#include "nr_natural_math.inl"
#undef K033_NATURAL_MATH
R"(
float4 FinalPicturePixel(uint2 at,float4 raw) {
    if(applymodel==0u || any(!isfinite(raw.rgb)))return raw;
    if(split>0.001f && float(at.x)<=split*float(dst_size.x)+2.0f)return raw;
    float4 sharpened=ClarityPixel(at,raw);
    // replace=5 reuses the existing strength constant for this output-only grade.
    // All ordinary NR composition dispatches keep their original interpretation.
    if(strength<=0.0f)return sharpened;
    float3 original=ClarityLinear(sharpened.rgb);
    // Keep signed/out-of-gamut HDR values intact instead of destroying them.
    if(any(!isfinite(original)) || any(original<0.0f))return sharpened;
    float3 luma=passthrough==2u?float3(.2627f,.6780f,.0593f):kLuma;
    float y=dot(original,luma);
    if(y<=1e-6f)return sharpened;
    float3 skinColor=passthrough==2u?Nr2020To709(original):original;
    float skin=SkinWeightLinear(skinColor);
    float chroma=NaturalChromaScale(y,skin,strength);
    // Preserve chroma direction, avoid negative channels and SDR clipping by
    // limiting chroma uniformly rather than clipping individual RGB channels.
    float3 delta=original-y;
    [unroll]for(int channel=0;channel<3;++channel) {
        if(delta[channel]<-1e-6f)chroma=min(chroma,y/-delta[channel]);
    }
    float3 result=(y+delta*chroma)*NaturalLumaGain(y,strength);
    if(passthrough==1u) {
        float peak=max(result.r,max(result.g,result.b));
        if(peak>1.0f)result/=peak;
    }
    float3 stored=ClarityStore(result);
    if(any(!isfinite(stored)))return sharpened;
    // PQ remains bounded by its representation; scRGB retains super-white.
    if(passthrough==1u||passthrough==2u)stored=saturate(stored);
    stored=EffectShadowOutput(raw.rgb,stored,passthrough,diffuse_white,1.0f,guard);
    return float4(stored,raw.a);
}
)"
