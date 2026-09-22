#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <array>
#include <algorithm>
#include <vector>
#include "fault_features.inc"
#include "../src/nr_layer_bank.h"
#include "../src/nr_feature_params.h"
#include "../src/nr_fault.h"
#include "../src/nr_recovery_policy.h"
static uint64_t TestNowValue=1000;
#define GetTickCount64() TestNowValue
#include "../src/nr_contract.h"
#include "../src/render_core_abi.h"
#include "../src/nr_layer_resolution.h"
#include "../src/nr_distribution_policy.h"
#if __has_include("../src/nr_stage_diagnostic.h")
#include "../src/nr_stage_diagnostic.h"
#else
namespace nrstage033 {enum class Reason{BlitCreate};}
#endif

// No DXGI/D3D/NGX implementation is linked. These types only satisfy the
// signatures of the verbatim production slices and count attempted calls.
static int closes=0,resets=0,submits=0,polls=0;
struct ID3D12Device{};struct ID3D12GraphicsCommandList{int Reset(void*,void*){++resets;return 0;}int Close(){++closes;return 0;}};struct ID3D12Resource{};
using ID3D12CommandList=ID3D12GraphicsCommandList;
struct NVSDK_NGX_Handle{};
using NVSDK_NGX_Result=int;using NVSDK_NGX_Feature=int;
constexpr int NVSDK_NGX_Result_Success=1,NVSDK_NGX_Result_Fail=0;
struct NVSDK_NGX_Parameter{template<class T>void Set(const char*,T){}};
static int calls=0,coreCalls=0,destroys=0,releases=0,checks=0,failed=0,throwAt=0,failAt=0;
static bool coreThrow=false,coreFail=false;
static constexpr DWORD Fault=0xc0000005;
static NVSDK_NGX_Parameter params,paramPool[6];static int opaque,opaquePool[12],allocs=0;
static NVSDK_NGX_Parameter* lastEvalParams=nullptr;
static void Check(bool x,const char* s){++checks;if(!x){++failed;printf("FAIL %s\n",s);}}
static void Log(const char*,...){}
// Diagnostics that production added inside the verbatim slices only record or log: count them, never capture frames.
static int faultLogs=0,watchdogNotes=0;
namespace watchdog033 {static void Note(const char*){++watchdogNotes;}}
namespace nrframeobs033 {constexpr uint32_t StageOutcome=0;static struct{bool Active()const{return false;}}collector;static void Native(uint32_t,const std::array<uint64_t,10>&){}}
static void Hit(int n){if(n==throwAt)RaiseException(Fault,0,0,nullptr);}
static int Alloc(NVSDK_NGX_Parameter** p){*p=&paramPool[allocs++];return 1;}
static int CreateCore(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle** p){
    ++coreCalls;if(coreThrow)RaiseException(Fault,0,0,nullptr);if(coreFail)return 0;*p=reinterpret_cast<NVSDK_NGX_Handle*>(coreCalls==1?&opaque:&opaquePool[coreCalls]);return 1;}
static int EvalCore(ID3D12GraphicsCommandList*,NVSDK_NGX_Handle*,NVSDK_NGX_Parameter* p,void*){lastEvalParams=p;++calls;Hit(calls);return calls==failAt?0:1;}
static int EvalForward(...){++calls;Hit(calls);return calls==failAt?0:1;}
static void Destroy(NVSDK_NGX_Parameter*){++destroys;}
static int ReleaseCore(NVSDK_NGX_Handle*){++releases;Hit(releases);return releases==failAt?0:1;}
static void* CreateForward(const wchar_t*,const wchar_t*,ID3D12Device*,ID3D12GraphicsCommandList*,void*,unsigned,unsigned,int,float,int,float,float,float,int,int,float){
    ++calls;Hit(calls);return calls==failAt?nullptr:(calls==1?&opaque:&opaquePool[calls]);}
static void* CreateOld(const wchar_t*,const wchar_t*,ID3D12Device*,ID3D12GraphicsCommandList*,void*,unsigned,unsigned,int,float,int,float,float,float,int,int){return nullptr;}
static int ReleaseForward(void*){++releases;Hit(releases);return releases==failAt?0:1;}
static void ReleaseOld(void*){}
static void Probe(void*,const char*,float,int){}
static bool g_core_probe_ok=false;
static struct {decltype(&Alloc) alloc=Alloc;decltype(&CreateCore) create=CreateCore;decltype(&EvalCore) evaluate=EvalCore;
    decltype(&Destroy) destroy=Destroy;decltype(&ReleaseCore) release=ReleaseCore;} g_ngx;
