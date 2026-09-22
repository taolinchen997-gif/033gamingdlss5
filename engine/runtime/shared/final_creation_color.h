#pragma once
#include "final_frame.h"

namespace k033 {
enum class FinalColorOrigin : uint32_t {
    Unknown = 0,
    DxgiCreationDefault = 1,
    SuccessfulSetColorSpace = 2
};
struct FinalColorObservation {
    FinalColorDeclaration declaration{};
    FinalColorOrigin origin = FinalColorOrigin::Unknown;
};

// The caller must have observed the actual system swapchain creation, not an
// arbitrary FP16 texture, an NGX output or a swapchain attached after creation.
// This metadata helper does not grant that provenance to its caller.
// Microsoft documents these defaults for final DXGI swapchains. In particular,
// R10G10B10A2_UNORM does NOT imply PQ/HDR10 without a successful declaration.
// https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
inline FinalColorObservation final_creation_color(uint32_t actual_format,
                                                  uint64_t generation,
                                                  bool system_creation_observed) noexcept {
    FinalColorObservation out{};
    if (!system_creation_observed || !generation) return out;
    switch (actual_format) {
        case 10: // DXGI_FORMAT_R16G16B16A16_FLOAT
            out.declaration.dxgi_color_space = 1; // RGB_FULL_G10_NONE_P709
            break;
        case 24: // DXGI_FORMAT_R10G10B10A2_UNORM
        case 28: // DXGI_FORMAT_R8G8B8A8_UNORM
        case 87: // DXGI_FORMAT_B8G8R8A8_UNORM
            out.declaration.dxgi_color_space = 0; // RGB_FULL_G22_NONE_P709
            break;
        default:
            return out;
    }
    out.declaration.generation = generation;
    out.declaration.accepted = true;
    out.origin = FinalColorOrigin::DxgiCreationDefault;
    // SDR reference-white settings are captured separately by the real owner.
    // This helper supplies no luminance, NR projection, queue or guide proof.
    return out;
}
} // namespace k033
