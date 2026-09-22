#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>
namespace yyworker {
constexpr uint32_t Magic=0x34595930,Version=1,Edge=640,Pixels=Edge*Edge;
struct Message {
 uint32_t magic=Magic,version=Version,width=0,height=0;
 uint64_t sequence=0,completed=0;
 int64_t adapter=0;
 uint32_t status=0,reserved=0;
 double inferenceMs=0;
 char error[384]{};
 uint8_t rgba[Pixels*4]{},coverage[Pixels]{},groups[Pixels]{};
};
// S27: bits 8..15 of Message::reserved carry this recognition's protagonist
// state (bits 0..7 stay the provider code); an older worker sends zero there.
namespace protagonist {constexpr uint32_t Shift=8,Locked=1,Seen=2,Rescued=4,Missing=8;
 inline uint32_t Flags(uint32_t reserved){return (reserved>>Shift)&0xFFu;}
 inline bool MissingOnTrack(uint32_t flags){return (flags&Locked)&&(flags&Missing);}}
inline bool Valid(const Message& m){return m.magic==Magic&&m.version==Version&&m.sequence&&m.width&&m.height&&m.width<=Edge&&m.height<=Edge;}
struct Handle {
 HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
 Handle()=default;explicit Handle(HANDLE v):h(v){};Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
class Client {
 Handle mapping_,request_,response_,job_,process_;Message* data_=nullptr;uint64_t sequence_=0;
public:
 ~Client(){if(data_)UnmapViewOfFile(data_);} // Closing job kills only this owned child, including on game exit.
 bool Ready()const{return data_&&process_.h&&WaitForSingleObject(process_.h,0)==WAIT_TIMEOUT;}
 void Start(const std::filesystem::path& exe,int64_t adapter){
  if(process_.h)throw std::runtime_error("recognition worker already started");
  SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
  mapping_.h=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(Message),nullptr);
  request_.h=CreateEventW(&sa,FALSE,FALSE,nullptr);response_.h=CreateEventW(&sa,FALSE,FALSE,nullptr);
  if(!mapping_.h||!request_.h||!response_.h)throw std::runtime_error("worker IPC creation failed");
  data_=static_cast<Message*>(MapViewOfFile(mapping_.h,FILE_MAP_ALL_ACCESS,0,0,sizeof(Message)));
  if(!data_)throw std::runtime_error("worker IPC mapping failed");new(data_)Message;data_->adapter=adapter;
  job_.h=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!job_.h||!SetInformationJobObject(job_.h,JobObjectExtendedLimitInformation,&limits,sizeof limits))throw std::runtime_error("worker lifetime job failed");
  SIZE_T attrBytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&attrBytes);std::vector<uint8_t> attrs(attrBytes);
  STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
  if(!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&attrBytes))throw std::runtime_error("worker handle list failed");
  struct Attr{LPPROC_THREAD_ATTRIBUTE_LIST p;~Attr(){DeleteProcThreadAttributeList(p);}} release{si.lpAttributeList};
  HANDLE allowed[]={mapping_.h,request_.h,response_.h};
  if(!UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof allowed,nullptr,nullptr))throw std::runtime_error("worker inherited handles failed");
  wchar_t cmd[32768];swprintf_s(cmd,L"\"%s\" --033-private-worker %llu %llu %llu",exe.c_str(),uint64_t(mapping_.h),uint64_t(request_.h),uint64_t(response_.h));
  PROCESS_INFORMATION pi{};
  if(!exe.is_absolute()||!CreateProcessW(exe.c_str(),cmd,nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,exe.parent_path().c_str(),&si.StartupInfo,&pi))throw std::runtime_error("recognition worker executable unavailable");
  process_.h=pi.hProcess;Handle thread(pi.hThread);
  if(!AssignProcessToJobObject(job_.h,pi.hProcess)){TerminateProcess(pi.hProcess,1);throw std::runtime_error("recognition worker lifetime assignment failed");}
  if(ResumeThread(thread.h)==DWORD(-1))throw std::runtime_error("recognition worker resume failed");
 }
 const Message& Run(const std::vector<uint8_t>& rgba,unsigned w,unsigned h){
  if(!Ready()||!w||!h||w>Edge||h>Edge||rgba.size()!=size_t(w)*h*4)throw std::runtime_error("invalid recognition worker input");
  data_->width=w;data_->height=h;data_->sequence=++sequence_;data_->completed=0;data_->status=0;data_->error[0]=0;
  std::copy(rgba.begin(),rgba.end(),data_->rgba);MemoryBarrier();
  if(!SetEvent(request_.h))throw std::runtime_error("recognition worker request failed");
  HANDLE wait[]={response_.h,process_.h};const auto code=WaitForMultipleObjects(2,wait,FALSE,sequence_==1?60000:10000);
  if(code!=WAIT_OBJECT_0)throw std::runtime_error(code==WAIT_TIMEOUT?"recognition worker timed out":"recognition worker exited");
  MemoryBarrier();if(!Valid(*data_)||data_->completed!=sequence_||data_->width!=w||data_->height!=h)throw std::runtime_error("recognition worker response mismatch");
  if(data_->status!=1){data_->error[sizeof data_->error-1]=0;throw std::runtime_error(data_->error[0]?data_->error:"recognition worker failed");}
  return *data_;
 }
};
}
