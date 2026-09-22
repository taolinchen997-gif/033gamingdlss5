#pragma once
#include "panel_yanyun_recipe.h"
#include "yanyun_dual_policy.h"
namespace studio033 {
inline void RecipeChanged(RecipeView& v){v.dirty=true;v.draft.kind=yanyunrecipe::Custom;}
inline void RecipeField(RecipeView& v,yanyunrecipe::Group group,Id id,const char* label){
 using namespace yanyunrecipe;auto& value=v.draft.values[group][Index(id)];const auto& d=definitions[id];
 ImGui::PushID(int(id));char key[48];std::snprintf(key,sizeof(key),"yy_value_%u_%u",unsigned(group),unsigned(id));bool changed=false;
 if(d.kind==Toggle){bool on=value!=0;changed=ImGui::Checkbox(label,&on);if(changed)value=on?1.f:0.f;ObserveItem(key);}
 else {float maximum=d.maximum;
  if(id==Work&&v.outputW&&v.renderW){const int limit=yanyundual::MaximumWork(v.outputW,v.outputH,v.renderW,v.renderH,Get(v.draft,group,Full)!=0);if(limit>=25)maximum=float(limit);}
  changed=Range(label,value,d.minimum,maximum,d.kind==Scalar?"%.2f":"%.0f",d.kind==Scalar?.01f:1.f,key);}
 if(changed){if(d.kind!=Scalar)value=std::round(value);RecipeChanged(v);}ImGui::PopID();
}
inline void RecipeChoice(RecipeView& v,yanyunrecipe::Group group,Id id,const char* const* labels,int count,const char* observed){
 using namespace yanyunrecipe;int selected=int(Get(v.draft,group,id));ImGui::PushID(int(id));ImGui::SetNextItemWidth(-1);
 if(ImGui::BeginCombo("##choice",selected>=0&&selected<count?labels[selected]:L("保留原值"))){
  for(int i=0;i<count;++i){if(ImGui::Selectable(labels[i],selected==i)){Set(v.draft,group,id,float(i));RecipeChanged(v);}
   char key[64];std::snprintf(key,sizeof(key),"%s_%d",observed,i);ObserveItem(key);}
  ImGui::EndCombo();
 }
 ObserveItem(observed);ImGui::PopID();
}
inline bool RecipePrecisionInvalid(const RecipeView& v,const char*& badGroup,int& allowed){
 using namespace yanyunrecipe;
 if(v.outputW&&v.renderW)for(unsigned g=0;g<3;++g)if(v.draft.regional?g!=0:g==0){
  const auto group=Group(g);const int limit=yanyundual::MaximumWork(v.outputW,v.outputH,v.renderW,v.renderH,Get(v.draft,group,Full)!=0);
  if(Get(v.draft,group,Work)>limit){badGroup=g==0?L("全局"):g==1?L("人物"):L("场景");allowed=limit;return true;}}
 return false;
}
inline bool RecipeToolbar(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;
 if(!v.initialized&&s.available&&s.controlsFresh){auto initial=CaptureValues(s.values);if(Valid(initial)){v.draft=initial;v.initialized=true;}}
 if(!v.initialized){ImGui::TextDisabled(L("等待参数就绪"));return false;}
 const float u=visual::unit(),gap=ImGui::GetStyle().ItemSpacing.x,w=ImGui::GetContentRegionAvail().x,h=34*u;
 const auto p=ImGui::GetCursorScreenPos();const bool wide=w>=650*u;const float mw=(std::min)(280*u,w*.57f),aw=(std::min)(132*u,w*.36f);
 auto* dl=ImGui::GetWindowDrawList();const auto edge=ImGui::GetColorU32(ImGuiCol_Border);
 const auto mp=p;dl->AddRect(mp,ImVec2(mp.x+mw,mp.y+h),edge,17*u);
 for(unsigned mode=0;mode<2;++mode){if(mode)ImGui::SameLine(0,0);const float part=mode?mw*.60f:mw*.40f;
  if(visual::segment(mode?L("人物 / 场景"):L("全局"),v.draft.regional==mode,ImVec2(part,h))&&v.draft.regional!=mode){v.draft.regional=mode;v.preview=false;RecipeChanged(v);}
  ObserveItem(mode?"yy_mode_regional":"yy_mode_whole");
 }
 const char* badGroup="";int allowed=0;const bool invalid=RecipePrecisionInvalid(v,badGroup,allowed);
 ImGui::SameLine();ImGui::SetCursorScreenPos(ImVec2(p.x+w-aw,p.y-3*u));ImGui::BeginDisabled(!s.available||s.paused||invalid);
 if(visual::primary_button(v.dirty?L("应用修改"):L("应用方案"),ImVec2(aw,h+6*u)))e.applyWhole=true;
 ObserveItem("yy_apply_whole");ImGui::EndDisabled();Tip(L("前置、SR、NR 共用同一份方案；点击应用后统一生效。"));
 if(invalid)ImGui::TextWrapped(L("%s精度超限：请在 SR 页调到 %d%% 以内。"),badGroup,allowed);
 if(wide&&v.dirty){const float x=mp.x+mw+28*u;dl->AddCircleFilled(ImVec2(x,p.y+h*.5f),5*u,ImGui::GetColorU32(visual::accent()),20);dl->AddText(ImGui::GetFont(),11*u,ImVec2(x+13*u,p.y+11*u),visual::ink(),yyappearance::Text("有未应用的修改","Unapplied changes"));}
 if(!wide&&v.dirty)ImGui::TextColored(ImColor(Accent),L("有未应用的修改"));
 if(v.runtimeNote&&v.runtimeNote[0]){ImGui::TextWrapped("%s",L(v.runtimeNote));}
 Space(.25f);
 return true;
}
inline void RecipeColumnTitle(const RecipeView& v,yanyunrecipe::Group group){
 const char* names[]={L("全局"),L("人物"),L("场景")};const bool active=v.draft.regional?group!=yanyunrecipe::Whole:group==yanyunrecipe::Whole;
 const auto at=ImGui::GetCursorScreenPos();const float u=visual::unit(),w=ImGui::GetContentRegionAvail().x;auto* dl=ImGui::GetWindowDrawList();
 visual::bank_icon(at.x+8*u,at.y+5*u,32*u,unsigned(group));
 dl->AddText(ImGui::GetFont(),20*u,ImVec2(at.x+62*u,at.y+11*u),visual::ink(),names[group]);
 ImGui::Dummy(ImVec2(w,40*u));char key[32];std::snprintf(key,sizeof(key),"yy_column_%u",unsigned(group));ObserveItem(key);
 Tip(active?L("当前模式使用本列"):L("当前模式不使用本列，参数保留"));
}
inline bool RecipeTable(const RecipeView& v,const char* id){
 if(!ImGui::BeginTable(id,3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_PadOuterX))return false;
 const char* names[]={L("全局"),L("人物"),L("场景")};
 for(unsigned g=0;g<3;++g){const bool active=v.draft.regional?g!=0:g==0;
  ImGui::TableSetupColumn(names[g],active?ImGuiTableColumnFlags_WidthStretch:ImGuiTableColumnFlags_WidthFixed,active?1.f:56*visual::unit());}
 return true;
}
inline bool RecipeFolded(RecipeView& v,yanyunrecipe::Group group){
 const bool active=v.draft.regional?group!=yanyunrecipe::Whole:group==yanyunrecipe::Whole;if(active)return false;
 const char* names[]={L("全局"),L("人物"),L("场景")};ImGui::PushID(int(group));
 const float u=visual::unit();const auto at=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
 if(ImGui::Button("##fold",ImVec2(width,462*u))){v.draft.regional=group==yanyunrecipe::Whole?0:1;v.preview=false;RecipeChanged(v);}
 visual::bank_icon(at.x+(width-23*u)*.5f,at.y+15*u,23*u,unsigned(group));
 auto* dl=ImGui::GetWindowDrawList();const float tw=ImGui::CalcTextSize(names[group]).x;dl->AddText(ImVec2(at.x+(width-tw)*.5f,at.y+49*u),visual::ink(),names[group]);
 dl->AddText(ImVec2(at.x+width*.5f-4*u,at.y+80*u),visual::ink(),">");
 char key[32];std::snprintf(key,sizeof(key),"yy_fold_%u",unsigned(group));ObserveItem(key);
 std::snprintf(key,sizeof(key),"yy_column_%u",unsigned(group));ObserveItem(key);Tip(L("已收起，参数保留。点击切换处理模式。"));ImGui::PopID();return true;
}
inline void RecipeColumn(RecipeView& v,yanyunrecipe::Group group){
 using namespace yanyunrecipe;ImGui::PushID(int(group));
 ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::ColorConvertU32ToFloat4(visual::card_colour()));ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(9*visual::unit(),8*visual::unit()));ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6*visual::unit(),3*visual::unit()));
 ImGui::BeginChild("bank",ImVec2(0,0),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);visual::approved_card(ImGui::GetWindowPos(),ImGui::GetWindowSize().x,ImGui::GetWindowSize().y);RecipeColumnTitle(v,group);
 const auto afterTitle=ImGui::GetCursorScreenPos();const float bankWidth=ImGui::GetContentRegionAvail().x;
 ImGui::SetCursorScreenPos(ImVec2(afterTitle.x+bankWidth-28*visual::unit(),afterTitle.y-38*visual::unit()));
 if(ImGui::Button("...##bank_options",ImVec2(25*visual::unit(),23*visual::unit())))ImGui::OpenPopup("bank_options");Tip(L("混合与颜色"));
 ImGui::SetNextWindowSize(ImVec2(290*visual::unit(),0));if(ImGui::BeginPopup("bank_options")){
  ImGui::TextUnformatted(L("混合与颜色"));RecipeField(v,group,Blend,L("效果混合 %"));RecipeField(v,group,Colour,L("模型颜色"));ImGui::EndPopup();
 }ImGui::SetCursorScreenPos(afterTitle);

 const float u=visual::unit(),gap=ImGui::GetStyle().ItemSpacing.x,all=ImGui::GetContentRegionAvail().x,width=(std::min)(64*u,all/3);
 const auto selector=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos(ImVec2(selector.x+all-3*width,selector.y));
 for(int count=1;count<=3;++count){if(count>1)ImGui::SameLine(0,0);ImGui::PushID(count);const bool selected=int(Get(v.draft,group,Passes))==count;
  char label[16];std::snprintf(label,sizeof(label),yyappearance::Text("%d 层","%d"),count);
  if(visual::segment(label,selected,ImVec2(width,26*u))&&!selected){Set(v.draft,group,Passes,float(count));RecipeChanged(v);}
  char key[32];std::snprintf(key,sizeof(key),"yy_count_%u_%d",unsigned(group),count);ObserveItem(key);ImGui::PopID();
 }
 const int visible=v.opened[group];
 for(int layer=0;layer<3;++layer){char key[40];std::snprintf(key,sizeof(key),"yy_layer_%u_%d",unsigned(group),layer+1);const bool on=layer<int(Get(v.draft,group,Passes));
  LayerCard card(key,key,layer,on?L("启用"):L("未启用"),v.opened[group],visible,true);if(!card.expanded)continue;
  if(!on)ImGui::TextDisabled(L("本层未启用；请先在上方选择对应层数。"));
  const char* styles[]={L("默认"),L("自然"),L("电影")};char styleKey[40];std::snprintf(styleKey,sizeof(styleKey),"yy_style_%u_%d",unsigned(group),layer+1);
  const auto origin=ImGui::GetCursorScreenPos();const float area=ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorScreenPos(ImVec2(origin.x+1*u,origin.y+2*u));ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(yyappearance::Text("风格","Style"));
  ImGui::SetCursorScreenPos(ImVec2(origin.x+43*u,origin.y+2*u));ImGui::PushItemWidth((std::min)(94*u,area*.39f));
  {int selected=int(Get(v.draft,group,LayerId(Style,layer)));ImGui::PushID(layer);ImGui::SetNextItemWidth((std::min)(94*u,area*.39f));
   const auto comboPos=ImGui::GetCursorScreenPos();const float comboWidth=(std::min)(94*u,area*.39f);const bool comboOpen=ImGui::BeginCombo("##style",styles[std::clamp(selected,0,2)],ImGuiComboFlags_NoArrowButton);if(comboOpen){for(int i=0;i<3;++i){if(ImGui::Selectable(styles[i],selected==i)){Set(v.draft,group,LayerId(Style,layer),float(i));RecipeChanged(v);}char key[64];std::snprintf(key,sizeof(key),"%s_%d",styleKey,i);ObserveItem(key);}ImGui::EndCombo();}ObserveItem(styleKey);visual::chevron(ImVec2(comboPos.x+comboWidth-10*u,comboPos.y+ImGui::GetFrameHeight()*.5f),3*u,comboOpen,visual::ink());ImGui::PopID();}
  ImGui::PopItemWidth();
  ImGui::SetCursorScreenPos(ImVec2(origin.x,origin.y+ImGui::GetFrameHeight()+8*u));
  for(auto pair:{std::pair<Id,const char*>{Intensity,L("整体强度")},{Structure,L("细节强度")},{LocalTone,L("局部明暗")},{GlobalTone,L("全局色调")}})RecipeField(v,group,LayerId(pair.first,layer),pair.second);
  // Skin options belong to people: the scene column has none (fixed neutral at apply).
  if(group!=Scene){RecipeField(v,group,LayerId(Skin,layer),L("皮肤质感"));RecipeField(v,group,LayerId(AutoMask,layer),L("模型肤色保护"));}

 }
 ImGui::EndChild();ImGui::PopStyleVar(2);ImGui::PopStyleColor();ImGui::PopID();
}
inline bool RecipeDisclosure(const char* id,const char* label,bool& open,unsigned icon){
 const auto p=ImGui::GetCursorScreenPos();const float u=visual::unit(),w=ImGui::GetContentRegionAvail().x,h=40*u;auto* dl=ImGui::GetWindowDrawList();
 const bool pressed=ImGui::Button(id,ImVec2(w,h));ObserveItem(id);if(pressed)open=!open;
 visual::approved_sprite(ImVec2(p.x+17*u,p.y+10*u),19*u,21*u,yyappearance::light?811.f:57.f,icon?856.f:807.f,20,22);
 dl->AddText(ImVec2(p.x+52*u,p.y+(h-F())*.5f),visual::ink(),label);
 const float x=p.x+w-23*u,y=p.y+h*.5f,z=4*u,sign=open?-1.f:1.f;dl->AddLine(ImVec2(x-z,y-sign*z*.5f),ImVec2(x,y+sign*z*.5f),visual::ink(),1.1f*u);dl->AddLine(ImVec2(x,y+sign*z*.5f),ImVec2(x+z,y-sign*z*.5f),visual::ink(),1.1f*u);
 return open;
}
inline void RecipeStorage(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;
 // S23/S25: the player's own layout actions fit the window both ways -- whole /
 // regional, the two cards, the layer accordions and the number of saved records.
 // Every other height change only grows it (panel_studio.h Fit).
 static uint64_t fittedLayout=~0ull;
 const uint64_t layout=uint64_t(v.draft.regional!=0)|uint64_t(v.detectionOpen)<<1|uint64_t(v.storageOpen)<<2|uint64_t(v.opened[0]&7)<<3|
  uint64_t(v.opened[1]&7)<<6|uint64_t(v.opened[2]&7)<<9|uint64_t((std::min)(v.library.size(),size_t(255)))<<12;
 if(layout!=fittedLayout){fittedLayout=layout;RequestFit();}
 if(RecipeDisclosure("##yy_storage",L("保存与分享"),v.storageOpen,1)){
  // S23 (user): every save leaves a record below; share codes carry the author credit.
  if(ImGui::Button(L("保存方案")))e.save=true;ObserveItem("yy_save");
  ImGui::SameLine();if(ImGui::Button(L("生成分享码"))){auto text=ShareText(v.draft);std::snprintf(v.code,sizeof(v.code),"%s",text.c_str());}ObserveItem("yy_encode");
  ImGui::SameLine();if(ImGui::Button(L("复制")))ImGui::SetClipboardText(v.code);ObserveItem("yy_copy");
  ImGui::InputTextMultiline("##yy_code",v.code,sizeof(v.code),ImVec2(-1,48*visual::unit()));
  if(ImGui::Button(L("导入为草稿"))){Recipe next;if(DecodeShared(v.code,next)==DecodeResult::Ok){v.draft=next;v.dirty=true;v.note=L("已导入，应用后生效。");}else v.note=L("代码不兼容或损坏，原方案已保留。");}ObserveItem("yy_import");
  Space(.3f);ImGui::TextDisabled("%s",!v.libraryLoaded?L("保存记录暂时读不到，请稍后再试。"):v.library.empty()?L("还没有保存记录。点「保存方案」会在这里留下一条。"):L("保存记录（名字可直接改）"));
  const auto& style=ImGui::GetStyle();const float gap=style.ItemSpacing.x;
  const float switchW=ImGui::CalcTextSize(L("切换")).x+style.FramePadding.x*2,removeW=ImGui::CalcTextSize(L("确认删除")).x+style.FramePadding.x*2;
  for(int i=0;i<int(v.library.size());++i){auto& entry=v.library[size_t(i)];ImGui::PushID(i);char key[40];
   ImGui::SetNextItemWidth((std::max)(80*visual::unit(),ImGui::GetContentRegionAvail().x-switchW-removeW-2*gap));
   // Every edit is written at once, so closing the panel mid-edit keeps the name;
   // it is tidied (trimmed, never empty) when the field is left.
   if(ImGui::InputText("##name",entry.name,sizeof(entry.name)))e.libraryChanged=true;std::snprintf(key,sizeof(key),"yy_record_name_%d",i);ObserveItem(key);
   if(ImGui::IsItemDeactivatedAfterEdit()){SetName(entry,entry.name);e.libraryChanged=true;}
   ImGui::SameLine();if(ImGui::Button(L("切换"),ImVec2(switchW,0))){e.switchTo=i;v.removeArmed=-1;}std::snprintf(key,sizeof(key),"yy_record_switch_%d",i);ObserveItem(key);
   // Deleting takes a second click on the same row.
   ImGui::SameLine();const bool armed=v.removeArmed==i;if(armed)ImGui::PushStyleColor(ImGuiCol_Text,visual::accent());
   const bool removeClicked=ImGui::Button(armed?L("确认删除"):L("删除"),ImVec2(removeW,0));if(armed)ImGui::PopStyleColor();
   if(removeClicked){if(armed){e.remove=i;v.removeArmed=-1;}else v.removeArmed=i;}
   std::snprintf(key,sizeof(key),"yy_record_remove_%d",i);ObserveItem(key);
   ImGui::PopID();}
 }
 if(v.note&&v.note[0])ImGui::TextWrapped("%s",L(v.note));
 if(v.dirty){
  const char* bad="";int allowed=0;const bool invalid=RecipePrecisionInvalid(v,bad,allowed);
  ImGui::BeginDisabled(!s.available||s.paused||invalid);
  ImGui::PushID("bottom_apply");
  if(visual::primary_button(L("应用修改"),ImVec2(132*visual::unit(),32*visual::unit())))e.applyWhole=true;
  ObserveItem("yy_apply_bottom");ImGui::PopID();ImGui::EndDisabled();
  if(invalid)ImGui::TextWrapped(L("%s精度超限：请在 SR 页调到 %d%% 以内。"),bad,allowed);
 }

}
inline void RecipeLoaded(RecipeView& v,const yanyunrecipe::Recipe& loaded){
 v.draft=loaded;v.initialized=true;v.dirty=true;v.note=L("已读取草稿，应用后生效。");
}
inline void RecipeSaved(RecipeView& v,bool ok){
 v.note=ok?(v.dirty?L("草稿已保存，尚未应用。"):L("方案已保存。")):L("保存失败，原方案已保留。");
}
inline void RecipeFooter(const State& s,Edits& e){
 Space(.25f);ImGui::Separator();Space(.15f);ImGui::BeginDisabled(!s.available||s.paused);
 if(ImGui::Button(s.values[Enabled]!=0?L("关闭 NR"):L("开启 NR")))e.toggle=true;ObserveItem("toggle_nr");ImGui::EndDisabled();
 Tip(L("F11 切换 NR；Home / Shift+退格打开面板；Shift+F9 开关监控。"));ImGui::SameLine();
 ImGui::TextDisabled("%s",s.paused?L("本次 NR 已暂停"):s.failed?L("NR 出现异常"):s.modelPending?L("正在准备模型"):s.running?L("NR 处理中"):s.values[Enabled]!=0?L("等待画面"):L("NR 已关闭"));
 ImGui::TextDisabled(L("Shift+F9  显示 / 隐藏监控"));
}
inline void RecipeColumns(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;
 if(!RecipeToolbar(s,v,e))return;
 if(RecipeTable(v,"yy_nr_columns")){
  for(unsigned g=0;g<Groups;++g){ImGui::TableNextColumn();if(!RecipeFolded(v,Group(g)))RecipeColumn(v,Group(g));}ImGui::EndTable();
 }
 if(v.draft.regional&&RecipeDisclosure("##yy_detection",L("人物识别"),v.detectionOpen,0)){
  if(v.recognitionFailed)ImGui::TextWrapped(L("识别失败：人物 / 场景参数及预览当前未生效，原画保留。"));
  else if(v.runtimeNote&&v.runtimeNote[0])ImGui::TextWrapped("%s",L(v.runtimeNote));
  if(!v.regionalApplied)ImGui::TextWrapped(L("人物 / 场景方案尚未应用；识别预览可独立开关。"));
  ImGui::BeginDisabled(v.recognitionFailed);
  // S19: Range() draws fixed "##track"/"##number" widgets; keep an ID scope per slider.
  // S28 (user: 「人物保真度和场景强度默认不打折，直接去掉！」): person fidelity and scene
  // strength are gone; the person and scene columns apply in full (yanyun_dual_policy.h).
  ImGui::PushID("yy_feather");if(Range(L("边缘柔化"),v.draft.feather,0,8,"%.2f",.1f))RecipeChanged(v);ImGui::PopID();
  ImGui::BeginDisabled(s.values[Enabled]==0||s.paused);
  ImGui::Checkbox(L("显示人物识别区域"),&v.preview);ObserveItem("yy_mask_preview");ImGui::EndDisabled();
  if(s.values[Enabled]==0||s.paused)ImGui::TextDisabled(L("开启 NR 后可以预览识别区域。"));
  ImGui::EndDisabled();
  if(v.dirty)ImGui::TextColored(ImColor(Accent),L("参数尚未应用，请点击应用修改。"));
 }
 RecipeStorage(s,v,e);
}
inline void RecipeSr(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;if(!RecipeToolbar(s,v,e))return;
 if(RecipeTable(v,"yy_sr_columns")){
  for(unsigned g=0;g<Groups;++g){ImGui::TableNextColumn();auto group=Group(g);if(RecipeFolded(v,group))continue;ImGui::PushID(int(g));RecipeColumnTitle(v,group);
   RecipeField(v,group,Work,L("第 1 层精度 %"));RecipeField(v,group,PassWork,L("第 2 层精度 %"));RecipeField(v,group,PassWork3,L("第 3 层精度 %"));
   Tip(L("对应 NR 的同一层；是否启用由 NR 页的层数决定。"));
   if(ImGui::CollapsingHeader(L("更多"))){
    RecipeField(v,group,Full,L("以输出分辨率为基准"));Tip(L("开启：以最终输出尺寸计算精度；关闭：以游戏渲染尺寸计算。两者相同时不会改变结果。"));
    RecipeField(v,group,Sharpen,L("最终清晰度"));Tip(L("NR 合成完成后增强边缘，与运动自适应锐化分开。"));
    RecipeField(v,group,Mas,L("运动自适应锐化"));if(Get(v.draft,group,Mas)!=0){RecipeField(v,group,MasStill,L("静止强度"));RecipeField(v,group,MasMoving,L("运动强度"));RecipeField(v,group,MasThreshold,L("运动阈值"));}
   }ImGui::PopID();
  }ImGui::EndTable();
 }
 ImGui::TextWrapped(L("SR 精度对应 NR 每层；游戏自带的 DLSS 超分档位仍在游戏设置中调整。"));RecipeStorage(s,v,e);
}
inline void RecipePre(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;if(!RecipeToolbar(s,v,e))return;
 if(RecipeTable(v,"yy_pre_columns")){
  for(unsigned g=0;g<Groups;++g){ImGui::TableNextColumn();auto group=Group(g);if(RecipeFolded(v,group))continue;ImGui::PushID(int(g));RecipeColumnTitle(v,group);
   RecipeField(v,group,Grade,L("启用前置调色"));ImGui::BeginDisabled(Get(v.draft,group,Grade)==0);
   const char* styles[]={L("中性"),L("自然"),L("柔和"),L("动漫色阶")};char key[40];std::snprintf(key,sizeof(key),"yy_pre_style_%u",g);RecipeChoice(v,group,PreStyle,styles,4,key);
   ImGui::BeginDisabled(Get(v.draft,group,PreStyle)==0);
   RecipeField(v,group,PreStyleStrength,L("风格强度"));ImGui::EndDisabled();
   for(auto pair:{std::pair<Id,const char*>{Exposure,L("曝光")},{Contrast,L("对比度")},{Saturation,L("饱和度")},{Warmth,L("冷暖")},{Tint,L("色偏")},{Highlights,L("高光")}})RecipeField(v,group,pair.first,pair.second);
   ImGui::EndDisabled();ImGui::PopID();
  }ImGui::EndTable();
 }RecipeStorage(s,v,e);
}
}
