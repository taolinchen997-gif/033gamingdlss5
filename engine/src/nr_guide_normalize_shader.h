#pragma once
namespace nrnormalize033 {
inline constexpr char Shader[]=R"(
Texture2D<float> sourceDepth : register(t0);
Texture2D<float4> sourceMotion : register(t1);
RWTexture2D<float> outputDepth : register(u0);
RWTexture2D<float2> outputMotion : register(u1);
cbuffer Size : register(b0) { uint width; uint height; }
[numthreads(8,8,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if(p.x>=width || p.y>=height)return;
    outputDepth[p.xy]=sourceDepth.Load(int3(p.xy,0));
    outputMotion[p.xy]=sourceMotion.Load(int3(p.xy,0)).xy;
}
)";
}
