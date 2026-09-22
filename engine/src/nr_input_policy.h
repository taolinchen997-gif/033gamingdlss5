#pragma once
#include <atomic>
#include <cstdint>
namespace nrinput033 {
enum class Route : unsigned { None, Upscale, Presentation };
// Independent from the engine owner: both routes call the same 033 model,
// but may never process one real frame twice or swap owners mid-session.
class Ownership {
    std::atomic<Route> route{Route::None};
public:
    Route Get() const { return route.load(std::memory_order_acquire); }
    bool Claim(Route requested) {
        if(requested==Route::None)return false;
        auto expected=Route::None;
        return route.compare_exchange_strong(expected,requested)||expected==requested;
    }
};
inline Ownership ownership;
// A positively identified semantic adapter can choose the presentation entry
// without rewriting a legacy inject setting. Existing owner/FG gates still win.
inline bool PreferUpscale(bool configured,bool compatibleAdapter){return configured&&!compatibleAdapter;}
inline bool CanPresent(bool enabled,bool paused,bool integrated,bool preferUpscale,
                       uint64_t offered,bool nativeFg,bool validSurface,Route route) {
    return enabled&&!paused&&integrated&&!nativeFg&&validSurface&&
        route!=Route::Upscale&&(!preferUpscale||route==Route::Presentation)&&
        (!offered||route==Route::Presentation);
}
struct Context {bool presentation=false,nativeGuides=false;uint32_t depthState=0,motionState=0;bool depthPlane0=false,normalizeRe4=false;};
inline thread_local Context context;
struct PresentScope {
    Context previous=context;
    PresentScope(bool native=false,uint32_t depth=0,uint32_t motion=0,bool plane0=false,bool normalize=false){context={true,native,depth,motion,plane0,normalize};}
    ~PresentScope(){context=previous;}
};
inline bool HasNativeGuide(bool supplied){return supplied&&(!context.presentation||context.nativeGuides);}
inline bool NeedsGameState(){return !context.presentation;}
inline int Reset(int requested){return context.presentation&&!context.nativeGuides?1:requested;}
}
