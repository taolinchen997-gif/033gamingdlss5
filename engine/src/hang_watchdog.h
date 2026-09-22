#pragma once
// S25 (user 2026-09-22: 「玩着玩着会闪退」). What the player sees as a crash is an
// application hang: the game stops presenting, Windows marks the window as not
// responding and logs event 1002 / AppHangB1 when it is closed. That happened six
// times on 2026-09-21/22, and with the same hang signature since 2026-09-04 under
// many different cores, but Windows keeps no dump for a hang, so nobody could see
// where it stops. This watchdog writes ONE dump of the process (every thread's
// stack, handles, modules) when the game has stopped presenting AND Windows
// reports its window as hung. It changes nothing in the game and never ends it.
#include <windows.h>
#include <dbghelp.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include "../runtime/shared/settings_session_native.h"
namespace hangwatch033 {
// IsHungAppWindow itself needs 5 s without message processing; one more second of
// no presents keeps loading screens, pauses and alt-tab out of it.
inline constexpr ULONGLONG StallMs=6000;
// Pure decision (CPU-tested): a real hang, not a pause, a minimise or a load screen.
inline bool ShouldDump(ULONGLONG nowMs,ULONGLONG lastPresentMs,bool windowKnown,bool visible,bool iconic,bool hung,bool alreadyWritten){
 return !alreadyWritten&&lastPresentMs!=0&&nowMs>=lastPresentMs+StallMs&&windowKnown&&visible&&!iconic&&hung;
}
inline std::atomic<ULONGLONG> lastPresent{0};
inline std::atomic<HWND> window{nullptr};
inline std::atomic<bool> started{false},written{false};
// Set by the core: its logger may be held by the stuck thread, so it is used last.
inline void (*sink)(const char*)=nullptr;
using WriteDumpFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
inline void Note(const std::wstring& path,const char* text){
 HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE)return;
 DWORD done=0;WriteFile(f,text,DWORD(std::strlen(text)),&done,nullptr);CloseHandle(f);
}
inline void Run(){
 // Everything the dump needs is prepared now, never while a stuck thread might
 // hold the loader lock. A module reference keeps this code mapped.
 HMODULE self=nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Run),&self);
 HMODULE dbghelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
 const auto writeDump=dbghelp?reinterpret_cast<WriteDumpFn>(GetProcAddress(dbghelp,"MiniDumpWriteDump")):nullptr;
 std::wstring folder;DWORD failure=0;
 if(k033settings::NativeSettingsLocation::resolve(folder,failure)==K033_OK){
  CreateDirectoryW(folder.c_str(),nullptr);folder+=L"\\hang";CreateDirectoryW(folder.c_str(),nullptr);
 }else folder.clear();
 for(;;){
  Sleep(500);
  const HWND w=window.load(std::memory_order_relaxed);
  const bool known=w!=nullptr&&IsWindow(w)!=FALSE;
  // None of these send a message to the window, so none can block on it.
  const bool visible=known&&IsWindowVisible(w),iconic=known&&IsIconic(w),hung=known&&IsHungAppWindow(w);
  const ULONGLONG last=lastPresent.load(std::memory_order_relaxed),now=GetTickCount64();
  if(!ShouldDump(now,last,known,visible,iconic,hung,written.load()))continue;
  written.store(true);
  SYSTEMTIME t{};GetLocalTime(&t);wchar_t stem[96]{};
  swprintf_s(stem,L"\\hang-%04u%02u%02u-%02u%02u%02u-%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
  BOOL ok=FALSE;DWORD error=ERROR_PATH_NOT_FOUND;LARGE_INTEGER size{};
  if(writeDump&&!folder.empty()){
   HANDLE f=CreateFileW((folder+stem+L".dmp").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
   if(f!=INVALID_HANDLE_VALUE){
    // S26: plus the memory that stacks and registers point at (lock owners,
    // container nodes), so the next dump shows the data, not only the calls.
    const auto type=MINIDUMP_TYPE(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithHandleData|MiniDumpWithUnloadedModules|MiniDumpWithProcessThreadData|MiniDumpWithIndirectlyReferencedMemory);
    ok=writeDump(GetCurrentProcess(),GetCurrentProcessId(),f,type,nullptr,nullptr,nullptr);
    error=ok?0:GetLastError();GetFileSizeEx(f,&size);CloseHandle(f);
   }else error=GetLastError();
  }else if(!writeDump)error=ERROR_PROC_NOT_FOUND;
  char text[512]{};
  std::snprintf(text,sizeof text,"[033 hang] no present for %llu ms and Windows reports the game window as not responding; dump=%s bytes=%lld error=%lu pid=%lu. "
   "Send this folder (%%LOCALAPPDATA%%\\033YanYunRuntime\\hang) to the author. 游戏卡死时自动留下的现场，发给作者。",
   now-last,ok?"written":"failed",size.QuadPart,error,GetCurrentProcessId());
  if(!folder.empty())Note(folder+stem+L".txt",(std::string(text)+"\r\n").c_str());
  if(sink)sink(text);
  if(self)FreeLibraryAndExitThread(self,0);
  return;
 }
}
// Every real present (the core's present event). Starts the watchdog once.
inline void OnPresent(HWND w){
 lastPresent.store(GetTickCount64(),std::memory_order_relaxed);
 if(w)window.store(w,std::memory_order_relaxed);
 bool expected=false;
 if(started.compare_exchange_strong(expected,true))std::thread(Run).detach();
}
}
