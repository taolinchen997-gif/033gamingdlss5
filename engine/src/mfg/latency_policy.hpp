// Frame generation policy and version-bounded option copies. No GPU/UI work.
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstring>
#include <streamline/sl_dlss_g.h>

namespace mfgunlock::latency {
// Keep the user-selected multiplier; explicit override can both raise and lower.
inline unsigned DesiredGenerated(unsigned force, unsigned requested) {
    return force >= 2 && force <= 6 ? force - 1 : requested;
}

inline size_t OptionsBytes(const sl::DLSSGOptions& o) {
    // Do not sizeof-copy a v1 caller using the v5 SDK: older allocations can end
    // at the last member for that version. Padding need not be read either.
    #define K033_MEMBER_END(m) (size_t(reinterpret_cast<const char*>(&o.m) - reinterpret_cast<const char*>(&o)) + sizeof(o.m))
    switch (o.structVersion) {
    case 1: return K033_MEMBER_END(onErrorCallback);
    case 2: return K033_MEMBER_END(bReserved15);
    case 3: return K033_MEMBER_END(queueParallelismMode);
    case 4: return K033_MEMBER_END(enableUserInterfaceRecomposition);
    case 5: return K033_MEMBER_END(dynamicTargetFrameRate);
    default: return 0; // Future layouts must pass through, not be truncated.
    }
    #undef K033_MEMBER_END
}
inline bool CopyOptions(const sl::DLSSGOptions& source, sl::DLSSGOptions& dest) {
    const size_t bytes = OptionsBytes(source);
    if (!bytes) return false;
    std::memcpy(&dest, &source, bytes);
    return true;
}
} // namespace mfgunlock::latency
