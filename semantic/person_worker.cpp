// 033 private, automatically managed recognition process. No windows or console.
#include "worker_protocol.h"
#include "ort_person_cpu.h"
#if !defined(YY_WORKER_CPU_FIXTURE) && !defined(YY_WORKER_CPU_MODEL)
#include "recognition_route.h"
#include "dml_first_failure.h"
#include "worker_dll_isolation.h"
#include <dxgi1_4.h>
#endif
#include <memory>
static_assert(yanyunmask::PLocked==yyworker::protagonist::Locked&&yanyunmask::PSeen==yyworker::protagonist::Seen&&
 yanyunmask::PRescued==yyworker::protagonist::Rescued&&yanyunmask::PMissing==yyworker::protagonist::Missing,"S27 protagonist flags are one layout");
#if !defined(YY_WORKER_CPU_FIXTURE) && !defined(YY_WORKER_CPU_MODEL)
// S27: pictures for offline tuning, only where the owner created
// %LOCALAPPDATA%\033YanYunRuntime\recognition-samples (never on players' PCs):
// the last picture that still had the protagonist and the first miss after it,
// at most 40 files per worker process. 24-bit bottom-up BMP.
namespace yysamples {
constexpr unsigned Limit=40;
inline std::filesystem::path Folder(){
 wchar_t local[32768]{};const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
 if(!n||n>=32768)return {};
 const auto folder=std::filesystem::path(local)/L"033YanYunRuntime"/L"recognition-samples";
 std::error_code error;return std::filesystem::is_directory(folder,error)?folder:std::filesystem::path{};
}
inline bool WriteBmp(const std::filesystem::path& path,const uint8_t* rgba,unsigned w,unsigned h)noexcept{
 try{
  const uint32_t row=(w*3u+3u)&~3u,image=row*h;uint8_t header[54]{'B','M'};
  auto put32=[&](size_t at,uint32_t v){std::memcpy(header+at,&v,4);};auto put16=[&](size_t at,uint16_t v){std::memcpy(header+at,&v,2);};
  put32(2,54+image);put32(10,54);put32(14,40);put32(18,w);put32(22,h);put16(26,1);put16(28,24);put32(34,image);
  std::ofstream f(path,std::ios::binary);if(!f)return false;
  f.write(reinterpret_cast<const char*>(header),54);std::vector<uint8_t> line(row,0);
  for(unsigned y=0;y<h;++y){const uint8_t* src=rgba+size_t(h-1-y)*w*4;
   for(unsigned x=0;x<w;++x){line[x*3]=src[x*4+2];line[x*3+1]=src[x*4+1];line[x*3+2]=src[x*4];}
   f.write(reinterpret_cast<const char*>(line.data()),row);}
  return bool(f);
 }catch(...){return false;}
}
}
#endif
#if !defined(YY_WORKER_CPU_FIXTURE) && !defined(YY_WORKER_CPU_MODEL)
namespace yyrouteclock {
#ifdef YY_WORKER_ROUTE_FIXTURE
// Offline fixture only (worker_route_gpu_test): every real pass runs, on the
// real GPU and CPU sessions, but the route sees one request per second and
// scripted GPU times: 4 slow passes, then a slow probe and two quick ones.
inline uint64_t Now(uint64_t seq,uint64_t){return seq*1000;}
inline double GpuMs(const yyroute::Route& r,double measured){
 if(r.mode==yyroute::Where::Gpu&&r.gpuRuns<4)return 150;
 if(r.mode==yyroute::Where::Cpu)return r.probes==0?150:10;
 return measured;
}
#else
inline uint64_t Now(uint64_t,uint64_t tick){return tick;}
inline double GpuMs(const yyroute::Route&,double measured){return measured;}
#endif
}
#endif
int wmain(int argc,wchar_t** argv){
 if(argc!=5||std::wstring(argv[1])!=L"--033-private-worker")return 2;
 yyworker::Handle map(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10))),request(reinterpret_cast<HANDLE>(_wcstoui64(argv[3],nullptr,10))),response(reinterpret_cast<HANDLE>(_wcstoui64(argv[4],nullptr,10)));
 auto* m=static_cast<yyworker::Message*>(MapViewOfFile(map.h,FILE_MAP_ALL_ACCESS,0,0,sizeof(yyworker::Message)));if(!m)return 3;
 std::unique_ptr<yanyunmask::CpuPersonModel> model;
