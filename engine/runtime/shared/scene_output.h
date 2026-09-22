#ifdef __cplusplus
#pragma once
#include <cmath>
namespace k033scene {
struct Pixel {float r,g,b,a;};
inline Pixel merge(Pixel reconstructed,Pixel original){
    if(!std::isfinite(reconstructed.r)||!std::isfinite(reconstructed.g)||!std::isfinite(reconstructed.b))return original;
    return {reconstructed.r,reconstructed.g,reconstructed.b,original.a};
}
}
#else
float4 SceneMerge(float4 reconstructed,float4 original){
    return all(isfinite(reconstructed.rgb))?float4(reconstructed.rgb,original.a):original;
}
#endif
