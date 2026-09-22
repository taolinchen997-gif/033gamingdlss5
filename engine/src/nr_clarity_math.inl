// CAS adaptive negative-lobe math adapted from AMD FidelityFX CAS (MIT).
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// See licenses/FidelityFX-CAS.txt for the complete license and pinned source.
// 033 additions: luminance-only gain, HDR local scale, noise/skin attenuation,
// local-extrema and relative-gain limits. This is not unmodified AMD CAS.
K033_CLARITY_MATH(
float ClarityGain(float y[9],float amount,float skin) {
    if(!isfinite(amount)||amount<=0.0f)return 1.0f;
    for(int valid=0;valid<9;++valid)if(!isfinite(y[valid])||y[valid]<0.0f)return 1.0f;
    float center=y[4];if(center<=1e-6f)return 1.0f;
    float lo=y[0],hi=y[0];
    for(int rangeIndex=1;rangeIndex<9;++rangeIndex){lo=min(lo,y[rangeIndex]);hi=max(hi,y[rangeIndex]);}
    if(hi-lo<=1e-7f)return 1.0f;
    float crossLo=min(center,min(min(y[1],y[3]),min(y[5],y[7])));
    float crossHi=max(center,max(max(y[1],y[3]),max(y[5],y[7])));
    float softLo=lo+crossLo,softHi=hi+crossHi;
    // Continuous local headroom keeps HDR cross maxima from forcing the CAS weight to zero.
    float scale=max(1.0f,hi*1.25f);
    float amplitude=sqrt(saturate(min(softLo,2.0f*scale-softHi)/max(softHi,1e-6f)));
    float a=saturate(amount),weight=-amplitude/(8.0f-3.0f*a);
    float result=(center+weight*(y[1]+y[3]+y[5]+y[7]))/(1.0f+4.0f*weight);
    result=clamp(result,lo,hi);
    float range=hi-lo,quiet=0.002f+0.01f*center;
    float gate=smoothstep(quiet,quiet*4.0f,range)*smoothstep(0.003f,0.03f,center);
    float mask=saturate(skin);
    // Expand the upper slider range without extra taps or local overshoot.
    // Clamp before the skin attenuation so protection also holds at the gain cap.
    float shaped=a*(1.0f+2.0f*a*a);
    float enhanced=clamp(center+(result-center)*shaped*gate,lo,hi);
    float bounded=clamp((enhanced-center)/center,-0.35f,0.35f);
    return 1.0f+bounded*(1.0f-0.7f*mask);
}
)
