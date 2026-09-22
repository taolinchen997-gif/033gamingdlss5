// Only the independent hidden test process loads this UI validation adapter.
// Execute the shipping host dispatcher in a real ReShade ImGui frame.
#include <Windows.h>
#include <cstdio>
#include <cstring>
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#define K033_EMBEDDED_UI_NO_DRAW
#include "embedded_ui.h"
extern "C" __declspec(dllexport) const char* NAME="033 independent UI validation";
static unsigned frames=0,calls=0,headers=0,failures=0;
static void Init(reshade::api::effect_runtime* runtime){runtime->open_overlay(true,reshade::api::input_source::keyboard);}
static int __cdecl Dispatch(ui033abi::Call* call){
    ++calls;
    if(call->op==ui033abi::CollapsingHeader||call->op==ui033abi::TreeNode){
        ++headers;ImGui::SetNextItemOpen(true,ImGuiCond_Always);
    }
    return embeddedui::Invoke(call);
}
static void Draw(reshade::api::effect_runtime*){
    auto engine=GetModuleHandleW(L"winmm.dll");if(!engine)return;
    auto render=reinterpret_cast<ui033abi::Render>(GetProcAddress(engine,"K033_RenderEmbeddedControls"));
    if(!render)return;
    ImGui::SetNextWindowSize(ImVec2(960,720),ImGuiCond_Always);
    ImGui::Begin("Independent 033 UI test");
    if(ImGui::GetID("###Motion Adaptive Sharpness##2")!=ImGui::GetID(embeddedui::Label("Motion Adaptive Sharpness##2")))++failures;
    if(ImGui::GetID(embeddedui::Label("Motion Adaptive Sharpness##2"))==ImGui::GetID(embeddedui::Label("Motion Adaptive Sharpness##3")))++failures;
    if(std::strstr(embeddedui::Label("Sharpness",false),"###"))++failures;
    static const ui033abi::Api api{sizeof(api),ui033abi::Version,Dispatch};
    if(render(&api)==1)++frames;
    ImGui::End();
}
extern "C" __declspec(dllexport) unsigned K033_TestUiFrames(){return failures?0:frames;}
extern "C" __declspec(dllexport) unsigned K033_TestUiCalls(){return calls;}
extern "C" __declspec(dllexport) unsigned K033_TestUiHeaders(){return headers;}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        if(!reshade::register_addon(module))return FALSE;
        reshade::register_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::register_event<reshade::addon_event::reshade_overlay>(Draw);
    }else if(reason==DLL_PROCESS_DETACH){
        reshade::unregister_event<reshade::addon_event::reshade_overlay>(Draw);
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::unregister_addon(module);
    }
    return TRUE;
}
