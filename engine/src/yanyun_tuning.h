#pragma once
// S35 (owner 2026-09-24, four screenshots of the 前置 / SR / NR / FG pages: 「以上参数专门设置个
// 启用033特调，放在大标志033旁边，跟033这个logo风格差不多」). The owner's own settings as a
// built-in preset: the recipe they applied at 14:06 (active-recipe.v2.txt, verbatim: 模式二, first
// layer 110 %, person pre-grade 柔和, scene 自然 with 2 layers, person blend 85 %), SR model L (K on
// RTX 20/30, as the owner's own labels advise) and 6x native frame generation (RTX 40/50 only;
// the 20/30 bridge keeps its own setting, which applies after a restart).
// S36 (owner, with 033特调 on and three more screenshots: 「033特调对应这样调整」): the recipe they
// applied at 14:42 instead -- first layer 100 %, detail 1.00, person contrast 0.91 and blend 95 %,
// scene saturation 0.87; everything else unchanged.
// On: the player's current recipe, SR model and multiplier go to 033-tuning-before.v1.txt and the
// preset is applied. Off: they are put back. The file's presence is the switch; no value guessing.
#include "yanyun_recipe_store.h"
#include "sr_model_abi.h"
#include <charconv>
#include <string>
#include <string_view>
namespace yanyuntuning {
inline constexpr char Code[]=
 "033YY2|6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927|0,2,0.9958234,1,5.465394,100,"
 "3,0,0,1,2,1,2,2,-1,1,1,100,1,1,1,3.16,3,0.7,203,1,0,1.04,0.95,-0.04,0,0.04,0,0.2,0.05,8,0.12,0,100,0,"
 "0,0,1,2,0,1.9782971,2,2,-1,2,1,1,2,0,2,2,2,-1,2,1,1,100,0,0,140,1,0,0,2,1,1,1,1,-1,1,1,95,1,1,1,3.16,"
 "3,1,203,1,0.26217222,0.9073034,1.0224719,0.079588026,-0.00093632936,0,1,0.25468826,0.25,7.507784,0.2991295,"
 "0,70,2,1,0,1,1,0,2,2,2,1.977387,2,1,1,1,0,2,2,2,2,1.9949749,1,1,70,0,0,100,2,0,0,2,2,2,2,2,-1,1,1,100,"
 "1,1,1,3.16,3,0.7,203,1,0.24719095,1.014045,0.8707865,-0.088951305,0.004681647,0.035580523,0,0.2,0.05,"
 "8,1,0,100,1,1,0,1,2,0,2,2,2,-1,2,1,1,2,0,2,2,2,-1,2,1,1,100,0,0,|a2b9a75e";
inline bool Preset(yanyunrecipe::Recipe& out){return yanyunrecipe::Decode(Code,out)==yanyunrecipe::DecodeResult::Ok;}
inline uint32_t SrModelFor(unsigned gpuGeneration){return gpuGeneration==20||gpuGeneration==30?srmodelabi::K:srmodelabi::L;}
inline constexpr uint32_t Multiplier=6;
inline bool ValidMultiplier(uint32_t count){return count==0||(count>=2&&count<=6);}
struct Before {yanyunrecipe::Recipe recipe{};uint32_t srModel=srmodelabi::GameDefault,multiplier=0;bool frameGeneration=false;};
inline std::string Serialize(const Before& b){
 const auto recipe=yanyunrecipe::Encode(b.recipe);
 if(recipe.empty()||!srmodelabi::Allowed(b.srModel)||!ValidMultiplier(b.multiplier))return {};
 return "schema=1\nrecipe="+recipe+"\nsrmodel="+std::to_string(b.srModel)+"\nfg="+(b.frameGeneration?"1":"0")+
  "\nmultiplier="+std::to_string(b.multiplier)+"\n";
}
inline bool Parse(std::string_view text,Before& out){
 Before b;bool schema=false,recipe=false,sr=false,fg=false,multiplier=false;
 while(!text.empty()){
  const auto end=text.find('\n');if(end==std::string_view::npos)return false; // every line ends with a newline
  const auto line=text.substr(0,end);text.remove_prefix(end+1);
  const auto eq=line.find('=');if(eq==std::string_view::npos)return false;
  const auto key=line.substr(0,eq),value=line.substr(eq+1);
  auto number=[&](uint32_t& n){auto r=std::from_chars(value.data(),value.data()+value.size(),n);return r.ec==std::errc()&&r.ptr==value.data()+value.size();};
  uint32_t n=0;
  if(key=="schema"){if(schema||!number(n)||n!=1)return false;schema=true;}
  else if(key=="recipe"){if(recipe||yanyunrecipe::Decode(value,b.recipe)!=yanyunrecipe::DecodeResult::Ok)return false;recipe=true;}
  else if(key=="srmodel"){if(sr||!number(n)||!srmodelabi::Allowed(n))return false;b.srModel=n;sr=true;}
  else if(key=="fg"){if(fg||!number(n)||n>1)return false;b.frameGeneration=n==1;fg=true;}
  else if(key=="multiplier"){if(multiplier||!number(n)||!ValidMultiplier(n))return false;b.multiplier=n;multiplier=true;}
  else return false;
 }
 if(!(schema&&recipe&&sr&&fg&&multiplier))return false;
 out=b;return true;
}
enum class Read {Ok,Missing,Unreadable};
enum class Presence {Yes,No,Unknown}; // Unknown: the settings folder is not reachable yet; ask again later
template<class Location=k033settings::NativeSettingsLocation>class Store {
 k033settings::SettingsSessionGate<Location> session;
 std::wstring Path(){return session.folder()+L"\\033-tuning-before.v1.txt";}
public:
 Presence Exists(){
  if(session.join()!=K033_OK)return Presence::Unknown;
  if(GetFileAttributesW(Path().c_str())!=INVALID_FILE_ATTRIBUTES)return Presence::Yes;
  const DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?Presence::No:Presence::Unknown;
 }
 Read Load(Before& out){
  if(session.join()!=K033_OK)return Read::Unreadable;
  std::string data;bool missing=false;
  if(!yanyunrecipe::ReadStoreFile(Path(),yanyunrecipe::MaxCode+256,false,data,&missing))return missing?Read::Missing:Read::Unreadable;
  return Parse(data,out)?Read::Ok:Read::Unreadable;
 }
 bool Save(const Before& b){
  const auto data=Serialize(b);if(data.empty()||session.join()!=K033_OK)return false;
  return yanyunrecipe::WriteStoreFile(Path(),data);
 }
 bool Remove(){
  if(session.join()!=K033_OK)return false;
  return DeleteFileW(Path().c_str())||GetLastError()==ERROR_FILE_NOT_FOUND;
 }
};
}
