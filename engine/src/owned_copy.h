// S17 prototype: one owned background COPY operation, with its own lifetime.
// No game hooks, queue submission, allocation, or device work occurs at include.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifndef YYCOPY_CPU_MOCK_TYPES
#include <d3d12.h>
#endif
#include <wrl/client.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace yycopied {
using Microsoft::WRL::ComPtr;
enum class SourceStatus { Ready, NotReady, Invalid };
enum class StartResult { Started, SourceNotReady, Busy, Failed };
enum class Status { Idle, Pending, Ready, Failed };
struct SourceCompletion {
 // Standalone harness: its real source-queue fence and exact signalled value.
 ID3D12Fence* fence=nullptr;
 UINT64 value=0;
 // Production alternative: checks the exact job/ticket/source/device against
 // the existing immutable gate, including legal full retirement. Called only
 // before this helper submits. It must not enqueue, wait, or retain a lease lock.
 SourceStatus (*revalidate)(void*,ID3D12Resource*,ID3D12Device*)=nullptr;
 void* context=nullptr;
};
struct Failure {HRESULT result=S_OK;const char* stage="none";};

namespace detail {
inline void Require(HRESULT hr,const char* stage){if(FAILED(hr))throw std::runtime_error(stage);}
inline bool SameDevice(ID3D12Device* expected,ID3D12DeviceChild* child){
 if(!expected||!child)return false;
 ComPtr<ID3D12Device> actual;ComPtr<IUnknown> left,right;
 if(FAILED(child->GetDevice(IID_PPV_ARGS(&actual)))||FAILED(expected->QueryInterface(IID_PPV_ARGS(&left)))||FAILED(actual->QueryInterface(IID_PPV_ARGS(&right))))return false;
 return left.Get()==right.Get();
}
// A process-local hard cap. A sticky failure stops subsequent copies instead
// of allocating a second unknown in-flight context.
inline std::atomic_bool inFlight{false},poisoned{false};
struct Job {
 ComPtr<ID3D12Resource> source,readback;
 ComPtr<ID3D12Fence> sourceFence;
 ComPtr<ID3D12CommandAllocator> allocator;
 ComPtr<ID3D12GraphicsCommandList> list;
 UINT64 fenceValue=0,bytes=0,offset=0;
 bool submitted=false,discard=false,eventArmed=false;
};
struct State {
 ComPtr<ID3D12Device> device;
 ComPtr<ID3D12CommandQueue> queue;
 ComPtr<ID3D12Fence> fence;
 HANDLE eventHandle=nullptr;
 std::unique_ptr<Job> job;
 Status status=Status::Idle;
 Failure failure;
 UINT64 nextFence=0,executions=0;
 bool ownsSlot=false;
 std::mutex lock;
 State* quarantineNext=nullptr;
 ~State(){if(eventHandle)CloseHandle(eventHandle);}
};
inline std::mutex quarantineLock;
inline State* quarantineHead=nullptr; // Intentionally never destroyed/released.
inline void Quarantine(State* state)noexcept{
 // No allocation while saving an in-flight owner, even under memory pressure.
 // The State itself already owns every object and its job before Execute.
 poisoned.store(true,std::memory_order_release);
 try{std::lock_guard<std::mutex> guard(quarantineLock);state->quarantineNext=quarantineHead;quarantineHead=state;}
 catch(...){/* Leaking the existing State still retains all references. */}
}
inline void Fail(State& state,HRESULT result,const char* stage)noexcept{
 if(state.status!=Status::Failed)state.failure={result,stage};
 state.status=Status::Failed;poisoned.store(true,std::memory_order_release);
 // DO NOT reset job/queue/fence/event or relinquish the global slot here.
}
inline void ClearCompleted(State& state)noexcept{
 state.job.reset();state.status=Status::Idle;
 if(state.ownsSlot){state.ownsSlot=false;inFlight.store(false,std::memory_order_release);}
}
}

class MappedReadback {
 ComPtr<ID3D12Resource> resource_;
 void* data_=nullptr;
 size_t bytes_=0,offset_=0;
 friend class OwnedCopy;
 void Release()noexcept{if(data_&&resource_){D3D12_RANGE noWrite{};resource_->Unmap(0,&noWrite);}data_=nullptr;resource_.Reset();}
public:
 MappedReadback()=default;
 MappedReadback(const MappedReadback&)=delete;
 MappedReadback& operator=(const MappedReadback&)=delete;
 MappedReadback(MappedReadback&& other)noexcept:resource_(std::move(other.resource_)),data_(std::exchange(other.data_,nullptr)),bytes_(other.bytes_),offset_(other.offset_){}
 MappedReadback& operator=(MappedReadback&& other)noexcept{if(this!=&other){Release();resource_=std::move(other.resource_);data_=std::exchange(other.data_,nullptr);bytes_=other.bytes_;offset_=other.offset_;}return *this;}
 ~MappedReadback(){Release();}
 const void* Data()const noexcept{return data_;}
 size_t Bytes()const noexcept{return bytes_;}
 size_t CopiedOffset()const noexcept{return offset_;}
 ID3D12Resource* Resource()const noexcept{return resource_.Get();}
};

