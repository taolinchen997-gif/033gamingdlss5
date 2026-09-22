// Shared production/CPU math. RGB is linear BT.709, one = diffuse white.
// No neighbours or history: constrain only the added effect, never blur source.
K033_EFFECT_MATH(
float EffectDecode1(float v) {
    return v<=0.04045f?v/12.92f:pow(max((v+0.055f)/1.055f,0.0f),2.4f);
}
float EffectEncode1(float v) {
    return v<=0.0031308f?v*12.92f:1.055f*pow(max(v,0.0f),1.0f/2.4f)-0.055f;
}
float3 EffectDecode(float3 c) {
    return float3(EffectDecode1(c[0]),EffectDecode1(c[1]),EffectDecode1(c[2]));
}
float3 EffectEncode(float3 c) {
    return float3(EffectEncode1(c[0]),EffectEncode1(c[1]),EffectEncode1(c[2]));
}
float3 EffectShadowLinear(float3 source,float3 changed,float limit) {
    if(all(source==changed))return source;
    if(!all(isfinite(source))||!all(isfinite(changed))||any(source<0.0f)||any(changed<0.0f))return changed;
    float y=dot(source,float3(0.2126f,0.7152f,0.0722f));
    // Same 5%-of-diffuse-white knee as the retained dark-region policy.
    // The bright branch returns the exact proposed value, not a round trip.
    if(y>=0.05f)return changed;
    if(y<=0.0f)return source;
    float t=saturate(y/0.05f);t=t*t*(3.0f-2.0f*t);
    // Release the budget continuously before the identity branch. A finite
    // ceiling at the knee would jump for large model edits (especially the
    // curve-3 bridge). The denominator floor is far beyond physical HDR range.
    float g=1.0f+clamp(limit,1.0f,8.0f)*t/max(1.0f-t,1e-7f);
    float3 edit=changed-source;
    float peak=max(source[0],max(source[1],source[2]));
    float span=max(abs(edit[0]),max(abs(edit[1]),abs(edit[2])));
    if(span<=0.0f)return source;
    float a=min(1.0f,peak*(g-1.0f)/span);
    float dy=dot(edit,float3(0.2126f,0.7152f,0.0722f));
    if(dy>0.0f)a=min(a,y*(g-1.0f)/dy);
    else if(dy<0.0f)a=min(a,y*(1.0f-1.0f/g)/-dy);
    for(int c=0;c<3;++c)if(edit[c]<0.0f)a=min(a,source[c]/-edit[c]);
    // One scalar preserves the direction of the edit. Mild broad lighting
    // within the budget is exact; original texture/noise is never filtered.
    if(a>=1.0f)return changed;
    return source+edit*saturate(a);
}
)
