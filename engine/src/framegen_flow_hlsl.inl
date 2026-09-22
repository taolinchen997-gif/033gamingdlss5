// Original 033 image-only interpolation prototype. Bidirectional block matching
#pragma once
// and consistency validation; no extracted third-party shaders or model weights.
// Motion fields use flow-grid pixels and point from the reference to the target.
static constexpr char kFramegenFlowShader[] = R"(
Texture2D<float4> A : register(t0);
Texture2D<float4> B : register(t1);
Texture2D<float4> FA : register(t2);
Texture2D<float4> FB : register(t3);
Texture2D<float> Protect : register(t4);
Texture2D<float> SceneCut : register(t5);
RWTexture2D<float4> Out : register(u0);
SamplerState LinearClamp : register(s0);
cbuffer Params : register(b0) {
 uint2 size; uint2 flowSize; float phase; uint radius; uint hasProtect; uint reset;
};
// Same gate is compiled by the production HLSL and offline CPU regression.
bool FlowFitSubpixel(float zeroMotionCost, float confidence) {
    // Preserve the estimator's existing stationary decision. An asymmetric
    // neighbourhood is not evidence of movement between identical frames.
    return zeroMotionCost >= 0.00001f && confidence > 0.05f;
}
float luminance(float3 c) {return dot(c,float3(.2126,.7152,.0722));}
float luma(Texture2D<float4> tex,int2 p) {
 float3 c=tex.Load(int3(clamp(p,int2(0,0),int2(flowSize)-1),0)).rgb;
 return log2(1.0+max(0.0,luminance(c)));
}
float cost(int2 p,int2 d) {
 float s=0;
 [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x)
   s+=abs(luma(A,p+int2(x,y))-luma(B,p+int2(x,y)+d));
 return s/25.0;
}
[numthreads(8,8,1)]
void downsample(uint3 tid:SV_DispatchThreadID) {
 if(any(tid.xy>=flowSize))return;
 float2 uv=(float2(tid.xy)+.5)/flowSize;
 float2 delta=.25/float2(flowSize);
 float4 c=A.SampleLevel(LinearClamp,uv+float2(-delta.x,-delta.y),0);
 c+=A.SampleLevel(LinearClamp,uv+float2(delta.x,-delta.y),0);
 c+=A.SampleLevel(LinearClamp,uv+float2(-delta.x,delta.y),0);
 c+=A.SampleLevel(LinearClamp,uv+delta,0);
 Out[tid.xy]=c*.25;
}
[numthreads(8,8,1)]
void estimate(uint3 tid:SV_DispatchThreadID) {
 if(any(tid.xy>=flowSize))return;
 int2 p=int2(tid.xy),bestD=0;
 float zeroMotionCost=cost(p,0),best=zeroMotionCost,second=1e6;
 int r=int(min(radius,12u));
 // A bounded search keeps a bad scene from causing unbounded GPU work. A future
 // pyramid / hardware-flow backend can replace this pass without changing ABI.
 [loop]for(int y=-r;y<=r;++y)[loop]for(int x=-r;x<=r;++x){
   int2 d=int2(x,y);if((x==0&&y==0)||any(p+d<0)||any(p+d>=int2(flowSize)))continue;
   float c=cost(p,d);
   if(c<best){second=best;best=c;bestD=d;}else second=min(second,c);
 }
 float variation=0,center=luma(A,p);
 [unroll]for(int k=-2;k<=2;++k){variation+=abs(luma(A,p+int2(k,0))-center);variation+=abs(luma(A,p+int2(0,k))-center);}
 float reliable=saturate((second-best)/max(second,.002))*saturate(variation*10)*saturate(1-best*16);
 // An identical area does not need a guessed direction, including flat UI.
 if(zeroMotionCost<.00001){bestD=0;reliable=1;}
 // Fit a bounded subpixel offset: reduce coarse-grid stepping on slow pans.
 float2 sub=0;
 if(FlowFitSubpixel(zeroMotionCost,reliable) && all(abs(bestD)<r)){
   float left=cost(p,bestD+int2(-1,0)),right=cost(p,bestD+int2(1,0));
   float up=cost(p,bestD+int2(0,-1)),down=cost(p,bestD+int2(0,1));
   sub=clamp(.5*float2(left-right,up-down)/max(float2(left+right,up+down)-2*best,.0001),-.5,.5);
 }
 Out[p]=float4(float2(bestD)+sub,reliable,best);
}
// Shared CPU/HLSL arithmetic, not a second reference implementation.
// Preserve a small absolute-lightness term while matching local structure.
float FlowPatchCost(float absoluteDifference, float centeredDifference) {
    return (centeredDifference + 0.15f * absoluteDifference) / 9.0f;
}
float FlowBlockCost(float a[9], float b[9]) {
    float ma=0,mb=0;
    for(int k=0;k<9;++k){ma+=a[k];mb+=b[k];}
    ma/=9;mb/=9;float absolute=0,centered=0;
    for(int k=0;k<9;++k){
        float raw=a[k]-b[k],local=(a[k]-ma)-(b[k]-mb);
        absolute+=raw<0?-raw:raw;centered+=local<0?-local:local;
    }
    return FlowPatchCost(absolute,centered);
}
float FlowConsistency(float residualPixels) {
    return residualPixels >= 2.0f ? 0.0f : 1.0f - residualPixels * 0.5f;
}


