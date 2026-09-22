#include <Windows.h>
#include <string>
#include <vector>
#include <cstdio>
struct MockModule {std::wstring path;uintptr_t handle;bool primary=true,companion=true,forwarded=false;};
static std::vector<MockModule> modules;
static bool truncatePath=false,pinFails=false;static unsigned checks=0,failed=0;
static DWORD WINAPI MockFileName(HMODULE,LPWSTR out,DWORD size){
    if(truncatePath)return size;wcscpy_s(out,size,L"C:\\Fixture\\game.exe");return DWORD(wcslen(out));
}
static HMODULE WINAPI MockModuleHandle(LPCWSTR path){for(auto& m:modules)if(m.path==path)return reinterpret_cast<HMODULE>(m.handle);return nullptr;}
static FARPROC WINAPI MockProc(HMODULE module,LPCSTR name){for(auto& m:modules)if(m.handle==uintptr_t(module)){
    if(std::string(name)=="role"&&m.primary)return reinterpret_cast<FARPROC>(m.handle+100);
    if(std::string(name)=="companion"&&m.companion)return reinterpret_cast<FARPROC>(m.handle+200);
}return nullptr;}
static BOOL WINAPI MockModuleEx(DWORD flags,LPCWSTR address,HMODULE* out){
    if(pinFails&&(flags&GET_MODULE_HANDLE_EX_FLAG_PIN))return FALSE;
    for(auto& m:modules)if(uintptr_t(address)==m.handle+100||uintptr_t(address)==m.handle+200){
        *out=reinterpret_cast<HMODULE>(m.forwarded?999:m.handle);return TRUE;
    }return FALSE;
}
#define GetModuleFileNameW MockFileName
#define GetModuleHandleW MockModuleHandle
#define GetProcAddress MockProc
#define GetModuleHandleExW MockModuleEx
#include "nr_game_module.h"
static void Check(bool ok,const char* name){++checks;if(!ok){++failed;printf("FAIL %s\n",name);}}
int main(){using namespace nrgame033;
    modules={{L"C:\\Windows\\System32\\winmm.dll",1000}};
    Check(!FindGameExport("role","companion").address,"system module is never selected by basename");
    for(auto name:ModuleNames){modules={{std::wstring(L"C:\\Fixture\\")+name,1000}};
        auto found=FindGameExport("role","companion");Check(found.module==1000&&PinGameExport(found),"full game-local path found and pinned");}
    modules={{L"C:\\Fixture\\version.dll",1000,true,false}};
    Check(!FindGameExport("role","companion").address,"companion export required");
    modules={{L"C:\\Fixture\\version.dll",1000,true,true,true}};
    Check(!FindGameExport("role","companion").address,"forwarded export from another module rejected");
    modules={{L"C:\\Fixture\\version.dll",1000},{L"C:\\Fixture\\winmm.dll",2000}};
    Check(FindGameExport("role","companion").ambiguous,"two local candidates do not silently pick one");
    modules={{L"C:\\Fixture\\version.dll",1000}};pinFails=true;
    Check(!PinGameExport(FindGameExport("role","companion")),"pin failure cannot produce dangling callback");pinFails=false;
    truncatePath=true;Check(!FindGameExport("role","companion").address,"truncated executable path rejected");
    printf("Automatic module discovery mock CPU: %u checks, %u failures; all Win32 module APIs replaced, no DLL load\n",checks,failed);
    return failed?1:0;
}