namespace nrfwd {
static bool s_core_ok=false;static void* s_core_feat=nullptr;static NVSDK_NGX_Parameter* s_core_params=nullptr;
static nrlayers::FeatureParams<NVSDK_NGX_Parameter> s_core_models;
static NVSDK_NGX_Parameter* s_caps=&params;static std::wstring s_snippet=L"mock",s_data=L"mock";
static int s_float_slot=0;static std::string s_note;static unsigned long long s_release_failed=0;
static auto s_create_v2=CreateForward;static auto s_create=CreateOld;static auto s_probe=Probe;
static auto s_eval_v2=EvalForward;static auto s_eval=EvalForward;
static auto s_release_checked=ReleaseForward;static auto s_release=ReleaseOld;
static bool loaded(){return true;}
static int last_init(){return 0;}static int last_create(){return 0;}
static const char* note(){return nrfault033::Blocked()?nrfault033::Note():s_note.c_str();}
static void log_fault(DWORD,unsigned,const char*){++faultLogs;}
#include "fault_create.inc"
#include "fault_evaluate.inc"
#include "fault_release.inc"
static void* create(ID3D12Device* d,ID3D12GraphicsCommandList* l,unsigned w,unsigned h,int p,float i,int s,float a,float b,float c,int m,int u,DWORD* e,float g){return create_inner(d,l,w,h,p,i,s,a,b,c,m,u,e,g);}
}
int TestInvoke(k033core::AfterUpscale,const k033core::Frame*);
const void* FaultStateAddress();
int TestCapabilities(bool);
bool TestClaim(uint32_t);
static int __cdecl Callback(const k033core::Frame*){++calls;Hit(calls);return calls==failAt?0:1;}