// Three levels, prepared in log luminance once per level. Matching therefore
// does not repeat logarithms thousands of times for each motion vector.
[numthreads(8,8,1)]
void pyramidDown(uint3 tid:SV_DispatchThreadID) {
 if(any(tid.xy>=flowSize))return;
 float2 uv=(float2(tid.xy)+.5)/flowSize,delta=.25/float2(flowSize);
 float3 c=A.SampleLevel(LinearClamp,uv-delta,0).rgb;
 c+=A.SampleLevel(LinearClamp,uv+float2(delta.x,-delta.y),0).rgb;
 c+=A.SampleLevel(LinearClamp,uv+float2(-delta.x,delta.y),0).rgb;
 c+=A.SampleLevel(LinearClamp,uv+delta,0).rgb;
 float v=(hasProtect&8)!=0?c.x*.25:log2(1+max(0,luminance(c*.25)));Out[tid.xy]=float4(v,v,v,1);
}
float pyramidLuma(Texture2D<float4> tex,int2 p) {
 return tex.Load(int3(clamp(p,int2(0,0),int2(flowSize)-1),0)).x;
}
float pyramidCost(int2 p,int2 d) {
 float av[9],bv[9];int k=0;
 [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
  av[k]=pyramidLuma(A,p+int2(x,y));bv[k]=pyramidLuma(B,p+int2(x,y)+d);++k;
 }
 return FlowBlockCost(av,bv);
}
[numthreads(8,8,1)]
void pyramidEstimate(uint3 tid:SV_DispatchThreadID) {
 if(any(tid.xy>=flowSize))return;
 int2 p=int2(tid.xy),seed=0;float zeroMotionCost=pyramidCost(p,0),seedCost=zeroMotionCost;
 // hasProtect bit 4 means FA holds the preceding coarser level. Evaluate
 // adjacent seed candidates instead of blending foreground/background motion.
 if((hasProtect&4)!=0){
  uint cw,ch;FA.GetDimensions(cw,ch);float2 scale=float2(flowSize)/float2(cw,ch);
  int2 cell=int2(floor((float2(p)+.5)/scale-.5));
  [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
   int2 d=int2(round(FA.Load(int3(clamp(cell+int2(x,y),int2(0,0),int2(cw,ch)-1),0)).xy*scale));
   if(any(p+d<0)||any(p+d>=int2(flowSize)))continue;
   float c=pyramidCost(p,d);if(c<seedCost){seed=d;seedCost=c;}
  }
 }
 int2 bestD=seed;float best=seedCost,second=1e6;int r=int(radius);
 [loop]for(int y=-r;y<=r;++y)[loop]for(int x=-r;x<=r;++x){
  if(x==0&&y==0)continue;int2 d=seed+int2(x,y);
  if(any(p+d<0)||any(p+d>=int2(flowSize)))continue;
  float c=pyramidCost(p,d);if(c<best){second=best;best=c;bestD=d;}else second=min(second,c);
 }
 float center=pyramidLuma(A,p),variation=0;
 [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x)variation+=abs(pyramidLuma(A,p+int2(x,y))-center);
 float reliable=saturate((second-best)/max(second,.0002)*4)*saturate(variation*10)*saturate(1-best*12);
 if(zeroMotionCost<.00001){bestD=0;reliable=1;}
 float2 sub=0;
 if(FlowFitSubpixel(zeroMotionCost,reliable) && all(p+bestD>0) && all(p+bestD<int2(flowSize)-1)){
  float l=pyramidCost(p,bestD+int2(-1,0)),rgt=pyramidCost(p,bestD+int2(1,0));
  float u=pyramidCost(p,bestD+int2(0,-1)),dwn=pyramidCost(p,bestD+int2(0,1));
  sub=clamp(.5*float2(l-rgt,u-dwn)/max(float2(l+rgt,u+dwn)-2*best,.0001),-.5,.5);
 }
 Out[p]=float4(float2(bestD)+sub,reliable,best);
}

