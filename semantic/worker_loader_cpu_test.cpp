// CPU-only, inert DLL fixture. No graphics module or device is loaded.
#include "worker_dll_isolation.h"
#include <cstdio>
#include <vector>
#ifdef YY_LOADER_DLL_FIXTURE
extern "C" __declspec(dllexport) int LoaderFixtureTag(){return YY_LOADER_TAG;}
#elif defined(YY_LOADER_LEGACY_CHILD)
extern "C" __declspec(dllimport) int LoaderFixtureTag();
int wmain(){return LoaderFixtureTag();}
#else
int Child(const std::filesystem::path& exe,const std::wstring& args){
 std::wstring command=L"\""+exe.wstring()+L"\" "+args;
 STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi))throw std::runtime_error("fixture process creation");
 CloseHandle(pi.hThread);const auto wait=WaitForSingleObject(pi.hProcess,10000);DWORD code=0;
 if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,99);CloseHandle(pi.hProcess);throw std::runtime_error("owned CPU fixture timed out");}
 GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);return int(code);
}
int wmain(int argc,wchar_t** argv)try{
 if(argc!=5)throw std::runtime_error("fixture arguments");
 const std::wstring mode=argv[1];const auto trusted=std::filesystem::absolute(argv[2]);const auto trap=std::filesystem::absolute(argv[3]);const auto legacy=std::filesystem::absolute(argv[4]);
 if(mode==L"--safe"||mode==L"--preloaded"){
  wchar_t inherited[32768]{};if(!GetDllDirectoryW(32768,inherited)||_wcsicmp(inherited,trap.c_str()))throw std::runtime_error("inherited fixture route missing");
  yyworker::IsolateDllSearch();if(GetDllDirectoryW(32768,inherited)!=0)throw std::runtime_error("inherited search route survived");
  if(mode==L"--preloaded"){
   auto wrong=LoadLibraryExW((trap/L"033-loader-fixture.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!wrong)throw std::runtime_error("trap load failed");
   bool rejected=false;try{yyworker::LoadExactLibrary(trusted/L"033-loader-fixture.dll",LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);}catch(const std::exception&){rejected=true;}
   if(!rejected)throw std::runtime_error("unexpected preloaded DLL admitted");return 0;
  }
  bool rejected=false;try{yyworker::LoadExactLibrary(L"033-loader-fixture.dll",LOAD_LIBRARY_SEARCH_SYSTEM32);}catch(const std::exception&){rejected=true;}
  if(!rejected)throw std::runtime_error("relative DLL admitted");
  auto module=yyworker::LoadExactLibrary(trusted/L"033-loader-fixture.dll",LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  auto tag=reinterpret_cast<int(*)()>(GetProcAddress(module,"LoaderFixtureTag"));if(!tag||tag()!=1)throw std::runtime_error("wrong fixture selected");
  if(!yyworker::SameModulePath(module,trusted/L"033-loader-fixture.dll"))throw std::runtime_error("module path not pinned");return 0;
 }
 if(mode!=L"--parent")throw std::runtime_error("unknown fixture mode");
 if(!SetDllDirectoryW(trap.c_str()))throw std::runtime_error("fixture inherited route setup");
 if(Child(legacy,L"")!=2)throw std::runtime_error("old static import did not reproduce inherited DLL selection");
 wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
 const auto args=L" \""+trusted.wstring()+L"\" \""+trap.wstring()+L"\" \""+legacy.wstring()+L"\"";
 for(const auto* choice:{L"--safe",L"--preloaded"})if(Child(self,std::wstring(choice)+args))throw std::runtime_error("isolated child failed");
 wchar_t preserved[32768]{};GetDllDirectoryW(32768,preserved);if(_wcsicmp(preserved,trap.c_str()))throw std::runtime_error("parent DLL route changed");
 printf("PASS loader CPU: old static child selects inherited trap; fixed child selects exact DLL; preloaded mismatch and relative paths rejected; parent unchanged; no graphics modules/devices\n");return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 91;}
#endif
