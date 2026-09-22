// Plain C ABI: each module renders with its own ImGui. No ImGui context,
// std::string, allocator or editable Config object crosses a DLL boundary.
#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <cstring>
#include "exposure_policy.h"
#include "nr_feature_policy.h"
namespace nrcontrolsabi {
constexpr uint32_t Version=12;
enum class PortraitStatus:uint32_t { Off,WaitingPath,WaitingFrame,WaitingGpu,Detecting,NoFace,Stale,Submitted,Failed };
enum Kind:uint32_t { Toggle, Integer, Scalar };
// id, carrier config member, kind, min, max, group, label
#define K033_NR_CONTROLS(X) \
 X(Enabled,enabled,Toggle,0,1,"Neural rendering","Enable 033 NR") \
 X(Work,work,Integer,25,200,"Neural rendering","Model resolution (%)") \
 X(Passes,passes,Integer,1,nrfeatures::MaxPasses,"Neural rendering","Independent model passes") \
 X(Full,modelfull,Toggle,0,1,"Neural rendering","Use output resolution as model base") \
 X(Preset,preset,Integer,0,3,"Model appearance","NR preset (experimental; visual effect unverified)") \
 X(Style,style,Integer,0,3,"Model appearance","Native NR style (3 is undocumented)") \
 X(Intensity,intensity,Scalar,0,2,"Model appearance","Intensity") \
 X(Structure,local_structure,Scalar,0,2,"Model appearance","Local structure") \
 X(GlobalTone,global_tone,Scalar,0,2,"Model appearance","Native global tone (visual effect unverified)") \
 X(LocalTone,local_tone,Scalar,0,2,"Model appearance","Local tone") \
 X(Skin,skin_structure,Scalar,-1,2,"Portrait","Model skin structure (-1 follows structure)") \
 X(AutoMask,auto_mask,Toggle,0,1,"Portrait","Model automatic skin mask") \
 X(UiCorrect,ui_correct,Toggle,0,1,"Composition","Model UI correction") \
 X(Blend,blend,Integer,0,200,"Composition","Result strength (%)") \
 X(Replica,replica,Toggle,0,1,"Composition","Bounded reversible composition") \
 X(Compose,compose,Integer,0,2,"Composition","Other composition (when reversible is off)") \
 X(Curve,curve,Integer,0,2,"Composition","Other curve (when reversible is off)") \
 X(White,whitepoint,Scalar,0.01f,32,"Composition","Fixed white scale") \
 X(Guard,guard,Scalar,1,8,"Composition","Highlight edit bound") \
 X(Colour,colour,Scalar,0,1,"Composition","Model colour strength") \
 X(DiffuseWhite,diffuse_white,Scalar,80,1000,"Composition","HDR diffuse white (nits)") \
 X(Grade,pre.enabled,Toggle,0,1,"Before NR","Enable pre-grade") \
 X(Exposure,pre.exposure,Scalar,-2,2,"Before NR","Exposure (EV)") \
 X(Contrast,pre.contrast,Scalar,0.75f,1.25f,"Before NR","Contrast") \
 X(Saturation,pre.saturation,Scalar,0,1.5f,"Before NR","Saturation") \
 X(Warmth,pre.warmth,Scalar,-0.25f,0.25f,"Before NR","Warmth") \
 X(Tint,pre.tint,Scalar,-0.25f,0.25f,"Before NR","Tint") \
 X(Highlights,pre.highlights,Scalar,0,0.25f,"Before NR","Highlight compression") \
 X(Mas,mas,Toggle,0,1,"Sharpening","Motion adaptive sharpening") \
 X(MasStill,mas_still,Scalar,0,1,"Sharpening","Stationary strength") \
 X(MasMoving,mas_moving,Scalar,0,1,"Sharpening","Moving strength") \
 X(MasThreshold,mas_threshold,Scalar,0.5f,32,"Sharpening","Motion threshold (pixels / real frame)") \
 X(Sharpen,sharpen,Scalar,0,1,"Sharpening","033 final clarity (CAS luminance adaptation)") \
 X(Resample,resample,Integer,0,1,"Sharpening","Model resampling filter") \
 X(ApplyModel,applymodel,Toggle,0,1,"Comparison","Apply model result") \
 X(Hold,holdframe,Toggle,0,1,"Comparison","Freeze complete inputs") \
 X(Split,comparepct,Integer,0,100,"Comparison","Comparison split (%)") \
 X(PassWork,passwork,Integer,50,100,"Neural rendering","Extra-pass resolution (%)") \
 X(PreStyle,pre.style,Integer,0,3,"Before NR","Input style (0 neutral / 1 natural / 2 soft cinema / 3 anime steps)") \
 X(PreStyleStrength,pre.styleStrength,Scalar,0,1,"Before NR","Input style strength") \
 X(WhiteSource,white_source,Integer,0,2,"Input normalization","White source (0 keep existing / 1 fixed / 2 game exposure)") \
 X(WhiteTrim,white_trim,Scalar,0.25f,4,"Input normalization","Game exposure white trim") \
 X(L2Style,extra[0].style,Integer,0,3,"Layer 2","Layer 2 Style") \
 X(L2Preset,extra[0].preset,Integer,0,3,"Layer 2","Layer 2 Preset") \
 X(L2Intensity,extra[0].intensity,Scalar,0,2,"Layer 2","Layer 2 Intensity") \
 X(L2Structure,extra[0].structure,Scalar,0,2,"Layer 2","Layer 2 Structure") \
 X(L2LocalTone,extra[0].tone,Scalar,0,2,"Layer 2","Layer 2 LocalTone") \
 X(L2Skin,extra[0].skin,Scalar,-1,2,"Layer 2","Layer 2 Skin") \
 X(L2GlobalTone,extra[0].globalTone,Scalar,0,2,"Layer 2","Layer 2 GlobalTone") \
 X(L2AutoMask,extra[0].autoMask,Toggle,0,1,"Layer 2","Layer 2 AutoMask") \
 X(L2UiCorrect,extra[0].uiCorrect,Toggle,0,1,"Layer 2","Layer 2 UiCorrect") \
 X(L3Style,extra[1].style,Integer,0,3,"Layer 3","Layer 3 Style") \
 X(L3Preset,extra[1].preset,Integer,0,3,"Layer 3","Layer 3 Preset") \
 X(L3Intensity,extra[1].intensity,Scalar,0,2,"Layer 3","Layer 3 Intensity") \
 X(L3Structure,extra[1].structure,Scalar,0,2,"Layer 3","Layer 3 Structure") \
 X(L3LocalTone,extra[1].tone,Scalar,0,2,"Layer 3","Layer 3 LocalTone") \
 X(L3Skin,extra[1].skin,Scalar,-1,2,"Layer 3","Layer 3 Skin") \
 X(L3GlobalTone,extra[1].globalTone,Scalar,0,2,"Layer 3","Layer 3 GlobalTone") \
 X(L3AutoMask,extra[1].autoMask,Toggle,0,1,"Layer 3","Layer 3 AutoMask") \
 X(L3UiCorrect,extra[1].uiCorrect,Toggle,0,1,"Layer 3","Layer 3 UiCorrect") \
 X(PassWork3,passwork3,Integer,50,100,"Neural rendering","Layer 3 resolution (%)") \
 X(RetiredEffect,retired_effect,Integer,0,0,"Reserved","Removed effect (reserved ABI slot)") \
 /* ---- appended after the frozen 62-control ABI (FrozenCount); new controls go BELOW this line only ---- */ \
 X(SkinLift,skin_lift,Scalar,0,1,"Portrait","Skin brightening (midtones only; hue preserved)") \
 X(NaturalLook,natural_look,Scalar,0,1,"Output picture","033 natural lighting grade")
enum Id:uint32_t {
#define X(id,field,kind,lo,hi,group,label) id,
 K033_NR_CONTROLS(X)
#undef X
 Count
};
static_assert(Count<=64,"Studio edit mask capacity");
inline Id LayerId(Id first,int layer){
    if(layer==0)return first;
    switch(first){
    case Style:return layer==1?L2Style:L3Style;
    case Preset:return layer==1?L2Preset:L3Preset;
    case Intensity:return layer==1?L2Intensity:L3Intensity;
    case Structure:return layer==1?L2Structure:L3Structure;
    case LocalTone:return layer==1?L2LocalTone:L3LocalTone;
    case Skin:return layer==1?L2Skin:L3Skin;
    case GlobalTone:return layer==1?L2GlobalTone:L3GlobalTone;
    case AutoMask:return layer==1?L2AutoMask:L3AutoMask;
    case UiCorrect:return layer==1?L2UiCorrect:L3UiCorrect;
    default:return first;}
}
struct Definition {Kind kind;float minimum,maximum;const char* group;const char* label;};
inline constexpr Definition definitions[]={
#define X(id,field,kind,lo,hi,group,label) {kind,lo,hi,group,label},
 K033_NR_CONTROLS(X)
#undef X
};
inline bool Valid(uint32_t id,float value){return id<Count && std::isfinite(value) && value>=definitions[id].minimum && value<=definitions[id].maximum
    && (definitions[id].kind==Scalar || value==std::floor(value));}
enum Action:uint32_t { Save=1,Capture=2,PortraitNatural=3,NeutralGrade=4 };
// ★控件快照 ABI：新控件只许追加在末尾，旧大小必须一直读得通★ (2026-09-13 V6.1 热修)
//   已发布的冻结组件 —— 64 位 dlss5-feed.addon64、32 位路线的 dlss5-feed-host64.exe —— 按 62 个控件编译，
//   快照是 {size=872, version=12, count=62}，每帧读快照都要求三者跟自己编译时完全一致，否则不处理帧。
//   V6.1 beta 在表中间插了 SkinLift(63 个 / 880 字节)，Read 严格比 size 每帧拒绝：所有不带 DLSS 的游戏
//   一直「等待输入」，而 version 没变，连一行报错都没有。现在：追加在末尾 + Read 认冻结大小并按旧布局回填。
inline constexpr uint32_t FrozenCount=62;
template<uint32_t N> struct SnapshotOf {
 uint32_t size=sizeof(SnapshotOf),version=Version,count=N,pending=0;
 float values[N]={};
 uint32_t modelW=0,modelH=0,presetBuilt=0,presetReadback=0,tuningPending=0;
 uint64_t frames=0,leaseBypass=0;
 uint32_t mfgRequested=0,mfgAccepted=0;
 float modelValues[N]={};
 uint32_t modelActive=0,mfgPacingReady=0,mfgBlocked=0,mfgOff=0;
 uint32_t portraitAvailable=0,portraitFaces=0,portraitUnavailable=0;
 float portraitCpuMs=0;
 PortraitStatus portraitStatus=PortraitStatus::Off;
 uint32_t portraitCaptureW=0,portraitCaptureH=0,portraitAgeMs=0;
 exposurepolicy::Status whiteStatus=exposurepolicy::Status::Waiting;
 float effectiveWhite=3.16f;
 int32_t whiteEncoding=-1;
 uint32_t hotkey=0x7A;
 uint64_t exposureRecorded=0,exposureHeld=0,exposureBypass=0;
 char modelWait[192]={};
 uint32_t activeLayers=0,cachedLayers=0;
 uint32_t layerModelW[3]={},layerModelH[3]={};
};
using Snapshot=SnapshotOf<Count>;
using FrozenSnapshot=SnapshotOf<FrozenCount>;
static_assert(Count>=FrozenCount,"controls may only be appended after the frozen ABI");
// 872 is the literal compiled into the shipped dlss5-feed.addon64 and dlss5-feed-host64.exe. If a field is
// added to SnapshotOf this fires: keep the frozen layout readable and extend Narrow below.
static_assert(sizeof(FrozenSnapshot)==872,"frozen feeder snapshot layout changed");
// Fill an older consumer's layout: the first N controls keep their ids because new controls are appended.
template<uint32_t N> inline SnapshotOf<N> Narrow(const Snapshot& s){
 SnapshotOf<N> d;d.pending=s.pending;
 for(uint32_t i=0;i<N;++i){d.values[i]=s.values[i];d.modelValues[i]=s.modelValues[i];}
 d.modelW=s.modelW;d.modelH=s.modelH;d.presetBuilt=s.presetBuilt;d.presetReadback=s.presetReadback;d.tuningPending=s.tuningPending;
 d.frames=s.frames;d.leaseBypass=s.leaseBypass;d.mfgRequested=s.mfgRequested;d.mfgAccepted=s.mfgAccepted;
 d.modelActive=s.modelActive;d.mfgPacingReady=s.mfgPacingReady;d.mfgBlocked=s.mfgBlocked;d.mfgOff=s.mfgOff;
 d.portraitAvailable=s.portraitAvailable;d.portraitFaces=s.portraitFaces;d.portraitUnavailable=s.portraitUnavailable;
 d.portraitCpuMs=s.portraitCpuMs;d.portraitStatus=s.portraitStatus;
 d.portraitCaptureW=s.portraitCaptureW;d.portraitCaptureH=s.portraitCaptureH;d.portraitAgeMs=s.portraitAgeMs;
 d.whiteStatus=s.whiteStatus;d.effectiveWhite=s.effectiveWhite;d.whiteEncoding=s.whiteEncoding;d.hotkey=s.hotkey;
 d.exposureRecorded=s.exposureRecorded;d.exposureHeld=s.exposureHeld;d.exposureBypass=s.exposureBypass;
 for(size_t i=0;i<sizeof(d.modelWait);++i)d.modelWait[i]=s.modelWait[i];
 d.activeLayers=s.activeLayers;d.cachedLayers=s.cachedLayers;
 for(int i=0;i<3;++i){d.layerModelW[i]=s.layerModelW[i];d.layerModelH[i]=s.layerModelH[i];}
 return d;
}
// Bounded replies for both shipped 62- and 63-control consumers.
inline bool ReplyHeader(uint32_t bytes,uint32_t version,uint32_t count){
 return version==Version && ((count==62&&bytes==sizeof(SnapshotOf<62>)) ||
     (count==63&&bytes==sizeof(SnapshotOf<63>)) || (count==Count&&bytes==sizeof(Snapshot)));
}
inline bool Reply(void* output,uint32_t bytes,uint32_t version,uint32_t count,const Snapshot& s){
 if(!output||!ReplyHeader(bytes,version,count))return false;
 if(count==62){auto n=Narrow<62>(s);std::memcpy(output,&n,sizeof(n));}
 else if(count==63){auto n=Narrow<63>(s);std::memcpy(output,&n,sizeof(n));}
 else std::memcpy(output,&s,sizeof(s));
 return true;
}
struct Api {
 uint32_t size=sizeof(Api),version=Version;
 int(__cdecl* read)(Snapshot*)=nullptr;
 int(__cdecl* set)(uint32_t,float)=nullptr;
 int(__cdecl* action)(uint32_t)=nullptr;
 int(__cdecl* setMfg)(uint32_t)=nullptr;
};
using GetApi=const Api*(__cdecl*)(uint32_t);
}
