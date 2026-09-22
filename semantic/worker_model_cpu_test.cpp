// Actual production CPU model through the private child IPC. No graphics API.
#include "worker_protocol.h"
#include "ort_person_cpu.h"
#include <fstream>
#include <chrono>
int wmain(int argc,wchar_t** argv)try{
 if(argc!=4)throw std::runtime_error("explicit worker, fixture, runtime paths required");
 const unsigned w=640,h=453;std::vector<uint8_t> rgba(size_t(w)*h*4);
 std::ifstream input(argv[2],std::ios::binary);input.read(reinterpret_cast<char*>(rgba.data()),rgba.size());
 if(!input||input.peek()!=EOF)throw std::runtime_error("fixture size");
 const auto dir=std::filesystem::path(argv[3]);
 yanyunmask::CpuPersonModel reference(dir/L"onnxruntime.dll",dir/L"yolo11n-seg.onnx");
 yanyunmask::PersonResult expected;std::string error;
 if(!reference.Run(rgba.data(),rgba.size(),w*4,{1,1,1,1,w,h,false},expected,error))throw std::runtime_error(error);
 yyworker::Client client;client.Start(std::filesystem::absolute(argv[1]),123);
 std::vector<double> wall;
 for(int n=0;n<12;++n){const auto start=std::chrono::steady_clock::now();const auto& out=client.Run(rgba,w,h);
  const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(out.completed!=uint64_t(n+1)||!std::equal(expected.mask.coverage.begin(),expected.mask.coverage.end(),out.coverage)||!std::equal(expected.mask.groupIds.begin(),expected.mask.groupIds.end(),out.groups))throw std::runtime_error("production IPC differs from reference pixels");
  if(n)wall.push_back(elapsed);printf("CPU MODEL round=%d provider=CPU IPC_total_ms=%.3f inference_decode_ms=%.3f exact_mask_match=1\n",n,elapsed,out.inferenceMs);
 }
 std::sort(wall.begin(),wall.end());printf("PASS CPU-only model fixture and IPC: 12 exact pixel comparisons; steady median_ms=%.3f max_ms=%.3f; production GPU not executed\n",wall[wall.size()/2],wall.back());
 return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
