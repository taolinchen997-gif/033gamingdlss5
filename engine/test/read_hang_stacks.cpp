// Diagnostic reader only: noninvasive attach, no thread suspension/resume,
// no input, no target code execution, no terminate on exit.
#include <Windows.h>
#include <DbgEng.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct OutputSink final:IDebugOutputCallbacks {
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(id==__uuidof(IUnknown)||id==__uuidof(IDebugOutputCallbacks)){*p=this;return S_OK;}*p=nullptr;return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override{return 1;}
 ULONG STDMETHODCALLTYPE Release() override{return 1;}
 HRESULT STDMETHODCALLTYPE Output(ULONG,PCSTR s) override{fputs(s,stdout);fflush(stdout);return S_OK;}
};
int main(int argc,char** argv){
 if(argc<2)return 2;
 IDebugClient* client=nullptr;IDebugControl* control=nullptr;IDebugSymbols* symbols=nullptr;
 HRESULT hr=DebugCreate(__uuidof(IDebugClient),(void**)&client);if(FAILED(hr))return 3;
 OutputSink sink;client->SetOutputCallbacks(&sink);client->QueryInterface(__uuidof(IDebugControl),(void**)&control);client->QueryInterface(__uuidof(IDebugSymbols),(void**)&symbols);
 symbols->SetSymbolPath("");
 const bool dump=argc>2&&!std::strcmp(argv[1],"dump");
 if(dump)hr=client->OpenDumpFile(argv[2]);
 else hr=client->AttachProcess(0,strtoul(argv[1],nullptr,10),DEBUG_ATTACH_NONINVASIVE|DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND);
 printf("noninvasive no-suspend attach=%08lx\n",hr);
 if(SUCCEEDED(hr)){hr=control->WaitForEvent(0,10000);printf("wait=%08lx\n",hr);
  if(SUCCEEDED(hr)){
   control->Execute(DEBUG_OUTCTL_THIS_CLIENT,"lm",DEBUG_EXECUTE_DEFAULT);
   if(dump)control->Execute(DEBUG_OUTCTL_THIS_CLIENT,".ecxr; k40; .exr -1; u @rip-20 L20",DEBUG_EXECUTE_DEFAULT);
   else {control->Execute(DEBUG_OUTCTL_THIS_CLIENT,"~*k8",DEBUG_EXECUTE_DEFAULT);if(argc>2)control->Execute(DEBUG_OUTCTL_THIS_CLIENT,argv[2],DEBUG_EXECUTE_DEFAULT);}
  }
 }
 client->DetachProcesses();symbols->Release();control->Release();client->SetOutputCallbacks(nullptr);client->Release();return FAILED(hr)?1:0;
}
