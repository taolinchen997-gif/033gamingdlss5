#pragma once
// A zero cap with no cap previously owned is not a request to reconfigure
// native Reflex. Explicit limiting and FG conversion retain their own path.
namespace nativereflex {
inline bool SkipCapWrite(bool nativePath,float requestedFps,unsigned ownedIntervalUs) {
    return nativePath && requestedFps==0.0f && ownedIntervalUs==0;
}
inline unsigned ReleaseCap(bool nativePath,unsigned gameIntervalUs) {
    return nativePath?gameIntervalUs:0;
}
}
