#pragma once
#include <cstdint>
#include "../include/033_input.h"

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct ID3D12Fence;

namespace k033 {
// Internal, process-local views, never serialized or exported. Copying one does
// not retain a COM object, acquire a lease, wait, or submit work. The native
// owner retains every referenced object until its submitted consumers retire.
struct FinalFrameKey {
    uint64_t chain = 0, generation = 0, present_serial = 0;
    bool valid() const noexcept { return chain && generation && present_serial; }
    bool operator==(const FinalFrameKey& b) const noexcept {
        return chain == b.chain && generation == b.generation && present_serial == b.present_serial;
    }
};

struct FinalTarget12 {
    // queue is the actual DIRECT queue supplied when this chain was created
    // (or a successful ResizeBuffers1 replacement), not a new private queue.
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12Resource* resource = nullptr;
    uint32_t buffer_index = 0, width = 0, height = 0, format = 0;
    K033_Rect rect{};
    uint32_t original_state = 0;
    bool state_known = false;
};

struct FinalColorDeclaration {
    // Only a successful declaration for this target generation. Float format,
    // an NGX IsHDR flag, a successful Evaluate, or another chain is insufficient.
    uint64_t generation = 0;
    uint32_t dxgi_color_space = 0;
    bool accepted = false;
    // SDR white is explicit common settings, with its own captured revision.
    // PQ/scRGB conversion uses their defined units, not an inferred gain of 1.
    float sdr_reference_white_nits = 0.f;
    uint64_t reference_white_revision = 0;
};

struct FinalPrivateColor12 {
    ID3D12Resource* resource = nullptr;
    ID3D12Fence* completion = nullptr;
    uint64_t completion_value = 0;
    uint64_t owner = 0, lease = 0;
    uint32_t width = 0, height = 0, format = 0, state = 0;
    // A submitted copy is not a ready image. The owner checks this fence's
    // completion/error on its normal queue-progress path before borrowing it.
};

enum class FinalGuideAssociation : uint32_t {
    Unknown = 0,
    // Native adapter correlates this exact successful game submission and its
    // output resource with this final target, retaining the original guide lease.
    NativeSubmission = 1
};
struct FinalGuides {
    FinalFrameKey target{};
    K033_InputFrame3 input{};
    uint64_t owner = 0, association = 0;
    FinalGuideAssociation source = FinalGuideAssociation::Unknown;
    // input already carries per-field origin, jitter, exposure and resource
    // rectangles. Preserve those values. No "latest Evaluate" substitution.
    // Merely filling this view does not validate native provenance or resources;
    // only the guide owner's actual acquisition returns a usable association.
};

struct FinalFrame {
    FinalFrameKey key{};
    FinalTarget12 target{};
    FinalColorDeclaration color{};
    FinalPrivateColor12 original{};
    FinalGuides guides{};
    uint64_t settings_generation = 0, nr_generation = 0;
};

// Metadata comparisons only; these deliberately do not grant GPU/resource or
// color qualification. Native owners also recheck their live tickets and leases
// immediately before submission/return, without holding a Chain metadata lock.
inline bool same_final_settings(const FinalFrame& a, const FinalFrame& b) noexcept {
    return a.key.valid() && a.key == b.key && a.settings_generation && a.nr_generation &&
        a.settings_generation == b.settings_generation && a.nr_generation == b.nr_generation;
}
inline bool final_guides_address_this_frame(const FinalFrame& f) noexcept {
    return f.key.valid() && f.guides.target == f.key &&
        f.guides.source == FinalGuideAssociation::NativeSubmission &&
        f.guides.owner && f.guides.association && f.guides.input.frame.input.lease;
}
} // namespace k033
