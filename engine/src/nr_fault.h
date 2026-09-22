#pragma once
#include <atomic>
#include <cstdint>

// One monotonic state in the monolithic 033 DLL, shared by the renderer and
// core translation units. No ABI change, lock, allocation, config write or
// automatic recovery. This cannot undo an in-flight call or repair a device.
namespace nrfault033 {
enum class Site : uint32_t { Callback=1, Capabilities, CoreCreate, ForwardCreate,
                            CoreEvaluate, ForwardEvaluate, CoreRelease, ForwardRelease };
inline std::atomic<uint64_t> first{0};
inline bool Blocked() noexcept { return first.load(std::memory_order_acquire)!=0; }
inline uint32_t Code() noexcept { return uint32_t(first.load(std::memory_order_acquire)); }
inline void Record(uint32_t code,Site site) noexcept {
    if(!code)return;
    uint64_t empty=0;
    first.compare_exchange_strong(empty,(uint64_t(site)<<32)|code,
                                 std::memory_order_acq_rel,std::memory_order_acquire);
}
inline const char* Note() noexcept { return "神经渲染发生异常，本次运行不再重试；设置保留，请重启游戏"; }
}
