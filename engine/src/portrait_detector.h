// YuNet CPU inference is statically linked into the one 033 engine.
// Background work is bounded to one job; rendering never waits for the detector.
#pragma once
#include <array>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <windows.h>
#include "../third_party/FaceDetect033/src/facedetectcnn.h"
#undef MIN
#undef MAX
namespace portraitdetector {
constexpr unsigned MaxEdge=640;
struct Face {float box[4]{},eyes[4]{},mouth[4]{};};
struct Result {uint64_t epoch=0,tick=0;unsigned w=0,h=0,count=0;double ms=0;std::array<Face,4> faces;std::vector<unsigned char> rgba;};
inline std::mutex mutex;
inline std::atomic_bool busy{false};
inline std::atomic<unsigned> jobs{0},finished{0},failed{0};
inline std::atomic<DWORD> lastError{0};
inline Result last;
inline Result Detect(std::vector<unsigned char> rgba,unsigned w,unsigned h,uint64_t epoch,uint64_t tick){
 Result r;r.epoch=epoch;r.tick=tick;r.w=w;r.h=h;r.rgba=std::move(rgba);
 if(!w||!h||w>MaxEdge||h>MaxEdge||r.rgba.size()!=size_t(w)*h*4)return r;
 std::vector<unsigned char> bgr(size_t(w)*h*3),buffer(FACEDETECTION_RESULT_BUFFER_SIZE);
 for(size_t i=0;i<size_t(w)*h;++i){bgr[i*3]=r.rgba[i*4+2];bgr[i*3+1]=r.rgba[i*4+1];bgr[i*3+2]=r.rgba[i*4];}
 LARGE_INTEGER start,stop,freq;QueryPerformanceCounter(&start);QueryPerformanceFrequency(&freq);
 int* results=facedetect_cnn(buffer.data(),bgr.data(),int(w),int(h),int(w*3));
 QueryPerformanceCounter(&stop);r.ms=1000.*double(stop.QuadPart-start.QuadPart)/double(freq.QuadPart);
 if(!results)return r;
 for(int i=0;i<(std::min)(results[0],FACEDETECTION_RESULT_MAX_FACES)&&r.count<4;++i){
  const short* p=reinterpret_cast<const short*>(buffer.data()+4)+FACEDETECTION_RESULT_STRIDE_SHORTS*i;
  if(p[0]<88||p[3]<18||p[4]<18)continue;
  Face f;f.box[0]=float(p[1])/w;f.box[1]=float(p[2])/h;f.box[2]=float(p[3])/w;f.box[3]=float(p[4])/h;
  if(f.box[0]<0||f.box[1]<0||f.box[0]+f.box[2]>1||f.box[1]+f.box[3]>1)continue;
  for(int j=0;j<4;++j)f.eyes[j]=float(p[5+j])/(j%2?h:w);
  f.mouth[0]=float(p[11]+p[13])/(2*w);f.mouth[1]=float(p[12]+p[14])/(2*h);f.mouth[2]=float(p[0])/100;
  r.faces[r.count++]=f;
 }
 return r;
}
struct Job {std::vector<unsigned char> data;unsigned w,h;uint64_t epoch,tick;HMODULE module;};
inline DWORD WINAPI Worker(void* value){
 HMODULE pinned=nullptr;
 {std::unique_ptr<Job> job(static_cast<Job*>(value));pinned=job->module;
  try{auto r=Detect(std::move(job->data),job->w,job->h,job->epoch,job->tick);
      std::lock_guard<std::mutex> lock(mutex);last=std::move(r);++finished;}catch(...){++failed;}
 }
 busy.store(false);
 FreeLibraryAndExitThread(pinned,0);
}
inline bool Start(std::vector<unsigned char> rgba,unsigned w,unsigned h,uint64_t epoch,uint64_t tick){
 if(busy.exchange(true))return false;
 HMODULE pinned=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&pinned)){lastError=GetLastError();busy=false;return false;}
 Job* job=nullptr;
 try{job=new Job{std::move(rgba),w,h,epoch,tick,pinned};}catch(...){FreeLibrary(pinned);busy=false;return false;}
 HANDLE worker=CreateThread(nullptr,0,Worker,job,0,nullptr);
 if(!worker){lastError=GetLastError();delete job;FreeLibrary(pinned);busy=false;return false;}
 ++jobs;CloseHandle(worker);return true;
}
inline bool Read(Result& result){std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock()||!last.tick||result.tick==last.tick)return false;result=last;return true;}
}