class OwnedCopy {
 std::unique_ptr<detail::State> state_;
 static Status PollLocked(detail::State& state,DWORD waitMs){
  if(state.status!=Status::Pending)return state.status;
  const auto completed=[&]()->bool{
   const auto done=state.fence->GetCompletedValue();
   if(done==UINT64_MAX){detail::Fail(state,DXGI_ERROR_DEVICE_REMOVED,"copy fence device removed");return false;}
   return done>=state.job->fenceValue;
  };
  bool ready=completed();
  if(state.status==Status::Failed)return state.status;
  if(!ready&&waitMs){
   if(!state.job->eventArmed){
    const auto hr=state.fence->SetEventOnCompletion(state.job->fenceValue,state.eventHandle);
    if(FAILED(hr)){detail::Fail(state,hr,"copy SetEventOnCompletion");return state.status;}
    state.job->eventArmed=true;
   }
   const auto result=WaitForSingleObject(state.eventHandle,waitMs>1000?1000:waitMs);
   if(result!=WAIT_OBJECT_0&&result!=WAIT_TIMEOUT){detail::Fail(state,HRESULT_FROM_WIN32(GetLastError()),"copy event wait");return state.status;}
   ready=completed();
  }
  if(state.status==Status::Failed)return state.status;
  if(!ready)return Status::Pending;
  const auto hr=state.device->GetDeviceRemovedReason();
  if(FAILED(hr)){detail::Fail(state,hr,"copy device status");return state.status;}
  state.status=Status::Ready;
  if(state.job->discard)detail::ClearCompleted(state);
  return state.status;
 }
public:
 static constexpr UINT64 MaximumBytes=16ull*1024*1024;
 explicit OwnedCopy(ID3D12Device* device){
  if(!device||detail::poisoned.load(std::memory_order_acquire))throw std::runtime_error("owned copy device missing or process copy path failed");
  auto value=std::make_unique<detail::State>();value->device=device;
  D3D12_COMMAND_QUEUE_DESC queue{};queue.Type=D3D12_COMMAND_LIST_TYPE_COPY;
  detail::Require(device->CreateCommandQueue(&queue,IID_PPV_ARGS(&value->queue)),"owned copy queue creation");
  detail::Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&value->fence)),"owned copy fence creation");
  value->eventHandle=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  if(!value->eventHandle)throw std::runtime_error("owned copy event creation");
  state_=std::move(value);
 }
 OwnedCopy(const OwnedCopy&)=delete;
 OwnedCopy& operator=(const OwnedCopy&)=delete;
 ~OwnedCopy()noexcept{
  if(!state_)return;
  // No waiting in a destructor. A known complete job is releasable; a pending
  // or failed submitted job keeps the entire owner alive until process exit.
  if(state_->job&&state_->job->submitted){
   if(state_->status==Status::Pending){try{PollLocked(*state_,0);}catch(...){detail::Fail(*state_,E_FAIL,"owned copy destructor poll exception");}}
   if(state_->status==Status::Pending||state_->status==Status::Failed){detail::Quarantine(state_.release());return;}
  }
  if(state_->ownsSlot){state_->ownsSlot=false;detail::inFlight.store(false,std::memory_order_release);}
 }
 StartResult Start(ID3D12Resource* source,UINT64 offset,UINT64 bytes,const SourceCompletion& proof){
  auto& state=*state_;
  if(!source||!bytes||bytes>MaximumBytes||offset>=bytes||(offset&3)||(bytes&3))throw std::runtime_error("owned copy invalid range");
  ComPtr<ID3D12Resource> keepSource=source;
  if(!detail::SameDevice(state.device.Get(),source))throw std::runtime_error("owned copy source device mismatch");
  const auto desc=source->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||desc.Width<bytes)throw std::runtime_error("owned copy source must be a sufficient buffer");
  D3D12_HEAP_PROPERTIES sourceHeap{};D3D12_HEAP_FLAGS sourceFlags{};
  detail::Require(source->GetHeapProperties(&sourceHeap,&sourceFlags),"owned copy source heap");
  if(sourceHeap.Type!=D3D12_HEAP_TYPE_DEFAULT)throw std::runtime_error("owned copy source must use DEFAULT heap");
  if(bool(proof.fence)==bool(proof.revalidate))throw std::runtime_error("owned copy requires exactly one real source-completion proof");
  ComPtr<ID3D12Fence> keepSourceFence;
  if(proof.fence){
   if(!proof.value||!detail::SameDevice(state.device.Get(),proof.fence))throw std::runtime_error("owned copy source fence mismatch");
   keepSourceFence=proof.fence;
   const auto value=proof.fence->GetCompletedValue();
   if(value==UINT64_MAX)throw std::runtime_error("owned copy source fence device removed");
   if(value<proof.value)return StartResult::SourceNotReady;
  }else{
   // In production this callback locks/rechecks the exact lease ticket and
   // immutable gate, or recognizes original lawful full retirement. It must
   // return with that global lock released BEFORE this private queue is used.
   const auto result=proof.revalidate(proof.context,source,state.device.Get());
   if(result==SourceStatus::NotReady)return StartResult::SourceNotReady;
   if(result!=SourceStatus::Ready)throw std::runtime_error("owned copy source proof invalid");
  }
  std::lock_guard<std::mutex> guard(state.lock);
  if(state.status==Status::Failed||detail::poisoned.load(std::memory_order_acquire))return StartResult::Failed;
  if(state.job)return StartResult::Busy;
  bool expected=false;if(!detail::inFlight.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return StartResult::Busy;
  state.ownsSlot=true;
  try{
   auto job=std::make_unique<detail::Job>();job->source=std::move(keepSource);job->sourceFence=std::move(keepSourceFence);job->bytes=bytes;job->offset=offset;
   D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
   D3D12_RESOURCE_DESC target{};target.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;target.Width=bytes;target.Height=1;target.DepthOrArraySize=1;target.MipLevels=1;target.SampleDesc.Count=1;target.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   detail::Require(state.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&target,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&job->readback)),"owned readback allocation");
   detail::Require(state.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,IID_PPV_ARGS(&job->allocator)),"owned copy allocator creation");
   detail::Require(state.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COPY,job->allocator.Get(),nullptr,IID_PPV_ARGS(&job->list)),"owned copy list creation");
   // Source must have been sealed and left in COMMON. COPY queue promotion to
   // COPY_SOURCE is read-only; no game-list or source-state mutation is added.
   job->list->CopyBufferRegion(job->readback.Get(),offset,source,offset,bytes-offset);
   detail::Require(job->list->Close(),"owned copy list close");
   detail::Require(state.device->GetDeviceRemovedReason(),"owned copy device before submit");
   if(state.nextFence==UINT64_MAX-1)throw std::runtime_error("owned copy fence counter exhausted");
   if(!ResetEvent(state.eventHandle))throw std::runtime_error("owned copy event reset before submit");
   job->fenceValue=++state.nextFence;
   state.job=std::move(job);state.status=Status::Pending;
  }catch(...){detail::ClearCompleted(state);throw;} // Nothing submitted yet.
  // No allocation, QueryInterface or throwing C++ work after this point.
  // Every COM reference is already attached to the long-lived State.
  state.job->submitted=true;++state.executions;
  ID3D12CommandList* list=state.job->list.Get();state.queue->ExecuteCommandLists(1,&list);
  const auto signal=state.queue->Signal(state.fence.Get(),state.job->fenceValue);
  if(FAILED(signal)){detail::Fail(state,signal,"owned copy Signal");return StartResult::Failed;}
  return StartResult::Started;
 }
 Status Poll(DWORD waitMs=0)noexcept{
  try{std::lock_guard<std::mutex> guard(state_->lock);return PollLocked(*state_,waitMs);}
  catch(...){detail::Fail(*state_,E_FAIL,"owned copy poll exception");return Status::Failed;}
 }
 MappedReadback Take(){
  std::lock_guard<std::mutex> guard(state_->lock);auto& state=*state_;
  if(PollLocked(state,0)!=Status::Ready)throw std::runtime_error("owned readback is not complete");
  MappedReadback result;result.resource_=state.job->readback;result.bytes_=size_t(state.job->bytes);result.offset_=size_t(state.job->offset);
  D3D12_RANGE readRange{result.offset_,result.bytes_};
  // This Map runs ONLY after the independent copy fence has completed. A Map
  // failure is safely pre-consumption; the completed job stays available.
  void* mapped=nullptr;detail::Require(result.resource_->Map(0,&readRange,&mapped),"owned completed readback Map");
  result.data_=mapped;
  detail::ClearCompleted(state);return result;
 }
 void DiscardWhenComplete()noexcept{
  try{std::lock_guard<std::mutex> guard(state_->lock);if(!state_->job)return;state_->job->discard=true;if(PollLocked(*state_,0)==Status::Ready)detail::ClearCompleted(*state_);}
  catch(...){detail::Fail(*state_,E_FAIL,"owned copy discard exception");}
 }
 Failure LastFailure()const noexcept{return state_->failure;}
 UINT64 ExecutionCount()const noexcept{return state_->executions;}
};
}