namespace carrier {
struct Config{int preset=0,style=0,auto_mask=0,ui_correct=0,passes=2,work=100,modelfull=1,passwork=100,passwork3=100;float intensity=1,local_structure=1,local_tone=1,skin_structure=1,global_tone=1;
    nrlayers::Model extra[2]={nrlayers::Neutral(1),nrlayers::Neutral(2)};}cfg;
static bool g_inject_dead=false;
static struct{void* nr_feat=nullptr;}g;
static unsigned TuneSig(bool=true){return 1;}static int TypedColorFormat(int x){return x;}
}
namespace nrdispatch {struct WriterAccess{bool entered=true;};struct LongStep{void end(){}};}
namespace nrinput033 {enum class Route{Presentation,Upscale};static struct{Route Get(){return Route::Upscale;}}ownership;}
namespace nrresize033 {
struct Geometry{void* dev;unsigned w,h,gw,gh;int fmt;};
static bool DrainBeforeBuild(bool,bool,Geometry,Geometry){return false;}
template<class A,class B,class C>bool Prepare(bool,A,B,C){return true;}
}
namespace scale {static int creates=0,destroys=0,failCreates=0;template<class T>bool Create(T& b,ID3D12Device*){++creates;if(failCreates){--failCreates;b.error="mock allocation failure";return false;}b.ready=true;return true;}template<class T>void Destroy(T& b){++destroys;b.ready=false;}}
namespace pumpfixture {
using UINT64=uint64_t;
struct Fence{UINT64 completed=1;UINT64 GetCompletedValue(){++polls;return completed;}}fence;
struct Allocator{int Reset(){++resets;return 0;}}allocator;
static int executes=0,signals=0,signalFailures=0;static std::vector<UINT64>signalValues;
struct Queue{void ExecuteCommandLists(int,ID3D12CommandList**){++submits;++executes;}HRESULT Signal(Fence*,UINT64 value){++submits;++signals;signalValues.push_back(value);if(signalFailures){--signalFailures;return E_FAIL;}return S_OK;}}queue;
static ID3D12Device device;static ID3D12GraphicsCommandList list;
static auto* s_bf=&fence;static auto* s_ba=&allocator;static auto* s_bl=&list;static auto* s_bq=&queue;
static std::atomic<bool>s_building{false},s_cpu_building{false};
static bool s_candidate_valid=false,s_want_build=true,s_failed=false,s_build_defer=false;
static ID3D12Device* s_want_dev=&device;static auto* s_dev=&device;
static int currentOpaque;static void* s_feat=&currentOpaque;
static unsigned s_w=64,s_h=64,s_built_gw=64,s_built_gh=64,s_want_w=64,s_want_h=64,s_want_gw=64,s_want_gh=64;
static int s_fmt=1,s_want_fmt=1,s_retry_cool=0,s_grace=0,s_build_cool=0,s_selected_passes=2;
static UINT64 s_bv=1,s_first_generation=1,s_generation=1,s_memory_budget=0,s_memory_usage=0,s_bank_revision=0;
static UINT64 s_pass_frame[3]={};static int s_hist_age=0,s_pass_builds=0;static double s_last_build_ms=0;
static std::string s_build_note,s_note;static struct{void invalidate(){}}s_history;
static struct{bool ready=true;std::string error;}s_blit;
static nrrecovery033::Retry s_blit_retry,s_signal_retry;
static nrrecovery033::Evaluation s_evaluate_recovery;
static bool s_signal_pending=false;
static std::atomic<unsigned>s_auto_recovery{0};
static struct{int Exit(nrstage033::Reason,int v){return v;}}stageDiagnostic;
struct ModelBank{
    void* feat=nullptr;void* extra_feat[2]={};
    ID3D12Resource *full=nullptr,*model_input=nullptr,*out=nullptr,*res=nullptr,*spare_full=nullptr,*spare_res=nullptr,
        *extra_out=nullptr,*extra_alt=nullptr,*pass_input=nullptr,*pass_input3=nullptr,*hold_depth=nullptr,*hold_motion=nullptr,*hold_white=nullptr;
    ID3D12Resource *hist[2]={},*refined[2]={};
    ID3D12Device* dev=nullptr;unsigned built_tune=1,built_appearance=1,w=64,h=64,sw=64,sh=64,ew=64,eh=64,tw=64,th=64,built_gw=64,built_gh=64;
    int built_work=100,built_full=1,built_passwork=100,built_passwork3=100,built_passes=2,selected_passes=2,fmt=1,model_fmt=1;
    carrier::Config model_cfg;
};
static ModelBank s_candidate,s_quarantined_candidate,s_candidate_borrowed,live;
static uint64_t s_candidate_revision=0;static bool s_candidate_reused=false;
static int retired=0;
static bool releaseOnPark=false;
static ModelBank ReadBank(){ModelBank b=live;b.feat=s_feat;return b;}
static void WriteBank(const ModelBank& b){live=b;s_feat=b.feat;}
#if !NR_LAYER_FAULT_TEST
static void RetireBank(const ModelBank&){++retired;}
#endif
static void ParkTick(){if(releaseOnPark){void* p=&opaque;nrfwd::release_inner(p,false);}if(s_retry_cool>0)--s_retry_cool;}
static bool AnyParkedFeature(){return false;}
static bool faultOnRetire=false;static std::vector<void*> retiredFeatures;static std::vector<ID3D12Resource*> retiredTextures;
static void Release(){++retired;for(void* p:{s_feat,live.extra_feat[0],live.extra_feat[1]})if(p)retiredFeatures.push_back(p);
    for(auto p:{live.full,live.model_input,live.out,live.res,live.extra_out,live.extra_alt,live.pass_input,live.pass_input3})if(p)retiredTextures.push_back(p);
    if(faultOnRetire){void* p=s_feat?s_feat:live.extra_feat[0];nrfwd::release_inner(p,false);}s_feat=nullptr;}
static void BuildWait(const char*){}
static bool EnsureBuilder(ID3D12Device*){return true;}
static void SampleMemory(ID3D12Device*){}
static bool Build(ID3D12Device*,ID3D12GraphicsCommandList*,unsigned,unsigned,int);
#if NR_LAYER_FAULT_TEST
static ID3D12Resource textures[12];static int allocations=0;
constexpr int D3D12_RESOURCE_STATE_UNORDERED_ACCESS=0;
static bool MakeTexOn(ID3D12Device*,ID3D12Resource** p,const char*,unsigned,unsigned,int,int){*p=&textures[allocations++];return true;}
#include "fault_layer_bank.inc"
#endif
#include "recovery_helpers.inc"
#include "fault_pump.inc"
#if NR_LAYER_FAULT_TEST
static int liveExtra[2];static ID3D12Resource borrowedTexture;
static void Reuse(){live.dev=&device;live.feat=&currentOpaque;s_feat=live.feat;live.extra_feat[0]=&liveExtra[0];live.extra_feat[1]=&liveExtra[1];
    live.full=live.model_input=live.out=live.res=live.extra_out=live.extra_alt=live.pass_input=live.pass_input3=&borrowedTexture;
    live.model_cfg=carrier::cfg;live.built_passes=live.selected_passes=carrier::cfg.passes;}
static void Edits(){carrier::cfg.style=2;carrier::cfg.extra[0].style=1;carrier::cfg.extra[1].style=2;carrier::cfg.passes=3;s_want_build=true;s_failed=false;}
static void NoBorrowRetired(){Check(std::find(retiredFeatures.begin(),retiredFeatures.end(),&currentOpaque)==retiredFeatures.end(),"borrowed first model not retired");
    Check(std::find(retiredFeatures.begin(),retiredFeatures.end(),&liveExtra[0])==retiredFeatures.end(),"borrowed second model not retired");
    Check(retiredTextures.empty(),"borrowed frame textures not retired");}
#endif
}
namespace nrstack {struct Tune{int preset=0,style=0;float intensity=1,structure=1,tone=1,skin=1,globalTone=1;};template<class...T>Tune ForPass(T...){return {};}}
namespace hostfixture {
static ID3D12Device device;static ID3D12GraphicsCommandList list;
static void* s_feat=nullptr;static void* s_extra_feat[2]={};static bool s_defer_build=true,s_build_defer=false,s_failed=false;
static std::string s_note;static int parkReleases=0,bankReleases=0;
static bool parked=false;
static bool AnyParkedFeature(){return parked;}
static void ReleaseParkedFeaturesNow(){++parkReleases;}
static void Release(){++bankReleases;}
static bool ConfigurePasses(ID3D12Device* dev,ID3D12GraphicsCommandList* cl,unsigned sw,unsigned sh,int){
    const int count=carrier::cfg.passes;unsigned ew=sw,eh=sh;const auto layout=nrlayersr::Make({sw,sh},nrlayersr::ClampWork(carrier::cfg.passwork),nrlayersr::ClampWork(carrier::cfg.passwork3));
    if(count>1){
#include "fault_extra_create.inc"
    return true;
}
static bool MainCreate(){auto* dev=&device;auto* cl=&list;unsigned sw=64,sh=64;int model_fmt=0;
#include "fault_main_create.inc"
    return true;
}
}
bool pumpfixture::Build(ID3D12Device*,ID3D12GraphicsCommandList*,unsigned,unsigned,int){
    hostfixture::s_feat=nullptr;bool ok=hostfixture::MainCreate();s_feat=hostfixture::s_feat;return ok;}
static void* Forward(DWORD* code){return nrfwd::create_inner(&hostfixture::device,&hostfixture::list,64,64,0,1,0,1,1,1,0,0,code,1);}
static int Evaluate(DWORD* code){return nrfwd::evaluate(&hostfixture::list,&opaque,nullptr,nullptr,nullptr,nullptr,64,64,64,64,0,0,1,0,1,1,1,0,1,1,code);}
namespace commandlife {
#include "fault_retire_predicate.inc"
}
}
namespace leasefixture {
static int released=0,queried=0;
struct Ref{uint64_t value=0;void Release(){++released;}uint64_t GetCompletedValue(){++queried;return value;}} ref,fence,queue;
struct Use{Ref* fence=nullptr;Ref* queue=nullptr;uint64_t value=1;};
struct Slot{bool active=false,reset=true,recording=false,pinned=false;unsigned pending=0;
    struct{bool Invalidated()const{return false;}}allocator;
    Ref* refs[1]={};Use uses[1]={};void* heap=nullptr;};
static std::array<Slot,1>slots;static int allocator_retired=0,retired=0;
static bool Discarded(const Slot&){return false;}
#include "fault_collect.inc"
static void Prepare(){slots[0].active=true;slots[0].refs[0]=&ref;slots[0].uses[0]={&fence,&queue,1};}
}
int main(int argc,char**argv){
    const int scenario=argc>1?atoi(argv[1]):0;DWORD code=0;k033core::Frame frame;
    const auto config=carrier::cfg;const bool oldChoice=carrier::g_inject_dead;
    switch(scenario){
    case 1:failAt=1;Check(TestInvoke(Callback,&frame)==0,"normal callback failure");Check(TestInvoke(Callback,&frame)==1&&calls==2,"normal callback retries");break;
    case 2:throwAt=1;Check(TestInvoke(Callback,&frame)==0,"caught callback SEH");TestInvoke(Callback,&frame);Check(calls==1,"callback SEH refuses reentry");Check(nrfault033::Code()==Fault,"callback code retained");break;
    case 3:throwAt=1;Check(!Forward(&code)&&code==Fault,"forward create propagates SEH");Forward(&code);Check(calls==1,"forward create SEH refuses retry");break;
    case 4:g_core_probe_ok=true;coreThrow=true;Check(!Forward(&code)&&code==Fault,"core create propagates SEH");Check(calls==0&&destroys==0,"core SEH neither destroys nor falls back");Forward(&code);Check(coreCalls==1&&calls==0,"core SEH cannot change route");break;
    case 5:g_core_probe_ok=true;coreFail=true;Check(Forward(&code)!=nullptr&&code==0,"ordinary core error falls back");Check(coreCalls==1&&calls==1&&destroys==1,"ordinary core cleanup preserved");break;
    case 6:failAt=1;Check(!Forward(&code)&&code==0,"ordinary create failure");Check(Forward(&code)!=nullptr&&calls==2,"ordinary create retry");break;
    case 7:throwAt=2;carrier::cfg.passes=2;Check(!hostfixture::MainCreate(),"additional model SEH fails build");Check(calls==2,"exactly first and additional calls");Forward(&code);Check(calls==2,"additional SEH blocks future route");Check(hostfixture::bankReleases==0,"additional SEH retains partial bank");break;
    case 8:throwAt=1;{void* p=&opaque;nrfwd::release_inner(p,false);nrfwd::release_inner(p,false);Check(releases==1&&p==&opaque,"release SEH retains pointer and refuses retry");}break;
    case 9:failAt=1;{void* p=&opaque;nrfwd::release_inner(p,false);Check(p==&opaque,"ordinary release failure retains pointer");nrfwd::release_inner(p,false);Check(releases==2&&!p,"ordinary release retry succeeds");}break;
    case 10:throwAt=1;Check(!hostfixture::MainCreate(),"main model SEH fails build");Check(hostfixture::bankReleases==0&&hostfixture::parkReleases==0,"main SEH retains without release");Check(carrier::g_inject_dead==oldChoice,"main SEH does not route fallback");break;
    case 11:throwAt=1;Check(Evaluate(&code)==0&&code==Fault,"forward evaluate SEH propagated");Evaluate(&code);Check(calls==1,"evaluate never retries after SEH");break;
    case 12:failAt=1;Check(Evaluate(&code)==0&&code==0,"ordinary evaluate error");Check(Evaluate(&code)==1&&calls==2,"ordinary evaluate retry allowed");break;
    case 13:nrfwd::s_core_feat=&opaque;nrfwd::s_core_params=&params;*nrfwd::s_core_models.Free()={&opaque,&params};throwAt=1;Evaluate(&code);Evaluate(&code);Check(calls==1&&code==Fault,"core evaluate SEH cannot reenter");break;
    case 14:throwAt=1;Forward(&code);Check(TestInvoke(Callback,&frame)==0&&calls==1,"renderer fault blocks core callback");break;
    case 15:Check(TestCapabilities(true)==0,"capabilities SEH caught, latched and not reentered");break;
    case 16:Check(TestCapabilities(false)==0,"ordinary capability failure retries");break;
    case 17:throwAt=2;pumpfixture::PumpBuild();Check(closes==0&&submits==0&&pumpfixture::retired==0,"faulted build not closed submitted or retired");
        Check(pumpfixture::s_feat==&pumpfixture::currentOpaque&&pumpfixture::s_quarantined_candidate.feat==&opaque,"active and partial candidate both retained");
        {const int before=resets;for(int i=0;i<300;++i)pumpfixture::PumpBuild();Check(calls==2&&resets==before&&polls==0,"faulted present pump does no consumer or GPU maintenance");}break;
    case 18:failAt=1;pumpfixture::PumpBuild();Check(!nrfault033::Blocked()&&pumpfixture::s_retry_cool==120,"ordinary build retains cooldown");
        Check(pumpfixture::s_feat==&pumpfixture::currentOpaque,"ordinary failure retains current bank");
        for(int i=0;i<120;++i)pumpfixture::PumpBuild();Check(calls>=3&&submits==2,"ordinary build retries and submits");break;
    case 19:failAt=1;throwAt=2;hostfixture::parked=true;Check(!hostfixture::MainCreate(),"fallback creation SEH fails build");Check(hostfixture::bankReleases==0&&!carrier::g_inject_dead,"fallback SEH does not clean or route around gate");break;
    case 20:throwAt=1;pumpfixture::releaseOnPark=true;pumpfixture::PumpBuild();Check(nrfault033::Blocked()&&releases==1,"retirement release fault latched");Check(resets==0&&closes==0&&calls==0,"retirement fault stops current pump before reset/create");break;
    case 21:leasefixture::Prepare();leasefixture::fence.value=1;nrfault033::Record(Fault,nrfault033::Site::Callback);leasefixture::CollectLocked();
        Check(leasefixture::released==0&&leasefixture::queried==0,"faulted lease collector neither queries nor releases");Check(leasefixture::slots[0].active,"faulted lease retained");break;
    case 22:leasefixture::Prepare();leasefixture::CollectLocked();Check(leasefixture::released==0&&leasefixture::slots[0].active,"unfinished GPU refs retained");
        leasefixture::fence.value=1;leasefixture::CollectLocked();Check(leasefixture::released==3&&leasefixture::retired==1,"normal proven-complete lease retires");break;
    case 23:nrfault033::Record(Fault,nrfault033::Site::Callback);Check(!TestClaim(k033core::Host033)&&!TestClaim(k033core::NativeVulkan),"fatal session cannot acquire another route");break;
    case 24:Check(TestClaim(k033core::Host033)&&!TestClaim(k033core::NativeVulkan),"provider 0 allows host and refuses competing native ownership");break;
#if NR_LAYER_FAULT_TEST
    case 25:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=2;throwAt=1;PumpBuild();
        Check(nrfault033::Blocked()&&calls==1&&closes==0&&submits==0&&retired==0,"incremental create SEH neither closes submits nor retires");
        Check(s_quarantined_candidate.feat==&currentOpaque&&s_feat==&currentOpaque,"incremental fault retains borrowed and live bank");
        const int before=resets;Edits();for(int i=0;i<300;++i)PumpBuild();Forward(&code);TestInvoke(Callback,&frame);
        Check(resets==before&&polls==0&&calls==1&&!TestClaim(k033core::Host033),"layer edits cannot retry poll reset or change NR route after SEH");carrier::cfg=config;break;}
    case 26:{using namespace pumpfixture;carrier::cfg.passes=3;Reuse();carrier::cfg.extra[0].style=1;carrier::cfg.extra[1].style=2;throwAt=2;PumpBuild();
        Check(calls==2&&s_quarantined_candidate.extra_feat[0]==&opaque,"later layer SEH retains already created candidate model");
        Check(s_feat==&currentOpaque&&retired==0&&submits==0&&closes==0,"later layer SEH retains live bank and unknown initialization list");carrier::cfg=config;break;}
    case 27:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=1;PumpBuild();fence.completed=UINT64_MAX;PumpBuild();
        Check(s_candidate_valid&&s_feat==&currentOpaque&&retired==0,"unknown initialization completion cannot publish or retire");
        const int queries=polls;auto* candidate=s_candidate.extra_feat[0];nrfault033::Record(Fault,nrfault033::Site::Callback);Edits();for(int i=0;i<300;++i)PumpBuild();
        Check(polls==queries&&s_candidate.extra_feat[0]==candidate&&retired==0,"SEH freezes already submitted candidate despite edits");carrier::cfg=config;break;}
    case 28:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=1;failAt=1;PumpBuild();
        Check(!nrfault033::Blocked()&&s_retry_cool==120&&s_feat==&currentOpaque,"ordinary incremental failure retains active bank and cooldown");NoBorrowRetired();
        for(int i=0;i<120;++i)PumpBuild();Check(calls==2&&submits==2&&s_candidate_valid,"ordinary incremental failure retries without another edit");NoBorrowRetired();carrier::cfg=config;break;}
    case 29:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=1;PumpBuild();carrier::cfg.extra[0].style=2;fence.completed=s_bv;throwAt=1;faultOnRetire=true;PumpBuild();
        Check(nrfault033::Blocked()&&releases==1&&s_candidate_valid,"superseded candidate retirement SEH retains ownership snapshot");NoBorrowRetired();
        const int queries=polls,resetCount=resets;Edits();for(int i=0;i<300;++i)PumpBuild();
        Check(polls==queries&&resets==resetCount&&calls==1&&releases==1,"retirement SEH cannot be bypassed by style edits");carrier::cfg=config;break;}
    case 30:{g_core_probe_ok=true;coreThrow=true;Forward(&code);auto& entry=nrfwd::s_core_models.entries[0];
        Check(entry.params==&paramPool[0]&&!entry.feature&&nrfwd::s_core_models.Any(),"faulted core creation retains unknown parameter ownership");
        Check(nrfwd::s_core_models.Free()!=&entry&&destroys==0,"parameter-only fault entry cannot be reused or destroyed");
        pumpfixture::Edits();Forward(&code);Check(coreCalls==1&&calls==0&&allocs==1,"edited settings cannot select another core or forward route");carrier::cfg=config;break;}
    case 31:{g_core_probe_ok=true;void* a=Forward(&code);void* b=Forward(&code);auto* pa=nrfwd::s_core_models.Find(a)->params;auto* pb=nrfwd::s_core_models.Find(b)->params;
        Check(a!=b&&pa!=pb,"core models own distinct parameter blocks");Evaluate(&code);Check(lastEvalParams==pa,"first core evaluation selects its own parameters");
        nrfwd::evaluate(&hostfixture::list,b,nullptr,nullptr,nullptr,nullptr,64,64,64,64,0,0,1,0,1,1,1,0,1,1,&code);
        Check(lastEvalParams==pb,"second core evaluation selects its own parameters");nrfwd::release_inner(b,false);
        Check(!b&&nrfwd::s_core_models.Find(a)->params==pa&&destroys==1,"successful core retirement leaves other model parameters intact");break;}
    case 32:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=1;PumpBuild();++s_bank_revision;fence.completed=s_bv;PumpBuild();
        Check(s_feat==&currentOpaque&&!s_candidate_valid&&s_want_build,"borrow revision change rejects candidate publication");
        Check(retiredFeatures.size()==1&&retiredFeatures[0]==&opaque,"superseded owned model retires exactly once");NoBorrowRetired();carrier::cfg=config;break;}
    case 33:{g_core_probe_ok=true;void* a=Forward(&code);failAt=1;nrfwd::release_inner(a,false);
        Check(a==&opaque&&destroys==0&&nrfwd::s_core_models.Find(a),"ordinary core release failure preserves handle and parameter record");
        nrfwd::release_inner(a,false);Check(!a&&destroys==1&&!nrfault033::Blocked(),"ordinary core release retries and destroys only after success");break;}
    case 34:{g_core_probe_ok=true;void* a=Forward(&code);throwAt=1;nrfwd::release_inner(a,false);nrfwd::release_inner(a,false);
        Check(a==&opaque&&releases==1&&destroys==0&&nrfwd::s_core_models.Find(a)->params==&paramPool[0],"core release SEH retains model and parameters without retry");break;}
