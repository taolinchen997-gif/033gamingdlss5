// Atomic diagnostic publication. A menu must not borrow a frame-generation
// object while the render owner may destroy it during a switch or resize.
#pragma once
#include <atomic>
#include <cstdint>
namespace fgcompat033 {
enum Bits:uint32_t { Colour=1,Depth=2,Motion=4,Direct12=8 };
inline std::atomic<uint64_t> inputs{0},dispatchTick{0},dispatchCount{0};
inline void Observe(uint64_t now,uint32_t flags){inputs.store((now<<8)|(flags&255),std::memory_order_release);}
inline uint32_t Flags(uint64_t now){auto s=inputs.load(std::memory_order_acquire);auto t=s>>8;
    return t && now>=t && now-t<=1500?uint32_t(s&255):0;}
inline bool Ready(uint32_t flags){return (flags&(Colour|Depth|Motion|Direct12))==(Colour|Depth|Motion|Direct12);}
inline void Dispatched(uint64_t now){dispatchTick.store(now);++dispatchCount;}
}
