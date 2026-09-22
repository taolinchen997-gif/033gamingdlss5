#pragma once
#include <atomic>
#include <cstdint>
// Process-local image-history epoch. Model switches do not change the swapchain
// format, but their two appearances must never be paired by image interpolation.
namespace fgscene033 {
inline std::atomic<uint64_t> revision{0};
inline std::atomic<uint64_t> modelKey{UINT64_MAX};
inline std::atomic<bool> enabled{false};
inline void Enabled(bool value){if(enabled.exchange(value)!=value)revision.fetch_add(1,std::memory_order_release);}
inline void Model(uint64_t key){if(modelKey.exchange(key)!=key)revision.fetch_add(1,std::memory_order_release);}
inline uint64_t Revision(){return revision.load(std::memory_order_acquire);}
}