#endif

    case 35:{using namespace pumpfixture;scale::failCreates=1;s_blit.ready=false;
        Check(!EnsureBlitter(&device)&&!s_failed,"ordinary pipeline error must not disable session");
        Check(scale::creates==1&&scale::destroys==1,"unsubmitted partial pipeline cleaned");
        TestNowValue+=249;Check(!EnsureBlitter(&device)&&scale::creates==1,"pipeline retry is throttled");
        ++TestNowValue;Check(EnsureBlitter(&device)&&s_blit.ready&&!s_failed,"pipeline recovers without game restart");break;}
    case 36:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=1;signalFailures=1;fence.completed=0;PumpBuild();
        Check(s_candidate_valid&&executes==1&&signals==1,"failed signal retains submitted candidate");
        auto* candidate=s_candidate.extra_feat[0];const int before=resets;
        for(int i=0;i<20;++i)PumpBuild();Check(signals==1&&executes==1&&resets==before,"pending signal does not storm replay or reset");
        TestNowValue+=250;PumpBuild();Check(signals==2&&executes==1,"same initialization is confirmed without reexecution");
        Check(s_feat==&currentOpaque&&s_candidate_valid,"signal success alone is not GPU completion");
        fence.completed=s_bv;PumpBuild();Check(live.extra_feat[0]==candidate&&!s_candidate_valid,"candidate adopted after real completion");
        Check(signalValues.size()==2&&signalValues[0]==signalValues[1],"confirmation retry keeps exact target value");carrier::cfg=config;break;}
    case 37:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=1;signalFailures=1;fence.completed=0;PumpBuild();
        fence.completed=UINT64_MAX;TestNowValue+=250;const int before=resets;PumpBuild();
        Check(s_candidate_valid&&signals==1&&executes==1&&resets==before&&s_feat==&currentOpaque,"device removal is neither healed nor resubmitted");carrier::cfg=config;break;}
    case 38:{using namespace pumpfixture;Reuse();fence.completed=0;
        for(int i=0;i<3;++i)s_evaluate_recovery.Failed(1,TestNowValue);
        PumpBuild();Check(calls==1&&s_candidate.extra_feat[0]!=&liveExtra[0]&&s_candidate.feat==&currentOpaque,"failed unchanged layer gets fresh feature only");
        Check(s_evaluate_recovery.Pending()&&retiredFeatures.empty(),"pending candidate is not recovery and old resources remain");
        auto* candidate=s_candidate.extra_feat[0];fence.completed=s_bv;PumpBuild();
        Check(!s_evaluate_recovery.Pending()&&live.extra_feat[0]==candidate,"only actual replacement clears recovery request");
        Check(retiredFeatures.size()==1&&retiredFeatures[0]==&liveExtra[0],"only replaced model enters existing lease retirement");carrier::cfg=config;break;}
    case 39:{using namespace pumpfixture;Reuse();carrier::cfg.extra[0].style=1;fence.completed=0;PumpBuild();
        for(int i=0;i<3;++i)s_evaluate_recovery.Failed(0,TestNowValue);
        fence.completed=s_bv;PumpBuild();Check(s_evaluate_recovery.Pending(),"unrelated in-flight edit cannot clear failed borrowed feature");
        s_want_build=true;PumpBuild();Check(s_candidate.feat!=&currentOpaque,"next candidate replaces newly failed borrowed feature");carrier::cfg=config;break;}
    case 40:{using namespace pumpfixture;Reuse();s_evaluate_recovery.mask=2;failAt=1;PumpBuild();
        Check(!nrfault033::Blocked()&&s_feat==&currentOpaque&&live.extra_feat[0]==&liveExtra[0]&&s_evaluate_recovery.Pending(),"failed recovery build preserves active model and request");
        NoBorrowRetired();carrier::cfg=config;break;}

    default:return 2;
    }
    Check(carrier::cfg.intensity==config.intensity&&carrier::cfg.style==config.style,"image settings unchanged");
    Check(FaultStateAddress()==&nrfault033::first,"core and renderer share one latch");
    if(scenario==1||scenario==5||scenario==6||scenario==9||scenario==12)Check(!nrfault033::Blocked(),"normal error never latches SEH");
    if(scenario==3||scenario==8||scenario==11)Check(faultLogs==0,"retained baseline guarded diagnostic count");
    if(scenario==1||scenario==5||scenario==6||scenario==9||scenario==12)Check(faultLogs==0,"ordinary errors are not logged as SEH");
    printf("scenario=%d checks=%d failures=%d\n",scenario,checks,failed);return failed?1:0;
}
