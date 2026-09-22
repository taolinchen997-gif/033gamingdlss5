#pragma once
#include "yanyun_appearance.h"
#include <Windows.h>
#include <string>
#include <cstdio>
namespace yyappearance {
inline std::wstring Directory(){wchar_t base[32768]{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);return n&&n<32768?std::wstring(base)+L"\\033YanYunRuntime":std::wstring();}
inline unsigned long long nextRead=0;
inline bool saveFailed=false;
inline void Save(){
 const auto dir=Directory();if(dir.empty()){saveFailed=true;return;}CreateDirectoryW(dir.c_str(),nullptr);
 const auto file=dir+L"\\appearance.cfg",temp=file+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
 FILE* f=nullptr;if(_wfopen_s(&f,temp.c_str(),L"wb")||!f){saveFailed=true;return;}
 const bool written=std::fprintf(f,"schema=1\nlight=%u\nenglish=%u\n",unsigned(light),unsigned(english))>0;
 const bool closed=std::fclose(f)==0;
 saveFailed=!(written&&closed&&MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));
 if(!saveFailed)nextRead=GetTickCount64()+1000;
}
inline void Refresh(){
 changed=&Save;const auto now=GetTickCount64();if(now<nextRead)return;nextRead=now+1000;
 const auto dir=Directory();if(dir.empty())return;FILE* f=nullptr;
 if(_wfopen_s(&f,(dir+L"\\appearance.cfg").c_str(),L"rb")||!f)return;
 char line[128];bool nextLight=light,nextEnglish=english,valid=false;
 while(std::fgets(line,sizeof(line),f)){if(!std::strcmp(line,"schema=1\n"))valid=true;
  if(!std::strcmp(line,"light=0\n"))nextLight=false;else if(!std::strcmp(line,"light=1\n"))nextLight=true;
  if(!std::strcmp(line,"english=0\n"))nextEnglish=false;else if(!std::strcmp(line,"english=1\n"))nextEnglish=true;}
 std::fclose(f);if(valid){light=nextLight;english=nextEnglish;}
}
}
