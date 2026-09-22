// CPU/WARP regression substitute for NGX. No NVIDIA model/driver is loaded.
// Real Feeder/ReShade code still creates, copies, flushes and retires textures.
#include <Windows.h>
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <map>
#include <string>
#include <variant>
#include <type_traits>
struct NVSDK_NGX_Handle {unsigned Id;};
struct Params:NVSDK_NGX_Parameter {
 using Value=std::variant<unsigned long long,float,double,unsigned,int,ID3D11Resource*,ID3D12Resource*,void*>;
 std::map<std::string,Value> values;
 template<class T> NVSDK_NGX_Result Read(const char* k,T* out)const{auto it=values.find(k);if(it==values.end())return static_cast<NVSDK_NGX_Result>(0xBAD00001);
  bool valid=false;std::visit([&](auto v){using V=decltype(v);if constexpr(std::is_convertible_v<V,T>){*out=static_cast<T>(v);valid=true;}},it->second);return valid?NVSDK_NGX_Result_Success:static_cast<NVSDK_NGX_Result>(0xBAD00001);}
#define VALUE(T) void Set(const char*k,T v)override{values[k]=v;} NVSDK_NGX_Result Get(const char*k,T*out)const override{return Read(k,out);}
 VALUE(unsigned long long) VALUE(float) VALUE(double) VALUE(unsigned) VALUE(int) VALUE(ID3D11Resource*) VALUE(ID3D12Resource*) VALUE(void*)
#undef VALUE
 void Reset()override{values.clear();}
};
static unsigned evaluations=0,initializations=0;
#define PARAM(T,S) void NVSDK_CONV NVSDK_NGX_Parameter_Set##S(NVSDK_NGX_Parameter*p,const char*k,T v){p->Set(k,v);} NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_Get##S(NVSDK_NGX_Parameter*p,const char*k,T*out){return p->Get(k,out);}
PARAM(unsigned long long,ULL) PARAM(float,F) PARAM(double,D) PARAM(unsigned,UI) PARAM(int,I) PARAM(ID3D11Resource*,D3d11Resource) PARAM(ID3D12Resource*,D3d12Resource) PARAM(void*,VoidPointer)
#undef PARAM
extern "C" __declspec(dllexport) unsigned K033_FeedMockEvaluations(){return evaluations;}
extern "C" __declspec(dllexport) unsigned K033_FeedMockInitializations(){return initializations;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init(unsigned long long,const wchar_t*,ID3D12Device*,const NVSDK_NGX_FeatureCommonInfo*,NVSDK_NGX_Version){++initializations;return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_with_ProjectID(const char*,NVSDK_NGX_EngineType,const char*,const wchar_t*,ID3D12Device*,const NVSDK_NGX_FeatureCommonInfo*,NVSDK_NGX_Version){++initializations;return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter**p){*p=new Params;return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter**p){static Params caps;caps.Set(NVSDK_NGX_Parameter_SuperSampling_Available,1);*p=&caps;return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter*p){delete static_cast<Params*>(p);return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Shutdown1(ID3D12Device*){return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**h){*h=new NVSDK_NGX_Handle{1};return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle*h){delete h;return NVSDK_NGX_Result_Success;}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature_C(ID3D12GraphicsCommandList*cl,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*p,PFN_NVSDK_NGX_ProgressCallback_C){
 ID3D12Resource *src=nullptr,*dst=nullptr;p->Get(NVSDK_NGX_Parameter_Color,&src);p->Get(NVSDK_NGX_Parameter_Output,&dst);if(!src||!dst)return static_cast<NVSDK_NGX_Result>(0xBAD00001);
 D3D12_RESOURCE_BARRIER b[2]{};for(auto&x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
 b[0].Transition.pResource=src;b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
 b[1].Transition.pResource=dst;b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
 cl->ResourceBarrier(2,b);cl->CopyResource(dst,src);for(auto&x:b)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);cl->ResourceBarrier(2,b);++evaluations;return NVSDK_NGX_Result_Success;
}
