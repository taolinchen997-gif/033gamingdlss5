#pragma once
#include <cstdint>
// Engine adapters report semantic inputs, not a guessed texture selected by size.
// Acquire returns one COM reference on depth, motion and queue; caller releases
// each. No STL object, allocation or graphics command crosses this ABI.
namespace nrgame033 {
constexpr uint32_t Version=1;
enum Source:uint32_t { Unknown=0, NgxDeclared=1, ReEngineScene=2, ImageEstimated=3 };
enum Flags:uint32_t {
    DepthReversed=1, MotionUnjittered=2, DisplayResolved=4, JitterKnown=8,
    SceneReset=16, FullExtent=32
};
struct Frame {
    uint32_t size=sizeof(Frame),version=Version,source=Unknown,flags=0;
    uint64_t serial=0;
    uintptr_t stream=0;
    void* depth=nullptr;
    void* motion=nullptr;
    void* queue=nullptr;
    uint32_t width=0,height=0;
    float scaleX=0,scaleY=0,jitterX=0,jitterY=0;
};
using Acquire=int(__cdecl*)(Frame*);
// Optional, separately named export. The original Frame/Acquire ABI remains
// byte-for-byte v1 and does not accept unequal presentation/guide extents.
struct Frame2 {
    uint32_t size=sizeof(Frame2),version=2;
    Frame frame;
    uint32_t motionWidth=0,motionHeight=0,outputWidth=0,outputHeight=0;
};
using Acquire2=int(__cdecl*)(Frame2*);
// V2 return values. Negative results own no COM references and report only
// the adapter's current semantic lookup outcome, not sampled GPU contents.
enum SceneAcquire:int { SceneAvailable=1,SceneNotPublished=0,SceneNotReady=-1,
    SceneOutputMissing=-2,SceneMissing=-3,SceneDepthMissing=-4,SceneMotionMissing=-5,
    SceneQueueMissing=-6,SceneCoverageInvalid=-7,SceneUnsupported=-8 };
// Separate discovery contract leaves the existing Frame ABI unchanged.
enum AdapterId:uint32_t { Re4Tdb71=1 };
// Descriptor v2 adds an explicit renderer lifecycle. Frame ABI stays at v1.
// v1 consumers still receive compatible=0/1 from a v2 adapter.
enum RendererState:uint32_t { Initializing=0, RendererReady=1, Unavailable=2 };
struct Adapter {
    uint32_t size=sizeof(Adapter),version=2,id=0,source=Unknown;
    uint32_t frameVersion=Version,sceneSchema=0,compatible=0,reserved=0;
};
using Describe=int(__cdecl*)(Adapter*);
inline bool KnownAdapter(const Adapter& a){
    return a.size==sizeof(Adapter)&&(a.version==1||a.version==2)&&a.frameVersion==Version&&
        a.id==Re4Tdb71&&a.source==ReEngineScene&&a.sceneSchema==71&&a.reserved==0;
}
inline bool Supported(const Adapter& a){return KnownAdapter(a)&&a.compatible==RendererReady;}
inline bool WaitingForRenderer(const Adapter& a){
    return KnownAdapter(a)&&a.version==2&&a.compatible==Initializing;
}
enum class Result { Missing, Contract, Extent, Device, Queue, State, Stale, Ready, MotionExtent };
}
