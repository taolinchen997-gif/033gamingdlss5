#include "../src/mfg/framecount.hpp"
#include <cstdio>
#include <vector>
#include <cstring>
// Minimal host exports for the real ReShade header's logging bridge. No game,
// hooks, graphics device or runtime is initialized by these wrapper tests.
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
using namespace mfgunlock;
using namespace mfgunlock::framecount;
using namespace mfgunlock::framecount::internal;
static std::vector<unsigned> seen;
static const sl::DLSSGOptions* last_pointer;
static sl::DLSSGOptions last_copy{};
static bool reject_override=false;
static unsigned original_request=1, pacing_calls=0;
static bool pacing=false;
static sl::Result MockSet(const sl::ViewportHandle&, const sl::DLSSGOptions& o) {
    seen.push_back(o.numFramesToGenerate); last_pointer=&o;
    latency::CopyOptions(o,last_copy);
    return reject_override && o.numFramesToGenerate!=original_request ? sl::Result::eErrorFeatureManagerInvalidState : sl::Result::eOk;
}
static sl::Result MockFeature(sl::Feature,const char*,void*& f) { return sl::Result::eOk; }
static sl::Result MockState(const sl::ViewportHandle&,sl::DLSSGState& s,const sl::DLSSGOptions*) {
    s.status=sl::DLSSGStatus::eOk;s.numFramesActuallyPresented=12;
    if(s.structVersion>=2)s.numFramesToGenerateMax=1;
    return sl::Result::eOk;
}
static int reflex_calls=0;
static const sl::ReflexOptions* reflex_pointer=nullptr;
static sl::Result MockReflex(const sl::ReflexOptions& o) {++reflex_calls;reflex_pointer=&o;return sl::Result::eOk;}
struct Token : sl::FrameToken { operator uint32_t() const override {return 77;} };
static int sleep_original_calls=0;
static sl::Result MockSleep(const sl::FrameToken& t) {++sleep_original_calls;return uint32_t(t)==77 ? sl::Result::eOk : sl::Result::eErrorInvalidParameter;}
static sl::Result MockReflexState(sl::ReflexState& s) {
    s.lowLatencyAvailable=true;s.latencyReportAvailable=true;
    s.frameReport[0].frameID=1;s.frameReport[0].simStartTime=1000;s.frameReport[0].gpuRenderEndTime=21000;
    s.frameReport[1].frameID=2;s.frameReport[1].simStartTime=2000;s.frameReport[1].gpuRenderEndTime=14000;s.frameReport[1].gpuFrameTimeUs=8000;
    return sl::Result::eOk;
}
int main() {
    int checks=0, failures=0;
    auto check=[&](bool b,const char* name){++checks;if(!b){++failures;std::printf("FAIL %s\n",name);}};
    g_real_set_options=MockSet;g_real_get_state=MockState;
    g_ensure_pacing=[](){++pacing_calls;};g_pacing_ready=[](){return pacing;};
    sl::ViewportHandle viewport{};
    sl::DLSSGOptions o{};o.mode=sl::DLSSGMode::eOn;
    g_force_multiplier=0;seen.clear();HookedSetOptions(viewport,o);
    check(seen==std::vector<unsigned>{1}&&pacing_calls==0,"unforced native 2x uses original pacing");
    g_force_multiplier=6;pacing=true;seen.clear();HookedSetOptions(viewport,o);
    check(seen==std::vector<unsigned>{5},"preserve the user selected 6x");
    o.numFramesToGenerate=5;pacing=false;pacing_calls=0;seen.clear();
    g_ensure_pacing=[](){++pacing_calls;pacing=true;};
    HookedSetOptions(viewport,o);
    check(pacing_calls==1&&pacing&&seen==std::vector<unsigned>{5},"native matching 6x still initializes software pacing");
    check(last_pointer==&o&&o.numFramesToGenerate==5,"matching 6x forwards original const options intact");
    seen.clear();HookedSetOptions(viewport,o);
    check(pacing_calls==1&&seen==std::vector<unsigned>{5},"matching 6x pacing preparation is idempotent");
    pacing=false;g_ensure_pacing=[](){++pacing_calls;};seen.clear();HookedSetOptions(viewport,o);
    check(seen==std::vector<unsigned>{5}&&pacing_calls==2,"failed pacing keeps native 6x intact and calls original once");
    g_force_multiplier=0;pacing_calls=0;seen.clear();HookedSetOptions(viewport,o);
    check(pacing_calls==0&&seen==std::vector<unsigned>{5},"disabled force leaves native MFG pacing untouched");
    g_force_multiplier=6;
    o.mode=sl::DLSSGMode::eOff;seen.clear();HookedSetOptions(viewport,o);
    check(last_pointer==&o&&g_accepted_mode==0,"off remains off even with 6x configured");
    check(pacing_calls==0,"FG off never initializes software pacing");
    o.mode=sl::DLSSGMode::eOn;o.numFramesToGenerate=5;g_force_multiplier=2;seen.clear();HookedSetOptions(viewport,o);
    check(seen==std::vector<unsigned>{1}&&o.numFramesToGenerate==5,"explicit reduction works without modifying caller");
    g_force_multiplier=0;seen.clear();HookedSetOptions(viewport,o);
    check(last_pointer==&o&&seen==std::vector<unsigned>{5},"disabled force preserves game request");
    g_force_multiplier=6;o.numFramesToGenerate=1;pacing=false;seen.clear();HookedSetOptions(viewport,o);
    check(seen==std::vector<unsigned>{1}&&pacing_calls>=1,"unverified MFG pacing refuses only override");
    pacing=true;g_advertised_max_generated=3;seen.clear();HookedSetOptions(viewport,o);
    check(seen==std::vector<unsigned>{3},"MFG respects verified runtime ceiling");
    check(!g_declined_no_pacing.load(),"resolved pacing failure no longer leaves a stale UI warning");
    reject_override=true;seen.clear();HookedSetOptions(viewport,o);
    check(seen==std::vector<unsigned>({3,1})&&g_accepted_generated==1,"failed override falls back once and records accepted original");
    reject_override=false;
    SYSTEM_INFO si{};GetSystemInfo(&si);
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,si.dwPageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    DWORD old{};VirtualProtect(pages+si.dwPageSize,si.dwPageSize,PAGE_NOACCESS,&old);
    sl::BaseStructure chain(sl::StructType{},1);
    for(unsigned version=1;version<=5;++version){
        o.structVersion=version;o.numFramesToGenerate=5;o.next=&chain;
        o.colorWidth=5120;o.colorHeight=2160;o.dynamicResWidth=2560;
        const auto size=latency::OptionsBytes(o);auto* short_o=reinterpret_cast<sl::DLSSGOptions*>(pages+si.dwPageSize-size);
        VirtualProtect(pages,si.dwPageSize,PAGE_READWRITE,&old);std::memcpy(short_o,&o,size);
        std::vector<unsigned char> before(size);std::memcpy(before.data(),short_o,size);
        VirtualProtect(pages,si.dwPageSize,PAGE_READONLY,&old);
        g_force_multiplier=2;seen.clear();HookedSetOptions(viewport,*short_o);
        check(seen==std::vector<unsigned>{1}&&last_copy.structVersion==version,"old structure at guard page: no overread");
        check(std::memcmp(short_o,before.data(),size)==0,"read-only caller structure is never mutated");
        check(last_copy.next==&chain&&last_copy.colorWidth==5120&&last_copy.dynamicResWidth==2560,"copy preserves chain, version and resource fields");
    }
    VirtualFree(pages,0,MEM_RELEASE);
    o.structVersion=6;seen.clear();HookedSetOptions(viewport,o);
    check(last_pointer==&o&&seen==std::vector<unsigned>{5},"future option version passes through intact");
    sl::DLSSGState state{};g_force_multiplier=2;g_advertised_max_generated=5;
    HookedGetState(viewport,state,nullptr);
    check(state.numFramesActuallyPresented==12,"wrapper does not change presented frame counter");
    HookedGetState(viewport,state,nullptr);
    check(state.numFramesToGenerateMax==5,"MFG profile retains ceiling advertisement");
    g_real_get_feature_function=MockFeature;
    void* f=reinterpret_cast<void*>(MockSet);
    HookedGetFeatureFunction(sl::kFeatureDLSS_G,"slDLSSGSetOptions",f);
    HookedGetFeatureFunction(sl::kFeatureDLSS_G,"slDLSSGSetOptions",f);
    check(g_real_set_options==MockSet&&f==reinterpret_cast<void*>(&HookedSetOptions),"repeat feature lookup cannot self-hook recursively");
    f=reinterpret_cast<void*>(MockReflex);HookedGetFeatureFunction(sl::kFeatureReflex,"slReflexSetOptions",f);
    HookedGetFeatureFunction(sl::kFeatureReflex,"slReflexSetOptions",f);
    sl::ReflexOptions ro{};ro.mode=sl::ReflexMode::eLowLatencyWithBoost;ro.frameLimitUs=8100;ro.useMarkersToOptimize=false;
    reinterpret_cast<::PFun_slReflexSetOptions*>(f)(ro);
    check(reflex_calls==1&&reflex_pointer==&ro&&reflex::mode==2&&reflex::cap_us==8100,"Reflex original called once, cap/markers/boost preserved");
    reflex::real_sleep=MockSleep;Token token;reflex::Sleep(token);
    check(sleep_original_calls==1&&reflex::sleep_calls==1&&reflex::sleep_errors==0,"Reflex sleep observed once without inserting duplicate sleep");
    reflex::real_state=MockReflexState;sl::ReflexState rs{};reflex::GetState(rs);
    check(reflex::render_latency_us==12000&&reflex::gpu_frame_us==8000&&rs.frameReport[1].frameID==2,"observer uses newest valid report without changing game data");
    reflex::Report(1,1);
    check(sleep_original_calls==1&&reflex_calls==1,"diagnostic reporting makes no extra Reflex calls");
    f=reinterpret_cast<void*>(MockSet);HookedGetFeatureFunction(sl::kFeatureReflex,"slDLSSGSetOptions",f);
    check(f==reinterpret_cast<void*>(MockSet),"other features with same function name are not wrapped");
    std::printf("MFG latency: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
