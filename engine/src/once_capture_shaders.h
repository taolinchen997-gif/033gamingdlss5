#pragma once
namespace yyonce {
inline constexpr char ClaimShader[]=R"hlsl(
RWByteAddressBuffer control : register(u0);
[numthreads(1,1,1)] void main(uint3 tid:SV_DispatchThreadID){
 uint old;control.InterlockedCompareExchange(0,0,1,old);
 control.Store(4,old==0?1:0);
}
)hlsl";
inline constexpr char GridShader[]=R"hlsl(
Texture2D<float4> source : register(t0);
RWByteAddressBuffer control : register(u0);
RWByteAddressBuffer destination : register(u1);
cbuffer Sizes : register(b0) {uint2 sourceSize;uint2 targetSize;uint rowPitch;uint pixelBytes;};
groupshared uint groupAllowed;
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID,uint groupIndex:SV_GroupIndex,uint3 groupId:SV_GroupID){
 if(groupIndex==0)groupAllowed=control.Load(4);
 GroupMemoryBarrierWithGroupSync();
 if(groupAllowed!=1)return; // Uniform across this entire group.
 if(all(p.xy<targetSize)){
  // S27: average this texel's whole source footprint (at most 8x8 loads). One
  // point per texel aliased, and the pattern changed with every sub-pixel
  // camera move, so the detector's scores flickered; the detector was trained
  // on area-resized pictures. Non-finite source texels are skipped.
  uint2 lo=min(sourceSize-1,p.xy*sourceSize/targetSize);
  uint2 hi=clamp(((p.xy+1)*sourceSize+targetSize-1)/targetSize,lo+1,sourceSize);
  uint2 stride=max(uint2(1,1),(hi-lo+7)/8);
  float4 sum=0;float count=0;
  for(uint y=lo.y;y<hi.y;y+=stride.y)for(uint x=lo.x;x<hi.x;x+=stride.x){
   float4 v=source.Load(int3(x,y,0));
   if(all(isfinite(v))){sum+=v;count+=1;}
  }
  float4 value=count>0?sum/count:float4(0,0,0,0);
  uint offset=32768+p.y*rowPitch+p.x*pixelBytes;
  if(pixelBytes==8){
   uint4 halfValue=f32tof16(value);
   destination.Store2(offset,uint2((halfValue.x&65535)|(halfValue.y<<16),(halfValue.z&65535)|(halfValue.w<<16)));
  }else destination.Store4(offset,asuint(value));
 }
 // Edge groups include out-of-bounds lanes in this barrier. Each group owns
 // one distinct completion word, avoiding a contended system-memory atomic.
 DeviceMemoryBarrierWithGroupSync();
 if(groupIndex==0){
  uint groupsX=(targetSize.x+7)/8;destination.Store(256+(groupId.y*groupsX+groupId.x)*4,1);
  // CPU initialized once. Later sealed replays cannot write this stable word.
  // This is participation evidence, never a substitute for the Execute fence.
  if(groupId.x==0&&groupId.y==0)control.Store(256,1);
 }
}
)hlsl";
}
