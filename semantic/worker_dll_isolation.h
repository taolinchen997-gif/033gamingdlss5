// The private worker must not inherit the game's proxy DLL search route.
// All changes below affect this child process only, before any graphics load.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace yyworker {
inline std::filesystem::path ModulePath(HMODULE module){
 wchar_t path[32768]{};const auto n=GetModuleFileNameW(module,path,32768);
 if(!n||n>=32768)throw std::runtime_error("worker loaded module path unavailable");
 return std::filesystem::path(std::wstring(path,n));
}
inline bool SameModulePath(HMODULE module,const std::filesystem::path& expected){
 return _wcsicmp(ModulePath(module).lexically_normal().c_str(),expected.lexically_normal().c_str())==0;
}
inline void IsolateDllSearch(){
 // SetDllDirectory is inherited from the launching game. Clearing it after a
 // static DXGI import is too late, hence the worker has no graphics imports.
 if(!SetDllDirectoryW(L"")||!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32))
  throw std::runtime_error("worker DLL search isolation failed");
}
inline HMODULE LoadExactLibrary(const std::filesystem::path& path,DWORD flags){
 if(!path.is_absolute())throw std::runtime_error("worker DLL absolute path required");
 if(auto prior=GetModuleHandleW(path.filename().c_str());prior&&!SameModulePath(prior,path))
  throw std::runtime_error("worker unexpected preloaded DLL; refusing mixed graphics libraries");
 const auto loaded=LoadLibraryExW(path.c_str(),nullptr,flags);
 if(!loaded)throw std::runtime_error("worker explicit DLL load failed");
 if(!SameModulePath(loaded,path)){FreeLibrary(loaded);throw std::runtime_error("worker loaded DLL path mismatch");}
 // The graphics modules deliberately remain loaded until process exit, as
 // device/session children may retain their code after this helper returns.
 return loaded;
}
inline HMODULE LoadSystemLibrary(const wchar_t* name){
 if(!name||std::filesystem::path(name).filename()!=name)throw std::runtime_error("worker system DLL name invalid");
 wchar_t directory[32768]{};const auto n=GetSystemDirectoryW(directory,32768);
 if(!n||n>=32768)throw std::runtime_error("worker System32 path unavailable");
 return LoadExactLibrary(std::filesystem::path(directory)/name,LOAD_LIBRARY_SEARCH_SYSTEM32);
}
}
