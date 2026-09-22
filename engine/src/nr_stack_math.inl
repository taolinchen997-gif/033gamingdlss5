// Exact math body compiled as both embedded HLSL and CPU vector arithmetic.
// Texture sampling, skin detection, GPU ordering and neural response are NOT
// emulated by the CPU checks. Neighbour index 4 is the center of a 3x3 tile.
K033_STACK_MATH(
float3 StackCondition(float3 original[9],float3 prior[9],float skin){
    float3 a=original[4],b=prior[4];
    if(!all(isfinite(b)) || all(a==b))return a;
    float y0=dot(a,kLuma),y=dot(b,kLuma);
    float3 c0=a-y0,c=b-y,low=c0,high=c0,edits[9];
    float originalSurround=0.f,priorSurround=0.f;
    for(int i=0;i<9;++i){
        float3 ai=original[i],bi=prior[i];
        if(!all(isfinite(bi)))bi=ai;
        float ay=dot(ai,kLuma),by=dot(bi,kLuma);
        float3 ac=ai-ay,bc=bi-by;
        low=min(low,ac);high=max(high,ac);edits[i]=bc-ac;
        if(i!=4){originalSurround+=ay;priorSurround+=by;}
    }
    // A robust common colour edit: the median rejects a one-pixel fringe,
    // while a spatially consistent colour/lighting change survives intact.
    // Compare/swap is component-wise; project the median back to zero luma.
    for(int end=8;end>0;--end)for(int j=0;j<end;++j){
        float3 lo=min(edits[j],edits[j+1]),hi=max(edits[j],edits[j+1]);
        edits[j]=lo;edits[j+1]=hi;
    }
    float3 common=edits[4]-dot(edits[4],kLuma);
    float3 margin=float3(.025f,.025f,.025f)+.15f*(high-low);
    float3 d=c-c0-common;float amount=1.f;
    for(int channel=0;channel<3;++channel){
        if(d[channel]>1e-6f)amount=min(amount,(high[channel]+margin[channel]-c0[channel])/d[channel]);
        else if(d[channel]<-1e-6f)amount=min(amount,(low[channel]-margin[channel]-c0[channel])/d[channel]);
    }
    c=c0+common+saturate(amount)*d;
    // Existing local crease bound; does not brighten broad facial lighting.
    // Skin is only a colour cue here, not a semantic face segmentation.
    float originalDetail=y0-originalSurround*.125f;
    float detail=y-priorSurround*.125f;
    // Bound the model's ADDED local detail around the original detail. An
    // absolute magnitude bound allowed an existing light ridge to become a
    // dark crease of the same magnitude, even with full skin protection.
    // Broad lighting and original pores/creases survive; no temporal averaging.
    float added=detail-originalDetail;
    float allowed=.5f*abs(originalDetail)+.025f;
    float protectedDetail=originalDetail+clamp(added,-allowed,allowed);
    y=lerp(y,priorSurround*.125f+protectedDetail,saturate(skin));
    float neutral=saturate(y),fit=1.f;
    for(int gamutChannel=0;gamutChannel<3;++gamutChannel){
        if(c[gamutChannel]>1e-6f)fit=min(fit,(1.f-neutral)/c[gamutChannel]);
        else if(c[gamutChannel]<-1e-6f)fit=min(fit,-neutral/c[gamutChannel]);
    }
    return float3(neutral,neutral,neutral)+saturate(fit)*c;
}
)
