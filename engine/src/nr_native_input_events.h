#pragma once
#include <atomic>
#include <cstdint>
#include <d3d12.h>
namespace nrnative033 {
using ResetNotice=void(*)(uint64_t);
using SubmitNotice=void(*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
inline std::atomic<ResetNotice> resetNotice{nullptr};
inline std::atomic<SubmitNotice> submitNotice{nullptr};
constexpr unsigned ResourceBarrierSlot=26,EnhancedBarrierSlot=80;
}
