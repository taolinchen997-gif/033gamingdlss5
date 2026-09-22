
// Explicit effect-space contract; source mode 0 is already linear BT.709.
float3 EffectTo709(float3 value,uint mode,float diffuseWhite) {
    if(mode==1u)return EffectDecode(value);
    if(mode==2u)return Nr2020To709(NrPqToNits(value))/max(diffuseWhite,1.0f);
    if(mode==3u)return value*(80.0f/max(diffuseWhite,1.0f));
    return value;
}
float3 EffectFrom709(float3 value,uint mode,float diffuseWhite) {
    if(mode==1u)return EffectEncode(value);
    if(mode==2u)return NrNitsToPq(Nr709To2020(value)*max(diffuseWhite,1.0f));
    if(mode==3u)return value*(max(diffuseWhite,1.0f)/80.0f);
    return value;
}
float3 EffectShadowOutput(float3 source,float3 changed,uint mode,float diffuseWhite,float sceneWhite,float limit) {
    if(all(source==changed))return source;
    float scale=mode==0u?max(sceneWhite,1e-4f):1.0f;
    float3 original=EffectTo709(source,mode,diffuseWhite)/scale;
    // Most pixels need no shadow work. Avoid decoding the proposed output or
    // doing an encode round trip for the unchanged bright/signed HDR branch.
    if(!all(isfinite(original))||any(original<0.0f))return changed;
    if(dot(original,float3(0.2126f,0.7152f,0.0722f))>=0.05f)return changed;
    float3 proposed=EffectTo709(changed,mode,diffuseWhite)/scale;
    float3 bounded=EffectShadowLinear(original,proposed,limit);
    if(all(bounded==proposed))return changed;
    if(all(bounded==original))return source;
    return source+EffectFrom709(bounded*scale,mode,diffuseWhite)-EffectFrom709(original*scale,mode,diffuseWhite);
}