#if !defined(YY_WORKER_CPU_FIXTURE) && !defined(YY_WORKER_CPU_MODEL)
 // S29: the same model on 4 CPU threads, used while the GPU pass is slow; both
 // sessions follow one main character.
 std::unique_ptr<yanyunmask::CpuPersonModel> cpuModel;yanyunmask::ProtagonistTrack sharedTrack;
 yyroute::Route route;uint64_t cpuBuckets[4]{};unsigned routeLogs=0;
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
 Microsoft::WRL::ComPtr<IDMLDevice1> nativeDml;
 Microsoft::WRL::ComPtr<IDMLDevice> portableDml;
 auto journal=std::make_shared<yytrace::Journal>();
 wchar_t ownPath[32768]{};GetModuleFileNameW(nullptr,ownPath,32768);
 SYSTEMTIME utc{};GetSystemTime(&utc);char evidenceName[128]{};
 std::snprintf(evidenceName,sizeof evidenceName,"033-person-worker-%04u%02u%02u-%02u%02u%02u-%lu.log",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,GetCurrentProcessId());
 const auto evidencePath=std::filesystem::path(ownPath).parent_path()/evidenceName;
 auto evidence=[&](const std::string& text)noexcept{try{std::ofstream f(evidencePath,std::ios::binary|std::ios::app);f<<text.substr(0,16384)<<"\n";}catch(...){}};
 auto module=[&](const wchar_t* label,HMODULE handle){wchar_t path[32768]{};if(handle)GetModuleFileNameW(handle,path,32768);char utf8[32768]{};WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof utf8,nullptr,nullptr);char name[128]{};WideCharToMultiByte(CP_UTF8,0,label,-1,name,sizeof name,nullptr,nullptr);evidence(std::string("module ")+name+"="+utf8);};
 auto dump=[&](const char* message)noexcept{try{journal->Save(evidencePath,message,device?device->GetDeviceRemovedReason():S_OK,nativeDml?nativeDml->GetDeviceRemovedReason():S_OK);}catch(...){}};
 evidence("033 YanYun S29 worker (S14 runtime); opset21 FP32; CPU graph fallback forbidden in the GPU session; isolated System32 graphics loader; graph fusion enabled; native DML compile flags; no retry; timestamps UTC; person-first detection, protagonist lock, minimum person area; high-priority recognition queue; separate CPU session while the GPU is congested");
 const auto sampleFolder=yysamples::Folder();unsigned samplesWritten=0;bool sampledThisRun=false;
 uint64_t ortBuckets[5]{}; // S27b: ort_run_ms <10 / 10-25 / 25-50 / 50-100 / >=100 (S29: GPU passes only)
 std::vector<uint8_t> lastSeen;unsigned lastSeenW=0,lastSeenH=0;uint64_t lastSeenSeq=0;
 if(!sampleFolder.empty())evidence("S27 recognition samples enabled: "+sampleFolder.u8string());
