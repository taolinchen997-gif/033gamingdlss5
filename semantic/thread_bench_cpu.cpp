#include "ort_person_cpu.h"
#include <chrono>
#include <cstdio>
int wmain(int argc,wchar_t** argv)try{
 if(argc!=3)return 2;const auto dir=std::filesystem::path(argv[1]);
 std::ifstream f(argv[2],std::ios::binary);std::vector<uint8_t> rgba(640*453*4);f.read(reinterpret_cast<char*>(rgba.data()),rgba.size());if(!f)throw std::runtime_error("fixture");
 for(int threads:{3,4,6}){
  yanyunmask::CpuPersonModel model(dir/L"onnxruntime.dll",dir/L"yolo11n-seg.onnx",[&](Ort::SessionOptions& o){o.SetIntraOpNumThreads(threads);},true);
  std::vector<double> times;
  for(int i=0;i<7;++i){yanyunmask::PersonResult r;std::string error;const auto start=std::chrono::steady_clock::now();
   if(!model.Run(rgba.data(),rgba.size(),640*4,{1,1,1,1,640,453,false},r,error))throw std::runtime_error(error);
   const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(i)times.push_back(elapsed);
  }std::sort(times.begin(),times.end());printf("CPU ONLY threads=%d median_ms=%.3f maximum_ms=%.3f\n",threads,times[times.size()/2],times.back());fflush(stdout);
 }return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
