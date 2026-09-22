// Exercise the production NGX detours against local mock entry points. No game,
// driver NGX library, desktop input or graphics device is opened by this test.
#include <Windows.h>
#include <d3d12.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include "ngx/nvsdk_ngx_params.h"
using PFN_CreateFeature=NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,const NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using PFN_EvaluateFeature=NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*);
struct NgxStub { HMODULE mod=nullptr;PFN_CreateFeature create=nullptr;PFN_EvaluateFeature evaluate=nullptr;std::string core_dir; } g_ngx;
static void load_ngx_once() {}
static std::string game_dir() { return "."; }
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*,uint32_t){return true;}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*){}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*,int,const char*){}
#include "../src/nrscale.h"
struct Params : NVSDK_NGX_Parameter {
    int writes=0;bool output=true;
    #define PARAM_TYPE(T) \
    void Set(const char*,T) override {++writes;} \
    NVSDK_NGX_Result Get(const char*,T*) const override {return NVSDK_NGX_Result_Fail;}
    PARAM_TYPE(unsigned long long)
    PARAM_TYPE(float)
    PARAM_TYPE(double)
    PARAM_TYPE(int)
    PARAM_TYPE(ID3D11Resource*)
    PARAM_TYPE(ID3D12Resource*)
    #undef PARAM_TYPE
    void Set(const char*,unsigned) override {++writes;}
    void Set(const char*,void*) override {++writes;}
    NVSDK_NGX_Result Get(const char* key,unsigned* value) const override {
        if(std::strcmp(key,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags)==0)*value=107;
        else if(std::strcmp(key,NVSDK_NGX_Parameter_OutWidth)==0)*value=5120;
        else if(std::strcmp(key,NVSDK_NGX_Parameter_OutHeight)==0)*value=2160;
        else return NVSDK_NGX_Result_Fail;
        return NVSDK_NGX_Result_Success;
    }
    NVSDK_NGX_Result Get(const char* key,void** value) const override {
        if(std::strcmp(key,NVSDK_NGX_Parameter_Output)!=0)return NVSDK_NGX_Result_Fail;
        *value=output?reinterpret_cast<void*>(0x1000):nullptr;return NVSDK_NGX_Result_Success;
    }
    void Reset() override {++writes;}
};
static char handle_storage[32];
static NVSDK_NGX_Handle* Handle(int n){return reinterpret_cast<NVSDK_NGX_Handle*>(&handle_storage[n]);}
static std::atomic<int> creates{0},evaluates{0},releases{0},afters{0};
static bool fail_create=false,fail_eval=false,fail_release=false;
static int override_slot=-1;
__declspec(noinline) NVSDK_NGX_Result __cdecl MockCoreCreate(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature f,const NVSDK_NGX_Parameter*,NVSDK_NGX_Handle** out){
    ++creates;if(fail_create)return NVSDK_NGX_Result_Fail;
    *out=Handle(override_slot>=0?override_slot:int(f));return NVSDK_NGX_Result_Success;
}
__declspec(noinline) NVSDK_NGX_Result __cdecl MockCoreEval(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*){
    ++evaluates;return fail_eval?NVSDK_NGX_Result_Fail:NVSDK_NGX_Result_Success;
}
__declspec(noinline) NVSDK_NGX_Result __cdecl MockCoreRelease(NVSDK_NGX_Handle*){
    ++releases;return fail_release?NVSDK_NGX_Result_Fail:NVSDK_NGX_Result_Success;
}
// Volatile pointers keep calls going through the actual patched entry addresses.
static PFN_CreateFeature volatile create_entry=&MockCoreCreate;
static PFN_EvaluateFeature volatile eval_entry=&MockCoreEval;
static nrscale::PFN_ReleaseFeature18 volatile release_entry=&MockCoreRelease;
__declspec(noinline) NVSDK_NGX_Result __cdecl MockShellCreate(ID3D12GraphicsCommandList* c,NVSDK_NGX_Feature f,const NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h){
    return create_entry(c,f,p,h);
}
__declspec(noinline) NVSDK_NGX_Result __cdecl MockShellEval(ID3D12GraphicsCommandList* c,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,void* cb){return eval_entry(c,h,p,cb);}
static int __cdecl After(ID3D12GraphicsCommandList* c,const NVSDK_NGX_Parameter* p,const NVSDK_NGX_Handle*){
    ++afters;
    // NR itself must not recursively inject even if it reuses the SR parameter block.
    eval_entry(c,Handle(18),p,nullptr);return 1;
}
int main(){
    int checks=0,failures=0;
    auto check=[&](bool ok,const char* n){++checks;if(!ok){++failures;std::printf("FAIL %s\n",n);}};
    nrscale::o_create=MockShellCreate;nrscale::o_create_core=MockCoreCreate;
    nrscale::o_eval=MockShellEval;nrscale::o_eval_core=MockCoreEval;
    nrscale::o_release_core=MockCoreRelease;nrscale::g_after_eval=After;
    const auto attached=nrscale::attach_resolved_hooks();
    check(attached==NO_ERROR,"production transaction hooks both primary and direct core entry points");
    if(attached!=NO_ERROR)return 2;
    nrscale::installed=true;
    Params p;NVSDK_NGX_Handle *sr=nullptr,*fg=nullptr,*rr=nullptr;
    create_entry(nullptr,static_cast<NVSDK_NGX_Feature>(1),&p,&sr);
    create_entry(nullptr,static_cast<NVSDK_NGX_Feature>(11),&p,&fg);
    check(nrscale::feature_of(sr)==1&&nrscale::feature_of(fg)==11,"direct core feature creation is recorded with thin wrapper present");
    const auto info=nrscale::feature_info(sr);
    check(info.haveFlags&&info.flags==107&&info.outputW==5120&&info.outputH==2160,"creation contract retained for SR");
    PFN_CreateFeature volatile shell_create=MockShellCreate;
    shell_create(nullptr,static_cast<NVSDK_NGX_Feature>(12),&p,&rr);
    check(creates==3&&nrscale::feature_of(rr)==12,"nested wrapper/core creation calls runtime once");
    PFN_EvaluateFeature volatile shell_eval=MockShellEval;
    shell_eval(nullptr,sr,&p,nullptr);
    check(afters==1&&evaluates==2&&nrscale::game_eval==1,"wrapper/core SR injects once and own NR does not recurse");
    // Reproduce sixfold scheduling with an SR Output still in the parameter block.
    for(int i=0;i<5;++i)eval_entry(nullptr,fg,&p,nullptr);
    check(afters==1&&evaluates==7&&nrscale::game_eval==1,"five generated calls with stale Output do no additional NR");
    check(nrdispatch::non_image_calls==5&&nrdispatch::framegen_calls==5&&!nrdispatch::consume_gap(),"generated calls are counted separately and do not invalidate SR history");
    eval_entry(nullptr,Handle(29),&p,nullptr);
    check(afters==1&&nrdispatch::unknown_calls==1,"unknown handle with stale Output is withheld");
    eval_entry(nullptr,rr,&p,nullptr);
    check(afters==2,"registered ray reconstruction still runs NR");
    fail_eval=true;eval_entry(nullptr,sr,&p,nullptr);fail_eval=false;
    check(afters==2,"failed image evaluation does not run NR");
    {nrdispatch::AfterScope occupied;std::thread worker([&]{eval_entry(nullptr,fg,&p,nullptr);});worker.join();}
    check(!nrdispatch::consume_gap()&&nrdispatch::skipped==0,"FG on another thread never competes for the NR writer");
    fail_release=true;release_entry(sr);fail_release=false;
    check(nrscale::feature_of(sr)==1,"failed release retains feature identity");
    release_entry(sr);check(nrscale::feature_of(sr)==-1,"successful release removes feature identity");
    override_slot=1;create_entry(nullptr,static_cast<NVSDK_NGX_Feature>(11),&p,&sr);override_slot=-1;
    eval_entry(nullptr,sr,&p,nullptr);
    check(nrscale::feature_of(sr)==11&&afters==2,"recycled SR pointer created as FG never retains SR identity");
    fail_create=true;NVSDK_NGX_Handle* failed=Handle(30);
    create_entry(nullptr,static_cast<NVSDK_NGX_Feature>(1),&p,&failed);fail_create=false;
    check(nrscale::feature_of(failed)==-1,"failed creation cannot authorize stale output handle");
    check(p.writes==0,"routing does not mutate shared game parameters");
    nrscale::uninstall();check(!nrscale::installed,"production uninstall detaches the additional core creation hook");
    const int old_creates=nrscale::create_total.load();
    create_entry(nullptr,static_cast<NVSDK_NGX_Feature>(1),&p,&sr);
    check(nrscale::create_total==old_creates,"core entry restored after uninstall");
    std::printf("NR feature routing: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
