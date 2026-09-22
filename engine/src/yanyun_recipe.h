#pragma once
// Dedicated product document. Never append fields to the frozen feeder ABI.
#include "nr_controls_abi.h"
#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
namespace yanyunrecipe {
using namespace nrcontrolsabi;
inline constexpr char Model[]="6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927";
inline constexpr Id Fields[]={Work,Passes,Full,Preset,Style,Intensity,Structure,GlobalTone,LocalTone,Skin,AutoMask,UiCorrect,
 Blend,Replica,Compose,Curve,White,Guard,Colour,DiffuseWhite,Grade,Exposure,Contrast,Saturation,Warmth,Tint,Highlights,
 Mas,MasStill,MasMoving,MasThreshold,Sharpen,Resample,PassWork,PreStyle,PreStyleStrength,WhiteSource,WhiteTrim,
 L2Style,L2Preset,L2Intensity,L2Structure,L2LocalTone,L2Skin,L2GlobalTone,L2AutoMask,L2UiCorrect,
 L3Style,L3Preset,L3Intensity,L3Structure,L3LocalTone,L3Skin,L3GlobalTone,L3AutoMask,L3UiCorrect,PassWork3,SkinLift,NaturalLook};
inline constexpr unsigned FieldCount=sizeof(Fields)/sizeof(Fields[0]),MaxCode=8192;
enum Group:unsigned {Whole,Character,Scene,Groups};
enum PresetKind:unsigned {Custom,Realistic,Restore};
struct Recipe {
 uint32_t size=sizeof(Recipe),version=2,kind=Custom,reserved=0;
 char model[65]{};char padding[3]{};
 uint32_t regional=0;float fidelity=.85f,sceneStrength=1.f,feather=2.f;
 float values[Groups][FieldCount]{};
};
static_assert(std::is_standard_layout_v<Recipe> && std::is_trivially_copyable_v<Recipe>);
static_assert(sizeof(Recipe)==100+Groups*FieldCount*sizeof(float),"Dedicated recipe wire layout");
static_assert(nrcontrolsabi::Count==64 && sizeof(nrcontrolsabi::FrozenSnapshot)==872,"Do not expand the frozen feeder");
inline int Index(Id id){for(unsigned i=0;i<FieldCount;++i)if(Fields[i]==id)return int(i);return -1;}
inline void Set(Recipe& r,Group group,Id id,float value){const int i=Index(id);if(i>=0)r.values[group][i]=value;}
inline float Get(const Recipe& r,Group group,Id id){const int i=Index(id);return i<0?0:r.values[group][i];}
inline bool Valid(const Recipe& r){
 if(r.size!=sizeof(Recipe)||r.version!=2||r.kind>Restore||r.reserved||r.padding[0]||r.padding[1]||r.padding[2]||std::memcmp(r.model,Model,sizeof(Model)))return false;
 if(r.regional>1||!std::isfinite(r.fidelity)||r.fidelity<0||r.fidelity>1||!std::isfinite(r.sceneStrength)||r.sceneStrength<0||r.sceneStrength>1||!std::isfinite(r.feather)||r.feather<0||r.feather>8)return false;
 for(unsigned g=0;g<Groups;++g)for(unsigned i=0;i<FieldCount;++i)if(!nrcontrolsabi::Valid(Fields[i],r.values[g][i]))return false;
 return true;
}
inline Recipe CaptureValues(const float* values){
 Recipe r;std::memcpy(r.model,Model,sizeof(Model));
 for(unsigned g=0;g<Groups;++g)for(unsigned i=0;i<FieldCount;++i)r.values[g][i]=values[Fields[i]];
 return r;
}
// Starting points for manual visual acceptance, never claims of face identity.
// Normalization/HDR values and resolution budgets come from the user's capture.
inline Recipe MakePreset(const Recipe& base,PresetKind kind){
 Recipe r=base;r.kind=kind;if(kind==Custom)return r;
 r.regional=1;r.fidelity=kind==Restore?.95f:.35f;r.sceneStrength=kind==Restore?.7f:1.f;r.feather=2;
 for(unsigned g=0;g<Groups;++g){auto group=Group(g);
  Set(r,group,Passes,1);Set(r,group,Grade,1);Set(r,group,PreStyle,0);Set(r,group,PreStyleStrength,0);
  Set(r,group,Exposure,0);Set(r,group,Tint,0);Set(r,group,Highlights,.04f);
  Set(r,group,Contrast,kind==Realistic?1.04f:1.f);Set(r,group,Saturation,kind==Realistic?.95f:1.f);
  Set(r,group,Warmth,kind==Realistic?-.04f:0.f);Set(r,group,Blend,100);
  Set(r,group,NaturalLook,0);Set(r,group,SkinLift,0);Set(r,group,Sharpen,kind==Realistic?.12f:.06f);
  Set(r,group,Mas,0);Set(r,group,Colour,kind==Realistic?.7f:.35f);
  for(int layer=0;layer<3;++layer){
   Set(r,group,LayerId(Preset,layer),0);Set(r,group,LayerId(Style,layer),1);
   const bool face=group==Character;
   Set(r,group,LayerId(Intensity,layer),kind==Realistic?(face?.8f:1.15f):(face?.25f:.6f));
   Set(r,group,LayerId(Structure,layer),kind==Realistic?(face?.7f:1.05f):(face?.2f:.55f));
   Set(r,group,LayerId(LocalTone,layer),kind==Realistic?.75f:.35f);
   Set(r,group,LayerId(GlobalTone,layer),1);Set(r,group,LayerId(Skin,layer),kind==Realistic?.65f:.2f);
   Set(r,group,LayerId(AutoMask,layer),1);Set(r,group,LayerId(UiCorrect,layer),1);
  }
 }
 return r;
}
// S18 (user: the scene column needs no skin options; the character column does).
// Scene layers therefore have no skin-specific model settings: skin structure
// follows each layer's detail strength (-1) and the model auto skin mask keeps
// its default (on). Applied recipes are normalized so no hidden value acts.
inline constexpr float SceneSkinStructure=-1.f,SceneAutoMask=1.f;
inline bool NeutralSceneSkin(Recipe& r){
 bool changed=false;
 for(int layer=0;layer<3;++layer){
  for(auto [id,value]:{std::pair<Id,float>{LayerId(Skin,layer),SceneSkinStructure},{LayerId(AutoMask,layer),SceneAutoMask}}){
   const int i=Index(id);if(i>=0&&r.values[Scene][i]!=value){r.values[Scene][i]=value;changed=true;}
  }
 }
 return changed;
}
template<class Cfg> void NeutralSceneSkin(Cfg& cfg){
 cfg.skin_structure=SceneSkinStructure;cfg.auto_mask=int(SceneAutoMask);
 for(auto& layer:cfg.extra){layer.skin=SceneSkinStructure;layer.autoMask=int(SceneAutoMask);}
}
inline uint32_t Checksum(std::string_view s){uint32_t h=2166136261u;for(unsigned char c:s)h=(h^c)*16777619u;return h;}
inline std::string Encode(const Recipe& r){
 if(!Valid(r))return {};std::string out="033YY2|";out+=Model;out+='|';char buf[64];
 auto append=[&](auto value){auto result=std::to_chars(buf,buf+sizeof(buf),value);out.append(buf,result.ptr);out+=',';};
 append(r.kind);append(r.regional);append(r.fidelity);append(r.sceneStrength);append(r.feather);for(const auto& group:r.values)for(float v:group)append(v);
 out+='|';auto result=std::to_chars(buf,buf+sizeof(buf),Checksum(out),16);out.append(buf,result.ptr);return out;
}
enum class DecodeResult {Ok,TooLong,Format,VersionOrModel,Checksum,Value};
inline DecodeResult Decode(std::string_view code,Recipe& output){
 if(code.size()>MaxCode)return DecodeResult::TooLong;
 const std::string prefix=std::string("033YY2|")+Model+"|";
 if(code.substr(0,prefix.size())!=prefix)return DecodeResult::VersionOrModel;
 const auto tail=code.rfind('|');if(tail==std::string_view::npos||tail<prefix.size())return DecodeResult::Format;
 uint32_t crc=0;auto check=std::from_chars(code.data()+tail+1,code.data()+code.size(),crc,16);
 if(check.ec!=std::errc()||check.ptr!=code.data()+code.size()||crc!=Checksum(code.substr(0,tail+1)))return DecodeResult::Checksum;
 Recipe candidate;std::memcpy(candidate.model,Model,sizeof(Model));const char* p=code.data()+prefix.size();const char* end=code.data()+tail;
 auto take=[&](auto& value){auto result=std::from_chars(p,end,value);if(result.ec!=std::errc()||result.ptr==end||*result.ptr!=',')return false;p=result.ptr+1;return true;};
 if(!take(candidate.kind)||!take(candidate.regional)||!take(candidate.fidelity)||!take(candidate.sceneStrength)||!take(candidate.feather))return DecodeResult::Format;
 for(auto& group:candidate.values)for(float& v:group)if(!take(v))return DecodeResult::Format;
 if(p!=end)return DecodeResult::Format;if(!Valid(candidate))return DecodeResult::Value;
 output=candidate;return DecodeResult::Ok;
}
// Whole-frame request queue only. Regions cannot silently fall back to colour
// thresholds, face boxes or applying a group's recipe over the whole picture.
enum class SubmitResult:uint32_t {Accepted,Invalid,RegionsUnavailable,Stale};
struct RequestQueue {
 Recipe next{};uint64_t revision=0,consumed=0;bool queued=false;
 SubmitResult Submit(const Recipe& r,bool regional,uint64_t expected,bool regionsAvailable=false){
  if(!Valid(r))return SubmitResult::Invalid;if(regional&&!regionsAvailable)return SubmitResult::RegionsUnavailable;
  if(expected!=revision)return SubmitResult::Stale;next=r;next.regional=regional?1u:0u;++revision;queued=true;return SubmitResult::Accepted;
 }
 template<class Sink>bool Consume(Sink&& sink){if(!queued)return false;sink(next);consumed=revision;queued=false;return true;}
};
}
