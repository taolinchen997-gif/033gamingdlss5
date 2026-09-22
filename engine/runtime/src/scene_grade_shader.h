#pragma once
#include "backend.h"
#include <cstddef>
namespace k033 {
// Scene-linear original pixels; no reconstruction, temporal model or SR lease.
struct SceneGradeConstants {
    uint32_t width,height,enabled,style;
    float exposure,contrast,saturation,warmth,tint,highlights,strength,diffuseWhite;
    uint32_t encoding,pad0,pad1,pad2;
};
static_assert(sizeof(SceneGradeConstants)==64&&offsetof(SceneGradeConstants,exposure)==16&&offsetof(SceneGradeConstants,tint)==32,"scene grade HLSL layout");
inline SceneGradeConstants scene_grade_constants(uint32_t w,uint32_t h,const K033_Settings& s,uint32_t encoding=0,float diffuseWhite=1){
    return {w,h,s.enabled,s.style,s.exposure,s.contrast,s.saturation,s.warmth,s.tint,s.highlights,s.style_strength,diffuseWhite,encoding,0,0,0};
}
inline const char* scene_grade_source=
#include "../shared/nr_color_hlsl.inl"
#include "../shared/pregrade_hlsl.inl"
R"(
Texture2D<float4> Original : register(t0);
RWTexture2D<float4> Output : register(u0);
cbuffer Params : register(b0) {
    uint Width;uint Height;uint Enabled;uint Style;
    float Exposure;float Contrast;float Saturation;float Warmth;
    float Tint;float Highlights;float Strength;float DiffuseWhite;
    uint Encoding;uint Pad0;uint Pad1;uint Pad2;
};
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID){
    if(id.x>=Width||id.y>=Height)return;
    float4 original=Original.Load(int3(id.xy,0));
    float3 color=GradeSource(original.rgb,Encoding,DiffuseWhite,Enabled,Exposure,Contrast,Saturation,
        Warmth,Tint,Highlights,0,Style,Strength);
    Output[id.xy]=all(isfinite(color))?float4(clamp(color,-65504.0,65504.0),original.a):original;
}
)";
}
