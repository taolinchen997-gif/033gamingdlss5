#pragma once
// S32 (owner 2026-09-24): 超分模型 -- which model the game's own DLSS Super Resolution
// uses (游戏默认 / K / M / L). Kept per user in %LOCALAPPDATA%\033YanYunRuntime\
// sr-model.cfg, handed to the bundled OptiScaler's preset override before the game
// creates DLSS, and changed live from the SR page (the DLSS feature is recreated
// once). What the creation really asked NGX for comes back in the status.
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "sr_model_abi.h"
extern "C" int __cdecl K033_SetSrPreset(uint32_t preset,uint32_t recreate);
extern "C" int __cdecl K033_GetSrPresetStatus(srmodelabi::Status* out);
namespace srmodel033 {
inline std::wstring File(){
 wchar_t base[32768]{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);
 return n&&n<32768?std::wstring(base)+L"\\033YanYunRuntime\\sr-model.cfg":std::wstring();
}
inline uint32_t Load(){
 const auto file=File();if(file.empty())return srmodelabi::GameDefault;
 FILE* f=nullptr;if(_wfopen_s(&f,file.c_str(),L"rb")||!f)return srmodelabi::GameDefault;
 char text[64]{};const size_t read=std::fread(text,1,sizeof(text)-1,f);std::fclose(f);text[read]=0;return srmodelabi::ParseStored(text);
}
inline bool Save(uint32_t preset){
 const auto file=File();if(file.empty()||!srmodelabi::Allowed(preset))return false;
 CreateDirectoryW(file.substr(0,file.find_last_of(L'\\')).c_str(),nullptr);
 const auto temp=file+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
 FILE* f=nullptr;if(_wfopen_s(&f,temp.c_str(),L"wb")||!f)return false;
 const bool written=std::fprintf(f,"schema=1\npreset=%u\n",unsigned(preset))>0;
 const bool closed=std::fclose(f)==0;
 if(!(written&&closed&&MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))){DeleteFileW(temp.c_str());return false;}
 return true;
}
inline const char* Letter(uint32_t preset){
 return preset==srmodelabi::K?"K":preset==srmodelabi::M?"M":preset==srmodelabi::L?"L":preset==srmodelabi::GameDefault?"game default":"?";
}
// Before the game's first DLSS creation: the stored choice waits for it.
inline void ApplyStored(){
 const auto preset=Load();const int ok=K033_SetSrPreset(preset,0);
 Log("[033 SR model] stored choice %s (%u) handed to the DLSS preset override: %s; render size ratio %.2f (0 = the game's own quality mode); takes effect when the game creates DLSS",Letter(preset),unsigned(preset),ok?"accepted":"refused",double(srmodelabi::LinkedRatio(preset)));
}
// SR page: apply now (one DLSS recreation) and remember it for the next start.
enum class RequestResult {Applied,Refused,NotSaved};
inline RequestResult Request(uint32_t preset){
 if(!srmodelabi::Allowed(preset))return RequestResult::Refused;
 const int ok=K033_SetSrPreset(preset,1);const bool saved=ok&&Save(preset);
 Log("[033 SR model] panel chose %s (%u): core %s, saved %s; the DLSS feature is recreated once; render size ratio %.2f (0 = the game's own quality mode), used from the game's next render-size question",
  Letter(preset),unsigned(preset),ok?"accepted":"refused",saved?"yes":"NO",double(srmodelabi::LinkedRatio(preset)));
 return !ok?RequestResult::Refused:saved?RequestResult::Applied:RequestResult::NotSaved;
}
inline srmodelabi::Status Status(){srmodelabi::Status s;if(!K033_GetSrPresetStatus(&s))s=srmodelabi::Status{};return s;}
// S33: log when the game asks for its render size -- the first time, whenever our answer
// changes, and a count now and then -- so one session shows whether the game asks every
// frame (the linkage is instant) or only when its own settings change.
inline void PollLog(){
 static uint32_t loggedQueries=0,lastW=0,lastH=0,lastMode=~0u,lastForced=~0u;static ULONGLONG lastCountLog=0;
 const auto s=Status();if(!s.queries||s.queries==loggedQueries)return;
 const bool changed=s.renderW!=lastW||s.renderH!=lastH||s.gameMode!=lastMode||s.forcedMilli!=lastForced;
 const ULONGLONG now=GetTickCount64();
 if(changed||!loggedQueries||now-lastCountLog>=60000){
  Log("[033 SR model] game asked for its render size %u times so far; last: output %ux%u, game quality mode %u -> render %ux%u (forced ratio %.2f, 0 = the game's own); size pending=%u",
   s.queries,s.outputW,s.outputH,s.gameMode,s.renderW,s.renderH,double(s.forcedMilli)/1000.0,s.sizePending);
  lastCountLog=now;
 }
 loggedQueries=s.queries;lastW=s.renderW;lastH=s.renderH;lastMode=s.gameMode;lastForced=s.forcedMilli;
}
}