#endif
 for(;;){
  if(WaitForSingleObject(request.h,INFINITE)!=WAIT_OBJECT_0)return 4;
  const auto seq=m->sequence;try{
   if(!yyworker::Valid(*m))throw std::runtime_error("invalid worker request");
#ifdef YY_WORKER_CPU_FIXTURE
   // Compiled only into the offline test executable; no GPU/model paths execute.
   yanyunmask::RequireSupportedMsvc();
   for(size_t i=0;i<size_t(m->width)*m->height;++i){m->coverage[i]=m->rgba[i*4];m->groups[i]=0;}m->inferenceMs=0;
#else
   if(!model){
    wchar_t path[32768];if(!GetModuleFileNameW(nullptr,path,32768))throw std::runtime_error("worker path unavailable");const auto dir=std::filesystem::path(path).parent_path();
    yanyunmask::RequireSupportedMsvc();
#ifdef YY_WORKER_CPU_MODEL
    // Compile-time-only offline fixture. The production executable does not
    // accept a runtime CPU switch and is never launched by the build checks.
    model=std::make_unique<yanyunmask::CpuPersonModel>(dir/L"onnxruntime.dll",dir/L"yolo11n-seg.onnx");
#else
    auto check=[](HRESULT hr,const char* what){if(FAILED(hr)){char text[160];std::snprintf(text,sizeof text,"%s hr=0x%08lX",what,static_cast<unsigned long>(hr));throw std::runtime_error(text);}};
    wchar_t inheritedDirectory[32768]{};const auto inheritedLength=GetDllDirectoryW(32768,inheritedDirectory);
    if(inheritedLength>=32768)throw std::runtime_error("worker inherited DLL directory too long");
    evidence("inherited DLL directory="+std::filesystem::path(inheritedDirectory).u8string());
    yyworker::IsolateDllSearch();
    // S28 raised this process's GPU scheduling class (HIGH, else ABOVE_NORMAL);
    // the S28 session still saw 100-250 ms passes, so S29 adds the CPU route.
    // The CPU class stays NORMAL (BELOW_NORMAL while recognising on the CPU,
    // so the 4 inference threads never outrank the game's own threads).
    try{using SetGpuClass=LONG(APIENTRY*)(HANDLE,int);
     const auto gdi=yyworker::LoadSystemLibrary(L"gdi32.dll");
     const auto setGpu=gdi?reinterpret_cast<SetGpuClass>(GetProcAddress(gdi,"D3DKMTSetProcessSchedulingPriorityClass")):nullptr;
     LONG high=-1,above=-1;if(setGpu){high=setGpu(GetCurrentProcess(),4);if(high<0)above=setGpu(GetCurrentProcess(),3);}
     char text[200]{};std::snprintf(text,sizeof text,"S29 process priority: cpu=normal (below normal while on the CPU) gpu_class_high=0x%08lX gpu_class_above_normal=0x%08lX (0 = set, -1 = not tried)",
      static_cast<unsigned long>(high),static_cast<unsigned long>(above));evidence(text);
    }catch(const std::exception& e){evidence(std::string("S29 process priority not set (recognition continues): ")+e.what());}
    // No static DXGI/D3D12 imports: the game directory must never be searched
    // before wmain. S10's ReShade.log1 proved the game proxy entered this child.
    const auto systemDxgi=yyworker::LoadSystemLibrary(L"dxgi.dll");
    const auto systemD3d12=yyworker::LoadSystemLibrary(L"d3d12.dll");
    const auto createFactory=reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(systemDxgi,"CreateDXGIFactory1"));
    const auto createDevice=reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(systemD3d12,"D3D12CreateDevice"));
    if(!createFactory||!createDevice)throw std::runtime_error("worker system graphics exports unavailable");
    module(L"DXGI",systemDxgi);module(L"D3D12",systemD3d12);
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    check(createFactory(IID_PPV_ARGS(&factory)),"recognition factory");
    LUID luid{};std::memcpy(&luid,&m->adapter,sizeof luid);
    char identity[160]{};std::snprintf(identity,sizeof identity,"request seq=%llu input=%ux%u adapter=%08lX:%08lX",seq,m->width,m->height,static_cast<unsigned long>(luid.HighPart),luid.LowPart);evidence(identity);
    check(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)),"recognition exact adapter");
    DXGI_ADAPTER_DESC1 info{};check(adapter->GetDesc1(&info),"recognition adapter description");
    if(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)throw std::runtime_error("software recognition adapter rejected");
    check(createDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"recognition D3D12 device");
    const auto library=LoadLibraryExW((dir/L"DirectML.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto create=library?reinterpret_cast<decltype(&DMLCreateDevice1)>(GetProcAddress(library,"DMLCreateDevice1")):nullptr;
    if(!create)throw std::runtime_error("private DirectML CreateDevice1 unavailable");
    check(create(device.Get(),DML_CREATE_DEVICE_FLAG_NONE,DML_FEATURE_LEVEL_5_0,IID_PPV_ARGS(&nativeDml)),"recognition DirectML device");
    module(L"DirectML",library);module(L"D3D12",GetModuleHandleW(L"d3d12.dll"));module(L"D3D12Core",GetModuleHandleW(L"D3D12Core.dll"));
    // S12 proved the game proxy, not the normal DML compiler, entered this
    // process. Keep that loader isolation; stop forcing the slower unfused /
    // no-metacommand workaround. The journal forwards native compile flags.
    portableDml.Attach(new yytrace::Device(nativeDml.Get(),journal));
    // S27b: in the S27 session inference took 100+ ms while the game kept the GPU
    // busy (recognition fell to ~4/s). One pass needs ~5 ms of GPU; a high-
    // priority queue lets it run between the game's work instead of after it.
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;desc.Flags=D3D12_COMMAND_QUEUE_FLAG_NONE;desc.Priority=D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    check(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"recognition own direct queue");
    model=std::make_unique<yanyunmask::CpuPersonModel>(dir/L"onnxruntime.dll",dir/L"yolo11n-seg.onnx",[&](Ort::SessionOptions& options){
     options.DisableMemPattern();options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);options.SetIntraOpNumThreads(1);
     options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
     // Unsupported operators must fail initialization, never silently run on CPU.
     options.AddConfigEntry("session.disable_cpu_ep_fallback","1");
     const OrtDmlApi* api=nullptr;
     Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML",ORT_API_VERSION,reinterpret_cast<const void**>(&api)));
     if(!api)throw std::runtime_error("private DirectML API unavailable");
     Ort::ThrowOnError(api->SessionOptionsAppendExecutionProvider_DML1(options,portableDml.Get(),queue.Get()));
    },false);
    module(L"onnxruntime",GetModuleHandleW(L"onnxruntime.dll"));module(L"MSVCP140",GetModuleHandleW(L"msvcp140.dll"));
    evidence("session ready; opset21 pinned; no CPU graph fallback; first inference pending");
    model->UseTrack(&sharedTrack);
    // S29: the CPU session (no GPU, no spinning threads). If it cannot be made,
    // recognition simply stays on the GPU as before.
    try{cpuModel=std::make_unique<yanyunmask::CpuPersonModel>(dir/L"onnxruntime.dll",dir/L"yolo11n-seg.onnx",std::function<void(Ort::SessionOptions&)>{},false);
     cpuModel->UseTrack(&sharedTrack);evidence("S29 CPU session ready: 4 threads, no spinning; used only while the GPU pass is slow");}
    catch(const std::exception& e){cpuModel.reset();evidence(std::string("S29 CPU session unavailable, GPU only: ")+e.what());}
#endif
   }
   const auto started=GetTickCount64();yanyunmask::PersonResult result;std::string error;
   yanyunmask::SourceStamp stamp{1,1,seq,started,m->width,m->height,false};
