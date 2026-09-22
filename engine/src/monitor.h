#pragma once
#include "monitor_abi.h"
#include "yanyun_appearance_store.h"
#include "panel_yanyun_monitor.h"
namespace monitor033 {
// Adapted from the retained 033 common monitor: same adapter-bound NVAPI/DXGI
// CPU telemetry, click-through overlay and sample-rate policy. No second UI host.
struct View {bool enabled=false,loaded=false;uint64_t next=0,frames=0;uintptr_t owner=0;Rate rate;PresentationRate display;GpuSample gpu;nrcontrolsabi::Snapshot controls;bool controlsValid=false;};
inline View view;
inline void Destroy(reshade::api::effect_runtime* runtime){if(view.owner==reinterpret_cast<uintptr_t>(runtime))view={};}
inline void Draw(reshade::api::effect_runtime* runtime){
 if(!runtime)return;const auto id=reinterpret_cast<uintptr_t>(runtime);if(view.owner&&view.owner!=id)return;view.owner=id;
 if(!view.loaded){std::ifstream f(game_dir()+"\\033-monitor.cfg");std::string line;while(std::getline(f,line))if(line=="enabled=1"||line=="enabled=1\r")view.enabled=true;view.loaded=true;}
 if(runtime->is_key_down(VK_SHIFT)&&runtime->is_key_pressed(VK_F9)){
  view.enabled=!view.enabled;const char* keys[]={"enabled"};configstore::Update(game_dir()+"\\033-monitor.cfg",keys,1,[&](FILE* f){std::fprintf(f,"enabled=%u\n",unsigned(view.enabled));});}
 const auto now=GetTickCount64();view.rate.Sample(now,++view.frames);if(!view.enabled)return;
 yyappearance::Refresh();
 if(now>=view.next){uint64_t luid=0;view.gpu={};if(runtime->get_device()->get_property(reshade::api::device_properties::adapter_luid,&luid))view.gpu=K033_MonitorGpu(luid);
  // Query DXGI's completed presentation statistics, not overlay callbacks or requested FG multipliers.
  // No DwmFlush, driver timing enablement, extra Present, or synchronization wait.
  DXGI_FRAME_STATISTICS stats{};LARGE_INTEGER frequency{};bool presentValid=false;
  if(runtime->get_device()->get_api()==reshade::api::device_api::d3d12&&runtime->get_native()){
   auto* chain=reinterpret_cast<IDXGISwapChain3*>(runtime->get_native());presentValid=SUCCEEDED(chain->GetFrameStatistics(&stats))&&QueryPerformanceFrequency(&frequency);}
  view.display.Sample(presentValid,stats.PresentCount,uint64_t(stats.SyncQPCTime.QuadPart),uint64_t(frequency.QuadPart));
  nrcontrolsabi::Snapshot controls;view.controlsValid=nrcontrols::Read(&controls)!=0;if(view.controlsValid)view.controls=controls;view.next=now+1000;}
 studio033::MonitorState s;s.gpu=view.gpu.name[0]?view.gpu.name:"等待显卡信息";s.status=yanyundual::note.load();
 s.fpsValid=view.display.valid;s.fps=view.display.fps;s.loadValid=view.gpu.loadValid;s.load=view.gpu.load;s.memoryValid=view.gpu.boardMemoryValid;s.temperatureValid=view.gpu.temperatureValid;s.temperature=view.gpu.temperature;
 s.memoryGiB=double(view.gpu.boardUsed)/(1024*1024*1024);s.budgetGiB=double(view.gpu.boardTotal)/(1024*1024*1024);
 const auto timing=gputime::snapshot();s.nrValid=timing.fresh();s.nrMs=timing.total;
 if(view.controlsValid){s.fg=view.controls.mfgAccepted;s.layers=view.controls.activeLayers;}
 s.regionalFrames=yanyundual::recorded.load();s.recognitions=yanyundual::recognitions.load();
 // Do not start the semantic worker just to display the HUD.
 if(s.recognitions){const auto result=yanyundual::Worker().Snapshot();if(result&&now>=result->source.capturedMs){s.recognitionValid=true;s.recognitionMs=result->inferenceMs;s.maskAge=now-result->source.capturedMs;}}
 studio033::Theme theme;const float u=studio033::visual::unit();ImGui::SetNextWindowPos(ImVec2(16*u,16*u),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(306*u,0));ImGui::SetNextWindowBgAlpha(.94f);
 if(ImGui::Begin("##033_YY_monitor",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_AlwaysAutoResize))studio033::MonitorBody(s);
 ImGui::End();
}
}
