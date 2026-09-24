#include "release_version.h"
// 033 presentation layer. Uses the same controls ABI as the running renderer.
// No render algorithms, runtime ownership or persistent settings live here.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "nr_controls_abi.h"
#include "panel_c4_visual.h"
#include "panel_yanyun_visual.h"
#include "panel_draft_state.h"
#include "yanyun_hotkey.h"

namespace studio033 {
using namespace nrcontrolsabi;
// Build capability for the exact model pinned by this test package.
// A different model requires renewed capability evidence, not reused labels.
inline constexpr char SinglePresetModelSha256[]="6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927";
constexpr ImU32 Ink=IM_COL32(235,234,228,255), Muted=IM_COL32(157,163,166,255);
constexpr ImU32 Surface=IM_COL32(29,33,36,255), Line=IM_COL32(53,59,62,255);
constexpr ImU32 Accent=IM_COL32(228,194,134,255), Green=IM_COL32(151,197,169,255);
struct State {
    float values[Count]{};
    float applied[Count]{};
    bool controlsFresh=true;unsigned controlPending=0;
    bool modelActive=false,mfgPacingReady=false,mfgBlocked=false,mfgOff=false;
    bool portraitAvailable=false,portraitUnavailable=false;unsigned portraitFaces=0;
    PortraitStatus portraitStatus=PortraitStatus::Off;
    unsigned portraitCaptureW=0,portraitCaptureH=0,portraitAgeMs=0;float portraitCpuMs=0;
    bool running=false,available=true,gradeAvailable=true,paused=false,failed=false,saved=false,mfgAvailable=false;
    bool settingsReady=false,settingsPending=false,settingsFailed=false;
    unsigned width=0,height=0,presetBuilt=0,mfgRequested=0,mfgAccepted=0;
    unsigned layerModelW[3]{},layerModelH[3]{};
    bool recoveryReady=false,recoveryWriteFailed=false;
    bool modelPending=false;
    unsigned activeLayers=0,cachedLayers=0;
    char modelWait[192]={};
    unsigned long long frames=0;
    float nrMs=-1.f,gpuFrameMs=-1.f;
    exposurepolicy::Status whiteStatus=exposurepolicy::Status::Waiting;
    float effectiveWhite=3.16f;
    int whiteEncoding=-1;
    unsigned hotkey=0x7A;
    uint64_t exposureRecorded=0,exposureHeld=0,exposureBypass=0;
    // 2026-09-12 移植自 P1 树：随包第三方转接件 nvidia_mfg_bridge 的文件事实与它 INI 里的值。
    unsigned gpuGen=0;bool gameFrameGen=false,bridgePresent=false,bridgeLegacy=false,bridgeProvider=false,bridgeSaved=false,bridgeSaveFailed=false;
    int bridgeMode=-1,bridgeForce=0;char bridgeLoader[32]={};
    bool tuningOn=false; // S35 033特调 (the before-file exists)
    unsigned hags=0,osBuild=0; // S38: Windows GPU scheduling 0 unknown / 1 off / 2 on; Windows build (0 unknown)
};
inline constexpr Id Appearance[]={Style,Intensity,Structure,LocalTone,AutoMask,UiCorrect,Preset,GlobalTone,Skin};
struct View {bool maximized=false;ImVec2 restoreSize{},restorePos{};int page=1,layer=0,srLayer=0;paneldraft::Fields<Count> drafts;};
struct Edits {uint64_t changed=0;uint64_t revisions[Count]{};bool maximize=false,toggle=false,neutral=false,portrait=false,save=false,recover=false,mfg=false,close=false,bridge=false,resetHistory=false,tuning=false;unsigned multiplier=0;int bridgeMode=-1,bridgeForce=-1,hotkey=-1;};
// S37: the NR key's name for the footer lines (set from the controls in Header), and whether the
// panel is waiting for the player to press a new one.
inline char nrKeyName[16]="F11";
inline bool keyCapture=false;
// S39: why the key just pressed while waiting cannot be the NR key (null: nothing to say).
inline const char* keyRefusal=nullptr;
// Preview harness observes real item rectangles; production leaves this null.
using Observe=void(*)(const char*,ImVec2,ImVec2);
inline Observe observer=nullptr;
inline void ObserveItem(const char* id){if(observer)observer(id,ImGui::GetItemRectMin(),ImGui::GetItemRectMax());}
inline float F(){return ImGui::GetFontSize();}
inline void Space(float amount=.7f){ImGui::Dummy(ImVec2(0,F()*amount));}
inline void Tip(const char* text){if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",text);}
using Theme=visual::Theme;
// S23 (user): the author credit is set larger and in the accent colour.
inline float AuthorSize(){return F()*1.25f;}
// S23 (user): the panel opens with everything in view -- no dragging or
// scrolling. For a few frames after the panel appears, after a page change and
// after the player's own layout actions (whole / regional, cards, layer
// accordions, saved records -- RequestFit), the owning window takes the body's
// content height (kept on the screen, moved up if needed). A size the user
// drags stays until the next of those events.
// S25 (user 2026-09-22 「面板会闪烁…撕裂」): any other change of the content
// height -- a hint or status line appearing, disappearing or rewrapping -- only
// ever GROWS the window. S23 fitted both ways on every such change, so hints that
// come and go while sliders move made the panel jump up and down.
struct Fit {int lastFrame=-10,frames=0,grow=0,page=-1;bool maximized=false,scrollbar=false;float overflow=0,content=-1;};
inline Fit fit;
inline void RequestFit(){fit.frames=4;}
// New {height, top} for the owning window: its height plus the body's missing
// (+) or spare (-) content height, within the display and an 8 px margin.
inline ImVec2 FitWindow(float height,float delta,float top,float display){
    const float margin=8.f,room=(std::max)(1.f,display-2*margin),minimum=(std::min)(240.f,room);
    const float h=std::clamp(height+delta,minimum,room);
    return ImVec2(h,(std::min)(top,(std::max)(margin,display-margin-h)));
}
inline bool BeginBody(){
    const float width=ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(nullptr,AuthorSize());const float authorHeight=ImGui::CalcTextSize(L("作者 B站@热心网友033"),nullptr,false,width).y;ImGui::PopFont();
    const float height=(std::max)(1.f,ImGui::GetContentRegionAvail().y-authorHeight-2*ImGui::GetStyle().ItemSpacing.y-2);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));
    const bool visible=ImGui::BeginChild("033_render_studio",ImVec2(width,height),ImGuiChildFlags_None,ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();return visible;
}
inline void EndBody(){
    // Missing (+) or spare (-) body height: the cursor ends below the last item.
    const float content=ImGui::GetCursorPosY();fit.overflow=content-ImGui::GetWindowHeight();
    // Other content height changes may only grow the window (S25), and never the
    // rewrap caused by the body's own scrollbar coming or going (a height the
    // user dragged would snap back).
    const bool scrollbar=ImGui::GetScrollMaxY()>0;
    if(std::fabs(content-fit.content)>2.f){if(fit.content>=0&&scrollbar==fit.scrollbar)fit.grow=4;fit.content=content;}
    fit.scrollbar=scrollbar;
    ImGui::EndChild();ImGui::Separator();
    const float w=ImGui::GetContentRegionAvail().x;const char* author=L("作者 B站@热心网友033");
    ImGui::PushFont(nullptr,AuthorSize());const ImVec2 authorSize=ImGui::CalcTextSize(author);ImGui::PopFont();
    const float lineY=ImGui::GetCursorPosY();const bool oneLine=w>=authorSize.x+330*visual::unit();
    if(oneLine){
        // The hotkey line keeps the body size, centred on the larger author line.
        const float x=ImGui::GetCursorPosX();ImGui::SetCursorPosY(lineY+(std::max)(0.f,(authorSize.y-F())*.5f));
        ImGui::TextDisabled("%s  NR    Shift+F9  %s",nrKeyName,yyappearance::Text("监控","Monitor"));ImGui::SameLine();ImGui::SetCursorPos(ImVec2(x+w-authorSize.x-1,lineY));
    }
    ImGui::PushFont(nullptr,AuthorSize());ImGui::PushStyleColor(ImGuiCol_Text,visual::accent());
    // Measured to fit: never wrap it at the edge (float rounding split it in two).
    if(oneLine)ImGui::TextUnformatted(author);else ImGui::TextWrapped("%s",author);ObserveItem("author_fixed");
    ImGui::PopStyleColor();ImGui::PopFont();visual::approved_frame();
    const int frame=ImGui::GetFrameCount();
    if(frame!=fit.lastFrame+1)RequestFit(); // the panel (re)appeared
    fit.lastFrame=frame;
    if(fit.frames>0||fit.grow>0){
        const bool both=fit.frames>0;if(fit.frames>0)--fit.frames;if(fit.grow>0)--fit.grow;
        if(!fit.maximized&&(both?std::fabs(fit.overflow)>2.f:fit.overflow>2.f)){
            const auto size=ImGui::GetWindowSize(),pos=ImGui::GetWindowPos();
            const auto target=FitWindow(size.y,fit.overflow,pos.y,ImGui::GetIO().DisplaySize.y);
            if(std::fabs(target.x-size.y)>.5f)ImGui::SetWindowSize(ImVec2(size.x,target.x));
            if(target.y<pos.y-.5f)ImGui::SetWindowPos(ImVec2(pos.x,target.y));
        }
    }
}
inline void ApplyWindowEdits(View& view,const Edits& edits){
    if(!edits.maximize)return;
    // This host has one game viewport (ViewportsEnable is absent). DisplaySize
    // is exported by the host's ImGui API; GetMainViewport is not.
    if(!view.maximized){view.restoreSize=ImGui::GetWindowSize();view.restorePos=ImGui::GetWindowPos();const auto size=ImGui::GetIO().DisplaySize;ImGui::SetWindowPos(ImVec2(8,8));ImGui::SetWindowSize(ImVec2((std::max)(1.f,size.x-16),(std::max)(1.f,size.y-16)));}
    else{ImGui::SetWindowPos(view.restorePos);ImGui::SetWindowSize(view.restoreSize);}
    view.maximized=!view.maximized;
}
inline void Heading(const char* title,const char* description=nullptr){
    ImGui::PushFont(nullptr,F()*1.12f);ImGui::TextUnformatted(title);ImGui::PopFont();
    if(description){ImGui::PushStyleColor(ImGuiCol_Text,ImColor(visual::muted()).Value);ImGui::TextWrapped("%s",description);ImGui::PopStyleColor();}
    Space(.15f);
}
inline void BeginCard(const char* id){ImGui::BeginChild(id,ImVec2(0,0),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);}
inline void EndCard(){ImGui::EndChild();}
inline void Changed(Edits& e,Id id){e.changed|=uint64_t(1)<<id;}
// Real caller uses the controls queue. Rejected fields remain retryable drafts.
inline void ObserveDraft(const State& s,View& v,Id id){v.drafts.Observe(id,s.values[id],s.controlsFresh,s.controlPending);}
inline void StageDraft(State& s,View& v,Edits& e,Id id){
    if(!v.drafts.NeedsSubmit(id))return;
    s.values[id]=v.drafts.Value(id);Changed(e,id);e.revisions[id]=v.drafts.Revision(id);
}
template<class Submit> inline void SubmitEdits(const State& s,View& v,const Edits& e,Submit&& submit){
    for(uint32_t id=0;id<Count;++id){const uint64_t bit=uint64_t(1)<<id;if(!(e.changed&bit))continue;
        const bool accepted=submit(id,s.values[id]);
        if(v.drafts.Managed(id)){if(accepted)v.drafts.Accepted(id,e.revisions[id],s.values[id]);else v.drafts.Failed(id,e.revisions[id]);}
    }
}
inline bool Range(const char* label,float& value,float lo,float hi,const char* format,float step=.01f,const char* observed=nullptr){
    const float f=F(),w=(std::max)(1.f,ImGui::GetContentRegionAvail().x),gap=ImGui::GetStyle().ItemSpacing.x;
    const float x=ImGui::GetCursorPosX(),labelWidth=(std::max)(f*5.5f,ImGui::CalcTextSize(label).x+gap);
    const bool horizontal=w>=14*f;ImGui::AlignTextToFramePadding();ImGui::TextColored(ImColor(visual::muted()),"%s",label);
    if(horizontal){ImGui::SameLine();ImGui::SetCursorPosX(x+labelWidth);}
    const float controls=horizontal?w-labelWidth:w,entry=(std::min)(controls,3.1f*f),slider=controls-entry-gap;
    bool changed=false;
    if(slider>=3*f){
        ImGui::SetNextItemWidth(slider);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
        for(auto col:{ImGuiCol_FrameBg,ImGuiCol_FrameBgHovered,ImGuiCol_FrameBgActive,ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive})ImGui::PushStyleColor(col,ImVec4(0,0,0,0));
        changed=ImGui::SliderFloat("##track",&value,lo,hi,"",ImGuiSliderFlags_NoInput|ImGuiSliderFlags_AlwaysClamp);
        ObserveItem(label);if(observed)ObserveItem(observed);ImGui::PopStyleColor(5);ImGui::PopStyleVar();
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const float half=4*visual::unit(),y=std::floor((a.y+b.y)*.5f);
        const float left=a.x+half,right=b.x-half,cx=left+(std::clamp)((value-lo)/(hi-lo),0.f,1.f)*(right-left);
        auto* dl=ImGui::GetWindowDrawList();dl->AddLine(ImVec2(left,y),ImVec2(right,y),ImGui::GetColorU32(ImGuiCol_Border),3*visual::unit());
        dl->AddLine(ImVec2(left,y),ImVec2(cx,y),ImGui::GetColorU32(visual::accent()),3*visual::unit());
        dl->AddCircleFilled(ImVec2(cx,y),4.5f*visual::unit(),yyappearance::light?ImGui::GetColorU32(ImGuiCol_SliderGrab):IM_COL32(239,239,232,255),20);
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(slider>=3*f?entry:controls);
    changed|=ImGui::DragFloat("##number",&value,step,lo,hi,format,ImGuiSliderFlags_AlwaysClamp);
    char numberId[128];std::snprintf(numberId,sizeof(numberId),"number_%s",label);ObserveItem(numberId);
    if(observed){std::snprintf(numberId,sizeof(numberId),"number_%s",observed);ObserveItem(numberId);}
    if(slider<3*f){ObserveItem(label);if(observed)ObserveItem(observed);}visual::interaction_outline();Tip(L("拖动数值，或 Ctrl 点击输入并按 Enter 确认。"));return changed;
}
inline void Slider(State& s,Edits& e,Id id,const char* label,const char* format="%.2f",const char* hint=nullptr){
    ImGui::PushID(int(id));const auto& d=definitions[id];
    const bool integer=d.kind==Integer;
    if(Range(label,s.values[id],d.minimum,d.maximum,format,integer?1.f:.01f)){
        // The unlabelled float track can yield fractions even with a %.0f
        // number field. Match the strict integer ABI before submitting.
        if(integer)s.values[id]=std::clamp(std::round(s.values[id]),d.minimum,d.maximum);
        Changed(e,id);
    }
    if(hint)Tip(hint);ImGui::PopID();
}
inline void Check(State& s,Edits& e,Id id,const char* label){bool on=s.values[id]!=0;
    if(ImGui::Checkbox(label,&on)){s.values[id]=on?1.f:0.f;Changed(e,id);}ObserveItem(label);
}
// Header window controls, drawn with lines in the text colour (accent on hover):
// 0 minimize, 1 maximize, 2 close.
inline bool HeaderGlyph(const char* key,int glyph){
    const float h=ImGui::GetFrameHeight();char id[40];std::snprintf(id,sizeof(id),"##%s",key);
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    const bool pressed=ImGui::Button(id,ImVec2(h,h));ObserveItem(key);
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();auto* dl=ImGui::GetWindowDrawList();
    const auto c=ImGui::GetColorU32(ImGui::IsItemHovered()?visual::accent():ImGui::GetStyleColorVec4(ImGuiCol_Text));
    const float in=h*.32f,t=(std::max)(1.f,h*.06f),mid=(a.y+b.y)*.5f;
    if(glyph==0)dl->AddLine(ImVec2(a.x+in,mid),ImVec2(b.x-in,mid),c,t);
    else if(glyph==1)dl->AddRect(ImVec2(a.x+in,a.y+in),ImVec2(b.x-in,b.y-in),c,0,0,t);
    else{dl->AddLine(ImVec2(a.x+in,a.y+in),ImVec2(b.x-in,b.y-in),c,t);dl->AddLine(ImVec2(a.x+in,b.y-in),ImVec2(b.x-in,a.y+in),c,t);}
    ImGui::PopStyleVar();ImGui::PopStyleColor();return pressed;
}
inline float HeaderSegmentWidth(const char* label){return ImGui::CalcTextSize(label).x+F()*1.1f;}
inline bool HeaderSegment(const char* key,const char* label,bool selected){
    // A hovered selected segment keeps its pill (the hover fill would cover it:
    // white text on a pale fill in the light theme).
    if(selected)ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0,0,0,0));
    const bool pressed=visual::segment(label,selected,ImVec2(HeaderSegmentWidth(label),ImGui::GetFrameHeight()));ObserveItem(key);
    if(selected)ImGui::PopStyleColor();return pressed;
}
inline void Header(State& s,View& view,Edits& e){
    {const char* key=hotkey033::Name(int(s.hotkey));std::snprintf(nrKeyName,sizeof(nrKeyName),"%s",key?L(key):"F11");}
    fit.maximized=view.maximized;if(view.page!=fit.page){fit.page=view.page;RequestFit();}
    const float f=F(),w=ImGui::GetContentRegionAvail().x;auto* dl=ImGui::GetWindowDrawList();
    const float u=visual::unit(),x0=ImGui::GetCursorPosX(),gap=.6f*f,glyph=ImGui::GetFrameHeight();
    // The pre-S1 vector 033 lettering on the left and real controls on the right;
    // no painted banner (the user found its baked typography cheap).
    const char* dark=yyappearance::Text("黑色","Dark");const char* light=yyappearance::Text("白色","Light");
    const float controls=HeaderSegmentWidth(dark)+HeaderSegmentWidth(light)+gap+HeaderSegmentWidth("中")+HeaderSegmentWidth("EN")+gap+3*glyph;
    const bool sameRow=w-controls-gap>=90*u; // otherwise the controls take their own row
    // S35 (owner 2026-09-24): 「启用033特调，放在大标志033旁边」. Beside the lettering when both
    // fit, otherwise on its own row under the edition name.
    const char* tuningLabel=L("特调");const float badgeH=34*u,badgeW=visual::tuning_badge_width(badgeH,tuningLabel);
    const float room=sameRow?w-controls-gap:w;const bool badgeBeside=room-badgeW-gap>=100*u;
    const float logo=(std::max)(1.f,(std::min)(164*u,badgeBeside?room-badgeW-gap:room));
    auto badge=[&]{
        if(visual::tuning_badge("##yy_033_tuning",tuningLabel,s.tuningOn,badgeH))e.tuning=true;ObserveItem("yy_033_tuning");
        Tip(s.tuningOn?L("033特调已启用。再点一下关闭，回到开启前你自己的设置；开启期间改过的参数不保留。"):
            L("启用033特调：一键换成 033 调好的整套参数（前置、SR、NR，超分模型 L、帧生成 6×；20、30 系超分模型用 K，帧生成倍率不动）。再点一下回到开启前你自己的设置。"));
    };
    visual::brand_mark(logo);
    if(badgeBeside){
        // Centred on the lettering, which brand_mark draws 4..58 units down its 64-unit row.
        const float scale=(std::min)(logo/164.f,u),drop=(std::max)(0.f,31*scale-badgeH*.5f-ImGui::GetStyle().ItemSpacing.y);
        ImGui::SameLine(0,gap);ImGui::BeginGroup();ImGui::Dummy(ImVec2(1,drop));badge();ImGui::EndGroup();
    }
    if(sameRow)ImGui::SameLine(x0+w-controls,0);else ImGui::SetCursorPosX(x0+(std::max)(0.f,w-controls));
    if(HeaderSegment("theme_dark",dark,!yyappearance::light))yyappearance::SetLight(false);ImGui::SameLine(0,0);
    if(HeaderSegment("theme_light",light,yyappearance::light))yyappearance::SetLight(true);ImGui::SameLine(0,gap);
    if(HeaderSegment("language_zh","中",!yyappearance::english))yyappearance::SetEnglish(false);ImGui::SameLine(0,0);
    if(HeaderSegment("language_en","EN",yyappearance::english))yyappearance::SetEnglish(true);ImGui::SameLine(0,gap);
    if(HeaderGlyph("minimize_panel",0))e.close=true;ImGui::SameLine(0,0);
    // The maximization request is applied to the owning window after EndBody.
    if(HeaderGlyph("maximize_panel",1))e.maximize=true;ImGui::SameLine(0,0);
    if(HeaderGlyph("close_panel",2))e.close=true;
    ImGui::TextDisabled("%s",yyappearance::Text(K033_PRODUCT_VERSION,"YanYun custom edition"));
    if(!badgeBeside){Space(.1f);badge();}
    Space(.2f);
    const char* names[]={yyappearance::Text("前置","Color"),"SR","NR","FG"};static const int pages[]={0,5,1,2};
    const float bw=(std::min)(82*u,w/4);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
    for(int i=0;i<4;++i){if(i)ImGui::SameLine(0,0);ImGui::PushID(i);const bool selected=view.page==pages[i];
        if(selected)ImGui::PushStyleColor(ImGuiCol_Text,visual::accent());
        if(ImGui::Button(names[i],ImVec2(bw,36*u)))view.page=pages[i];
        char id[16];std::snprintf(id,sizeof(id),"page_%d",i);ObserveItem(id);const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        if(selected)dl->AddLine(ImVec2(a.x+16*u,b.y),ImVec2(b.x-16*u,b.y),ImGui::GetColorU32(visual::accent()),2*u);
        if(selected)ImGui::PopStyleColor();ImGui::PopID();}
    ImGui::PopStyleColor();ImGui::PopStyleVar(2);
    ImGui::Separator();Space(.3f);
    if(s.paused){
        Heading(L("神经渲染已暂停"),s.recoveryReady?L("已安排下次恢复。请正常退出游戏后重新启动，当前这局仍暂停。"):L("检测到连续未完成的启动记录，本次暂未开启神经渲染。"));
        if(!s.recoveryReady){if(ImGui::Button(L("下次启动恢复")))e.recover=true;ObserveItem("recover_nr");}
        if(s.recoveryWriteFailed)ImGui::TextWrapped(L("恢复记录未能保存，请检查游戏目录写入权限后重试。"));
        Space();
    }
}
inline bool StyleChoice(int index,int selected,float width){
    const char* titles[]={L("中性"),L("自然"),L("柔和"),L("动漫色阶")};
    const float f=F();ImGui::PushID(index);
    ImGui::PushStyleColor(ImGuiCol_Button,ImColor(index==selected?IM_COL32(43,54,62,255):IM_COL32(36,41,44,255)).Value);
    ImGui::PushStyleColor(ImGuiCol_Text,ImColor(index==selected?Accent:Ink).Value);
    bool clicked=ImGui::Button(titles[index],ImVec2(width,f*2));
    char id[20];std::snprintf(id,sizeof(id),"style_%d",index);ObserveItem(id);
    ImGui::PopStyleColor(2);ImGui::PopID();return clicked;
}
inline void LightNatural(State& s,Edits& e){
    // Exact S52 light-natural shortcut; it only edits the grade group.
    const Id ids[]={Grade,PreStyle,Exposure,Contrast,Saturation,Warmth,Tint,Highlights,PreStyleStrength};
    const float values[]={1,0,0,1.03f,1.02f,0,0,.04f,1};
    for(unsigned i=0;i<9;++i){s.values[ids[i]]=values[i];Changed(e,ids[i]);}
}
inline void Picture(State& s,Edits& e){
    const float f=F();
    Heading("前置调色");
    BeginCard("look");
    ImGui::BeginDisabled(!s.gradeAvailable);Check(s,e,Grade,"启用调色");
    ImGui::BeginDisabled(s.values[Grade]==0);Space(.2f);
    const float w=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemSpacing.x;
    const int columns=w>=f*46?4:2;const float cw=(w-(columns-1)*gap)/columns;
    for(int i=0;i<4;++i){if(i%columns)ImGui::SameLine();if(StyleChoice(i,int(s.values[PreStyle]),cw)){s.values[PreStyle]=float(i);Changed(e,PreStyle);}}
    Space(.45f);ImGui::BeginDisabled(s.values[PreStyle]==0);
    Slider(s,e,PreStyleStrength,L("风格强度"),"%.2f","只调整前置风格的份量。不会增加神经模型遍数。");ImGui::EndDisabled();
    ImGui::EndDisabled();ImGui::EndDisabled();EndCard();Space(.85f);
    Heading("色彩");BeginCard("grade");
    ImGui::BeginDisabled(!s.gradeAvailable||s.values[Grade]==0);
    const int n=ImGui::GetContentRegionAvail().x>f*29?2:1;
    if(ImGui::BeginTable("grade_grid",n,ImGuiTableFlags_SizingStretchSame)){
        ImGui::TableNextColumn();Slider(s,e,Exposure,L("曝光"),"%+.2f EV");
        ImGui::TableNextColumn();Slider(s,e,Contrast,L("对比度"));
        ImGui::TableNextColumn();Slider(s,e,Saturation,L("饱和度"));
        ImGui::TableNextColumn();Slider(s,e,Warmth,L("冷暖"),"%+.2f");
        ImGui::TableNextColumn();Slider(s,e,Tint,"绿紫偏色","%+.2f");
        ImGui::TableNextColumn();Slider(s,e,Highlights,"高光压缩");
        ImGui::EndTable();}
    Space(.2f);ImGui::Separator();Space(.3f);
    ImGui::EndDisabled();Space(.25f);ImGui::BeginDisabled(!s.gradeAvailable);
    if(ImGui::Button("恢复中性调色"))e.neutral=true;ObserveItem("reset_grade");
    Tip("复位前置风格和调色数值，开启前置调教；保留模型与帧生成设置。");
    if(ImGui::GetContentRegionAvail().x>=f*22)ImGui::SameLine();
    if(ImGui::Button("轻度自然"))LightNatural(s,e);ObserveItem("light_natural");
    Tip("恢复中性后设置对比度 1.03、饱和度 1.02、高光压缩 0.04；只调整前置画面。");
    ImGui::EndDisabled();EndCard();
    if(!s.gradeAvailable){Space();ImGui::TextWrapped("当前渲染路径尚未接入前置调色。原生参数请在设置中查看。");}

}
inline void DraftSlider(const char* label,int& value,int lo,int hi,const char* fmt,const char* observed=nullptr){
    ImGui::PushID(label);float number=float(value);if(Range(label,number,float(lo),float(hi),lo==1?"%.0f 层":"%.0f%%",1.f,observed))value=int(std::round(number));ImGui::PopID();
}
struct LayerCard {
    ImVec2 origin;float width,pad;ImDrawList* dl;int backgroundFirst,backgroundLast;bool expanded,contained;
    LayerCard(const char* key,const char* observed,int number,const char* tag,int& open,int visibleOpen,bool bounded=false):origin(ImGui::GetCursorScreenPos()),width(ImGui::GetContentRegionAvail().x),pad((std::min)(10*visual::unit(),width*.06f)),dl(ImGui::GetWindowDrawList()),contained(bounded){
        ImGui::PushID(key);backgroundFirst=dl->VtxBuffer.Size;
        dl->AddRectFilled(origin,ImVec2(origin.x+width,origin.y+12),visual::card_colour(),6);
        backgroundLast=dl->VtxBuffer.Size;
        ImGui::BeginGroup();ImGui::Indent(pad);ImGui::Dummy(ImVec2(0,1*visual::unit()));
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
        if(ImGui::Button("##layer",ImVec2((std::max)(1.f,width-2*pad),27*visual::unit())))open=open==number?-1:number;
        ObserveItem(observed);ImGui::PopStyleVar();ImGui::PopStyleColor();expanded=visibleOpen==number;
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const float y=(a.y+b.y-F())*.5f;
        char caption[48];std::snprintf(caption,sizeof(caption),yyappearance::Text(L("第 %d 层"),"Layer %d"),number+1);dl->AddText(ImVec2(a.x+2,y),ImGui::GetColorU32(ImGuiCol_Text),caption);
        if(width>=12*F()){
            const float u=visual::unit(),right=b.x-28*u;const bool on=std::strcmp(tag,L("启用"))==0;
            if(on||std::strcmp(tag,L("未启用"))==0){
                const auto toggle=ImVec2(right-78*u,a.y+7*u);
                dl->AddRectFilled(toggle,ImVec2(toggle.x+27*u,toggle.y+16*u),ImGui::GetColorU32(on?visual::accent():visual::palette(ImVec4(.30f,.33f,.35f,1),ImVec4(.73f,.75f,.77f,1))),8*u);
                dl->AddCircleFilled(ImVec2(toggle.x+(on?19.f:8.f)*u,toggle.y+8*u),6.5f*u,IM_COL32(245,245,242,255),24);
                dl->AddText(ImGui::GetFont(),12*u,ImVec2(right-45*u,y+2*u),visual::ink(),tag);
            }else dl->AddText(ImVec2(right-ImGui::CalcTextSize(tag).x,y),visual::ink(),tag);
        }
        const float cx=b.x-10*visual::unit(),cy=(a.y+b.y)*.5f,z=5*visual::unit(),d=expanded?-1.f:1.f;
        dl->AddLine(ImVec2(cx-z,cy-d*z*.5f),ImVec2(cx,cy+d*z*.5f),ImGui::GetColorU32(visual::accent()),1.5f);dl->AddLine(ImVec2(cx,cy+d*z*.5f),ImVec2(cx+z,cy-d*z*.5f),ImGui::GetColorU32(visual::accent()),1.5f);
        if(expanded){dl->AddLine(ImVec2(origin.x+1,b.y+3),ImVec2(origin.x+width-1,b.y+3),ImGui::GetColorU32(ImGuiCol_Border));ImGui::Dummy(ImVec2(0,(contained?7:4)*visual::unit()));
            if(contained)ImGui::BeginChild("layer_content",ImVec2(width-2*pad,0),ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        }
    }
    ~LayerCard(){if(expanded&&contained)ImGui::EndChild();ImGui::Dummy(ImVec2(0,1*visual::unit()));ImGui::Unindent(pad);ImGui::EndGroup();const float bottom=ImGui::GetItemRectMax().y;
        for(int i=backgroundFirst;i<backgroundLast;++i)if(dl->VtxBuffer[i].pos.y>origin.y+6)dl->VtxBuffer[i].pos.y+=bottom-origin.y-12;
        dl->AddRect(origin,ImVec2(origin.x+width,bottom),ImGui::GetColorU32(ImGuiCol_Border),6*visual::unit());
        ImGui::PopID();Space(.06f);}
};
inline void Model(State& s,View& v,Edits& e){
    const float f=F();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(7,2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8,3));
    ObserveDraft(s,v,Passes);
    ImGui::BeginDisabled(!s.gradeAvailable);
    if(s.modelPending)ImGui::TextWrapped("%s",s.modelWait[0]?s.modelWait:"等待模型准备");
    else if(s.width)ImGui::TextColored(ImColor(visual::accent()),"当前模型   %u × %u",s.width,s.height);
    else ImGui::TextDisabled("等待模型开始处理");
    Space(.25f);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("处理层数");
    for(int count=1;count<=nrfeatures::MaxPasses;++count){ImGui::SameLine();ImGui::PushID(count);
        const bool selected=int(v.drafts.Value(Passes))==count;if(selected)ImGui::PushStyleColor(ImGuiCol_Text,visual::accent());
        char label[8];std::snprintf(label,sizeof(label),"%d",count);if(ImGui::Button(label,ImVec2(2.5f*f,0))&&int(v.drafts.Value(Passes))!=count){v.drafts.Edit(Passes,float(count));}
        char id[24];std::snprintf(id,sizeof(id),"nr_count_%d",count);ObserveItem(id);if(selected)ImGui::PopStyleColor();ImGui::PopID();}
    const bool dirty=v.drafts.NeedsSubmit(Passes);
    ImGui::EndDisabled();Space(.35f);
    ImGui::BeginDisabled(!s.gradeAvailable);
    for(int layer=0;layer<nrfeatures::MaxPasses;++layer)for(auto first:Appearance){const auto id=LayerId(first,layer);
        ObserveDraft(s,v,id);}
    const int visibleLayer=v.layer;
    for(int layer=0;layer<nrfeatures::MaxPasses;++layer){
        char cardId[32],observed[32];std::snprintf(cardId,sizeof(cardId),"nr_card_%d",layer+1);std::snprintf(observed,sizeof(observed),"model_layer_%d",layer+1);
        const char* tags[]={L("默认"),L("自然"),L("电影"),L("电影")};
        LayerCard card(cardId,observed,layer,tags[(std::clamp)(int(v.drafts.Value(LayerId(Style,layer))),0,3)],v.layer,visibleLayer);
        if(!card.expanded)continue;
        if(layer>=int(s.values[Passes]))ImGui::TextDisabled("此层未启用，参数仍保留。");
    State draft=s;Edits draftEdits;
    for(auto first:Appearance)draft.values[first]=v.drafts.Value(LayerId(first,layer));
    // The pinned model has one weight descriptor; legacy 0/1/2/3 IDs all
    // choose it. Preserve stored IDs without presenting false alternatives.
    const float presetX=ImGui::GetCursorPosX();ImGui::AlignTextToFramePadding();ImGui::TextColored(ImColor(visual::muted()),"模型预设");
    ImGui::SameLine();ImGui::SetCursorPosX(presetX+6*F());ImGui::TextUnformatted("当前模型 · 单一预设");
    Tip("当前模型库仅提供一种预设。旧编号保留兼容，不代表不同模型。");
    const char* styles[]={"0 · 默认","1 · 自然","2 · 电影"};
    int selected=int(draft.values[Style]);
    const float styleX=ImGui::GetCursorPosX();ImGui::AlignTextToFramePadding();ImGui::TextColored(ImColor(visual::muted()),"本层风格");
    ImGui::SameLine();ImGui::SetCursorPosX(styleX+(std::max)(6*F(),ImGui::CalcTextSize("本层风格").x+ImGui::GetStyle().ItemSpacing.x));ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::BeginCombo("##native_style",selected>=0&&selected<3?styles[selected]:selected==3?L("电影"):"未知参数")){
        for(int i=0;i<3;++i){if(ImGui::Selectable(styles[i],selected==i))draft.values[Style]=float(i);
            char id[32];std::snprintf(id,sizeof(id),"native_style_%d",i);ObserveItem(id);
            char choice[40];std::snprintf(choice,sizeof(choice),"nr_style_%d_%d",layer+1,i);ObserveItem(choice);}
        ImGui::EndCombo();}ObserveItem("native_style");
    char styleId[32];std::snprintf(styleId,sizeof(styleId),"nr_style_%d",layer+1);ObserveItem(styleId);
    if(selected==3)ObserveItem("legacy_style_mapping");
    Tip("模型自己的三套处理档位（名字来自社区实测，NVIDIA 的二进制里不带名字）：\n默认：最强的一档，加局部对比、压暗光影，会过饱和或显得风格化 ——\n      「模型把我游戏画面改了」多半就是这一档。\n自然：同样的细节处理但手轻，肤色和明暗更接近游戏原本渲染的样子。\n电影：压掉油光和过处理，偏电影感。\n改了要重建模型，过一会儿才生效。旧编号 3 等同电影，不自动改写已保存的值。");
    if(ImGui::BeginTable("native_grid",1,ImGuiTableFlags_SizingStretchSame)){
        ImGui::TableNextColumn();Slider(draft,draftEdits,Intensity,L("整体强度"));ImGui::TableNextColumn();Slider(draft,draftEdits,Structure,L("细节强度"));
        ImGui::TableNextColumn();Slider(draft,draftEdits,LocalTone,L("局部明暗"));ImGui::TableNextColumn();Slider(draft,draftEdits,GlobalTone,L("全局色调"));ImGui::EndTable();}
    Slider(draft,draftEdits,Skin,"皮肤质感（实验）","%.2f","往左拉减轻皮肤褶皱（法令纹这类）。拉到 -1 = 跟随「细节强度」，那是模型自己的默认。\n这一项随本游戏参数保存，重新启动后恢复。");
    Check(draft,draftEdits,AutoMask,"自动遮罩");Check(draft,draftEdits,UiCorrect,"界面校正");
    for(auto first:Appearance){const auto id=LayerId(first,layer);if(draft.values[first]!=v.drafts.Value(id)){v.drafts.Edit(id,draft.values[first]);}}
    } // only the expanded layer draws controls; all drafts remain in View
    bool appearanceDirty=dirty;uint64_t appearanceMask=uint64_t(1)<<Passes;
    bool appearanceInFlight=v.drafts.InFlight(Passes);
    for(int layer=0;layer<nrfeatures::MaxPasses;++layer)for(auto first:Appearance){const auto id=LayerId(first,layer);
        appearanceMask|=uint64_t(1)<<id;appearanceDirty|=v.drafts.NeedsSubmit(id);appearanceInFlight|=v.drafts.InFlight(id);}
    // NR model requests are explicit: editing, release, tab changes and Save
    // keep drafts local until the user presses Apply below.
    ImGui::TextDisabled("层数与各层参数修改后，点击“应用模型参数”才生效。");
    Space(.35f);ImGui::BeginDisabled(!appearanceDirty);
    if(visual::primary_button("应用模型参数",ImVec2(-1,38*visual::unit()))){
        
        // Commit all edited layers together; rejected fields remain available for retry.
        StageDraft(s,v,e,Passes);
        for(int layer=0;layer<nrfeatures::MaxPasses;++layer)for(auto first:Appearance){const auto id=LayerId(first,layer);
            StageDraft(s,v,e,id);}
    }
    ObserveItem("apply_appearance");
    if(ImGui::Button("放弃未提交修改",ImVec2(-1,0))){v.drafts.Discard(Passes);for(int layer=0;layer<nrfeatures::MaxPasses;++layer)for(auto first:Appearance)v.drafts.Discard(LayerId(first,layer));}
    ObserveItem("discard_appearance");ImGui::EndDisabled();
    if(ImGui::Button("清一次历史帧",ImVec2(-1,0)))e.resetHistory=true;
    ObserveItem("reset_history");
    Tip("把模型攒下来的时间历史丢掉，下一帧从头起步。\n糊影、残影、拖尾、切过画质之后画面不对，按一下就清干净，不用重进游戏。\n走的是游戏切换颜色格式时本来就在走的那条路：不重建模型、不释放显存，所以按了不会卡。");
    // V6.1 global skin controls (moved below Apply): they are not per-layer and must not push Apply under the footer.
    ImGui::TextDisabled("皮肤保护（实验）：已开启，叠的层数越多越强");
    Slider(s,e,NaturalLook,"自然光影（实验）","%.2f","033 原创自然调色：调整明暗层次和色彩，高光柔和收敛。0 = 关闭。\n在 NR 输出后处理，与清晰度共用一次处理；拖动实时生效并保存。\n不替换游戏的天气、贴图或灯光。肤色判断不是人脸识别；效果和耗时待游戏验证。");
    Slider(s,e,Sharpen,"画面清晰度（CAS 改进）","%.2f","在 NR 最终输出之后增强细节，0 = 关闭。拖动实时生效，随游戏设置保存。\n限制边缘光晕、暗部噪点和肤色细纹的放大；肤色判断不是人脸识别。\n额外一次全分辨率处理，实际耗时待游戏验证。显存不足时保留 NR 输出。");
    Slider(s,e,SkinLift,"肤色提亮（实验）","%.2f","把皮肤的中间调抬亮一点。0 = 关。\n只动中间调：真黑不抬、高光不动，三通道同增益所以不会变色。\n判据跟皮肤保护同一套逐像素肤色权重，不是人脸识别。\n这一项在【最终出画】做一次，不随层数叠加。");
    Tip("【本版新增】按肤色逐像素判定，不是人脸识别 —— 手、胳膊、头发边一样管，\n转视角再快也不会失效。它压住皮肤上的锐化，并且让模型对皮肤只改明暗、不改颜色。\n针对的就是「叠几层之后人物发黑、法令纹变重」。自动生效，不用手动调。");
    if(v.drafts.RejectedMask()&appearanceMask)ImGui::TextColored(ImColor(visual::accent()),"参数未被接收，修改已保留，请重试应用。");
    else if(appearanceDirty)ImGui::TextDisabled("有未提交修改");
    else if(appearanceInFlight)ImGui::TextDisabled("请求已提交，等待接收");
    else if(s.modelPending){ImGui::TextColored(ImColor(visual::accent()),"已提交，等待模型切换；原因见上方。");ObserveItem("model_submit_pending");}
    if(ImGui::TreeNode("模型应用记录")){
    if(v.layer>=0)ImGui::TextWrapped("本层提交参数：兼容预设编号 %.0f；风格 %.0f。",s.values[LayerId(Preset,v.layer)],s.values[LayerId(Style,v.layer)]);
    if(v.layer>=0 && s.modelActive && unsigned(v.layer)<s.cachedLayers){ImGui::TextWrapped("已建模型的请求参数：兼容预设编号 %.0f；风格 %.0f。",s.applied[LayerId(Preset,v.layer)],s.applied[LayerId(Style,v.layer)]);
    ImGui::TextWrapped("已建参数：风格 %.0f · 整体 %.2f · 细节 %.2f · 局部明暗 %.2f",
        s.applied[LayerId(Style,v.layer)],s.applied[LayerId(Intensity,v.layer)],s.applied[LayerId(Structure,v.layer)],s.applied[LayerId(LocalTone,v.layer)]);}
    if(unsigned(v.layer)>=s.activeLayers)ImGui::TextDisabled("此层当前未参与处理。");
    ImGui::TextWrapped("当前固定模型库仅有一种预设。以上是请求参数记录，不证明模型库采用，也不保证像素变化；保存不会提交草稿。");
        ImGui::TreePop();}
    ImGui::EndDisabled();ImGui::PopStyleVar(2);
}
inline void ModelDiagnostics(const State& s){
    if(ImGui::CollapsingHeader("模型兼容参数记录")){
        ImGui::Text("提交预设编号：%d   已建请求编号：%u",int(s.values[Preset]),s.presetBuilt);
        ImGui::Text("原生风格编号：%d   全局色调：%.2f",int(s.values[Style]),s.values[GlobalTone]);
        ImGui::TextWrapped("当前固定模型库只有一种预设，兼容编号不会选择不同模型。风格参数效果尚未验证；记录不代表实际画面变化。");
    }

}
// DLSS-G calls. It reads dlssg_to_fsr3.ini at launch, so a choice applies after restart.
// S38 (owner 2026-09-24: 「30系反应是游戏里面帧生成选项都没有」): on RTX 20/30 the game shows frame
// generation only with the bridge in place, Windows' hardware-accelerated GPU scheduling on and Windows 10
// 2004 or newer (YanYun's Streamline 2.11.1 checks the last two itself). Each missing one is named with the
// way to fix it; with all three in place the next step is the bridge's own log.
inline void FgConditions2030(const State& s){
    auto problem=[](const char* text){ImGui::PushStyleColor(ImGuiCol_Text,visual::accent());ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();};
    bool ok=true;
    if(!s.bridgePresent){ok=false;problem(L("随包的帧生成转接件没装上：用 033 安装器重新安装一次；还是没有，把安装报告发给作者。"));}
    if(s.hags==1){ok=false;problem(L("Windows 的「硬件加速 GPU 计划」没开，游戏会把帧生成选项藏起来。打开方法：Windows 设置里搜「图形设置」，打开「硬件加速 GPU 计划」，然后重启电脑。"));}
    else if(s.hags!=2){ok=false;problem(L("没查到「硬件加速 GPU 计划」的设置：Windows 设置里搜「图形设置」，确认它是打开的（改完要重启电脑）。"));}
    if(s.osBuild&&s.osBuild<19041){ok=false;problem(L("Windows 版本太旧：游戏的帧生成要 Windows 10 2004 或更新。"));}
    if(ok)ImGui::TextWrapped("%s",L("转接件已装，「硬件加速 GPU 计划」已开。游戏里还是没有帧生成选项的话：把游戏目录里 dlssg_to_fsr3.ini 的 EnableLogging 改成 1，进一次游戏，把 dlssg_to_fsr3.log 发给作者。"));
    ObserveItem(ok?"fg_conditions_ok":"fg_conditions_missing");Space(.3f);
}
inline void Bridge2030(State& s,Edits& e){
    Heading(L("帧生成"));BeginCard("frame_generation_2030");
    // 2026-09-12 起 40/50 系也走这张卡（业主定：帧生成用随包转接件，回到 V5.0 的做法），文案不能再只有 20/30。
    const char* series=s.gpuGen==20?L("20 系"):s.gpuGen==30?L("30 系"):s.gpuGen==40?L("40 系"):s.gpuGen==50?L("50 系"):L("这张显卡");
    const bool twentyThirty=s.gpuGen==20||s.gpuGen==30;
    if(twentyThirty)FgConditions2030(s);
    if(!s.bridgePresent){
        if(!twentyThirty){
            if(s.gameFrameGen)ImGui::TextWrapped(L("%s的多帧生成由随包的 nvidia_mfg_bridge 转接件提供。这个游戏自带 DLSS 帧生成，但目录里没有转接件；用 033 安装器重新安装即可放入。"),series);
            else ImGui::TextWrapped(L("%s的多帧生成需要游戏自带 DLSS 帧生成，这个游戏没有。"),series);
        }
        ObserveItem("bridge_absent");EndCard();return;
    }
    const char* engines[]={L("NVIDIA 原生 DLSS-G"),"FSR3.1"};
    // FGMode: 2 = NVIDIA model, 0 = FSR3.1 (the bridge default when the key is absent), 1 = XeSS.
    const bool xess=s.bridgeMode==1&&!s.bridgeLegacy;const int engine=s.bridgeMode==2&&!s.bridgeLegacy?0:1;
    ImGui::TextColored(ImColor(visual::muted()),L("插帧引擎"));ImGui::BeginDisabled(s.bridgeLegacy);ImGui::SetNextItemWidth(-FLT_MIN);
    bool open=ImGui::BeginCombo("##bridge_engine",xess?L("Intel XeSS（本包未附带）"):engines[engine]);
    if(!open)ObserveItem("bridge_engine");
    if(open){for(int i=0;i<2;++i){
        if(ImGui::Selectable(engines[i],!xess&&engine==i)){e.bridge=true;e.bridgeMode=i==0?2:0;s.bridgeMode=e.bridgeMode;}
        char id[24];std::snprintf(id,sizeof(id),"bridge_engine_%d",i);ObserveItem(id);
    }ImGui::EndCombo();}
    ImGui::EndDisabled();Space();
    const char* ratios[]={L("跟随游戏"),"3×","4×","5×","6×"};const int ratio=std::clamp(s.bridgeForce,0,4);
    ImGui::TextColored(ImColor(visual::muted()),L("插帧倍率"));ImGui::SetNextItemWidth(-FLT_MIN);
    open=ImGui::BeginCombo("##bridge_multiplier",ratios[ratio]);
    if(!open)ObserveItem("bridge_multiplier");
    if(open){for(int i=0;i<5;++i){
        if(ImGui::Selectable(ratios[i],ratio==i)){e.bridge=true;e.bridgeForce=i;s.bridgeForce=i;}
        char id[28];std::snprintf(id,sizeof(id),"bridge_multiplier_%d",i);ObserveItem(id);
    }ImGui::EndCombo();}
    Space();
    if(s.bridgeSaveFailed)ImGui::TextColored(ImColor(visual::accent()),L("写入 dlssg_to_fsr3.ini 失败，设置未改变；请确认游戏目录可写。"));
    else if(s.bridgeSaved)ImGui::TextColored(ImColor(visual::accent()),L("已写入 dlssg_to_fsr3.ini，退出游戏重进后生效。"));
    if(s.bridgeLegacy)ImGui::TextWrapped(L("这是旧版转接件（v0.4x，只有 FSR3.1）。用 033 安装器重新安装可升级到原生 DLSS-G。"));
    else if(xess)ImGui::TextWrapped(L("配置文件指定了 XeSS（FGMode=1），本包没有附带 XeSS 组件，请改选上面两种引擎之一。"));
    else if(engine==0&&!s.bridgeProvider)ImGui::TextColored(ImColor(visual::accent()),L("缺少 %s，原生 DLSS-G 无法加载；请用 033 安装器重新安装，或改用 FSR3.1。"),s.gpuGen==20?"nvngx_dlssg.sm75.dll":"nvngx_dlssg.sm86.dll");
    else if(engine==0&&s.gpuGen==20)ImGui::TextWrapped(L("20 系的原生 DLSS-G 路线，转接件作者尚未在真卡上验证；如果黑屏、闪退或画面异常，把引擎改成 FSR3.1。"));
    ImGui::TextWrapped(L("在游戏设置里打开 DLSS 帧生成，转接件会接管这个开关。倍率越高，真实帧占比越低；只用于单机游戏。"));
    if(s.bridgeLoader[0])ImGui::TextDisabled(L("转接件入口：%s"),s.bridgeLoader);
    EndCard();
}

