#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include "ngx/nvsdk_ngx_params.h"
#include "render_core_abi.h"
#include "nr_controls_abi.h"
#include <atomic>
static std::atomic<unsigned> calls{0},bad{0};
extern "C" __declspec(dllexport) int __cdecl K033_AfterUpscale(const k033core::Frame* f){
    if(!k033core::Valid(f)){++bad;return 0;}
    auto p=static_cast<NVSDK_NGX_Parameter*>(f->parameters);void* color=nullptr,*depth=nullptr,*mv=nullptr;
    p->Get(NVSDK_NGX_Parameter_Output,&color);p->Get(NVSDK_NGX_Parameter_Depth,&depth);p->Get(NVSDK_NGX_Parameter_MotionVectors,&mv);
    if(!color||!depth||!mv||!f->haveFlags||f->outputW!=128||f->outputH!=96){++bad;return 0;}
    ++calls;
#ifdef K033_REAL_ADDON_SMOKE
    auto real=reinterpret_cast<k033core::AfterUpscale>(GetProcAddress(GetModuleHandleW(nullptr),"K033_AfterUpscale"));
    return real?real(f):0;
#else
    return 1;
#endif
}
extern "C" __declspec(dllexport) unsigned __cdecl K033_FixtureCalls(){return calls.load();}
extern "C" __declspec(dllexport) unsigned __cdecl K033_FixtureBad(){return bad.load();}
extern "C" __declspec(dllexport) const nrcontrolsabi::Api* __cdecl K033_GetNrControls(uint32_t version){
#ifdef K033_REAL_ADDON_SMOKE
    auto get=reinterpret_cast<nrcontrolsabi::GetApi>(GetProcAddress(GetModuleHandleW(nullptr),"K033_GetNrControls"));
    return get?get(version):nullptr;
#else
    return nullptr;
#endif
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
