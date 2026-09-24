#pragma once
#include "panel_yanyun_recipe.h"
#include "yanyun_dual_policy.h"
#include "sr_model_abi.h"
namespace studio033 {
inline void RecipeChanged(RecipeView& v){v.dirty=true;v.draft.kind=yanyunrecipe::Custom;}
// S32: which columns a page renders. 模式二 uses all three on the NR and SR pages
// (whole = the first layer everyone shares, character = its composition only,
// scene = layers 2-3); its pre-grade page uses the character and scene columns
// exactly like 模式一.
enum class RecipePage {Pre,Nr,Sr};
inline bool ColumnActive(const RecipeView& v,yanyunrecipe::Group group,RecipePage page){
 using namespace yanyunrecipe;
 if(v.draft.regional==WholePicture)return group==Whole;
 if(v.draft.regional==SharedFirstLayer&&page!=RecipePage::Pre)return true;
 return group!=Whole;
}
inline bool SharedFirst(const RecipeView& v){return v.draft.regional==yanyunrecipe::SharedFirstLayer;}
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
 if(v.outputW&&v.renderW)for(unsigned g=0;g<3;++g)if(WorkRendered(v.draft.regional,Group(g))){
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
 // S32: 人物 / 场景 returns to the partition mode used last (模式一 unless 模式二 was chosen).
 if(v.draft.regional!=WholePicture&&v.draft.regional<PartitionCount)v.partition=v.draft.regional;
 const auto mp=p;dl->AddRect(mp,ImVec2(mp.x+mw,mp.y+h),edge,17*u);
 for(unsigned mode=0;mode<2;++mode){if(mode)ImGui::SameLine(0,0);const float part=mode?mw*.60f:mw*.40f;
  const bool selected=mode?v.draft.regional!=WholePicture:v.draft.regional==WholePicture;
  if(visual::segment(mode?L("人物 / 场景"):L("全局"),selected,ImVec2(part,h))&&!selected){v.draft.regional=mode?v.partition:uint32_t(WholePicture);v.preview=false;RecipeChanged(v);}
  ObserveItem(mode?"yy_mode_regional":"yy_mode_whole");
 }
 const char* badGroup="";int allowed=0;const bool invalid=RecipePrecisionInvalid(v,badGroup,allowed);
 ImGui::SameLine();ImGui::SetCursorScreenPos(ImVec2(p.x+w-aw,p.y-3*u));ImGui::BeginDisabled(!s.available||s.paused||invalid);
 if(visual::primary_button(v.dirty?L("应用修改"):L("应用方案"),ImVec2(aw,h+6*u)))e.applyWhole=true;
 ObserveItem("yy_apply_whole");ImGui::EndDisabled();Tip(L("前置、SR、NR 共用同一份方案；点击应用后统一生效。"));
 // S32 (owner: 「之前的叫模式一，现在改的叫模式二」): the two partition modes, under the switch.
 if(v.draft.regional!=WholePicture){
  const float pw=(std::min)(mw,w*.62f),ph=28*u;const auto pp=ImGui::GetCursorScreenPos();dl->AddRect(pp,ImVec2(pp.x+pw,pp.y+ph),edge,14*u);
  for(uint32_t mode=SeparateChains;mode<=SharedFirstLayer;++mode){if(mode!=SeparateChains)ImGui::SameLine(0,0);const bool selected=v.draft.regional==mode;
   if(visual::segment(mode==SeparateChains?L("模式一"):L("模式二"),selected,ImVec2(pw*.5f,ph))&&!selected){v.draft.regional=mode;v.partition=mode;RecipeChanged(v);}
   ObserveItem(mode==SeparateChains?"yy_partition_1":"yy_partition_2");
   Tip(mode==SeparateChains?L("模式一：人物和场景各自把整张画面算一遍，人物可以单独调模型参数。"):
    L("模式二：全局第 1 层整张画面只算一次，人物区域停在这一层，场景区域接着叠第 2、3 层；比模式一少算一整遍。"));
  }
 }
 if(invalid)ImGui::TextWrapped(L("%s精度超限：请在 SR 页调到 %d%% 以内。"),badGroup,allowed);
 if(wide&&v.dirty){const float x=mp.x+mw+28*u;dl->AddCircleFilled(ImVec2(x,p.y+h*.5f),5*u,ImGui::GetColorU32(visual::accent()),20);dl->AddText(ImGui::GetFont(),11*u,ImVec2(x+13*u,p.y+11*u),visual::ink(),yyappearance::Text("有未应用的修改","Unapplied changes"));}
 if(!wide&&v.dirty)ImGui::TextColored(ImColor(Accent),L("有未应用的修改"));
 if(v.runtimeNote&&v.runtimeNote[0]){ImGui::TextWrapped("%s",L(v.runtimeNote));}
 Space(.25f);
 return true;
}
inline void RecipeColumnTitle(const RecipeView& v,yanyunrecipe::Group group,RecipePage page){
 const char* names[]={L("全局"),L("人物"),L("场景")};const bool active=ColumnActive(v,group,page);
 const auto at=ImGui::GetCursorScreenPos();const float u=visual::unit(),w=ImGui::GetContentRegionAvail().x;auto* dl=ImGui::GetWindowDrawList();
 visual::bank_icon(at.x+8*u,at.y+5*u,32*u,unsigned(group));
 dl->AddText(ImGui::GetFont(),20*u,ImVec2(at.x+62*u,at.y+11*u),visual::ink(),names[group]);
 ImGui::Dummy(ImVec2(w,40*u));char key[32];std::snprintf(key,sizeof(key),"yy_column_%u",unsigned(group));ObserveItem(key);
 Tip(active?L("当前模式使用本列"):L("当前模式不使用本列，参数保留"));
}
inline bool RecipeTable(const RecipeView& v,const char* id,RecipePage page){
 if(!ImGui::BeginTable(id,3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_PadOuterX))return false;
 const char* names[]={L("全局"),L("人物"),L("场景")};
 for(unsigned g=0;g<3;++g){const bool active=ColumnActive(v,yanyunrecipe::Group(g),page);
  ImGui::TableSetupColumn(names[g],active?ImGuiTableColumnFlags_WidthStretch:ImGuiTableColumnFlags_WidthFixed,active?1.f:56*visual::unit());}
 return true;
}
inline bool RecipeFolded(RecipeView& v,yanyunrecipe::Group group,RecipePage page){
 const bool active=ColumnActive(v,group,page);if(active)return false;
 const char* names[]={L("全局"),L("人物"),L("场景")};ImGui::PushID(int(group));
 const float u=visual::unit();const auto at=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
 if(ImGui::Button("##fold",ImVec2(width,462*u))){v.draft.regional=group==yanyunrecipe::Whole?uint32_t(yanyunrecipe::WholePicture):v.partition;v.preview=false;RecipeChanged(v);}
 visual::bank_icon(at.x+(width-23*u)*.5f,at.y+15*u,23*u,unsigned(group));
 auto* dl=ImGui::GetWindowDrawList();const float tw=ImGui::CalcTextSize(names[group]).x;dl->AddText(ImVec2(at.x+(width-tw)*.5f,at.y+49*u),visual::ink(),names[group]);
 dl->AddText(ImVec2(at.x+width*.5f-4*u,at.y+80*u),visual::ink(),">");
 char key[32];std::snprintf(key,sizeof(key),"yy_fold_%u",unsigned(group));ObserveItem(key);
 std::snprintf(key,sizeof(key),"yy_column_%u",unsigned(group));ObserveItem(key);Tip(L("已收起，参数保留。点击切换处理模式。"));ImGui::PopID();return true;
}
inline void RecipeColumn(RecipeView& v,yanyunrecipe::Group group){
 using namespace yanyunrecipe;ImGui::PushID(int(group));
 ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::ColorConvertU32ToFloat4(visual::card_colour()));ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(9*visual::unit(),8*visual::unit()));ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6*visual::unit(),3*visual::unit()));
 ImGui::BeginChild("bank",ImVec2(0,0),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);visual::approved_card(ImGui::GetWindowPos(),ImGui::GetWindowSize().x,ImGui::GetWindowSize().y);RecipeColumnTitle(v,group,RecipePage::Nr);
 // S32 模式二: whole = the first layer everyone shares; character = only how that
 // layer is laid back onto the people (no model settings: the person region stops
 // after it); scene = the layer count and layers 2-3.
 const bool shared=SharedFirst(v);
 const auto afterTitle=ImGui::GetCursorScreenPos();const float bankWidth=ImGui::GetContentRegionAvail().x;
 if(!shared||group==Scene){
  ImGui::SetCursorScreenPos(ImVec2(afterTitle.x+bankWidth-28*visual::unit(),afterTitle.y-38*visual::unit()));
  if(ImGui::Button("...##bank_options",ImVec2(25*visual::unit(),23*visual::unit())))ImGui::OpenPopup("bank_options");Tip(L("混合与颜色"));
  ImGui::SetNextWindowSize(ImVec2(290*visual::unit(),0));if(ImGui::BeginPopup("bank_options")){
   ImGui::TextUnformatted(L("混合与颜色"));RecipeField(v,group,Blend,L("效果混合 %"));RecipeField(v,group,Colour,L("模型颜色"));ImGui::EndPopup();
  }ImGui::SetCursorScreenPos(afterTitle);
 }
 if(shared&&group==Character){
  ImGui::TextWrapped("%s",L("人物停在全局第 1 层；这里只调它的强弱和颜色。"));
  RecipeField(v,group,Blend,L("效果混合 %"));RecipeField(v,group,Colour,L("模型颜色"));
  ImGui::EndChild();ImGui::PopStyleVar(2);ImGui::PopStyleColor();ImGui::PopID();return;
 }

 const float u=visual::unit(),gap=ImGui::GetStyle().ItemSpacing.x,all=ImGui::GetContentRegionAvail().x,width=(std::min)(64*u,all/3);
 if(shared&&group==Whole)ImGui::TextDisabled("%s",L("第 1 层：人物和场景共用"));
 else{
  const auto selector=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos(ImVec2(selector.x+all-3*width,selector.y));
  for(int count=1;count<=3;++count){if(count>1)ImGui::SameLine(0,0);ImGui::PushID(count);const bool selected=int(Get(v.draft,group,Passes))==count;
   char label[16];std::snprintf(label,sizeof(label),yyappearance::Text("%d 层","%d"),count);
   if(visual::segment(label,selected,ImVec2(width,26*u))&&!selected){Set(v.draft,group,Passes,float(count));RecipeChanged(v);}
   char key[32];std::snprintf(key,sizeof(key),"yy_count_%u_%d",unsigned(group),count);ObserveItem(key);ImGui::PopID();
  }
 }
 const int visible=v.opened[group];
 for(int layer=0;layer<3;++layer){
  if(shared&&group==Whole&&layer>0)break; // 模式二 renders only the whole column's first layer
  if(shared&&group==Scene&&layer==0){ImGui::TextDisabled("%s",L("第 1 层在全局栏，人物和场景共用"));continue;}
  char key[40];std::snprintf(key,sizeof(key),"yy_layer_%u_%d",unsigned(group),layer+1);const bool on=(shared&&group==Whole)||layer<int(Get(v.draft,group,Passes));
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
  uint64_t(v.opened[1]&7)<<6|uint64_t(v.opened[2]&7)<<9|uint64_t((std::min)(v.library.size(),size_t(255)))<<12|uint64_t(v.draft.regional&3)<<20; // S32: 模式一 / 模式二 too
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
// S37 (owner 2026-09-24: 「另外快捷键能改」): the NR key. Click, then press the new key; Esc or a
// second click cancels. Only the keys hotkey033 offers are taken.
// S39 (owner: 「F11的热键修改有问题，有些键不能用」): every key hotkey033 now offers, plus the middle and
// side mouse buttons; a key that cannot be used says why (keyRefusal) and the panel keeps waiting.
struct NrKey {ImGuiKey key;int vk;};
inline constexpr NrKey NrKeys[]={
 {ImGuiKey_F1,0x70},{ImGuiKey_F2,0x71},{ImGuiKey_F3,0x72},{ImGuiKey_F4,0x73},{ImGuiKey_F5,0x74},{ImGuiKey_F6,0x75},{ImGuiKey_F7,0x76},{ImGuiKey_F8,0x77},
 {ImGuiKey_F9,0x78},{ImGuiKey_F10,0x79},{ImGuiKey_F11,0x7A},{ImGuiKey_F12,0x7B},
 {ImGuiKey_A,0x41},{ImGuiKey_B,0x42},{ImGuiKey_C,0x43},{ImGuiKey_D,0x44},{ImGuiKey_E,0x45},{ImGuiKey_F,0x46},{ImGuiKey_G,0x47},{ImGuiKey_H,0x48},{ImGuiKey_I,0x49},
 {ImGuiKey_J,0x4A},{ImGuiKey_K,0x4B},{ImGuiKey_L,0x4C},{ImGuiKey_M,0x4D},{ImGuiKey_N,0x4E},{ImGuiKey_O,0x4F},{ImGuiKey_P,0x50},{ImGuiKey_Q,0x51},{ImGuiKey_R,0x52},
 {ImGuiKey_S,0x53},{ImGuiKey_T,0x54},{ImGuiKey_U,0x55},{ImGuiKey_V,0x56},{ImGuiKey_W,0x57},{ImGuiKey_X,0x58},{ImGuiKey_Y,0x59},{ImGuiKey_Z,0x5A},
 {ImGuiKey_0,0x30},{ImGuiKey_1,0x31},{ImGuiKey_2,0x32},{ImGuiKey_3,0x33},{ImGuiKey_4,0x34},{ImGuiKey_5,0x35},{ImGuiKey_6,0x36},{ImGuiKey_7,0x37},{ImGuiKey_8,0x38},{ImGuiKey_9,0x39},
 {ImGuiKey_GraveAccent,0xC0},{ImGuiKey_Minus,0xBD},{ImGuiKey_Equal,0xBB},{ImGuiKey_LeftBracket,0xDB},{ImGuiKey_RightBracket,0xDD},{ImGuiKey_Backslash,0xDC},
 {ImGuiKey_Semicolon,0xBA},{ImGuiKey_Apostrophe,0xDE},{ImGuiKey_Comma,0xBC},{ImGuiKey_Period,0xBE},{ImGuiKey_Slash,0xBF},
 {ImGuiKey_Insert,0x2D},{ImGuiKey_Delete,0x2E},{ImGuiKey_End,0x23},{ImGuiKey_PageUp,0x21},{ImGuiKey_PageDown,0x22},
 {ImGuiKey_LeftArrow,0x25},{ImGuiKey_UpArrow,0x26},{ImGuiKey_RightArrow,0x27},{ImGuiKey_DownArrow,0x28},{ImGuiKey_ScrollLock,0x91},{ImGuiKey_Menu,0x5D},
 {ImGuiKey_Keypad0,0x60},{ImGuiKey_Keypad1,0x61},{ImGuiKey_Keypad2,0x62},{ImGuiKey_Keypad3,0x63},{ImGuiKey_Keypad4,0x64},{ImGuiKey_Keypad5,0x65},
 {ImGuiKey_Keypad6,0x66},{ImGuiKey_Keypad7,0x67},{ImGuiKey_Keypad8,0x68},{ImGuiKey_Keypad9,0x69},{ImGuiKey_KeypadDivide,0x6F},{ImGuiKey_KeypadMultiply,0x6A},
 {ImGuiKey_KeypadSubtract,0x6D},{ImGuiKey_KeypadAdd,0x6B},{ImGuiKey_KeypadDecimal,0x6E}};
inline constexpr NrKey NrRefusedKeys[]={
 {ImGuiKey_Home,0x24},{ImGuiKey_Backspace,0x08},{ImGuiKey_Enter,0x0D},{ImGuiKey_Space,0x20},{ImGuiKey_Tab,0x09},{ImGuiKey_CapsLock,0x14},{ImGuiKey_NumLock,0x90},
 {ImGuiKey_PrintScreen,0x2C},{ImGuiKey_Pause,0x13},{ImGuiKey_LeftShift,0xA0},{ImGuiKey_RightShift,0xA1},{ImGuiKey_LeftCtrl,0xA2},{ImGuiKey_RightCtrl,0xA3},
 {ImGuiKey_LeftAlt,0xA4},{ImGuiKey_RightAlt,0xA5},{ImGuiKey_LeftSuper,0x5B},{ImGuiKey_RightSuper,0x5C}};
struct NrButton {ImGuiMouseButton button;int vk;};
inline constexpr NrButton NrButtons[]={{2,0x04},{3,0x05},{4,0x06}}; // middle, side 1, side 2 (left / right click the panel)
inline void NrKeyButton(Edits& e){
 const bool capturing=keyCapture;char label[48];std::snprintf(label,sizeof(label),"%s###yy_nr_key",capturing?L("按新键…"):nrKeyName);
 if(capturing)ImGui::PushStyleColor(ImGuiCol_Text,visual::accent());
 if(ImGui::Button(label)){keyCapture=!keyCapture;keyRefusal=nullptr;}
 if(capturing)ImGui::PopStyleColor();
 ObserveItem("yy_nr_key");
 Tip(capturing?L("按想用的键：字母、数字、符号、F1 到 F12、方向键、Insert、Delete、End、PageUp、PageDown、小键盘，或者鼠标中键、侧键；Esc 取消。"):
  L("开关 NR 的快捷键。点一下，再按想用的键（大多数键和鼠标中键、侧键都行）就换成它。"));
 if(!capturing||!keyCapture)return;
 if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)){keyCapture=false;keyRefusal=nullptr;return;}
 for(const auto& k:NrKeys)if(ImGui::IsKeyPressed(k.key,false)&&hotkey033::Allowed(k.vk)){e.hotkey=k.vk;keyCapture=false;keyRefusal=nullptr;return;}
 for(const auto& b:NrButtons)if(ImGui::IsMouseClicked(b.button,false)&&hotkey033::Allowed(b.vk)){e.hotkey=b.vk;keyCapture=false;keyRefusal=nullptr;return;}
 for(const auto& k:NrRefusedKeys)if(ImGui::IsKeyPressed(k.key,false))if(const char* why=hotkey033::Refused(k.vk))keyRefusal=why;
}
inline void RecipeFooter(const State& s,Edits& e){
 Space(.25f);ImGui::Separator();Space(.15f);ImGui::BeginDisabled(!s.available||s.paused);
 if(ImGui::Button(s.values[Enabled]!=0?L("关闭 NR"):L("开启 NR")))e.toggle=true;ObserveItem("toggle_nr");ImGui::EndDisabled();
 static char keyTip[192];std::snprintf(keyTip,sizeof(keyTip),L("%s 切换 NR；Home / Shift+退格打开面板；Shift+F9 开关监控。"),nrKeyName);Tip(keyTip);ImGui::SameLine();
 NrKeyButton(e);ImGui::SameLine();
 ImGui::TextDisabled("%s",s.paused?L("本次 NR 已暂停"):s.failed?L("NR 出现异常"):s.modelPending?L("正在准备模型"):s.running?L("NR 处理中"):s.values[Enabled]!=0?L("等待画面"):L("NR 已关闭"));
 if(keyCapture&&keyRefusal){ImGui::TextColored(ImColor(visual::accent()),"%s",L(keyRefusal));ObserveItem("yy_nr_key_refused");}
 ImGui::TextDisabled(L("Shift+F9  显示 / 隐藏监控"));
}
inline void RecipeColumns(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;
 if(!RecipeToolbar(s,v,e))return;
 if(RecipeTable(v,"yy_nr_columns",RecipePage::Nr)){
  for(unsigned g=0;g<Groups;++g){ImGui::TableNextColumn();if(!RecipeFolded(v,Group(g),RecipePage::Nr))RecipeColumn(v,Group(g));}ImGui::EndTable();
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
// S32 (owner 2026-09-24, wording as given): 超分模型 of the game's own DLSS. It applies
// at once (one DLSS recreation), is remembered for the next start and is not part of
// the NR recipe or its share codes.
inline void SrModelRow(RecipeView& v,RecipeEdits& e){
 const float u=visual::unit();
 const uint32_t values[]={srmodelabi::GameDefault,srmodelabi::K,srmodelabi::M,srmodelabi::L};
 const char* names[]={L("游戏默认"),L("K（低）建议 20 系、30 系启用"),L("M（中）40 系、50 系可启用"),L("L（高）40 系、50 系可启用")};
 const char* shortNames[]={L("游戏默认"),L("K（低）"),L("M（中）"),L("L（高）")};
 const bool versionKnown=v.srMajor!=0,secondGeneration=!versionKnown||srmodelabi::SupportsSecondGeneration(v.srMajor,v.srMinor);
 int selected=0;for(int i=0;i<4;++i)if(values[i]==v.srModel)selected=i;
 ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(L("超分模型"));ImGui::SameLine();
 const float comboWidth=(std::min)(330*u,ImGui::GetContentRegionAvail().x);ImGui::SetNextItemWidth(comboWidth);const auto comboPos=ImGui::GetCursorScreenPos();
 const bool comboOpen=ImGui::BeginCombo("##yy_sr_model",names[selected],ImGuiComboFlags_NoArrowButton);
 if(comboOpen){
  for(int i=0;i<4;++i){const bool usable=!srmodelabi::SecondGeneration(values[i])||secondGeneration;ImGui::BeginDisabled(!usable);
   if(ImGui::Selectable(names[i],selected==i)&&selected!=i){e.srModel=int(values[i]);v.srModel=values[i];}
   ImGui::EndDisabled();char key[32];std::snprintf(key,sizeof(key),"yy_sr_model_%d",i);ObserveItem(key);}
  ImGui::EndCombo();
 }
 ObserveItem("yy_sr_model");visual::chevron(ImVec2(comboPos.x+comboWidth-10*u,comboPos.y+ImGui::GetFrameHeight()*.5f),3*u,comboOpen,visual::ink());
 Tip(L("游戏自带的 DLSS 超分用哪个模型；换模型时超分会重建一次，画面会顿一下。选 K、M、L 时游戏都按质量档分辨率渲染，游戏默认按游戏自己的档位；游戏设置里显示的档位不变。"));
 if(v.srPending)ImGui::TextDisabled("%s",L("正在按新模型重建超分……"));
 else if(v.srApplied!=srmodelabi::None){int shown=0;for(int i=0;i<4;++i)if(values[i]==v.srApplied)shown=i;
  if(versionKnown)ImGui::TextDisabled(L("游戏超分当前：%s · DLSS %u.%u.%u"),shortNames[shown],v.srMajor,v.srMinor,v.srPatch);
  else ImGui::TextDisabled(L("游戏超分当前：%s"),shortNames[shown]);}
 else ImGui::TextDisabled("%s",L("游戏还没开始用 DLSS 超分；进游戏画面后生效。"));
 // S33: only while the game still has to ask again for its render size. S34: the owner
 // dropped the render size line (「篮圈那里不用显示没事」).
 const uint32_t linked=srmodelabi::LinkedMode(v.srForcedMilli); // 1 Quality, 2 Balanced, 0 the game's own
 if(v.srSizePending)ImGui::TextWrapped("%s",linked==1?L("要换成质量档分辨率：在游戏设置里把 DLSS 档位切一下再切回来，或重进游戏。"):
  linked==2?L("要换成平衡档分辨率：在游戏设置里把 DLSS 档位切一下再切回来，或重进游戏。"):L("要回到游戏自己的分辨率：在游戏设置里把 DLSS 档位切一下再切回来，或重进游戏。"));
 if(versionKnown&&!secondGeneration)ImGui::TextDisabled("%s",L("这个游戏自带的 DLSS 低于 310.5，用不了 M 和 L。"));
 if(v.srExternal)ImGui::TextDisabled("%s",L("NVIDIA App 或 Profile Inspector 也在给这个游戏设超分模型。"));
}
inline void RecipeSr(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;if(!RecipeToolbar(s,v,e))return;
 SrModelRow(v,e);Space(.2f);
 if(RecipeTable(v,"yy_sr_columns",RecipePage::Sr)){
  const bool shared=SharedFirst(v);
  for(unsigned g=0;g<Groups;++g){ImGui::TableNextColumn();auto group=Group(g);if(RecipeFolded(v,group,RecipePage::Sr))continue;ImGui::PushID(int(g));RecipeColumnTitle(v,group,RecipePage::Sr);
   // S32 模式二: the whole column sizes the first layer everyone shares, the scene column
   // layers 2-3 (relative to that first layer); the character column has no model of its own.
   const bool firstLayer=!shared||group==Whole,laterLayers=!shared||group==Scene,composition=!shared||group!=Whole;
   if(firstLayer)RecipeField(v,group,Work,L("第 1 层精度 %"));
   if(laterLayers){RecipeField(v,group,PassWork,L("第 2 层精度 %"));RecipeField(v,group,PassWork3,L("第 3 层精度 %"));}
   if(firstLayer||laterLayers)Tip(shared&&group==Scene?L("按全局第 1 层的尺寸算；是否启用由 NR 页的层数决定。"):L("对应 NR 的同一层；是否启用由 NR 页的层数决定。"));
   if(shared&&group==Character)ImGui::TextWrapped("%s",L("人物用全局第 1 层的精度。"));
   if(ImGui::CollapsingHeader(L("更多"))){
    if(firstLayer){RecipeField(v,group,Full,L("以输出分辨率为基准"));Tip(L("开启：以最终输出尺寸计算精度；关闭：以游戏渲染尺寸计算。两者相同时不会改变结果。"));}
    if(composition){
     RecipeField(v,group,Sharpen,L("最终清晰度"));Tip(L("NR 合成完成后增强边缘，与运动自适应锐化分开。"));
     RecipeField(v,group,Mas,L("运动自适应锐化"));if(Get(v.draft,group,Mas)!=0){RecipeField(v,group,MasStill,L("静止强度"));RecipeField(v,group,MasMoving,L("运动强度"));RecipeField(v,group,MasThreshold,L("运动阈值"));}
    }
   }ImGui::PopID();
  }ImGui::EndTable();
 }
 ImGui::TextWrapped(L("SR 精度对应 NR 每层；游戏自带的 DLSS 超分档位仍在游戏设置中调整。"));RecipeStorage(s,v,e);
}
inline void RecipePre(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;if(!RecipeToolbar(s,v,e))return;
 if(RecipeTable(v,"yy_pre_columns",RecipePage::Pre)){
  for(unsigned g=0;g<Groups;++g){ImGui::TableNextColumn();auto group=Group(g);if(RecipeFolded(v,group,RecipePage::Pre))continue;ImGui::PushID(int(g));RecipeColumnTitle(v,group,RecipePage::Pre);
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
