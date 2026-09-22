#pragma once
// Compile the actual forwarder into this CPU test. Its Init/Create/Evaluate
// pointers are replaced below; no DLL is loaded and no D3D device is created.
#include "../src/nr033fwd.cpp"
#include <unordered_map>
#include <string>
#include <type_traits>

namespace nrcreatetest033 {
struct Params final:NVSDK_NGX_Parameter {
    std::unordered_map<std::string,unsigned long long> integers;
    std::unordered_map<std::string,double> floats;
    unsigned resets=0;
    void Set(const char* n,unsigned long long v)override{integers[n]=v;}
    void Set(const char* n,float v)override{floats[n]=v;}
    void Set(const char* n,double v)override{floats[n]=v;}
    void Set(const char* n,unsigned int v)override{integers[n]=v;}
    void Set(const char* n,int v)override{integers[n]=v;}
    void Set(const char* n,ID3D11Resource* v)override{integers[n]=reinterpret_cast<uintptr_t>(v);}
    void Set(const char* n,ID3D12Resource* v)override{integers[n]=reinterpret_cast<uintptr_t>(v);}
    void Set(const char* n,void* v)override{integers[n]=reinterpret_cast<uintptr_t>(v);}
    template<class T> NVSDK_NGX_Result Read(const char* n,T* out) const {
        auto it=integers.find(n);if(it==integers.end())return NVSDK_NGX_Result_FAIL_UnsupportedParameter;
        if constexpr(std::is_pointer_v<T>)*out=reinterpret_cast<T>(uintptr_t(it->second));
        else *out=static_cast<T>(it->second);
        return NVSDK_NGX_Result_Success;
    }
    NVSDK_NGX_Result Get(const char* n,unsigned long long* o)const override{return Read(n,o);}
    NVSDK_NGX_Result Get(const char* n,unsigned int* o)const override{return Read(n,o);}
    NVSDK_NGX_Result Get(const char* n,int* o)const override{return Read(n,o);}
    NVSDK_NGX_Result Get(const char* n,ID3D11Resource** o)const override{return Read(n,o);}
    NVSDK_NGX_Result Get(const char* n,ID3D12Resource** o)const override{return Read(n,o);}
    NVSDK_NGX_Result Get(const char* n,void** o)const override{return Read(n,o);}
    NVSDK_NGX_Result Get(const char* n,float* o)const override{auto i=floats.find(n);if(i==floats.end())return NVSDK_NGX_Result_FAIL_UnsupportedParameter;*o=float(i->second);return NVSDK_NGX_Result_Success;}
    NVSDK_NGX_Result Get(const char* n,double* o)const override{auto i=floats.find(n);if(i==floats.end())return NVSDK_NGX_Result_FAIL_UnsupportedParameter;*o=i->second;return NVSDK_NGX_Result_Success;}
    void Reset()override{++resets;integers.clear();floats.clear();}
};
static Params* observed=nullptr;
static int opaqueHandle=91;
static auto* handle=reinterpret_cast<NVSDK_NGX_Handle*>(&opaqueHandle);
static int creates=0,evaluates=0;
static NVSDK_NGX_Result NVSDK_CONV Init(unsigned long long,const wchar_t*,ID3D12Device*,unsigned,NVSDK_NGX_Parameter*) {return NVSDK_NGX_Result_Success;}
static NVSDK_NGX_Result NVSDK_CONV Create(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter* params,NVSDK_NGX_Handle** output) {
    observed=static_cast<Params*>(params);++creates;*output=handle;return NVSDK_NGX_Result_Success;
}
static NVSDK_NGX_Result NVSDK_CONV Evaluate(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback) {++evaluates;return NVSDK_NGX_Result_Success;}
}
static void nrCreateChecks() {
    using namespace nrcreatetest033;
    g_init=Init;g_create=Create;g_eval=Evaluate;g_inited=true;g_floatSlot=1;
    Params params;params.integers["Capability.Callback.Sentinel"]=0x123456;
    auto* device=reinterpret_cast<ID3D12Device*>(uintptr_t(1));
    auto* commands=reinterpret_cast<ID3D12GraphicsCommandList*>(uintptr_t(2));
    auto resource=[](uintptr_t n){return reinterpret_cast<ID3D12Resource*>(n);};
    auto seedFrame=[&](unsigned w,unsigned h){
        return nr033_evaluate_v2(commands,handle,&params,resource(11),resource(12),resource(13),resource(14),
            w,h,w,h,1,0,2.f,2,2.f,2.f,2.f,1,1.f,1.f,nullptr,1.f);
    };
    for(auto dims : {std::pair{800u,600u},std::pair{5120u,2160u},std::pair{800u,600u}}) {
        check(seedFrame(dims.first,dims.second)==NVSDK_NGX_Result_Success,"actual evaluation seeds persistent input bindings");
        check(params.integers["DLSSNR.Color"]==11 && params.integers["DLSSNR.MVec"]==13,"previous frame really remains in parameter block");
        for(auto key:{"DLSSNR.UI","DLSSNR.UIAlpha","DLSSNR.Backbuffer"})params.integers[key]=99;
        auto* result=nr033_create_v2(nullptr,nullptr,device,commands,&params,5120,2160,0,2.f,2,2.f,2.f,2.f,1,1,1.f);
        check(result==handle && observed==&params,"actual creation reaches mocked runtime using owned capability block");
        for(auto key:{"DLSSNR.Color","DLSSNR.Depth","DLSSNR.MVec","DLSSNR.Output","DLSSNR.UI","DLSSNR.UIAlpha","DLSSNR.Backbuffer"})
            check(params.integers[key]==0,"new feature must not retain prior evaluation resource pointers");
        check(params.integers["DLSSNR.Width"]==5120 && params.integers["DLSSNR.Height"]==2160,"new model size reaches runtime");
        check(params.integers["DLSSNR.ColorSubrectWidth"]==5120 && params.integers["DLSSNR.ColorSubrectHeight"]==2160,"new color region cannot inherit startup dimensions");
        check(params.integers["DLSSNR.OutputSubrectWidth"]==5120 && params.integers["DLSSNR.OutputSubrectHeight"]==2160,"new output region matches model dimensions");
        check(params.integers["DLSSNR.DepthSubrectWidth"]==0 && params.integers["DLSSNR.MVecSubrectHeight"]==0,"unbound guides carry no stale subrect");
        check(params.integers["Capability.Callback.Sentinel"]==0x123456 && params.resets==0,"creation preserves capability callbacks without Reset");
    }
    check(creates==3 && evaluates==3,"all boundary calls are CPU mocks only");
    check(seedFrame(5120,2160)==NVSDK_NGX_Result_Success && params.integers["DLSSNR.Color"]==11 && params.integers["DLSSNR.MVecSubrectHeight"]==2160,
          "evaluation repopulates current resources and guides after clean creation");
    g_init=nullptr;g_create=nullptr;g_eval=nullptr;g_inited=false;g_floatSlot=-1;
}