inline void FrameGeneration(State& s,Edits& e){
    Heading(L("帧生成"));BeginCard("frame_generation");
    ImGui::TextColored(ImColor(visual::muted()),L("原生多帧倍率"));ImGui::BeginDisabled(!s.mfgAvailable);
    const char* options[]={L("跟随游戏"),"2×","3×","4×","5×","6×"};int selected=s.mfgRequested>=2?int(s.mfgRequested)-1:0;
    ImGui::SetNextItemWidth(-FLT_MIN);if(ImGui::Combo("##multiplier",&selected,options,6)){e.mfg=true;e.multiplier=selected?unsigned(selected+1):0;}
    ObserveItem("multiplier");ImGui::EndDisabled();Space();
    if(s.mfgOff)ImGui::TextDisabled(L("游戏帧生成当前关闭"));
    else if(s.mfgAccepted){
        const bool mismatch=s.mfgRequested && s.mfgRequested!=s.mfgAccepted;
        ImGui::TextColored(ImColor(mismatch?Accent:Green),L("运行库接受   %u×%s"),s.mfgAccepted,mismatch?L("（与请求不一致）"):"");
        if(mismatch)ImGui::TextWrapped(L("请求 %u× 尚未生效。%s"),s.mfgRequested,
            s.mfgBlocked?L("节奏验证未通过，保留游戏原有请求。"):L("等待游戏重新提交帧生成设置；可在游戏中关闭帧生成再开启。"));
    }else ImGui::TextDisabled(L("等待游戏帧生成运行库反馈"));
    if(s.gpuFrameMs>=0)ImGui::Text(L("GPU 原生帧耗时   %.1f ms"),s.gpuFrameMs);
    if(s.nrMs>=0)ImGui::Text(L("其中神经渲染   %.1f ms"),s.nrMs);
    if(!s.mfgAvailable)ImGui::TextWrapped(L("尚未收到游戏的帧生成接口：在游戏设置里打开 DLSS 帧生成后再看。"));
    ImGui::TextWrapped(L("需要游戏自身提供 DLSS 帧生成。倍率不代表输入延迟；低延迟与其他帧生成选项位于设置。"));EndCard();
}
inline void InputNormalization(State& s,Edits& e){
    Heading("模型输入亮度");BeginCard("input_normalization");
    ImGui::BeginDisabled(!s.gradeAvailable);
    const char* sources[]={"保留原有设置","固定白点","跟随游戏曝光"};
    int source=int(s.values[WhiteSource]);ImGui::SetNextItemWidth(-FLT_MIN);
    const bool open=ImGui::BeginCombo("##white_source",sources[std::clamp(source,0,2)]);
    if(!open)ObserveItem("white_source"); // BeginCombo enters a popup when open.
    if(open){for(int i=0;i<3;++i){
        if(ImGui::Selectable(sources[i],source==i)){source=i;s.values[WhiteSource]=float(i);Changed(e,WhiteSource);}
        char id[24];std::snprintf(id,sizeof(id),"white_source_%d",i);ObserveItem(id);
    }ImGui::EndCombo();}
    const bool legacyFixed=source==exposurepolicy::Legacy && s.values[Replica]!=0;
    const bool sdr=s.whiteEncoding==0;
    ImGui::BeginDisabled(legacyFixed||sdr);
    Slider(s,e,White,source==exposurepolicy::Game?"备用白点尺度":"白点尺度","%.2f","用于神经渲染前的亮度归一化，并在合成时还原；不是屏幕亮度。跟随游戏曝光时，仅在缺少有效曝光值时使用备用白点。");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(source!=exposurepolicy::Game||sdr);
    Slider(s,e,WhiteTrim,"曝光微调","%.2f×");ImGui::EndDisabled();
    ImGui::EndDisabled();
    if(legacyFixed)ImGui::TextDisabled("原有设置使用固定白点 3.16。选择固定白点后可自行调整。");
    if(sdr)ImGui::TextWrapped("当前为 SDR 直通输入，白点尺度不参与处理。画面明暗可在调色页调整。");
    if(!s.gradeAvailable)ImGui::TextWrapped("当前路径由兼容渲染器管理，请使用其亮度设置。");
    else ImGui::TextWrapped("%s",exposurepolicy::Note(s.whiteStatus));
    if(source==exposurepolicy::Game)ImGui::TextWrapped("需要场景线性输入和可读取的游戏曝光。输入不兼容时使用固定尺度 %.2f；不会从处理后的画面反推曝光。",s.effectiveWhite);
    if(ImGui::TreeNode("曝光读取记录")){
        ImGui::Text("记录 %llu · 暂用 %llu · 回退 %llu",(unsigned long long)s.exposureRecorded,(unsigned long long)s.exposureHeld,(unsigned long long)s.exposureBypass);
        ImGui::TextWrapped("记录次数不代表 GPU 已完成；状态不会把收到纹理当成曝光内容已经验证。");ImGui::TreePop();}
    EndCard();Space();
}
inline void InternalSr(State& s,View& v,Edits& e){
    Heading("NRSR");
    ImGui::TextWrapped("分别设置三层 NR 的内部处理精度；不改变游戏自带超分档位。");
    const Id ids[]={Work,PassWork,PassWork3};int drafts[3];
    for(int i=0;i<3;++i){ObserveDraft(s,v,ids[i]);drafts[i]=int(v.drafts.Value(ids[i]));}
    ImGui::BeginDisabled(!s.available);uint64_t scaleMask=0;bool dirty=false,inFlight=false;
    const int visibleLayer=v.srLayer;
    for(int layer=0;layer<nrfeatures::MaxPasses;++layer){
        ImGui::PushID(int(ids[layer]));char key[32],observed[32],tag[24];std::snprintf(key,sizeof(key),"sr_card_%d",layer+1);std::snprintf(observed,sizeof(observed),"sr_open_%d",layer+1);std::snprintf(tag,sizeof(tag),"%d%%",drafts[layer]);
        {LayerCard card(key,observed,layer,tag,v.srLayer,visibleLayer);if(card.expanded){
        char id[32];std::snprintf(id,sizeof(id),"sr_layer_%d",layer+1);
        DraftSlider("处理精度",drafts[layer],layer==0?25:50,layer==0?200:100,"%d%%",id);
        if(float(drafts[layer])!=v.drafts.Value(ids[layer]))v.drafts.Edit(ids[layer],float(drafts[layer]));
        if(s.layerModelW[layer]&&s.layerModelH[layer])ImGui::Text("已建模型尺寸  %u × %u",s.layerModelW[layer],s.layerModelH[layer]);
        else ImGui::TextDisabled("等待此层模型建立");
        if(layer>=int(s.values[Passes]))ImGui::TextDisabled("此层未启用，精度设置仍保留。");
        }}ImGui::PopID();
        scaleMask|=uint64_t(1)<<ids[layer];dirty|=v.drafts.NeedsSubmit(ids[layer]);inFlight|=v.drafts.InFlight(ids[layer]);
    }
    ImGui::BeginDisabled(!dirty);
    if(visual::primary_button("应用 SR 精度",ImVec2(-1,38*visual::unit()))){for(auto id:ids)StageDraft(s,v,e,id);}
    ObserveItem("apply_sr");
    if(ImGui::Button("放弃精度修改",ImVec2(-1,0))){for(auto id:ids)v.drafts.Discard(id);}
    ObserveItem("discard_sr");ImGui::EndDisabled();ImGui::EndDisabled();
    if(v.drafts.RejectedMask()&scaleMask)ImGui::TextColored(ImColor(visual::accent()),"精度未被接收，修改已保留，请重试应用。");
    else if(dirty)ImGui::TextDisabled("有未提交的精度修改");
    else if(inFlight)ImGui::TextDisabled("精度已提交，等待接收");
    else if(s.modelPending)ImGui::TextWrapped("精度已提交：%s",s.modelWait[0]?s.modelWait:"等待模型准备完成");
    ImGui::TextWrapped("F11 关闭 NR 时停止其专用 SR 新处理；保留已设精度、普通调色及补帧设置。");
}
inline void Footer(const State& s,Edits& e){Space(.6f);ImGui::Separator();Space(.3f);
    const float f=F();
    const char* status=s.paused?"本次启动已暂停":s.failed?"神经渲染已停用":s.values[Enabled]==0?"神经渲染已关闭":s.modelPending?"模型切换待完成":s.running?"已有 NR 处理记录":"等待游戏画面";
    ImGui::TextColored(ImColor(s.paused||s.failed?Accent:s.running?Green:Muted),"%s",status);

    ImGui::BeginDisabled(!s.available||s.paused);
    char toggleLabel[64],keyLabel[24];
    if(s.hotkey>=0x70&&s.hotkey<=0x87)std::snprintf(keyLabel,sizeof(keyLabel),"F%u",s.hotkey-0x70+1);
    else std::snprintf(keyLabel,sizeof(keyLabel),"键码 %u",s.hotkey);
    std::snprintf(toggleLabel,sizeof(toggleLabel),"%s   %s",s.failed?"恢复 NR":s.values[Enabled]!=0?L("关闭 NR"):L("开启 NR"),keyLabel);
    if(ImGui::Button(toggleLabel,ImVec2(f*10.5f,0)))e.toggle=true;
    ObserveItem("toggle_nr");Tip("开关神经渲染。已有的超分和帧生成设置保持原值。");ImGui::EndDisabled();

    ImGui::TextColored(ImColor(visual::muted()),"Home/Shift+退格 面板 · F11 NR · Shift+F10 截图");
    if(ImGui::GetContentRegionAvail().x>=30*F())ImGui::SameLine();
    if(ImGui::Button("保存设置"))e.save=true;ObserveItem("save_settings");
    Tip("已应用的参数写入燕云专属设置。画面方案草稿请使用「保存方案」。");
    if(!s.settingsReady)ImGui::TextDisabled("燕云设置尚未就绪");
    else if(s.settingsFailed)ImGui::TextDisabled("燕云设置保存失败，已保留之前的文件");
    else if(s.settingsPending)ImGui::TextDisabled("正在保存燕云设置");
    else ImGui::TextDisabled("燕云专属设置 · 与通用版分别保存");
}
} // namespace studio033