#if !defined(YY_WORKER_CPU_MODEL)
   // S29 route: GPU unless the GPU has been slow; a failed CPU pass never fails
   // the recognition (it is redone on the GPU and the CPU session is dropped).
   auto where=route.Next(yyrouteclock::Now(seq,started),cpuModel!=nullptr);
   if(where==yyroute::Where::Cpu&&!cpuModel->Run(m->rgba,size_t(m->width)*m->height*4,m->width*4,stamp,result,error)){
    evidence("S29 CPU pass failed, GPU only from now on: "+error);cpuModel.reset();route.CpuLost();SetPriorityClass(GetCurrentProcess(),NORMAL_PRIORITY_CLASS);where=yyroute::Where::Gpu;}
   if(where==yyroute::Where::Gpu&&!model->Run(m->rgba,size_t(m->width)*m->height*4,m->width*4,stamp,result,error))throw std::runtime_error(error);
   {const bool probe=where==yyroute::Where::Gpu&&route.mode==yyroute::Where::Cpu;
    const double ms=where==yyroute::Where::Gpu?yyrouteclock::GpuMs(route,result.ortRunMs):result.ortRunMs;
    if(where==yyroute::Where::Cpu)++cpuBuckets[yyroute::Bucket(result.ortRunMs)];
    else ++ortBuckets[result.ortRunMs<10?0:result.ortRunMs<25?1:result.ortRunMs<50?2:result.ortRunMs<100?3:4];
    if(route.Done(where,ms,yyrouteclock::Now(seq,GetTickCount64()),cpuModel!=nullptr)){
     const bool onCpu=route.mode==yyroute::Where::Cpu;
     SetPriorityClass(GetCurrentProcess(),onCpu?BELOW_NORMAL_PRIORITY_CLASS:NORMAL_PRIORITY_CLASS);
     if(routeLogs<32){++routeLogs;char text[260]{};
      if(onCpu)std::snprintf(text,sizeof text,"S29 route: recognition moves to the CPU at seq=%llu (%u of the last %u GPU passes >= 60 ms, gpu_mean=%.1f ms, cpu_ema=%.1f ms)",
       seq,route.SlowPasses(),route.filled,route.GpuMean(),route.cpuEma);
      else std::snprintf(text,sizeof text,"S29 route: back to the GPU at seq=%llu (%u quick probes in a row, last probe %.1f ms, cpu_ema=%.1f ms)",
       seq,yyroute::QuickProbesToReturn,ms,route.cpuEma);
      evidence(text);}
    }else if(probe&&routeLogs<32&&route.probes<=4){++routeLogs;char text[160]{};
     std::snprintf(text,sizeof text,"S29 route: GPU probe at seq=%llu took %.1f ms; staying on the CPU",seq,ms);evidence(text);}
   }
