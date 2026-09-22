// Native CPU inference adapter for the pinned DXL YOLO model. No GPU provider.
// 033 original adapter; decoding/composition retain DXL's AGPL-3.0-only terms.
#pragma once
#define NOMINMAX
#define ORT_API_MANUAL_INIT
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include "third_party/onnxruntime/onnxruntime_cxx_api.h"
#include "yolo_mask_decode.h"
#include <fstream>
#include <filesystem>
#include <mutex>
#include <array>
#include <functional>
#include <chrono>
#include "runtime_compatibility.h"

namespace yanyunmask {
inline constexpr char ModelSha[]="4fa0b870a6075e6e7fcb55fe5fe7cb10abc61c244a60fc2bbc563af28ee94f26";
inline std::string Sha256(const std::vector<uint8_t>& bytes) {
 BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};ULONG objectBytes=0,resultBytes=0;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA provider");
 struct Close {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Close(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} close{alg,hash};
 if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&resultBytes,0)<0)throw std::runtime_error("SHA object");
 std::vector<uint8_t> object(objectBytes);uint8_t digest[32];
 if(BCryptCreateHash(alg,&hash,object.data(),objectBytes,nullptr,0,0)<0||bytes.size()>ULONG_MAX||
    BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0)<0||BCryptFinishHash(hash,digest,32,0)<0)throw std::runtime_error("SHA failed");
 const char hex[]="0123456789abcdef";std::string out;for(auto b:digest){out+=hex[b>>4];out+=hex[b&15];}return out;
}
inline std::vector<uint8_t> ReadPinnedModel(const std::filesystem::path& path) {
 if(!path.is_absolute())throw std::runtime_error("absolute model path required");
 std::ifstream f(path,std::ios::binary|std::ios::ate);
 if(!f||f.tellg()!=11740373)throw std::runtime_error("model length mismatch");
 std::vector<uint8_t> bytes(size_t(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
 if(!f||Sha256(bytes)!=ModelSha)throw std::runtime_error("model SHA mismatch");return bytes;
}
// ORT's C++ API table has process lifetime. Keep one explicitly selected module
// loaded and reject a conflicting later path; never fall back to DLL search.
inline void InitCpuRuntime(const std::filesystem::path& dll) {
 static std::mutex mutex;static HMODULE module=nullptr;static std::filesystem::path loaded;
 std::lock_guard<std::mutex> lock(mutex);
 if(!dll.is_absolute())throw std::runtime_error("absolute ORT DLL path required");
 if(module){if(dll!=loaded)throw std::runtime_error("conflicting ORT runtime");return;}
 RequireSupportedMsvc();
 auto candidate=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(!candidate)throw std::runtime_error("ORT load failed");
 const auto get=reinterpret_cast<decltype(&OrtGetApiBase)>(GetProcAddress(candidate,"OrtGetApiBase"));
 const auto base=get?get():nullptr;const auto api=base?base->GetApi(ORT_API_VERSION):nullptr;
 if(!api||std::string(base->GetVersionString())!="1.24.3"){FreeLibrary(candidate);throw std::runtime_error("ORT API/version mismatch");}
 Ort::InitApi(api);loaded=dll;module=candidate;
}
struct SourceStamp {
 uint64_t stream=0,generation=0,frame=0,capturedMs=0;
 uint32_t width=0,height=0;bool flipY=false;
 bool Valid()const noexcept{return stream&&generation&&frame&&width&&height&&width<=8192&&height<=8192&&size_t(width)*height<=33554432;}
};
struct PersonResult {SourceStamp source;DXL::SemanticMaskSnapshot mask;double preprocessMs=0,ortRunMs=0,decodeMs=0;uint32_t protagonist=0;float protagonistScore=0;unsigned personFirstKept=0;};
// Strict fresh-source contract. Owner changes, resize, history reset and late
// completion invalidate prior masks. This does not claim motion compensation.
inline bool Usable(const PersonResult& r,const SourceStamp& current,uint64_t now,uint64_t maxAge=100)noexcept {
 return r.source.Valid()&&current.Valid()&&r.source.stream==current.stream&&r.source.generation==current.generation&&
  r.source.width==current.width&&r.source.height==current.height&&r.source.flipY==current.flipY&&
  r.source.frame<=current.frame&&r.mask.version==r.source.frame&&r.mask.width==current.width&&r.mask.height==current.height&&
  r.mask.sourceFlipY==current.flipY&&r.mask.publishedMs==r.source.capturedMs&&r.mask.Valid(now)&&
  now>=r.source.capturedMs&&now-r.source.capturedMs<=maxAge;
}
class CpuPersonModel {
 Ort::Env env_{nullptr};Ort::Session session_{nullptr};Ort::MemoryInfo memory_{nullptr};
 std::vector<float> input_;std::string inputName_;std::array<std::string,2> outputNames_;
 std::mutex runMutex_;
 ProtagonistTrack track_; // S27: one main character per worker process, guarded by runMutex_
 ProtagonistTrack* shared_=nullptr; // S29: the GPU and CPU sessions follow ONE main character
public:
 // S27 worker-log totals (copy taken under the run lock).
 ProtagonistTrack Track(){std::lock_guard<std::mutex> lock(runMutex_);return shared_?*shared_:track_;}
 // S29: both sessions of a worker decode into the same track. The worker runs
 // one recognition at a time, so the two run locks never overlap on it.
 void UseTrack(ProtagonistTrack* shared){std::lock_guard<std::mutex> lock(runMutex_);shared_=shared;}
 CpuPersonModel(const std::filesystem::path& runtime,const std::filesystem::path& model,
   const std::function<void(Ort::SessionOptions&)>& configure={}, bool spinning=true) {
  const auto bytes=ReadPinnedModel(model);InitCpuRuntime(runtime);
  env_=Ort::Env(ORT_LOGGING_LEVEL_WARNING,"033-YanYun-person");
  Ort::SessionOptions options;options.SetIntraOpNumThreads(4);options.SetInterOpNumThreads(1);
  options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
  options.AddConfigEntry("session.intra_op.allow_spinning",spinning?"1":"0");
  options.AddConfigEntry("session.inter_op.allow_spinning","0");
  // Default is CPU-only for offline fixtures. The game worker may explicitly
  // provide the separately reviewed DML configurator; no automatic GPU probing.
  if(configure)configure(options);
  session_=Ort::Session(env_,bytes.data(),bytes.size(),options);
  if(session_.GetInputCount()!=1||session_.GetOutputCount()!=2)throw std::runtime_error("model arity");
  Ort::AllocatorWithDefaultOptions allocator;
  inputName_=session_.GetInputNameAllocated(0,allocator).get();
  for(int i=0;i<2;++i)outputNames_[i]=session_.GetOutputNameAllocated(i,allocator).get();
  auto inputType=session_.GetInputTypeInfo(0);auto type=inputType.GetTensorTypeAndShapeInfo();
  if(type.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||type.GetShape()!=std::vector<int64_t>{1,3,640,640})throw std::runtime_error("input contract");
  memory_=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
  input_.resize(3*640*640);
 }
 bool Run(const uint8_t* rgba,size_t bytes,uint32_t pitch,const SourceStamp& stamp,PersonResult& output,std::string& error)noexcept {
  if(!rgba||!stamp.Valid()||pitch<size_t(stamp.width)*4||bytes<size_t(pitch)*stamp.height){error="invalid source";return false;}
  try {
   std::lock_guard<std::mutex> lock(runMutex_);
   using Clock=std::chrono::steady_clock;const auto begin=Clock::now();
   std::fill(input_.begin(),input_.end(),114.f/255.f);
   const double scale=640.0/std::max(stamp.width,stamp.height);
   const unsigned cw=unsigned(std::lround(stamp.width*scale)),ch=unsigned(std::lround(stamp.height*scale));
   for(unsigned y=0;y<ch;++y){unsigned sy=std::min(unsigned(y/scale),stamp.height-1);if(stamp.flipY)sy=stamp.height-1-sy;
    for(unsigned x=0;x<cw;++x){const unsigned sx=std::min(unsigned(x/scale),stamp.width-1);const auto pixel=rgba+size_t(sy)*pitch+sx*4;
     for(unsigned c=0;c<3;++c)input_[size_t(c)*640*640+y*640+x]=pixel[c]/255.f;
    }
   }
   const int64_t shape[]={1,3,640,640};
   auto input=Ort::Value::CreateTensor<float>(memory_,input_.data(),input_.size(),shape,4);
   const char* names[]={outputNames_[0].c_str(),outputNames_[1].c_str()};const char* inputNames[]={inputName_.c_str()};
   const auto prepared=Clock::now();
   auto results=session_.Run(Ort::RunOptions{nullptr},inputNames,&input,1,names,2);
   const auto inferred=Clock::now();
   const float* det=nullptr,*proto=nullptr;
   for(auto& value:results){if(!value.IsTensor())throw std::runtime_error("non-tensor output");auto type=value.GetTensorTypeAndShapeInfo();
    if(type.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)throw std::runtime_error("non-float output");
    const auto dims=type.GetShape();if(dims==std::vector<int64_t>{1,116,8400})det=value.GetTensorData<float>();
    else if(dims==std::vector<int64_t>{1,32,160,160})proto=value.GetTensorData<float>();else throw std::runtime_error("output contract");
   }
   PersonResult next;next.source=stamp;
   DecodeReport report;
   if(!Decode(det,116*8400,proto,32*160*160,stamp.width,stamp.height,stamp.frame,stamp.capturedMs,next.mask,shared_?shared_:&track_,&report))throw std::runtime_error("decode failed");
   next.protagonist=report.protagonist;next.protagonistScore=report.protagonistScore;next.personFirstKept=report.personFirstKept;
   next.mask.sourceFlipY=stamp.flipY;DXL::RestoreSemanticSourceRows(next.mask);
   const auto decoded=Clock::now();
   next.preprocessMs=std::chrono::duration<double,std::milli>(prepared-begin).count();
   next.ortRunMs=std::chrono::duration<double,std::milli>(inferred-prepared).count();
   next.decodeMs=std::chrono::duration<double,std::milli>(decoded-inferred).count();
   output=std::move(next);error.clear();return true;
  }catch(const std::exception& e){error=e.what();return false;}catch(...){error="unknown inference failure";return false;}
 }
};
}