bool inside(float2 uv){return all(uv>=0)&&all(uv<=1);}
[numthreads(8,8,1)]
void interpolate(uint3 tid:SV_DispatchThreadID) {
 if(any(tid.xy>=size))return;
 float2 uv=(float2(tid.xy)+.5)/float2(size);
 float4 current=B.Load(int3(tid.xy,0));
 if((hasProtect&2)!=0 && SceneCut.Load(int3(0,0,0))>.5){Out[tid.xy]=current;return;}
 if(reset!=0||phase>=1){Out[tid.xy]=current;return;}
 if(phase<=0){Out[tid.xy]=A.Load(int3(tid.xy,0));return;}
 float4 initialF=FA.SampleLevel(LinearClamp,uv,0),initialB=FB.SampleLevel(LinearClamp,uv,0);
 float2 ua=uv-initialF.xy*phase/float2(flowSize);
 float2 ub=uv-initialB.xy*(1-phase)/float2(flowSize);
 // Refine inverse mapping once; sampling at the intermediate coordinate alone
 // chooses the wrong motion on foreground boundaries.
 float4 f=FA.SampleLevel(LinearClamp,ua,0),b=FB.SampleLevel(LinearClamp,ub,0);
 ua=uv-f.xy*phase/float2(flowSize);ub=uv-b.xy*(1-phase)/float2(flowSize);
 float4 aColor=A.SampleLevel(LinearClamp,ua,0),bColor=B.SampleLevel(LinearClamp,ub,0);
 float2 ba=FB.SampleLevel(LinearClamp,ua+f.xy/float2(flowSize),0).xy;
 float2 ab=FA.SampleLevel(LinearClamp,ub+b.xy/float2(flowSize),0).xy;
 float ca=f.z*FlowConsistency(length((f.xy+ba)*float2(size)/float2(flowSize)))*(inside(ua)?1:0);
 float cb=b.z*FlowConsistency(length((b.xy+ab)*float2(size)/float2(flowSize)))*(inside(ub)?1:0);
 // Do not blend a moving prediction with an unwarped current outline.
 // Occlusion selects the consistent side; uncertain regions use the nearer
 // real endpoint. HUD protection keeps current pixels, without smearing text.
 float4 endpoint=phase<.5?A.Load(int3(tid.xy,0)):current;
 if((hasProtect&1)!=0 && Protect.Load(int3(tid.xy,0))>.5){Out[tid.xy]=current;return;}
 float difference=length(aColor.rgb-bColor.rgb)/max(1,max(length(aColor.rgb),length(bColor.rgb)));
 float4 result=endpoint;
 if(ca>=.25 && cb>=.25 && difference<.12){
   float wa=ca*(1-phase),wb=cb*phase;result=(aColor*wa+bColor*wb)/max(wa+wb,1e-6);
 }else if(ca>=.35 && ca>cb*1.5){result=aColor;}
 else if(cb>=.35 && cb>ca*1.5){result=bColor;}
 Out[tid.xy]=result;
}
)";
