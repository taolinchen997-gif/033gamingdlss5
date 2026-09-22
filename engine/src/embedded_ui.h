#pragma once
#include "embedded_ui_abi.h"
#include "embedded_ui_labels.h"
namespace embeddedui {

static int __cdecl Invoke(ui033abi::Call* input){
 if(!input||input->size!=sizeof(*input)||input->op>=ui033abi::Count)return 0;
 auto& c=*input;using namespace ui033abi;
 switch(c.op){
case EndCombo: ImGui::EndCombo(); break;
case EndChild: ImGui::EndChild(); break;
case EndTable: ImGui::EndTable(); break;
case TableNextColumn: c.result=ImGui::TableNextColumn(); break;
case TableNextRow: ImGui::TableNextRow(); break;
case EndDisabled: ImGui::EndDisabled(); break;
case EndTooltip: ImGui::EndTooltip(); break;
case PopID: ImGui::PopID(); break;
case PopItemWidth: ImGui::PopItemWidth(); break;
case TreePop: ImGui::TreePop(); break;
case Spacing: ImGui::Spacing(); break;
case Separator: ImGui::Separator(); break;
case CloseCurrentPopup: ImGui::CloseCurrentPopup(); break;
case BeginTooltip: c.result=ImGui::BeginTooltip(); break;
case Indent: ImGui::Indent(c.f[0]); break;
case Unindent: ImGui::Unindent(c.f[0]); break;
case PushItemWidth: ImGui::PushItemWidth(c.f[0]); break;
case SetNextItemWidth: ImGui::SetNextItemWidth(c.f[0]); break;
case SetCursorPosX: ImGui::SetCursorPosX(c.f[0]); break;
case GetWindowWidth: c.f[0]=ImGui::GetWindowWidth(); break;
case GetCursorPosX: c.f[0]=ImGui::GetCursorPosX(); break;
case GetFontSize: c.f[0]=ImGui::GetFontSize(); break;
case GetTextLineHeight: c.f[0]=ImGui::GetTextLineHeight(); break;
case BeginDisabled: ImGui::BeginDisabled(c.i[0]!=0); break;
case IsItemHovered: c.result=ImGui::IsItemHovered(c.i[0]); break;
case IsWindowFocused: c.result=ImGui::IsWindowFocused(c.i[0]); break;
case TableSetColumnIndex: c.result=ImGui::TableSetColumnIndex(c.i[0]); break;
case SameLine: ImGui::SameLine(c.f[0],c.f[1]); break;
case Button: c.result=ImGui::Button(Label(c.text),ImVec2(c.f[0],c.f[1])); break;
case BeginCombo: c.result=ImGui::BeginCombo(Label(c.text),Label(c.text2,false),c.i[0]); break;
case Selectable: c.result=ImGui::Selectable(Label(c.text),c.i[0]!=0,c.i[1],ImVec2(c.f[0],c.f[1])); break;
case Checkbox: c.result=ImGui::Checkbox(Label(c.text),static_cast<bool*>(c.data)); break;
case CheckboxFlags: c.result=ImGui::CheckboxFlags(Label(c.text),static_cast<unsigned int*>(c.data),static_cast<unsigned int>(c.i[0])); break;
case RadioButton: c.result=ImGui::RadioButton(Label(c.text),c.i[0]!=0); break;
case RadioInt: c.result=ImGui::RadioButton(Label(c.text),static_cast<int*>(c.data),c.i[0]); break;
case TreeNode: c.result=ImGui::TreeNode(Label(c.text)); break;
case CollapsingHeader: c.result=ImGui::CollapsingHeader(Label(c.text),c.i[0]); break;
case BeginChild: c.result=ImGui::BeginChild(c.text,ImVec2(c.f[0],c.f[1]),c.i[0],c.i[1]); break;
case BeginTable: c.result=ImGui::BeginTable(c.text,c.i[0],c.i[1],ImVec2(c.f[0],c.f[1]),c.f[2]); break;
case TableSetupColumn: ImGui::TableSetupColumn(Label(c.text),c.i[0],c.f[0],static_cast<ImGuiID>(c.i[1])); break;
case SliderInt: c.result=ImGui::SliderInt(Label(c.text),static_cast<int*>(c.data),static_cast<int>(c.f[0]),static_cast<int>(c.f[1]),c.text2,c.i[0]); break;
case SliderFloat: c.result=ImGui::SliderFloat(Label(c.text),static_cast<float*>(c.data),static_cast<float>(c.f[0]),static_cast<float>(c.f[1]),c.text2,c.i[0]); break;
case InputInt: c.result=ImGui::InputInt(Label(c.text),static_cast<int*>(c.data),c.i[0],c.i[1],c.i[2]); break;
case InputFloat: c.result=ImGui::InputFloat(Label(c.text),static_cast<float*>(c.data),c.f[0],c.f[1],c.text2,c.i[0]); break;
case InputScalar: c.result=ImGui::InputScalar(Label(c.text),c.i[0],c.data,c.extra,c.extra2,c.text2,c.i[1]); break;
case Combo: c.result=ImGui::Combo(Label(c.text),static_cast<int*>(c.data),static_cast<const char* const*>(c.extra),c.i[0],c.i[1]); break;
case ColorEdit3: c.result=ImGui::ColorEdit3(Label(c.text),static_cast<float*>(c.data),c.i[0]); break;
case PushID: ImGui::PushID(c.text); break;
case PushIntID: ImGui::PushID(c.i[0]); break;
case Dummy: ImGui::Dummy(ImVec2(c.f[0],c.f[1])); break;
case GetContentRegionAvail:{auto p=ImGui::GetContentRegionAvail();c.f[0]=p.x;c.f[1]=p.y;break;}
case GetCursorScreenPos:{auto p=ImGui::GetCursorScreenPos();c.f[0]=p.x;c.f[1]=p.y;break;}
case GetItemRectMin:{auto p=ImGui::GetItemRectMin();c.f[0]=p.x;c.f[1]=p.y;break;}
case GetItemRectMax:{auto p=ImGui::GetItemRectMax();c.f[0]=p.x;c.f[1]=p.y;break;}
case GetWindowPos:{auto p=ImGui::GetWindowPos();c.f[0]=p.x;c.f[1]=p.y;break;}
case GetWindowSize:{auto p=ImGui::GetWindowSize();c.f[0]=p.x;c.f[1]=p.y;break;}
case TextPlain:ImGui::Text("%s",Label(c.text,false));break;
case TextMuted:ImGui::TextDisabled("%s",Label(c.text,false));break;
case TextWrap:ImGui::TextWrapped("%s",Label(c.text,false));break;
case Tooltip:ImGui::SetTooltip("%s",Label(c.text,false));break;
case TextColor:ImGui::TextColored(ImVec4(c.f[0],c.f[1],c.f[2],c.f[3]),"%s",Label(c.text,false));break;
case SeparatorLabel:ImGui::SeparatorText(Label(c.text,false));break;
case TextSize:{auto p=ImGui::CalcTextSize(c.text,c.text2,c.i[0]!=0,c.f[0]);c.f[0]=p.x;c.f[1]=p.y;break;}
case SmallButton: c.result=ImGui::SmallButton(Label(c.text)); break;
case IsItemDeactivatedAfterEdit: c.result=ImGui::IsItemDeactivatedAfterEdit(); break;
case PushTextWrapPos: ImGui::PushTextWrapPos(c.f[0]); break;
case PopTextWrapPos: ImGui::PopTextWrapPos(); break;
case TextUnformatted: ImGui::TextUnformatted(c.text,c.text2); break;
case Begin: c.result=ImGui::Begin(Label(c.text),static_cast<bool*>(c.data),c.i[0]); break;
case End: ImGui::End(); break;
case SetNextWindowPos: ImGui::SetNextWindowPos(ImVec2(c.f[0],c.f[1]),c.i[0],ImVec2(c.f[2],c.f[3])); break;
case SetNextWindowSize: ImGui::SetNextWindowSize(ImVec2(c.f[0],c.f[1]),c.i[0]); break;
case SetWindowFocus: ImGui::SetWindowFocus(); break;
case TextLinkOpenURL: ImGui::TextLinkOpenURL(Label(c.text),c.text2); break;
case PlotSamples:ImGui::PlotLines(c.text,static_cast<float*>(c.data),c.i[0],c.i[1],c.text2,c.f[0],c.f[1],ImVec2(c.f[2],c.f[3]));break;
case ViewMetrics:{const auto& io=ImGui::GetIO();c.f[0]=io.DisplaySize.x;c.f[1]=io.DisplaySize.y;c.f[2]=io.DeltaTime;break;}
case PushColor:{static constexpr ImGuiCol ids[]={ImGuiCol_Text,ImGuiCol_WindowBg,ImGuiCol_Border,ImGuiCol_FrameBg,ImGuiCol_PlotLines,ImGuiCol_Button,ImGuiCol_ButtonHovered,ImGuiCol_ButtonActive};if(c.i[0]<0||c.i[0]>=8)return 0;ImGui::PushStyleColor(ids[c.i[0]],ImVec4(c.f[0],c.f[1],c.f[2],c.f[3]));break;}
case PopColor:ImGui::PopStyleColor(c.i[0]);break;
case PushFontPx:ImGui::PushFont(nullptr,c.f[0]);break;
case PopFontPx:ImGui::PopFont();break;
 default:return 0;
 }return 1;
}
#ifndef K033_EMBEDDED_UI_NO_DRAW
static int SupplementalFramegen(){
 const auto* core=rendercore::Api();if(!core){ImGui::TextDisabled("等待 033 核心接入");return 0;}
 HMODULE mod=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(core->claim),&mod))return 0;
 auto render=reinterpret_cast<ui033abi::Render>(GetProcAddress(mod,"K033_RenderSupplementalFramegen"));
 static const ui033abi::Api api{sizeof(ui033abi::Api),ui033abi::Version,Invoke};
 if(render)return render(&api);ImGui::TextDisabled("当前核心未提供兼容插帧入口。");return 0;
}
static void Save(){
 const auto* core=rendercore::Api();if(!core)return;
 HMODULE mod=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(core->claim),&mod))return;
 auto save=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(mod,"K033_SaveEmbeddedSettings"));if(save)save();
}
static void Draw(){
 const auto* core=rendercore::Api();if(!core)return;
 HMODULE mod=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(core->claim),&mod))return;
 auto render=reinterpret_cast<ui033abi::Render>(GetProcAddress(mod,"K033_RenderEmbeddedControls"));
 if(!render){ImGui::TextDisabled("当前核心尚不支持统一面板，需要配套更新。");return;}
 static const ui033abi::Api api{sizeof(ui033abi::Api),ui033abi::Version,Invoke};
 const int result=render(&api);
 if(result==0)ImGui::TextDisabled("033 扩展功能正在初始化。");
}
#endif
}
