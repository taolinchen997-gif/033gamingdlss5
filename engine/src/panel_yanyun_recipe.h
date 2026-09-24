#pragma once
#include "yanyun_recipe.h"
#include "yanyun_recipe_library.h"
#include <vector>
// Presentation is shared with the CPU-only harness; no filesystem/renderer here.
namespace studio033 {
struct RecipeView {
 yanyunrecipe::Recipe draft{};bool initialized=false,dirty=false;
 bool preview=false;bool detectionOpen=false,storageOpen=false;
 bool recognitionFailed=false,regionalApplied=false;
 unsigned outputW=0,outputH=0,renderW=0,renderH=0;
 int group=0,layer=0,opened[3]{0,0,0};char code[yanyunrecipe::MaxCode+1]{};
 const char* note="选择画面方案，调整后应用。";
 const char* runtimeNote="尚未应用分区方案";
 uint64_t requestRevision=0,appliedRevision=0;
 // S32: the partition mode the 人物 / 场景 switch returns to (1 模式一, 2 模式二).
 uint32_t partition=1;
 // S32 超分模型 (the game's own DLSS): the last choice and what the last DLSS creation
 // really asked for, refreshed from the core every frame (srmodelabi values).
 uint32_t srModel=0,srApplied=0xFFFFFFFFu,srMajor=0,srMinor=0,srPatch=0;bool srPending=false,srExternal=false;
 // S33: the game's last render-size question and our answer (M / L force 1/2 / 1/3).
 uint32_t srQueries=0,srRenderW=0,srRenderH=0,srForcedMilli=0;bool srSizePending=false;
 // S23 (user): saved records shown under 保存与分享; the host loads and writes them.
 std::vector<yanyunrecipe::LibraryEntry> library;bool libraryLoaded=false;int removeArmed=-1;
};
struct RecipeEdits {bool load=false,save=false,applyWhole=false;int switchTo=-1,remove=-1;bool libraryChanged=false;int srModel=-1;};
inline const char* RecipeLabel(nrcontrolsabi::Id id){
 using namespace nrcontrolsabi;
 switch(id){
 case Full:return "以输出分辨率为基准";case Preset:return "模型预设编号（兼容）";case Style:return "第一层风格编号";
 case Intensity:return "第一层整体强度";case Structure:return "第一层细节强度";case GlobalTone:return "第一层全局色调";
 case LocalTone:return "第一层局部明暗";case Skin:return "第一层模型皮肤质感";case AutoMask:return "第一层模型自动遮罩";case UiCorrect:return "第一层界面校正";
 case Blend:return "结果强度 %";case Replica:return "可逆合成";case Compose:return "合成模式";case Curve:return "曲线模式";
 case White:return "固定白点";case Guard:return "高光编辑上限";case Colour:return "模型颜色强度";case DiffuseWhite:return "HDR 漫反射白点";
 case Grade:return "前置调色";case Exposure:return "曝光";case Contrast:return "对比度";case Saturation:return "饱和度";
 case Warmth:return "冷暖";case Tint:return "色调";case Highlights:return "高光压缩";
 case Mas:return "动态锐化";case MasStill:return "静止锐化";case MasMoving:return "运动锐化";case MasThreshold:return "运动阈值";
 case Sharpen:return "最终清晰度";case Resample:return "重采样滤镜";case PreStyle:return "前置风格编号";case PreStyleStrength:return "前置风格强度";
 case WhiteSource:return "白点来源编号";case WhiteTrim:return "曝光白点微调";
 case L2Style:return "第二层风格编号";case L2Preset:return "第二层预设编号（兼容）";case L2Intensity:return "第二层整体强度";
 case L2Structure:return "第二层细节强度";case L2LocalTone:return "第二层局部明暗";case L2Skin:return "第二层模型皮肤质感";
 case L2GlobalTone:return "第二层全局色调";case L2AutoMask:return "第二层模型自动遮罩";case L2UiCorrect:return "第二层界面校正";
 case L3Style:return "第三层风格编号";case L3Preset:return "第三层预设编号（兼容）";case L3Intensity:return "第三层整体强度";
 case L3Structure:return "第三层细节强度";case L3LocalTone:return "第三层局部明暗";case L3Skin:return "第三层模型皮肤质感";
 case L3GlobalTone:return "第三层全局色调";case L3AutoMask:return "第三层模型自动遮罩";case L3UiCorrect:return "第三层界面校正";
 case SkinLift:return "肤色提亮（颜色判断）";case NaturalLook:return "自然光影";
 default:return "参数";
 }
}
inline void RecipePanel(const State& s,RecipeView& v,RecipeEdits& e){
 using namespace yanyunrecipe;
 Heading("画面方案","写实 · 还原 · 自定义");
 if(!v.initialized&&s.available&&s.controlsFresh){auto initial=CaptureValues(s.values);if(Valid(initial)){v.draft=initial;v.initialized=true;}}
 if(!v.initialized){ImGui::TextWrapped("等待现有参数就绪，避免用默认值覆盖你的设置。");return;}
 const float width=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemSpacing.x;
 const char* names[]={"写实","还原","自定义"};const PresetKind kinds[]={Realistic,Restore,Custom};
 for(int i=0;i<3;++i){if(i)ImGui::SameLine();if(ImGui::Button(names[i],ImVec2((width-gap*2)/3,38*visual::unit()))){
   v.draft=MakePreset(v.draft,kinds[i]);v.dirty=true;if(i<2&&s.available&&!s.paused)e.applyWhole=true;v.note=i==0?"写实：冷色与细节，人物与场景独立调节。":i==1?"还原：优先保留人物原貌，适度增强场景。":"自定义：保留当前参数。";
  }char key[24];std::snprintf(key,sizeof(key),"yy_preset_%d",i);ObserveItem(key);}
 Space(.2f);
 bool regional=v.draft.regional!=0;
 if(ImGui::Checkbox("人物与场景分开调节",&regional)){v.draft.regional=regional?1u:0u;v.dirty=true;}
 ObserveItem("yy_regional");
 if(regional){
  // Unused legacy editor; own ID scope per slider for the same reason as RecipeColumns.
  ImGui::PushID("yy_fidelity");if(Range("人物保真程度",v.draft.fidelity,0,1,"%.2f",.01f)){v.draft.kind=Custom;v.dirty=true;}ImGui::PopID();ObserveItem("yy_fidelity");
  ImGui::PushID("yy_scene_strength");if(Range("场景增强程度",v.draft.sceneStrength,0,1,"%.2f",.01f)){v.draft.kind=Custom;v.dirty=true;}ImGui::PopID();ObserveItem("yy_scene_strength");
  ImGui::TextDisabled("人物保真越高，越接近游戏原貌。");
  ImGui::Checkbox("显示识别区域（白色为人物）",&v.preview);ObserveItem("yy_mask_preview");
 }
 ImGui::TextWrapped("%s",v.runtimeNote);
 ImGui::BeginDisabled(!s.available||s.paused);
 if(visual::primary_button(regional?"应用人物与场景方案":"应用全画面参数",ImVec2(-1,36*visual::unit())))e.applyWhole=true;
 ObserveItem("yy_apply_whole");ImGui::EndDisabled();
 if(ImGui::Button("保存方案"))e.save=true;ObserveItem("yy_save");ImGui::SameLine();
 if(ImGui::Button("读取方案"))e.load=true;ObserveItem("yy_load");
 const char* groups[]={"全画面","人物","场景"};
 for(int i=0;i<3;++i){if(i)ImGui::SameLine();if(ImGui::Selectable(groups[i],v.group==i,0,ImVec2((width-gap*2)/3,0)))v.group=i;
  char key[24];std::snprintf(key,sizeof(key),"yy_group_%d",i);ObserveItem(key);}
 const auto group=Group(v.group);
 ImGui::TextDisabled("%s",group==Whole?"全画面参数用于关闭分区时。":group==Character?"白色识别区域使用本组参数；漏检部分仍会归入场景。":"场景参数独立于人物参数。");
 // Editor uses the same bounded field definitions, but never the legacy edit mask.
 auto edit=[&](Id id,const char* label){const auto index=Index(id);ImGui::PushID(int(id));auto& value=v.draft.values[group][index];const auto& d=definitions[id];
  if(Range(label,value,d.minimum,d.maximum,d.kind==Scalar?"%.2f":"%.0f",d.kind==Scalar?.01f:1.f)){
   if(d.kind!=Scalar)value=std::round(value);v.draft.kind=Custom;v.dirty=true;
  }ImGui::PopID();};
 edit(Passes,"NR 层数");
 const char* layers[]={"第一层","第二层","第三层"};
 ImGui::SetNextItemWidth(-1);ImGui::Combo("##yy_layer",&v.layer,layers,3);
 for(auto entry:{std::pair<Id,const char*>{Style,"风格编号"},{Intensity,"整体强度"},{Structure,"细节强度"},{LocalTone,"局部明暗"}})edit(LayerId(entry.first,v.layer),entry.second);
 edit(v.layer==0?Work:v.layer==1?PassWork:PassWork3,"模型分辨率 %");
 if(ImGui::CollapsingHeader("更多参数")){
  for(unsigned i=0;i<FieldCount;++i){const auto id=Fields[i];if(id==Passes||id==Work||id==PassWork||id==PassWork3)continue;
   bool shown=false;for(auto first:{Style,Intensity,Structure,LocalTone})if(id==LayerId(first,v.layer))shown=true;if(shown)continue;
   edit(id,RecipeLabel(id));
  }
 }
 ImGui::TextWrapped("%s",v.note);
 if(ImGui::CollapsingHeader("分享与导入")){
  if(ImGui::Button("生成分享代码")){auto code=Encode(v.draft);std::snprintf(v.code,sizeof(v.code),"%s",code.c_str());}ObserveItem("yy_encode");
  ImGui::SameLine();if(ImGui::Button("复制"))ImGui::SetClipboardText(v.code);
  ImGui::InputTextMultiline("##yy_code",v.code,sizeof(v.code),ImVec2(-1,70*visual::unit()));
  if(ImGui::Button("导入为草稿")){Recipe next;if(Decode(v.code,next)==DecodeResult::Ok){v.draft=next;v.dirty=true;v.note="已导入草稿；尚未改变游戏画面。";}
   else v.note="导入失败：代码损坏、参数超限或模型版本不匹配。原方案已保留。";}ObserveItem("yy_import");
  ImGui::TextWrapped("代码包含三组画面参数；不含路径、账号、帧生成或快捷键设置。");
 }
}
}
