R"(
Texture2D<float4> source : register(t0);
Texture2D<float4> reference : register(t1);
RWTexture2D<float4> target : register(u0);
SamplerState linearClamp : register(s0);
cbuffer Portrait : register(b0) {
 uint2 outputSize; float amount; uint faceCount;
 float4 boxes[4];
 float4 eyes[4];
 float4 mouths[4];
};
float3 LumaWeights(){return float3(.2126,.7152,.0722);}
)"
#define K033_PORTRAIT_MATH(...) #__VA_ARGS__
#include "portrait_math.inl"
#undef K033_PORTRAIT_MATH
R"(
[numthreads(8,8,1)]
void Thumbnail(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=outputSize))return;
 float2 uv=(id.xy+.5)/outputSize;
 // Four taps reduce aliasing in the detector input; never read a full frame on CPU.
 float2 d=.25/outputSize;
 float4 a=source.SampleLevel(linearClamp,uv+d,0)+source.SampleLevel(linearClamp,uv-d,0)
         +source.SampleLevel(linearClamp,uv+float2(d.x,-d.y),0)+source.SampleLevel(linearClamp,uv+float2(-d.x,d.y),0);
 target[id.xy]=float4(saturate(a.rgb*.25),1);
}
float3 CurrentThumbnailCell(int2 cell,uint2 size){
 float2 center=(clamp(cell,int2(0,0),int2(size)-1)+.5)/size,d=.25/size;
 return saturate((source.SampleLevel(linearClamp,center+d,0).rgb+source.SampleLevel(linearClamp,center-d,0).rgb+
   source.SampleLevel(linearClamp,center+float2(d.x,-d.y),0).rgb+source.SampleLevel(linearClamp,center+float2(-d.x,d.y),0).rgb)*.25);
}
float3 CurrentThumbnail(float2 uv){
 uint w,h;reference.GetDimensions(w,h);uint2 size=uint2(w,h);
 float2 grid=uv*size-.5;int2 cell=int2(floor(grid));float2 t=frac(grid);
 // Compare equal filters and resolutions. A full-resolution pore or crease
 // must not be mistaken for motion against a filtered reference thumbnail.
 return lerp(lerp(CurrentThumbnailCell(cell,size),CurrentThumbnailCell(cell+int2(1,0),size),t.x),
             lerp(CurrentThumbnailCell(cell+int2(0,1),size),CurrentThumbnailCell(cell+int2(1,1),size),t.x),t.y);
}
float SkinMask(float2 uv,float3 colour){
 float skin=SkinColourWeight(colour.r,colour.g,colour.b);
 float mask=0,protection=1;
 [unroll]for(uint i=0;i<4;++i){if(i>=faceCount)break;
  // Eyes, eyebrows and mouth keep their original detail; this does not reshape faces.
  mask=max(mask,PortraitRegionWeight(uv,boxes[i],eyes[i],mouths[i]));
  protection*=PortraitFeatureProtection(uv,boxes[i],eyes[i],mouths[i]);
 }
 if(mask<.0001 && protection>.9999)return skin;
 float3 prior=reference.SampleLevel(linearClamp,uv,0).rgb;
 // A delayed detector result may only affect matching current-frame content.
 // Motion/occlusion changes suppress this optional pre-pass, never the NR result.
 float3 delta=abs(CurrentThumbnail(uv)-prior);
 float mismatch=max(max(delta.r,delta.g),delta.b);
 return SkinCombinedWeight(skin,mask,protection,PortraitMotionWeight(1,mismatch));
}
[numthreads(8,8,1)]
void Enhance(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=outputSize))return;
 float4 c=source.Load(int3(id.xy,0));
 if(amount<=0 || any(!isfinite(c))){target[id.xy]=c;return;}
 float2 uv=(id.xy+.5)/outputSize;
 float weight=SkinMask(uv,max(c.rgb,0))*saturate(amount);
 if(weight<.0001){target[id.xy]=c;return;}
 float y=dot(c.rgb,LumaWeights()),base=0,sum=0;
 [unroll]for(int dy=-1;dy<=1;++dy)[unroll]for(int dx=-1;dx<=1;++dx){
  int2 p=clamp(int2(id.xy)+int2(dx,dy)*2,int2(0,0),int2(outputSize)-1);
  float ny=dot(source.Load(int3(p,0)).rgb,LumaWeights());
  float w=exp2(-abs(ny-y)*24);base+=ny*w;sum+=w;
 }
 base/=max(sum,.001);float detail=y-base;
 // Pre-NR skin brightening: lift base lighting, keep the source texture.
 // Current-frame skin colour includes neck, arms and hands. Face landmarks
 // add local protection; delayed detection never gates body-skin brightening.
 float gain=PortraitLumaGain(y,detail,weight);
 float3 result=c.rgb*gain;
 float peak=max(max(result.r,result.g),result.b);
 if(peak>1)result/=peak;
 target[id.xy]=float4(result,c.a);
}
)"
