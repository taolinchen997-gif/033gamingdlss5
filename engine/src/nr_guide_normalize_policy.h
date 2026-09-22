#pragma once
#include <d3d12.h>
#include <cstdint>
namespace nrnormalize033 {
constexpr unsigned Capacity=8;
constexpr uint64_t MaxCacheBytes=512ull*1024*1024;
struct Plan {unsigned width=0,height=0;explicit operator bool()const{return width&&height;}};
inline bool Shape(const D3D12_RESOURCE_DESC& d){
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.DepthOrArraySize==1&&d.MipLevels==1&&
        d.SampleDesc.Count==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&d.Width&&d.Height&&
        d.Width<=8192&&d.Height<=8192&&d.Width*d.Height<=16777216;
}
// Only the identified RE4/TDB71 scene producer may select this conversion.
// Its RG/NDC convention is supported by the saved upstream contract; current
// game pixel content and NR acceptance still require manual validation.
inline Plan Select(bool identifiedRe4,const D3D12_RESOURCE_DESC& depth,const D3D12_RESOURCE_DESC& motion){
    if(!identifiedRe4||!Shape(depth)||!Shape(motion)||depth.Width!=motion.Width||depth.Height!=motion.Height||
        depth.Format!=DXGI_FORMAT_R32G8X24_TYPELESS||motion.Format!=DXGI_FORMAT_R16G16B16A16_SNORM)return {};
    return {unsigned(depth.Width),depth.Height};
}
// Known DXGI budgets permit eight full-sized guide slots, instead of a
// fixed 512 MiB cache. Unknown-budget behavior stays conservative.
constexpr uint64_t ExpandedCacheBytes=2048ull*1024*1024;
inline uint64_t BudgetHeadroom(uint64_t budget){
    const uint64_t scaled=budget/64;
    return scaled<128ull*1024*1024?128ull*1024*1024:
        scaled>256ull*1024*1024?256ull*1024*1024:scaled;
}
inline bool AvailableFits(uint64_t allocation,uint64_t available,uint64_t budget){
    const auto reserve=BudgetHeadroom(budget);
    return available>=reserve && allocation<=available-reserve;
}
inline bool BudgetFits(uint64_t retained,uint64_t allocation,bool budgetKnown,uint64_t available,uint64_t budget=0){
    const auto cap=budgetKnown?ExpandedCacheBytes:MaxCacheBytes;
    return allocation&&allocation<=256ull*1024*1024&&allocation<=cap&&retained<=cap-allocation&&
        (!budgetKnown||AvailableFits(allocation,available,budget));
}
inline unsigned Groups(unsigned size){return (size+7)/8;}
// Same typed SNORM interpretation used by the texture SRV; test boundaries
// without a graphics device. Conversion does not multiply NDC by model size.
inline float Snorm16(int16_t value){return value==INT16_MIN?-1.f:float(value)/32767.f;}
}
