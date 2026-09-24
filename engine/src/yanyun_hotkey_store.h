#pragma once
// S37: reading and writing %LOCALAPPDATA%\033YanYunRuntime\hotkey.cfg. Kept apart from the key
// table so the panel headers stay free of <Windows.h>.
#include <Windows.h>
#include <cstdio>
#include <string>
#include "yanyun_hotkey.h"
namespace hotkey033 {
inline std::wstring File(){
 wchar_t base[32768]{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);
 return n&&n<32768?std::wstring(base)+L"\\033YanYunRuntime\\hotkey.cfg":std::wstring();
}
inline int Load(){
 const auto file=File();if(file.empty())return Default;
 FILE* f=nullptr;if(_wfopen_s(&f,file.c_str(),L"rb")||!f)return Default;
 char text[64]{};const size_t read=std::fread(text,1,sizeof(text)-1,f);std::fclose(f);text[read]=0;return ParseStored(text);
}
inline bool Save(int vk){
 const auto file=File();if(file.empty()||!Allowed(vk))return false;
 CreateDirectoryW(file.substr(0,file.find_last_of(L'\\')).c_str(),nullptr);
 const auto temp=file+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
 FILE* f=nullptr;if(_wfopen_s(&f,temp.c_str(),L"wb")||!f)return false;
 const bool written=std::fprintf(f,"schema=1\nkey=%d\n",vk)>0;
 const bool closed=std::fclose(f)==0;
 if(!(written&&closed&&MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))){DeleteFileW(temp.c_str());return false;}
 return true;
}
}
