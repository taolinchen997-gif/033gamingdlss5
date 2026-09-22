#pragma once
#include "../../../../src/embedded_ui_abi.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cstdarg>
#include <cstdio>
#include <cfloat>
namespace Ui033 {
using namespace ImGui;
using namespace ui033abi;
inline thread_local const Api* active=nullptr;
inline thread_local ImGuiStyle isolatedStyle;
inline thread_local ImGuiIO isolatedIO;
inline void Send(Call& c){if(active)active->invoke(&c);}
struct Scope{const Api* previous;Scope(const Api* api):previous(active){active=api;}~Scope(){active=previous;}};
inline ImGuiStyle& GetStyle(){return active?isolatedStyle:ImGui::GetStyle();}
inline ImGuiIO& GetIO(){if(!active)return ImGui::GetIO();Call c{};c.op=ui033abi::ViewMetrics;Send(c);isolatedIO.DisplaySize=ImVec2(c.f[0],c.f[1]);isolatedIO.DeltaTime=c.f[2];return isolatedIO;}
inline void EndCombo(){if(!active){ImGui::EndCombo();return;} Call c{};c.op=ui033abi::EndCombo;Send(c);return;}
inline void EndChild(){if(!active){ImGui::EndChild();return;} Call c{};c.op=ui033abi::EndChild;Send(c);return;}
inline void EndTable(){if(!active){ImGui::EndTable();return;} Call c{};c.op=ui033abi::EndTable;Send(c);return;}
inline bool TableNextColumn(){if(!active)return ImGui::TableNextColumn(); Call c{};c.op=ui033abi::TableNextColumn;Send(c);return c.result!=0;}
inline void TableNextRow(){if(!active){ImGui::TableNextRow();return;} Call c{};c.op=ui033abi::TableNextRow;Send(c);return;}
inline void EndDisabled(){if(!active){ImGui::EndDisabled();return;} Call c{};c.op=ui033abi::EndDisabled;Send(c);return;}
inline void EndTooltip(){if(!active){ImGui::EndTooltip();return;} Call c{};c.op=ui033abi::EndTooltip;Send(c);return;}
inline void PopID(){if(!active){ImGui::PopID();return;} Call c{};c.op=ui033abi::PopID;Send(c);return;}
inline void PopItemWidth(){if(!active){ImGui::PopItemWidth();return;} Call c{};c.op=ui033abi::PopItemWidth;Send(c);return;}
inline void TreePop(){if(!active){ImGui::TreePop();return;} Call c{};c.op=ui033abi::TreePop;Send(c);return;}
inline void Spacing(){if(!active){ImGui::Spacing();return;} Call c{};c.op=ui033abi::Spacing;Send(c);return;}
inline void Separator(){if(!active){ImGui::Separator();return;} Call c{};c.op=ui033abi::Separator;Send(c);return;}
inline void CloseCurrentPopup(){if(!active){ImGui::CloseCurrentPopup();return;} Call c{};c.op=ui033abi::CloseCurrentPopup;Send(c);return;}
inline bool BeginTooltip(){if(!active)return ImGui::BeginTooltip(); Call c{};c.op=ui033abi::BeginTooltip;Send(c);return c.result!=0;}
inline void Indent(float width=0){if(!active){ImGui::Indent(width);return;} Call c{};c.op=ui033abi::Indent;c.f[0]=width;Send(c);return;}
inline void Unindent(float width=0){if(!active){ImGui::Unindent(width);return;} Call c{};c.op=ui033abi::Unindent;c.f[0]=width;Send(c);return;}
inline void PushItemWidth(float value){if(!active){ImGui::PushItemWidth(value);return;} Call c{};c.op=ui033abi::PushItemWidth;c.f[0]=value;Send(c);return;}
inline void SetNextItemWidth(float value){if(!active){ImGui::SetNextItemWidth(value);return;} Call c{};c.op=ui033abi::SetNextItemWidth;c.f[0]=value;Send(c);return;}
inline void SetCursorPosX(float value){if(!active){ImGui::SetCursorPosX(value);return;} Call c{};c.op=ui033abi::SetCursorPosX;c.f[0]=value;Send(c);return;}
inline float GetWindowWidth(){if(!active)return ImGui::GetWindowWidth(); Call c{};c.op=ui033abi::GetWindowWidth;Send(c);return c.f[0];}
inline float GetCursorPosX(){if(!active)return ImGui::GetCursorPosX(); Call c{};c.op=ui033abi::GetCursorPosX;Send(c);return c.f[0];}
inline float GetFontSize(){if(!active)return ImGui::GetFontSize(); Call c{};c.op=ui033abi::GetFontSize;Send(c);return c.f[0];}
inline float GetTextLineHeight(){if(!active)return ImGui::GetTextLineHeight(); Call c{};c.op=ui033abi::GetTextLineHeight;Send(c);return c.f[0];}
inline void BeginDisabled(bool disabled=true){if(!active){ImGui::BeginDisabled(disabled);return;} Call c{};c.op=ui033abi::BeginDisabled;c.i[0]=disabled;Send(c);return;}
inline bool IsItemHovered(ImGuiHoveredFlags flags=0){if(!active)return ImGui::IsItemHovered(flags); Call c{};c.op=ui033abi::IsItemHovered;c.i[0]=flags;Send(c);return c.result!=0;}
inline bool IsWindowFocused(ImGuiFocusedFlags flags=0){if(!active)return ImGui::IsWindowFocused(flags); Call c{};c.op=ui033abi::IsWindowFocused;c.i[0]=flags;Send(c);return c.result!=0;}
inline bool TableSetColumnIndex(int index){if(!active)return ImGui::TableSetColumnIndex(index); Call c{};c.op=ui033abi::TableSetColumnIndex;c.i[0]=index;Send(c);return c.result!=0;}
inline void SameLine(float offset=0,float spacing=-1){if(!active){ImGui::SameLine(offset,spacing);return;} Call c{};c.op=ui033abi::SameLine;c.f[0]=offset;c.f[1]=spacing;Send(c);return;}
inline bool Button(const char* text,const ImVec2& size=ImVec2(0,0)){if(!active)return ImGui::Button(text,size); Call c{};c.op=ui033abi::Button;c.text=text;c.f[0]=size.x;c.f[1]=size.y;Send(c);return c.result!=0;}
inline bool BeginCombo(const char* text,const char* preview,ImGuiComboFlags flags=0){if(!active)return ImGui::BeginCombo(text,preview,flags); Call c{};c.op=ui033abi::BeginCombo;c.text=text;c.text2=preview;c.i[0]=flags;Send(c);return c.result!=0;}
inline bool Selectable(const char* text,bool selected=false,ImGuiSelectableFlags flags=0,const ImVec2& size=ImVec2(0,0)){if(!active)return ImGui::Selectable(text,selected,flags,size); Call c{};c.op=ui033abi::Selectable;c.text=text;c.i[0]=selected;c.i[1]=flags;c.f[0]=size.x;c.f[1]=size.y;Send(c);return c.result!=0;}
inline bool Checkbox(const char* text,bool* value){if(!active)return ImGui::Checkbox(text,value); Call c{};c.op=ui033abi::Checkbox;c.text=text;c.data=value;Send(c);return c.result!=0;}
inline bool CheckboxFlags(const char* text,unsigned int* value,unsigned int mask){if(!active)return ImGui::CheckboxFlags(text,value,mask); Call c{};c.op=ui033abi::CheckboxFlags;c.text=text;c.data=value;c.i[0]=mask;Send(c);return c.result!=0;}
inline bool RadioButton(const char* text,bool activeValue){if(!active)return ImGui::RadioButton(text,activeValue); Call c{};c.op=ui033abi::RadioButton;c.text=text;c.i[0]=activeValue;Send(c);return c.result!=0;}
inline bool RadioButton(const char* text,int* value,int buttonValue){if(!active)return ImGui::RadioButton(text,value,buttonValue); Call c{};c.op=ui033abi::RadioInt;c.text=text;c.data=value;c.i[0]=buttonValue;Send(c);return c.result!=0;}
inline bool TreeNode(const char* text){if(!active)return ImGui::TreeNode(text); Call c{};c.op=ui033abi::TreeNode;c.text=text;Send(c);return c.result!=0;}
inline bool CollapsingHeader(const char* text,ImGuiTreeNodeFlags flags=0){if(!active)return ImGui::CollapsingHeader(text,flags); Call c{};c.op=ui033abi::CollapsingHeader;c.text=text;c.i[0]=flags;Send(c);return c.result!=0;}
inline bool BeginChild(const char* text,const ImVec2& size=ImVec2(0,0),ImGuiChildFlags childFlags=0,ImGuiWindowFlags flags=0){if(!active)return ImGui::BeginChild(text,size,childFlags,flags); Call c{};c.op=ui033abi::BeginChild;c.text=text;c.f[0]=size.x;c.f[1]=size.y;c.i[0]=childFlags;c.i[1]=flags;Send(c);return c.result!=0;}
inline bool BeginTable(const char* text,int columns,ImGuiTableFlags flags=0,const ImVec2& size=ImVec2(0,0),float innerWidth=0){if(!active)return ImGui::BeginTable(text,columns,flags,size,innerWidth); Call c{};c.op=ui033abi::BeginTable;c.text=text;c.i[0]=columns;c.i[1]=flags;c.f[0]=size.x;c.f[1]=size.y;c.f[2]=innerWidth;Send(c);return c.result!=0;}
inline void TableSetupColumn(const char* text,ImGuiTableColumnFlags flags=0,float width=0,ImGuiID id=0){if(!active){ImGui::TableSetupColumn(text,flags,width,id);return;} Call c{};c.op=ui033abi::TableSetupColumn;c.text=text;c.i[0]=flags;c.i[1]=id;c.f[0]=width;Send(c);return;}
inline bool SliderInt(const char* text,int* value,int low,int high,const char* format="%d",ImGuiSliderFlags flags=0){if(!active)return ImGui::SliderInt(text,value,low,high,format,flags); Call c{};c.op=ui033abi::SliderInt;c.text=text;c.text2=format;c.data=value;c.f[0]=float(low);c.f[1]=float(high);c.i[0]=flags;Send(c);return c.result!=0;}
inline bool SliderFloat(const char* text,float* value,float low,float high,const char* format="%.3f",ImGuiSliderFlags flags=0){if(!active)return ImGui::SliderFloat(text,value,low,high,format,flags); Call c{};c.op=ui033abi::SliderFloat;c.text=text;c.text2=format;c.data=value;c.f[0]=float(low);c.f[1]=float(high);c.i[0]=flags;Send(c);return c.result!=0;}
inline bool InputInt(const char* text,int* value,int step=1,int fast=100,ImGuiInputTextFlags flags=0){if(!active)return ImGui::InputInt(text,value,step,fast,flags); Call c{};c.op=ui033abi::InputInt;c.text=text;c.data=value;c.i[0]=step;c.i[1]=fast;c.i[2]=flags;Send(c);return c.result!=0;}
inline bool InputFloat(const char* text,float* value,float step=0,float fast=0,const char* format="%.3f",ImGuiInputTextFlags flags=0){if(!active)return ImGui::InputFloat(text,value,step,fast,format,flags); Call c{};c.op=ui033abi::InputFloat;c.text=text;c.data=value;c.f[0]=step;c.f[1]=fast;c.text2=format;c.i[0]=flags;Send(c);return c.result!=0;}
inline bool InputScalar(const char* text,ImGuiDataType type,void* value,const void* step=nullptr,const void* fast=nullptr,const char* format=nullptr,ImGuiInputTextFlags flags=0){if(!active)return ImGui::InputScalar(text,type,value,step,fast,format,flags); Call c{};c.op=ui033abi::InputScalar;c.text=text;c.text2=format;c.data=value;c.extra=step;c.extra2=fast;c.i[0]=type;c.i[1]=flags;Send(c);return c.result!=0;}
inline bool Combo(const char* text,int* value,const char* const items[],int count,int height=-1){if(!active)return ImGui::Combo(text,value,items,count,height); Call c{};c.op=ui033abi::Combo;c.text=text;c.data=value;c.extra=items;c.i[0]=count;c.i[1]=height;Send(c);return c.result!=0;}
inline bool ColorEdit3(const char* text,float value[3],ImGuiColorEditFlags flags=0){if(!active)return ImGui::ColorEdit3(text,value,flags); Call c{};c.op=ui033abi::ColorEdit3;c.text=text;c.data=value;c.i[0]=flags;Send(c);return c.result!=0;}
inline void PushID(const char* text){if(!active){ImGui::PushID(text);return;} Call c{};c.op=ui033abi::PushID;c.text=text;Send(c);return;}
inline void PushID(int id){if(!active){ImGui::PushID(id);return;} Call c{};c.op=ui033abi::PushIntID;c.i[0]=id;Send(c);return;}
inline void Dummy(const ImVec2& size){if(!active){ImGui::Dummy(size);return;} Call c{};c.op=ui033abi::Dummy;c.f[0]=size.x;c.f[1]=size.y;Send(c);return;}
inline ImVec2 GetContentRegionAvail(){if(!active)return ImGui::GetContentRegionAvail();Call c{};c.op=ui033abi::GetContentRegionAvail;Send(c);return ImVec2(c.f[0],c.f[1]);}
inline ImVec2 GetCursorScreenPos(){if(!active)return ImGui::GetCursorScreenPos();Call c{};c.op=ui033abi::GetCursorScreenPos;Send(c);return ImVec2(c.f[0],c.f[1]);}
inline ImVec2 GetItemRectMin(){if(!active)return ImGui::GetItemRectMin();Call c{};c.op=ui033abi::GetItemRectMin;Send(c);return ImVec2(c.f[0],c.f[1]);}
inline ImVec2 GetItemRectMax(){if(!active)return ImGui::GetItemRectMax();Call c{};c.op=ui033abi::GetItemRectMax;Send(c);return ImVec2(c.f[0],c.f[1]);}
inline ImVec2 GetWindowPos(){if(!active)return ImGui::GetWindowPos();Call c{};c.op=ui033abi::GetWindowPos;Send(c);return ImVec2(c.f[0],c.f[1]);}
inline ImVec2 GetWindowSize(){if(!active)return ImGui::GetWindowSize();Call c{};c.op=ui033abi::GetWindowSize;Send(c);return ImVec2(c.f[0],c.f[1]);}
inline void Text(const char* format,...){char b[8192];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);if(!active){ImGui::Text("%s",b);return;}Call c{};c.op=ui033abi::TextPlain;c.text=b;Send(c);}
inline void TextDisabled(const char* format,...){char b[8192];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);if(!active){ImGui::TextDisabled("%s",b);return;}Call c{};c.op=ui033abi::TextMuted;c.text=b;Send(c);}
inline void TextWrapped(const char* format,...){char b[8192];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);if(!active){ImGui::TextWrapped("%s",b);return;}Call c{};c.op=ui033abi::TextWrap;c.text=b;Send(c);}
inline void SetTooltip(const char* format,...){char b[8192];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);if(!active){ImGui::SetTooltip("%s",b);return;}Call c{};c.op=ui033abi::Tooltip;c.text=b;Send(c);}
inline void TextColored(const ImVec4& color,const char* format,...){char b[8192];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);if(!active){ImGui::TextColored(color,"%s",b);return;}Call c{};c.op=ui033abi::TextColor;c.text=b;c.f[0]=color.x;c.f[1]=color.y;c.f[2]=color.z;c.f[3]=color.w;Send(c);}
inline void SeparatorText(const char* text){if(!active){ImGui::SeparatorText(text);return;}Call c{};c.op=ui033abi::SeparatorLabel;c.text=text;Send(c);}
inline void SeparatorTextEx(ImGuiID id,const char* text,const char* end,float width){if(!active){ImGui::SeparatorTextEx(id,text,end,width);return;}SeparatorText(text);}
inline ImVec2 CalcTextSize(const char* text,const char* end=nullptr,bool hide=false,float wrap=-1){if(!active)return ImGui::CalcTextSize(text,end,hide,wrap);Call c{};c.op=ui033abi::TextSize;c.text=text;c.text2=end;c.i[0]=hide;c.f[0]=wrap;Send(c);return ImVec2(c.f[0],c.f[1]);}
inline bool SmallButton(const char* text){if(!active)return ImGui::SmallButton(text); Call c{};c.op=ui033abi::SmallButton;c.text=text;Send(c);return c.result!=0;}
inline bool IsItemDeactivatedAfterEdit(){if(!active)return ImGui::IsItemDeactivatedAfterEdit(); Call c{};c.op=ui033abi::IsItemDeactivatedAfterEdit;Send(c);return c.result!=0;}
inline void PushTextWrapPos(float value=0){if(!active){ImGui::PushTextWrapPos(value);return;} Call c{};c.op=ui033abi::PushTextWrapPos;c.f[0]=value;Send(c);return;}
inline void PopTextWrapPos(){if(!active){ImGui::PopTextWrapPos();return;} Call c{};c.op=ui033abi::PopTextWrapPos;Send(c);return;}
inline void TextUnformatted(const char* text,const char* end=nullptr){if(!active){ImGui::TextUnformatted(text,end);return;} Call c{};c.op=ui033abi::TextUnformatted;c.text=text;c.text2=end;Send(c);return;}
inline bool Begin(const char* text,bool* opened=nullptr,ImGuiWindowFlags flags=0){if(!active)return ImGui::Begin(text,opened,flags); Call c{};c.op=ui033abi::Begin;c.text=text;c.data=opened;c.i[0]=flags;Send(c);return c.result!=0;}
inline void End(){if(!active){ImGui::End();return;} Call c{};c.op=ui033abi::End;Send(c);return;}
inline void SetNextWindowPos(const ImVec2& pos,ImGuiCond cond=0,const ImVec2& pivot=ImVec2(0,0)){if(!active){ImGui::SetNextWindowPos(pos,cond,pivot);return;} Call c{};c.op=ui033abi::SetNextWindowPos;c.f[0]=pos.x;c.f[1]=pos.y;c.f[2]=pivot.x;c.f[3]=pivot.y;c.i[0]=cond;Send(c);return;}
inline void SetNextWindowSize(const ImVec2& size,ImGuiCond cond=0){if(!active){ImGui::SetNextWindowSize(size,cond);return;} Call c{};c.op=ui033abi::SetNextWindowSize;c.f[0]=size.x;c.f[1]=size.y;c.i[0]=cond;Send(c);return;}
inline void SetWindowFocus(){if(!active){ImGui::SetWindowFocus();return;} Call c{};c.op=ui033abi::SetWindowFocus;Send(c);return;}
inline void TextLinkOpenURL(const char* text,const char* url=nullptr){if(!active){ImGui::TextLinkOpenURL(text,url);return;} Call c{};c.op=ui033abi::TextLinkOpenURL;c.text=text;c.text2=url;Send(c);return;}
inline void PlotLines(const char* text,float(*get)(void*,int),void* data,int count,int offset=0,const char* overlay=nullptr,float low=FLT_MAX,float high=FLT_MAX,ImVec2 size=ImVec2(0,0)){if(!active){ImGui::PlotLines(text,get,data,count,offset,overlay,low,high,size);return;} if(count<0||count>4096)return;float samples[4096];for(int i=0;i<count;++i)samples[i]=get(data,i);Call c{};c.op=ui033abi::PlotSamples;c.text=text;c.text2=overlay;c.data=samples;c.i[0]=count;c.i[1]=offset;c.f[0]=low;c.f[1]=high;c.f[2]=size.x;c.f[3]=size.y;Send(c);}
inline void PushStyleColor(ImGuiCol idx,const ImVec4& color){if(!active){ImGui::PushStyleColor(idx,color);return;}Call c{};c.op=ui033abi::PushColor;switch(idx){case ImGuiCol_Text:c.i[0]=0;break;case ImGuiCol_WindowBg:c.i[0]=1;break;case ImGuiCol_Border:c.i[0]=2;break;case ImGuiCol_FrameBg:c.i[0]=3;break;case ImGuiCol_PlotLines:c.i[0]=4;break;case ImGuiCol_Button:c.i[0]=5;break;case ImGuiCol_ButtonHovered:c.i[0]=6;break;case ImGuiCol_ButtonActive:c.i[0]=7;break;default:c.i[0]=0;}c.f[0]=color.x;c.f[1]=color.y;c.f[2]=color.z;c.f[3]=color.w;Send(c);}
inline void PushStyleColor(ImGuiCol idx,ImU32 color){PushStyleColor(idx,ImGui::ColorConvertU32ToFloat4(color));} inline void PopStyleColor(int count=1){if(!active){ImGui::PopStyleColor(count);return;}Call c{};c.op=ui033abi::PopColor;c.i[0]=count;Send(c);}
inline void PushFontSize(float size){if(!active){ImGui::PushFontSize(size);return;}Call c{};c.op=ui033abi::PushFontPx;c.f[0]=size;Send(c);} inline void PopFontSize(){if(!active){ImGui::PopFontSize();return;}Call c{};c.op=ui033abi::PopFontPx;Send(c);} inline void PopFont(){if(!active){ImGui::PopFont();return;}PopFontSize();}
}
