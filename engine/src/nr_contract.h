#pragma once
#include <cstdint>

namespace nrcontract {
struct Rect {
    uint32_t x = 0, y = 0, width = 0, height = 0;
    bool fits(uint64_t resourceWidth, uint32_t resourceHeight) const {
        return width && height && x <= resourceWidth && y <= resourceHeight &&
               width <= resourceWidth - x && height <= resourceHeight - y;
    }
    bool operator==(const Rect &r) const { return x==r.x && y==r.y && width==r.width && height==r.height; }
};
// Versioned POD used by the optional v2 forwarder entry point. Older exports keep
// their ABI. A caller must reject offsets when the installed forwarder lacks v2.
struct Guides {
    uint32_t size = sizeof(Guides);
    Rect depth, motion;
    bool offset() const { return depth.x || depth.y || motion.x || motion.y; }
};
static_assert(sizeof(Guides) == 36, "forwarder contract ABI");
// Not part of the forwarder ABI. Resource pointers deliberately do not enter
// the history key: games normally rotate textures without changing the stream.
struct FrameKey {
    uintptr_t stream=0;
    unsigned grade=0;
    Rect output, depth, motion;
    float scaleX=1.f, scaleY=1.f;
    int depthInverted=0;
    bool operator==(const FrameKey &v) const {
        return grade==v.grade && stream==v.stream && output==v.output && depth==v.depth && motion==v.motion &&
            scaleX==v.scaleX && scaleY==v.scaleY && depthInverted==v.depthInverted;
    }
};
struct History {
    bool valid=false;
    FrameKey previous;
    // Diagnostics: 1=gap/initialization, 2=game, 4=stream, 8=rectangles, 16=motion contract.
    unsigned reset_reason(const FrameKey &v,bool gameReset) const {
        unsigned reason=gameReset ? 2u : 0u;
        if(!valid)return reason|1u;
        if(previous.stream!=v.stream)reason|=4u;
        if(!(previous.output==v.output)||!(previous.depth==v.depth)||!(previous.motion==v.motion))reason|=8u;
        if(previous.scaleX!=v.scaleX||previous.scaleY!=v.scaleY||previous.depthInverted!=v.depthInverted)reason|=16u;
        if(previous.grade!=v.grade)reason|=32u;
        return reason;
    }
    bool needs_reset(const FrameKey &v,bool gameReset) const { return reset_reason(v,gameReset)!=0; }
    void commit(const FrameKey &v) { previous=v;valid=true; }
    void invalidate() { valid=false; }
};
}
