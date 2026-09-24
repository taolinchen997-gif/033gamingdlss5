// Reused S52 view geometry; official ImGui API adaptation only.
#pragma once
// The owning host already provides the single current ImGui context.
#include <algorithm>
#include "yanyun_appearance.h"
#include "yanyun_text.h"

namespace studio033::visual {
inline ImVec4 palette(ImVec4 dark,ImVec4 light){return yyappearance::light?light:dark;}
inline ImVec4 surface(){return palette(ImVec4(27/255.f,32/255.f,34/255.f,1),ImVec4(.98f,.984f,.988f,1));}
inline ImVec4 selected(){return palette(ImVec4(.24f,.215f,.16f,1),ImVec4(.89f,.83f,.72f,1));}
inline ImU32 muted(){return ImGui::GetColorU32(palette(ImVec4(.69f,.71f,.70f,1),ImVec4(.36f,.40f,.43f,1)));}
inline ImU32 ink(){return ImGui::GetColorU32(palette(ImVec4(.94f,.94f,.92f,1),ImVec4(.10f,.14f,.17f,1)));}
// Local, reversible theme: the host's font, DPI and other ImGui clients are untouched.
struct Theme {
    Theme() {
        const float u=ImGui::GetFontSize()/16.f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14*u, 12*u));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7*u, 5*u));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8*u, 6*u));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8*u);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9*u);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7*u);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10*u);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, palette(ImVec4(.075f,.09f,.095f,1),ImVec4(.985f,.987f,.99f,1)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, surface());
        ImGui::PushStyleColor(ImGuiCol_Text, palette(ImVec4(.94f,.94f,.92f,1),ImVec4(.10f,.14f,.17f,1)));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, palette(ImVec4(.69f,.71f,.70f,1),ImVec4(.36f,.40f,.43f,1)));
        ImGui::PushStyleColor(ImGuiCol_Border, palette(ImVec4(.30f,.32f,.31f,.8f),ImVec4(.78f,.80f,.82f,.9f)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, surface());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, surface());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette(ImVec4(.16f,.19f,.20f,1),ImVec4(.91f,.925f,.93f,1)));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, selected());
        ImGui::PushStyleColor(ImGuiCol_Button, surface());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette(ImVec4(.16f,.19f,.20f,1),ImVec4(.91f,.925f,.93f,1)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, selected());
        ImGui::PushStyleColor(ImGuiCol_Header, surface());
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette(ImVec4(.16f,.19f,.20f,1),ImVec4(.91f,.925f,.93f,1)));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, selected());
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, palette(ImVec4(.89f,.76f,.53f,1),ImVec4(.65f,.48f,.26f,1)));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, palette(ImVec4(.96f,.86f,.67f,1),ImVec4(.55f,.37f,.16f,1)));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, palette(ImVec4(.89f,.76f,.53f,1),ImVec4(.65f,.48f,.26f,1)));
        ImGui::PushStyleColor(ImGuiCol_Separator, palette(ImVec4(.30f,.32f,.31f,.8f),ImVec4(.78f,.80f,.82f,.9f)));
        ImGui::PushStyleColor(ImGuiCol_NavCursor, palette(ImVec4(.89f,.76f,.53f,1),ImVec4(.65f,.48f,.26f,1)));
    }
    ~Theme() { ImGui::PopStyleColor(20); ImGui::PopStyleVar(9); }
    Theme(const Theme&) = delete;
    Theme& operator=(const Theme&) = delete;
};
inline ImVec4 accent() { return palette(ImVec4(.894f,.761f,.525f,1),ImVec4(.57f,.39f,.18f,1)); }
inline float unit() { return ImGui::GetFontSize()/16.0f; }
// Feedback changes paint only. No animation timer, control value or layout jump.
inline void interaction_outline(bool checkbox=false) {
    if(!ImGui::IsItemHovered()&&!ImGui::IsItemFocused()&&!ImGui::IsItemActive())return;
    auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    if(checkbox)b=ImVec2(a.x+ImGui::GetFrameHeight(),a.y+ImGui::GetFrameHeight());
    ImGui::GetWindowDrawList()->AddRect(a,b,ImGui::GetColorU32(accent()),2,0,ImGui::IsItemActive()?2.0f:1.0f);
}
inline void language_underline(bool selected) {
    interaction_outline();if(!selected)return;
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x+4,b.y-1),ImVec2(b.x-4,b.y-1),ImGui::GetColorU32(accent()),1);
}
inline bool primary_button(const char* label,ImVec2 size) {
    auto* list=ImGui::GetWindowDrawList();
    const auto a=ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x+(size.x<0?(std::max)(1.f,ImGui::GetContentRegionAvail().x+size.x):size.x),a.y+size.y);
    const float cut=(std::min)(10*unit(),(std::min)(b.x-a.x,b.y-a.y)*.25f);
    const ImVec2 points[]={{a.x+cut,a.y},{b.x-cut,a.y},{b.x,a.y+cut},{b.x,b.y-cut},
                          {b.x-cut,b.y},{a.x+cut,b.y},{a.x,b.y-cut},{a.x,a.y+cut}};
    // The ReShade table exports draw primitives but not ImDrawListSplitter.
    // Reserve the original S52 background first, then colour it after Button.
    const int first=list->VtxBuffer.Size;
    list->AddRectFilled(a,b,ImGui::GetColorU32(ImVec4(1,1,1,1)),7*unit());
    const int last=list->VtxBuffer.Size;
    for(auto color:{ImGuiCol_Button,ImGuiCol_ButtonHovered,ImGuiCol_ButtonActive})ImGui::PushStyleColor(color,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(11/255.0f,13/255.0f,16/255.0f,1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    const bool pressed=ImGui::Button(label,size);
    ImGui::PopStyleVar();ImGui::PopStyleColor(4);
    const bool hover=ImGui::IsItemHovered()||ImGui::IsItemFocused(),active=ImGui::IsItemActive();
    const ImVec4 top=active?ImVec4(.72f,.54f,.30f,1):(hover?ImVec4(245/255.0f,220/255.0f,170/255.0f,1):ImVec4(232/255.0f,204/255.0f,147/255.0f,1));
    const ImVec4 bottom=active?ImVec4(.57f,.40f,.22f,1):(hover?ImVec4(208/255.0f,160/255.0f,92/255.0f,1):ImVec4(183/255.0f,138/255.0f,77/255.0f,1));
    for(int i=first;i<last;++i){
        auto& vertex=list->VtxBuffer[i];const float t=std::clamp((vertex.pos.y-a.y)/(std::max)(1.0f,b.y-a.y),0.0f,1.0f);
        const auto color=ImGui::ColorConvertFloat4ToU32(ImVec4(top.x+(bottom.x-top.x)*t,top.y+(bottom.y-top.y)*t,top.z+(bottom.z-top.z)*t,1));
        vertex.col=(vertex.col&IM_COL32_A_MASK)|(color&~IM_COL32_A_MASK);
    }
    list->AddRect(a,b,ImGui::GetColorU32(accent()),7*unit(),0,hover?1.5f:1.0f);
    return pressed;
}
}

