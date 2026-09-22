// S29 offline check of the recognition route with the REAL sessions: the route
// fixture build of the production worker (YY_WORKER_ROUTE_FIXTURE) runs every
// pass on the real GPU (DirectML, this PC's NVIDIA adapter) or on the CPU
// session; only the times the route sees are scripted (4 slow GPU passes, then a
// slow probe and two quick ones). No game, no product DLL.
#include "worker_protocol.h"
#include "ort_person_cpu.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#pragma comment(lib,"dxgi.lib")
int wmain(int argc,wchar_t** argv)try{
 if(argc!=4)throw std::runtime_error("route worker, fixture, runtime paths required");
 const unsigned w=640,h=453;std::vector<uint8_t> rgba(size_t(w)*h*4);
 std::ifstream input(argv[2],std::ios::binary);input.read(reinterpret_cast<char*>(rgba.data()),rgba.size());
 if(!input||input.peek()!=EOF)throw std::runtime_error("fixture size");
 const auto dir=std::filesystem::path(argv[3]);
 yanyunmask::CpuPersonModel reference(dir/L"onnxruntime.dll",dir/L"yolo11n-seg.onnx");
 yanyunmask::PersonResult expected;std::string error;
 if(!reference.Run(rgba.data(),rgba.size(),w*4,{1,1,1,1,w,h,false},expected,error))throw std::runtime_error(error);
 size_t personPixels=0;for(auto c:expected.mask.coverage)personPixels+=c<128;
 if(personPixels<2000)throw std::runtime_error("fixture must contain a person");
 // The hardware adapter with the most dedicated memory (the NVIDIA card on this PC).
 Microsoft::WRL::ComPtr<IDXGIFactory4> factory;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))throw std::runtime_error("DXGI factory");
 LUID best{};SIZE_T bestMemory=0;char bestName[128]{};
 Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
 for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);
  if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&d.DedicatedVideoMemory>bestMemory){bestMemory=d.DedicatedVideoMemory;best=d.AdapterLuid;WideCharToMultiByte(CP_UTF8,0,d.Description,-1,bestName,sizeof bestName,nullptr,nullptr);}}
 if(!bestMemory)throw std::runtime_error("no hardware adapter");
 int64_t luid=0;std::memcpy(&luid,&best,sizeof luid);
 printf("adapter: %s (%llu MiB)\n",bestName,static_cast<unsigned long long>(bestMemory>>20));
 yyworker::Client client;client.Start(std::filesystem::absolute(argv[1]),luid);
 const uint32_t want[16]={2,2,2,2,1,2,1,2,1,2,2,2,2,2,2,2};
 unsigned cpuExact=0,gpuClose=0;double worstMean=0;
 for(int n=0;n<16;++n){const auto start=std::chrono::steady_clock::now();const auto& out=client.Run(rgba,w,h);
  const double wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  const uint32_t provider=out.reserved&0xFFu;
  if(out.completed!=uint64_t(n+1))throw std::runtime_error("sequence");
  if(provider!=want[n]){char text[96];std::snprintf(text,sizeof text,"request %d ran on provider %u, route expected %u",n+1,provider,want[n]);throw std::runtime_error(text);}
  const size_t pixels=size_t(w)*h;double sum=0;size_t offPixels=0;
  for(size_t i=0;i<pixels;++i){const int d=std::abs(int(out.coverage[i])-int(expected.mask.coverage[i]));sum+=d;offPixels+=d>16;}
  const double mean=sum/pixels;
  if(provider==1){
   if(!std::equal(expected.mask.coverage.begin(),expected.mask.coverage.end(),out.coverage)||!std::equal(expected.mask.groupIds.begin(),expected.mask.groupIds.end(),out.groups))
    throw std::runtime_error("CPU route result differs from the CPU reference");
   ++cpuExact;
  }else{
   // DirectML and the CPU differ in rounding only: the same person, nearly the same pixels.
   if(mean>1.0||offPixels>pixels/200)throw std::runtime_error("GPU route result is not the same person mask");
   ++gpuClose;worstMean=(std::max)(worstMean,mean);
  }
  printf("ROUTE request=%d provider=%s wall_ms=%.1f inference_decode_ms=%.1f mean_abs_diff=%.3f pixels_off_by_more_than_16=%zu\n",n+1,provider==1?"CPU":"GPU",wall,out.inferenceMs,mean,offPixels);
 }
 printf("PASS S29 route on the real sessions: 16 requests, provider sequence as scripted (GPU x4 -> CPU, slow probe, 2 quick probes -> GPU), %u CPU results exact to the CPU reference, %u GPU results within mean %.3f of it; hardware GPU executed offline (no game)\n",cpuExact,gpuClose,worstMean);
 return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
