R"(
float3 GradeSrgbToLinear(float3 v) {
    float3 lo=v/12.92f, hi=pow(max((v+0.055f)/1.055f,0.0f),2.4f);
    return float3(v.r<=0.04045f?lo.r:hi.r,v.g<=0.04045f?lo.g:hi.g,v.b<=0.04045f?lo.b:hi.b);
}
float3 GradeLinearToSrgb(float3 v) {
    float3 lo=v*12.92f,hi=1.055f*pow(max(v,0.0f),1.0f/2.4f)-0.055f;
    return float3(v.r<=0.0031308f?lo.r:hi.r,v.g<=0.0031308f?lo.g:hi.g,v.b<=0.0031308f?lo.b:hi.b);
}
float GradeSkinWeight(float3 c){
    float sum=c.r+c.g+c.b;if(sum<1e-4f)return 0;
    float r=c.r/sum,g=c.g/sum,b=c.b/sum;
    return saturate(smoothstep(.33f,.36f,r)*(1-smoothstep(.47f,.52f,r))*
        smoothstep(.26f,.29f,g)*(1-smoothstep(.36f,.39f,g))*smoothstep(0,.025f,r-g)*smoothstep(0,.025f,g-b));
}
float3 GradeSource(float3 input,uint mode,float diffuseWhite,uint enabled,
                   float exposure,float contrast,float saturation,float warmth,float tint,float highlights,float skinProtection,uint style,float styleStrength) {
    // Keep the neutral setting exact, including signed HDR and sub-black values.
    if(enabled==0 || (exposure==0 && contrast==1 && saturation==1 && warmth==0 && tint==0 && highlights==0 && (style==0 || styleStrength==0)))return input;
    float3 v=input;
    if(mode==1)v=GradeSrgbToLinear(v);
    else if(mode==2)v=NrPqToWorking(v,diffuseWhite);
    else if(mode==3)v*=80.0f/max(diffuseWhite,1.0f);
    float3 original=v;
    float skin=skinProtection*GradeSkinWeight(mode==1?max(input,0):GradeLinearToSrgb(max(v,0)));
    v*=exp2(exposure)*float3(exp2(warmth),exp2(tint),exp2(-warmth));
    float y=dot(v,float3(0.2126f,0.7152f,0.0722f));
    if(contrast!=1)v*=pow(max((max(y,0.0f)+0.18f)/0.36f,1e-6f),contrast-1.0f);
    y=dot(v,float3(0.2126f,0.7152f,0.0722f));
    v=y+(v-y)*saturation;
    // SDR pixels stop at linear white, so the HDR-only y>1 shoulder was a
    // dead control for display-referred Feeder input. Roll only its upper
    // half of linear light; retain the existing HDR curve and neutral setting.
    float highlightWeight=mode==1?smoothstep(.5f,1.f,max(y,0.f)):
        saturate(max(y-1.f,0.f)/(max(y-1.f,0.f)+1.f));
    v*=1.0f-highlights*highlightWeight;

    // Before-NR styles. Anime colour-step concept adapted from CC0
    // LordOfLunacy/Insane-Shaders BilateralComic.fx; see third_party/InsaneShaders033.
    // No image history, model pass, neighbourhood filter or full-size allocation.
    if(style!=0 && styleStrength>0) {
        float3 styled=v;
        float sy=max(dot(v,float3(.2126f,.7152f,.0722f)),0);
        if(style==1) { // Natural: a small midtone contrast lift, no hue rotation.
            styled*=pow(max((sy+.18f)/.36f,1e-6f),.08f);
            float ly=dot(styled,float3(.2126f,.7152f,.0722f));
            styled=ly+(styled-ly)*1.04f;
            styled*=1-.06f*saturate(max(sy-1,0)/(max(sy-1,0)+1));
        } else if(style==2) { // Soft cinema: slightly lifted shadows, restrained chroma.
            float target=sy+.012f*(sy/(sy+.012f))*exp2(-4*sy);
            styled*=sy>1e-6f?target/sy:1;
            float ly=dot(styled,float3(.2126f,.7152f,.0722f));
            styled=(ly+(styled-ly)*.94f)*float3(1.025f,1.f,.975f);
            styled*=1-.10f*saturate(max(sy-1,0)/(max(sy-1,0)+1));
        } else if(style==3 && sy>1e-6f) { // Anime colour steps, continuous at boundaries.
            float position=log2(1+sy*8)*4;
            float stepValue=floor(position)+smoothstep(.18f,.82f,frac(position));
            float target=(exp2(stepValue*.25f)-1)*.125f;
            float ratio=clamp(target/sy,.80f,1.20f);
            styled=(sy+(v-sy)*1.12f)*ratio;
        }
        v=lerp(v,styled,saturate(styleStrength));
    }
    v=lerp(v,original,saturate(skin));
    if(all(v==original))return input;
    if(mode==1)return GradeLinearToSrgb(saturate(v));
    if(mode==2)return NrWorkingToPq(v,diffuseWhite);
    if(mode==3)return v*max(diffuseWhite,1.0f)/80.0f;
    return v;
}
)"
