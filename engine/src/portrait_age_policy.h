#pragma once
#include <cstdint>
namespace portraitage {
// Include safe GPU retirement + asynchronous CPU detection in the age budget.
// The shader still rejects current pixels that disagree with the captured face.
constexpr uint64_t MaxAge=1000,FadeTime=300;
inline float Weight(uint64_t now,uint64_t captured) {
    if(!captured || now<captured || now-captured>=MaxAge)return 0.f;
    const auto remaining=MaxAge-(now-captured);
    return remaining>=FadeTime?1.f:float(remaining)/float(FadeTime);
}
}
