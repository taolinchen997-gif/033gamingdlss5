#pragma once
#include "yanyun_recipe.h"
#include "yanyun_recipe_library.h"
#include "../runtime/shared/settings_session_native.h"
namespace yanyunrecipe {
// User-triggered draft IO only; no per-frame writes and no implicit application.
// The same pinned directory/session gate prevents installer/runtime overlap.
// Plain file in the pinned folder; never a directory or reparse point.
inline bool ReadStoreFile(const std::wstring& path,size_t maxBytes,bool allowEmpty,std::string& data,bool* missing=nullptr){
 HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(missing)*missing=f==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_NOT_FOUND;
 if(f==INVALID_HANDLE_VALUE)return false;
 BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
 bool ok=GetFileInformationByHandle(f,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileSizeEx(f,&size)&&
  (allowEmpty?size.QuadPart>=0:size.QuadPart>0)&&size.QuadPart<=LONGLONG(maxBytes);
 data.assign(ok?size_t(size.QuadPart):0,'\0');DWORD read=0;
 if(ok&&!data.empty())ok=ReadFile(f,data.data(),DWORD(data.size()),&read,nullptr)&&read==data.size();
 CloseHandle(f);return ok;
}
// Locked, written to a temporary file, flushed, then atomically replaced.
inline bool WriteStoreFile(const std::wstring& path,const std::string& data){
 const auto lockPath=path+L".lock";
 HANDLE lock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(lock==INVALID_HANDLE_VALUE)return false;
 BY_HANDLE_FILE_INFORMATION info{};bool ok=GetFileInformationByHandle(lock,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
 const auto temp=path+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
 HANDLE f=ok?CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr):INVALID_HANDLE_VALUE;
 if(f==INVALID_HANDLE_VALUE){CloseHandle(lock);return false;}
 DWORD written=0;ok=(data.empty()||(WriteFile(f,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()))&&FlushFileBuffers(f);CloseHandle(f);
 if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(temp.c_str());CloseHandle(lock);return ok;
}
template<class Location=k033settings::NativeSettingsLocation>class Store {
 k033settings::SettingsSessionGate<Location> session;
 bool active=false;
 std::wstring Name()const{return active?L"\\active-recipe.v2.txt":L"\\recipes.v2.txt";}
public:
 explicit Store(bool applied=false):active(applied){}
 bool Load(Recipe& output){
  if(session.join()!=K033_OK)return false;
  std::string data;return ReadStoreFile(session.folder()+Name(),MaxCode,false,data)&&Decode(data,output)==DecodeResult::Ok;
 }
 bool Save(const Recipe& r){
  const std::string data=Encode(r);if(data.empty()||session.join()!=K033_OK)return false;
  return WriteStoreFile(session.folder()+Name(),data);
 }
};
// S23 (user): saved records, "recipe-library.v1.txt" next to the single-slot
// file of earlier builds. Missing = no record file yet; Failed = the folder or
// an existing file could not be read, and the caller must not write over it.
enum class LibraryRead {Ok,Missing,Failed};
template<class Location=k033settings::NativeSettingsLocation>class LibraryStore {
 k033settings::SettingsSessionGate<Location> session;
public:
 LibraryRead Load(std::vector<LibraryEntry>& output){
  output.clear();if(session.join()!=K033_OK)return LibraryRead::Failed;
  std::string data;bool missing=false;
  if(!ReadStoreFile(session.folder()+L"\\recipe-library.v1.txt",LibraryBytes,true,data,&missing))return missing?LibraryRead::Missing:LibraryRead::Failed;
  ParseLibrary(data,output);return LibraryRead::Ok;
 }
 bool Save(const std::vector<LibraryEntry>& list){
  if(list.size()>LibraryMax||session.join()!=K033_OK)return false;
  return WriteStoreFile(session.folder()+L"\\recipe-library.v1.txt",SerializeLibrary(list));
 }
};
}
