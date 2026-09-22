// SkinWeight accepts encoded BT.709. SkinWeightLinear and SkinLift accept linear BT.709.
// All three effects use the same cue domain; this is not semantic segmentation.
// This is a colour cue, not a face detector. No history or texture reads.
K033_SKIN_LIFT_MATH(
float SkinWeight(float3 c) {
    float sum=c[0]+c[1]+c[2];
    if(sum<1e-4f)return 0.0f;
    float rn=c[0]/sum,gn=c[1]/sum;
    float wr=smoothstep(0.33f,0.36f,rn)*(1.0f-smoothstep(0.47f,0.52f,rn));
    float wg=smoothstep(0.26f,0.29f,gn)*(1.0f-smoothstep(0.36f,0.39f,gn));
    float wo=smoothstep(0.0f,0.025f,rn-gn)*smoothstep(0.0f,0.025f,gn-c[2]/sum);
    return saturate(wr*wg*wo);
}
float SkinWeightLinear(float3 color) {
    if(!all(isfinite(color))||any(color<0.0f))return 0.0f;
    return SkinWeight(EffectEncode(color));
}
float3 SkinLift(float3 color,float amount) {
    if(amount<=0.0f||!isfinite(amount)||!all(isfinite(color))||any(color<0.0f))return color;
    float3 positive=max(color,0.0f);
    float weight=SkinWeightLinear(positive)*saturate(amount);
    if(weight<=1e-4f)return color;
    float luma=saturate(dot(positive,kLuma));
    float gain=1.0f+weight*(0.48f*smoothstep(0.003f,0.04f,luma)*saturate(1.0f-luma));
    return color*gain;
}
)
