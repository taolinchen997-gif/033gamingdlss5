#include "../src/nr_contract.h"
#include <cstdio>
#include <limits>
int main() {
    using nrcontract::Rect;
    int checks = 0, failures = 0;
    auto check = [&](bool v, const char *name) { ++checks; if (!v) { ++failures; std::printf("FAIL %s\n", name); } };
    check(Rect{0,0,1920,1080}.fits(1920,1080), "full frame");
    check(Rect{16,8,1920,1080}.fits(2048,1152), "atlas region");
    check(!Rect{16,8,1920,1080}.fits(1920,1080), "offset overflow");
    check(!Rect{0,0,0,1080}.fits(1920,1080), "zero width");
    check(!Rect{0,0,1920,0}.fits(1920,1080), "zero height");
    check(!Rect{UINT32_MAX,0,2,1}.fits(UINT32_MAX,1), "addition would wrap");
    check(Rect{UINT32_MAX,0,1,1}.fits(uint64_t(UINT32_MAX)+1,1), "64 bit resource bounds");
    check(!Rect{0,UINT32_MAX,1,2}.fits(1,UINT32_MAX), "height would wrap");
    nrcontract::Guides g;
    g.depth = {8,4,1280,720}; g.motion = {16,8,1920,1080};
    check(g.size == 36, "v2 ABI size");
    check(g.offset(), "offsets present");
    check(g.depth.fits(1296,736) && g.motion.fits(2048,1152), "independent guide dimensions");
    nrcontract::History history;
    nrcontract::FrameKey frame;
    frame.stream=123;frame.output={0,0,1920,1080};frame.depth=g.depth;frame.motion=g.motion;
    frame.scaleX=1920;frame.scaleY=1080;
    check(history.needs_reset(frame,false), "first NR frame must reset even if game does not");
    history.commit(frame);
    check(!history.needs_reset(frame,false), "unchanged contract preserves history");
    check(history.needs_reset(frame,true), "game camera cut resets history");
    auto changed=frame;changed.scaleX=-1920;
    check(history.needs_reset(changed,false), "MV convention change resets history");
    changed=frame;changed.stream=124;
    check(history.needs_reset(changed,false), "different NGX feature resets history");
    changed=frame;changed.motion.x++;
    check(history.needs_reset(changed,false), "atlas motion region change resets history");
    changed=frame;changed.output.width=1280;
    check(history.needs_reset(changed,false), "output resize resets history");
    changed=frame;changed.grade=123;
    check(history.reset_reason(changed,false)==32,"pre-NR grade change invalidates differently graded history");
    history.invalidate();check(history.needs_reset(frame,false), "failed or skipped frame invalidates history");
    history.commit(frame);check(!history.needs_reset(frame,false), "successful evaluation resumes history");
    std::printf("guide contract: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