#else
   if(!model->Run(m->rgba,size_t(m->width)*m->height*4,m->width*4,stamp,result,error))throw std::runtime_error(error);
#endif
   const auto n=size_t(m->width)*m->height;if(result.mask.coverage.size()!=n||result.mask.groupIds.size()!=n)throw std::runtime_error("worker mask size mismatch");
   std::copy(result.mask.coverage.begin(),result.mask.coverage.end(),m->coverage);std::copy(result.mask.groupIds.begin(),result.mask.groupIds.end(),m->groups);m->inferenceMs=double(GetTickCount64()-started);
#endif
   m->status=1;
#if !defined(YY_WORKER_CPU_FIXTURE) && !defined(YY_WORKER_CPU_MODEL)
   // Bits 0..7: provider (2 GPU with native compile policy, 1 CPU since S29);
   // bits 8..15: S27 protagonist flags. Neither is a frame acceptance flag.
   m->reserved=yyroute::Provider(where)|(result.protagonist<<yyworker::protagonist::Shift);
   if(!sampleFolder.empty()&&samplesWritten<yysamples::Limit){
    const size_t bytes=size_t(m->width)*m->height*4;
    if(result.protagonist&yanyunmask::PSeen){lastSeen.assign(m->rgba,m->rgba+bytes);lastSeenW=m->width;lastSeenH=m->height;lastSeenSeq=seq;sampledThisRun=false;}
    else if((result.protagonist&yanyunmask::PMissing)&&!sampledThisRun){
     sampledThisRun=true;SYSTEMTIME now{};GetSystemTime(&now);char stem[96]{};
     std::snprintf(stem,sizeof stem,"yy-%04u%02u%02u-%02u%02u%02u-%llu",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,seq);
     if(lastSeen.size()==bytes&&lastSeenW==m->width&&lastSeenH==m->height&&
        yysamples::WriteBmp(sampleFolder/(std::string(stem)+"-seen-"+std::to_string(lastSeenSeq)+".bmp"),lastSeen.data(),lastSeenW,lastSeenH))++samplesWritten;
     if(yysamples::WriteBmp(sampleFolder/(std::string(stem)+"-miss.bmp"),m->rgba,m->width,m->height))++samplesWritten;
    }
   }
   if(seq%256==0){const auto track=model->Track();char stats[640]{};
    std::snprintf(stats,sizeof stats,"S27 protagonist recognitions=%llu locked=%llu seen=%llu rescued=%llu missing=%llu acquired=%llu released=%llu miss_runs 1-2/3-6/7-18/longer=%llu/%llu/%llu/%llu person_first_kept=%llu samples=%u; S29 gpu_ort_run_ms <10/10-25/25-50/50-100/>=100=%llu/%llu/%llu/%llu/%llu",
     track.recognitions,track.lockedRecognitions,track.seen,track.rescued,track.missing,track.acquired,track.released,
     track.missRuns[0],track.missRuns[1],track.missRuns[2],track.missRuns[3],track.personFirstKept,samplesWritten,
     ortBuckets[0],ortBuckets[1],ortBuckets[2],ortBuckets[3],ortBuckets[4]);
    char size[160]{};std::snprintf(size,sizeof size,"; S28 small_people_dropped=%llu kept_by_hysteresis=%llu",track.smallDropped,track.smallKept);
    char routeText[320]{};std::snprintf(routeText,sizeof routeText,"; S29 route now=%s gpu_runs=%llu cpu_runs=%llu probes=%llu to_cpu=%llu to_gpu=%llu gpu_mean=%.1f slow_passes=%u last_probe=%.1f cpu_ema=%.1f cpu_ort_run_ms <25/25-50/50-100/>=100=%llu/%llu/%llu/%llu cpu_session=%d",
     route.mode==yyroute::Where::Cpu?"CPU":"GPU",route.gpuRuns,route.cpuRuns,route.probes,route.toCpu,route.toGpu,route.GpuMean(),route.SlowPasses(),route.lastProbe,route.cpuEma,cpuBuckets[0],cpuBuckets[1],cpuBuckets[2],cpuBuckets[3],cpuModel?1:0);
    evidence(std::string(stats)+size+routeText);}
   if(seq==1){dump("first inference decoded successfully; detailed capture now stopped");journal->Stop();}
   if(seq<=16 || (seq<=8192 && (seq&(seq-1))==0)){char timing[360]{};std::snprintf(timing,sizeof timing,"inference seq=%llu on=%s input=%ux%u compute_ms=%.1f preprocess_ms=%.3f ort_run_ms=%.3f decode_ms=%.3f; ort_run includes upload/GPU/wait/download; freshness checked by parent",seq,where==yyroute::Where::Cpu?"CPU":"GPU",m->width,m->height,m->inferenceMs,result.preprocessMs,result.ortRunMs,result.decodeMs);evidence(timing);}
#endif
  }catch(const std::exception& e){m->status=2;
#if !defined(YY_WORKER_CPU_FIXTURE) && !defined(YY_WORKER_CPU_MODEL)
   evidence("failure seq="+std::to_string(seq)+"; trace_enabled="+std::to_string(journal->Enabled()));dump(e.what());journal->Stop();
   std::snprintf(m->error,sizeof m->error,"GPU recognition failed; D3D12=0x%08lX DML=0x%08lX; full=%s: %.160s",device?static_cast<unsigned long>(device->GetDeviceRemovedReason()):0,nativeDml?static_cast<unsigned long>(nativeDml->GetDeviceRemovedReason()):0,evidenceName,e.what());
#else
   std::snprintf(m->error,sizeof m->error,"%s",e.what());
#endif
  }
  m->completed=seq;MemoryBarrier();if(!SetEvent(response.h))return 5;
 }
}
