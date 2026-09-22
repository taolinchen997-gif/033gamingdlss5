#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#define ImTextureID ImU64
#include <imgui.h>

// Compile the actual generated ReShade SDK used by the core. Only its module
// lookup imports are replaced; no DLL/device/window/SDK runtime is loaded.
FARPROC WINAPI Beta2MockGetProcAddress(HMODULE,LPCSTR);
HANDLE WINAPI Beta2MockGetCurrentProcess();
#define GetProcAddress Beta2MockGetProcAddress
#define GetCurrentProcess Beta2MockGetCurrentProcess
#define K32EnumProcessModules Beta2MockEnumProcessModules
#include <reshade.hpp>
#undef K32EnumProcessModules
#undef GetCurrentProcess
#undef GetProcAddress

static const auto core=reinterpret_cast<HMODULE>(uintptr_t(0x0330));
static const auto host=reinterpret_cast<HMODULE>(uintptr_t(0x6800));
static const auto foreign=reinterpret_cast<HMODULE>(uintptr_t(0x9911));
static int checks=0,failures=0,registered=0,logged=0,tableRequests=0,events=0,overlays=0;
static void* logOwner=nullptr;
static void check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);}}
static bool FakeRegister(void* owner,uint32_t api){
    // The fork's actual owner gate executes before its invalid-module log.
    if(owner!=core)return false;
    if(api!=RESHADE_API_VERSION)return false;
    ++registered;return true;
}
static void FakeUnregister(void*){}
static void FakeLog(void* owner,int,const char*){logOwner=owner;++logged;}
static const imgui_function_table* FakeTable(uint32_t version){
    ++tableRequests;
    static const imgui_function_table table{};
    return version==19250?&table:nullptr;
}
static void FakeEvent(void* owner,reshade::addon_event,void*){if(owner==core)++events;}
static void FakeOverlay(void* owner,const char*,void(*)(reshade::api::effect_runtime*)){if(owner==core)++overlays;}
FARPROC WINAPI Beta2MockGetProcAddress(HMODULE module,LPCSTR name){
    if(module!=host)return nullptr;
    if(!std::strcmp(name,"ReShadeRegisterAddon"))return reinterpret_cast<FARPROC>(&FakeRegister);
    if(!std::strcmp(name,"ReShadeUnregisterAddon"))return reinterpret_cast<FARPROC>(&FakeUnregister);
    if(!std::strcmp(name,"ReShadeLogMessage"))return reinterpret_cast<FARPROC>(&FakeLog);
    if(!std::strcmp(name,"ReShadeGetImGuiFunctionTable"))return reinterpret_cast<FARPROC>(&FakeTable);
    if(!std::strcmp(name,"ReShadeRegisterEventForAddon"))return reinterpret_cast<FARPROC>(&FakeEvent);
    if(!std::strcmp(name,"ReShadeRegisterOverlayForAddon"))return reinterpret_cast<FARPROC>(&FakeOverlay);
    return nullptr;
}
HANDLE WINAPI Beta2MockGetCurrentProcess(){return reinterpret_cast<HANDLE>(intptr_t(-1));}
extern "C" BOOL WINAPI Beta2MockEnumProcessModules(HANDLE,HMODULE* modules,DWORD bytes,LPDWORD needed){
    *needed=DWORD(2*sizeof(HMODULE));if(bytes<*needed)return FALSE;
    modules[0]=core;modules[1]=host;return TRUE;
}
static void EarlyMfgLog(){reshade::log::message(reshade::log::level::info,"CPU early MFG log");}
static bool SeedOwner(){return reshade::internal::get_current_module_handle(core)==core;}
static void OnDevice(reshade::api::device*){}
static void OnOverlay(reshade::api::effect_runtime*){}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    if(!std::strcmp(argv[1],"old")){
        EarlyMfgLog();
        check(logged==1&&logOwner==nullptr,"old early log permanently primes null owner");
        check(!reshade::register_addon(core),"old actual SDK registration rejected by owner gate");
        check(registered==0&&tableRequests==0,"rejection precedes ImGui check");
        check(!SeedOwner(),"late seed cannot repair already poisoned SDK cache");
    }else if(!std::strcmp(argv[1],"fixed")){
        check(SeedOwner(),"seed exact core before early log");
        EarlyMfgLog();check(logged==1&&logOwner==core,"early log carries exact owner");
        check(reshade::register_addon(core),"actual SDK registers core after early log");
        check(registered==1&&tableRequests==1,"19250 ImGui gate reached and accepted");
        reshade::register_event<reshade::addon_event::init_device>(OnDevice);
        reshade::register_overlay("033",OnOverlay);
        check(events==1&&overlays==1,"actual explicit-owner SDK dispatch retains owner");
        check(SeedOwner(),"repeated early entry preserves same owner");
    }else if(!std::strcmp(argv[1],"foreign")){
        check(reshade::internal::get_current_module_handle(foreign)==foreign,"foreign owner preexists");
        check(!SeedOwner(),"foreign owner is rejected not overwritten");
        check(reshade::internal::get_current_module_handle()==foreign,"foreign cache unchanged");
    }else return 2;
    std::printf("SDK owner %s: checks=%d failures=%d; no DLL/GPU execution\n",argv[1],checks,failures);
    return failures?1:0;
}
