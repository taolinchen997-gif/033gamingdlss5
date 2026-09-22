// Borrowed POD calls only. The ReShade module owns all ImGui objects and allocation.
#pragma once
#include <cstdint>
namespace ui033abi {
constexpr uint32_t Version=1;
enum Op:uint32_t {EndCombo,EndChild,EndTable,TableNextColumn,TableNextRow,EndDisabled,EndTooltip,PopID,PopItemWidth,TreePop,Spacing,Separator,CloseCurrentPopup,BeginTooltip,Indent,Unindent,PushItemWidth,SetNextItemWidth,SetCursorPosX,GetWindowWidth,GetCursorPosX,GetFontSize,GetTextLineHeight,BeginDisabled,IsItemHovered,IsWindowFocused,TableSetColumnIndex,SameLine,Button,BeginCombo,Selectable,Checkbox,CheckboxFlags,RadioButton,RadioInt,TreeNode,CollapsingHeader,BeginChild,BeginTable,TableSetupColumn,SliderInt,SliderFloat,InputInt,InputFloat,InputScalar,Combo,ColorEdit3,PushID,PushIntID,Dummy,GetContentRegionAvail,GetCursorScreenPos,GetItemRectMin,GetItemRectMax,GetWindowPos,GetWindowSize,TextPlain,TextMuted,TextWrap,TextColor,Tooltip,SeparatorLabel,TextSize,PushColor,PopColor,PushFontPx,PopFontPx,SmallButton,IsItemDeactivatedAfterEdit,PushTextWrapPos,PopTextWrapPos,TextUnformatted,Begin,End,SetNextWindowPos,SetNextWindowSize,SetWindowFocus,TextLinkOpenURL,PlotSamples,ViewMetrics,Count};
struct Call {uint32_t size=sizeof(Call),op=0;const char* text=nullptr;const char* text2=nullptr;void* data=nullptr;const void* extra=nullptr;const void* extra2=nullptr;int32_t i[8]={};float f[8]={};int32_t result=0;};
struct Api {uint32_t size=sizeof(Api),version=Version;int(__cdecl* invoke)(Call*)=nullptr;};
inline bool Valid(const Api* p){return p&&p->size==sizeof(Api)&&p->version==Version&&p->invoke;}
using Render=int(__cdecl*)(const Api*);
}
