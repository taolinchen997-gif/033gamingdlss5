#pragma once
namespace studio033 {
struct MonitorState {
 const char* gpu="等待显卡信息";const char* status="等待人物分区";
 bool temperatureValid=false;int temperature=0;
 bool fpsValid=false,loadValid=false,memoryValid=false,nrValid=false,recognitionValid=false;
 double fps=0,nrMs=0,recognitionMs=0;unsigned load=0,fg=0,layers=0;double memoryGiB=0,budgetGiB=0;
 uint64_t regionalFrames=0,recognitions=0,maskAge=0;
};
inline void MonitorBody(const MonitorState& s){
 const float u=visual::unit(),w=ImGui::GetContentRegionAvail().x;const auto p=ImGui::GetCursorScreenPos();
 // Original 033 polygon mark, scaled to a small HUD heading.
 visual::brand_mark(100*u);auto* dl=ImGui::GetWindowDrawList();dl->AddLine(ImVec2(p.x+112*u,p.y+18*u),ImVec2(p.x+w,p.y+18*u),ImGui::GetColorU32(visual::accent()),2*u);
 auto row=[&](const char* title,const char* left,const char* right){
  ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(title);
  ImGui::TableSetColumnIndex(1);ImGui::TextColored(visual::accent(),"%s",left);
  ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted(right);
 };
 if(ImGui::BeginTable("yy_monitor",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersInnerH)){
  ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthFixed,48*u);ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch,1.4f);ImGui::TableSetupColumn("detail",ImGuiTableColumnFlags_WidthStretch,1.f);
  char left[64],right[64];
  if(s.fpsValid)std::snprintf(left,sizeof(left),"%.0f FPS",s.fps);else std::snprintf(left,sizeof(left),"—");
  if(s.fpsValid&&s.fps>0)std::snprintf(right,sizeof(right),"%.1f ms",1000.0/s.fps);else std::snprintf(right,sizeof(right),"—");row(yyappearance::Text("帧率","FPS"),left,right);
  if(s.loadValid)std::snprintf(left,sizeof(left),"%u %%",s.load);else std::snprintf(left,sizeof(left),"—");
  if(s.temperatureValid)std::snprintf(right,sizeof(right),"%d °C",s.temperature);else std::snprintf(right,sizeof(right),"—");row("GPU",left,right);
  if(s.memoryValid)std::snprintf(left,sizeof(left),"%.1f / %.1f",s.memoryGiB,s.budgetGiB);else std::snprintf(left,sizeof(left),"—");row(yyappearance::Text("显存","VRAM"),left,"GB");
  ImGui::EndTable();
 }
}
}
