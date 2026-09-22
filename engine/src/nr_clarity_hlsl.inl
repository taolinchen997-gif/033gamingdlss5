#define K033_CLARITY_MATH(...) #__VA_ARGS__
#include "nr_clarity_math.inl"
#undef K033_CLARITY_MATH
R"(
float3 ClarityLinear(float3 v) {
    if(passthrough==1u){
        float3 hi=pow(max((v+0.055f)/1.055f,0.0f),2.4f);
        return float3(v.r<=0.04045f?v.r/12.92f:hi.r,v.g<=0.04045f?v.g/12.92f:hi.g,v.b<=0.04045f?v.b/12.92f:hi.b);
    }
    if(passthrough==2u)return NrPqToNits(v)/max(diffuse_white,1.0f);
    return v*(passthrough==3u?80.0f/max(diffuse_white,1.0f):1.0f);
}
float3 ClarityStore(float3 v) {
    if(passthrough==1u){
        float3 hi=1.055f*pow(max(v,0.0f),1.0f/2.4f)-0.055f;
        return float3(v.r<=0.0031308f?v.r*12.92f:hi.r,v.g<=0.0031308f?v.g*12.92f:hi.g,v.b<=0.0031308f?v.b*12.92f:hi.b);
    }
    if(passthrough==2u)return NrNitsToPq(v*max(diffuse_white,1.0f));
    return v*(passthrough==3u?max(diffuse_white,1.0f)/80.0f:1.0f);
}
float4 ClarityPixel(uint2 at,float4 raw) {
    if(sharpen<=0.0f||applymodel==0u||any(!isfinite(raw.rgb)))return raw;
    // Keep the raw side and separator untouched; never sample across the split.
    if(split>0.001f && float(at.x)<=split*float(dst_size.x)+2.0f)return raw;
    float3 center=ClarityLinear(raw.rgb);
    float y[9];int k=0;
    float3 luma=passthrough==2u?float3(0.2627f,0.6780f,0.0593f):kLuma;
    [unroll]for(int j=-1;j<=1;++j)[unroll]for(int i=-1;i<=1;++i){
        int2 p=clamp(int2(at)+int2(i,j),int2(0,0),int2(dst_size)-1);
        float3 sampledColor=ClarityLinear(full.Load(int3(p,0)).rgb);
        y[k++]=dot(sampledColor,luma);
    }
    float3 skinColor=passthrough==2u?Nr2020To709(center):center;
    float gain=ClarityGain(y,sharpen,SkinWeightLinear(skinColor));
    if(gain==1.0f)return raw;
    float3 outRgb=raw.rgb+ClarityStore(center*gain)-ClarityStore(center);
    if(any(!isfinite(outRgb)))return raw;
    if(passthrough==1u||passthrough==2u)outRgb=saturate(outRgb);
    outRgb=EffectShadowOutput(raw.rgb,outRgb,passthrough,diffuse_white,1.0f,guard);
    return float4(outRgb,raw.a);
}
)"