#pragma once

#include <algorithm>

namespace studio033::visual {
// Original continuous, chamfered 033 lettering; no font or third-party emblem.
// S35: the lettering alone, at any origin, scale and colour (the 033特调 badge reuses it).
inline ImU32 brand_colour(){return yyappearance::light?IM_COL32(30,35,38,255):IM_COL32(233,212,171,255);}
inline void brand_letters(ImDrawList* list,ImVec2 origin,float scale,ImU32 gold,ImU32 edge){
    auto point=[&](float x,float y){return ImVec2(origin.x+x*scale,origin.y+y*scale);};
    // Adjoining quadrilaterals leave a real open counter in the zero.
    const ImVec2 outer[]={{10,0},{38,0},{48,10},{48,44},{38,54},{10,54},{0,44},{0,10}};
    const ImVec2 inner[]={{15,11},{33,11},{36,14},{36,40},{33,43},{15,43},{12,40},{12,14}};
    // Only exterior edges need antialiasing; AA on shared quad edges leaves seams.
    const auto flags=list->Flags;list->Flags&=~ImDrawListFlags_AntiAliasedFill;
    for(int i=0;i<8;++i){
        const int j=(i+1)%8;
        const ImVec2 quad[]={point(outer[i].x,outer[i].y),point(outer[j].x,outer[j].y),
                            point(inner[j].x,inner[j].y),point(inner[i].x,inner[i].y)};
        list->AddConvexPolyFilled(quad,4,gold);
    }
    list->Flags=flags;
    ImVec2 outline[8],counter[8];
    for(int i=0;i<8;++i){outline[i]=point(outer[i].x,outer[i].y);counter[i]=point(inner[i].x,inner[i].y);}
    list->AddPolyline(outline,8,gold,ImDrawFlags_Closed,scale);
    list->AddPolyline(counter,8,gold,ImDrawFlags_Closed,scale);
    // Concave silhouettes give each 3 a connected spine and diagonal waist.
    const ImVec2 three[]={{0,0},{49,0},{49,10},{36,22},{49,32},{49,44},{39,54},{0,54},
                         {12,42},{33,42},{36,39},{36,33},{16,33},{26,22},{35,13},{9,13}};
    for(float x:{55.0f,110.0f}){
        ImVec2 polygon[16];for(int i=0;i<16;++i)polygon[i]=point(x+three[i].x,three[i].y);
        list->AddConcavePolyFilled(polygon,16,gold);
        list->AddLine(point(x+1,1),point(x+48,1),edge,scale);
    }
    list->AddLine(point(10,1),point(38,1),edge,scale);
}
inline void brand_mark(float available) {
    const auto origin=ImGui::GetCursorScreenPos();
    const float scale=(std::min)(available/164.0f,ImGui::GetFontSize()/16.0f);
    if(scale<=0)return;
    const auto edge=yyappearance::light?IM_COL32(70,73,73,150):IM_COL32(244,226,193,150);
    brand_letters(ImGui::GetWindowDrawList(),ImVec2(origin.x,origin.y+4*scale),scale,brand_colour(),edge);
    ImGui::Dummy(ImVec2(164*scale,64*scale));
}
// S35 (owner 2026-09-24): 「启用033特调，放在大标志033旁边，跟033这个logo风格差不多」. Chamfered
// like the lettering's zero, in the lettering's colour, with the same vector 033: filled when on,
// outlined when off, and the panel's on/off switch at the right.
inline float tuning_text_width(float size,const char* label){
    ImGui::PushFont(nullptr,size);const float width=ImGui::CalcTextSize(label).x;ImGui::PopFont();return width;
}
inline float tuning_badge_width(float h,const char* label){
    return h*.36f+159*h*.44f/54+h*.2f+tuning_text_width(h*.5f,label)+h*.3f+h*.8f+h*.3f;
}
inline bool tuning_badge(const char* id,const char* label,bool on,float h){
    const float u=unit(),w=tuning_badge_width(h,label),cut=h*.24f;
    const auto a=ImGui::GetCursorScreenPos();const ImVec2 b(a.x+w,a.y+h);
    const bool pressed=ImGui::InvisibleButton(id,ImVec2(w,h));
    const bool hover=ImGui::IsItemHovered()||ImGui::IsItemFocused(),held=ImGui::IsItemActive();
    auto* dl=ImGui::GetWindowDrawList();const ImU32 ink=brand_colour(),paper=ImGui::GetColorU32(surface());
    const ImVec2 shape[]={{a.x+cut,a.y},{b.x-cut,a.y},{b.x,a.y+cut},{b.x,b.y-cut},{b.x-cut,b.y},{a.x+cut,b.y},{a.x,b.y-cut},{a.x,a.y+cut}};
    if(on)dl->AddConvexPolyFilled(shape,8,ink);
    else if(hover||held)dl->AddConvexPolyFilled(shape,8,ImGui::GetColorU32(held?selected():palette(ImVec4(.16f,.19f,.20f,1),ImVec4(.91f,.925f,.93f,1))));
    dl->AddPolyline(shape,8,on&&hover?ImGui::GetColorU32(accent()):ink,ImDrawFlags_Closed,(hover?2.f:1.5f)*u);
    const ImU32 content=on?paper:ink;const float s=h*.44f/54;float x=a.x+h*.36f;
    brand_letters(dl,ImVec2(x,a.y+(h-54*s)*.5f),s,content,0);
    x+=159*s+h*.2f;
    const float size=h*.5f;ImGui::PushFont(nullptr,size);const ImVec2 text=ImGui::CalcTextSize(label);ImGui::PopFont();
    const ImVec2 at(x,a.y+(h-text.y)*.5f);
    // Drawn twice half a unit apart: the weight of the lettering beside it.
    dl->AddText(ImGui::GetFont(),size,at,content,label);dl->AddText(ImGui::GetFont(),size,ImVec2(at.x+.5f*u,at.y),content,label);
    x+=text.x+h*.3f;const float sw=h*.8f,sh=h*.47f;const ImVec2 t(x,a.y+(h-sh)*.5f);
    dl->AddRectFilled(t,ImVec2(t.x+sw,t.y+sh),on?paper:ImGui::GetColorU32(palette(ImVec4(.30f,.33f,.35f,1),ImVec4(.73f,.75f,.77f,1))),sh*.5f);
    dl->AddCircleFilled(ImVec2(on?t.x+sw-sh*.5f:t.x+sh*.5f,t.y+sh*.5f),sh*.4f,on?ink:IM_COL32(245,245,242,255),24);
    return pressed;
}
}
