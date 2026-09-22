// S17 offline candidate. This header records GPU commands only when explicitly called.
// CPU/shader compilation of this file does not validate a real adapter.
#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>
#include <utility>
#include "once_capture_policy.h"
#include "once_capture_shaders.h"
namespace yyonce {
using Microsoft::WRL::ComPtr;
inline void Require(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(message);}
// t0 input, u0 control, u1 payload. S26: the caller supplies the heap from a
// pool (yanyun_heap_pool.h) instead of Create making one per capture.
inline D3D12_DESCRIPTOR_HEAP_DESC ViewHeapDesc(){
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;return hd;
}
struct Capture {
 Layout layout;
 ComPtr<ID3D12Resource> control;
 ComPtr<ID3D12Resource> buffer;
 ComPtr<ID3D12DescriptorHeap> views;
};
struct SealCapability {
 ComPtr<ID3D12GraphicsCommandList2> list;
};
struct Pipeline {
 ComPtr<ID3D12Device> device;
 ComPtr<ID3D12RootSignature> root;
 ComPtr<ID3D12PipelineState> claim,grid;
 void Prepare(ID3D12Device* dev){
  if(!dev)throw std::runtime_error("once capture device missing");
  if(device){if(device.Get()!=dev)throw std::runtime_error("once capture device changed");return;}
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=1;
  ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=2;
  D3D12_ROOT_PARAMETER params[3]{};
  params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants.Num32BitValues=6;
  for(unsigned i=0;i<2;++i){params[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i+1].DescriptorTable={1,&ranges[i]};}
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=params;
  ComPtr<ID3DBlob> blob,error;ComPtr<ID3D12RootSignature> newRoot;
  Require(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"once root serialize");
  Require(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&newRoot)),"once root create");
  auto compile=[&](const char* source,size_t bytes,ComPtr<ID3D12PipelineState>& state){
   blob.Reset();error.Reset();Require(D3DCompile(source,bytes,"033_once_capture",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error),"once shader compile");
   D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=newRoot.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
   Require(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&state)),"once PSO create");
  };
  ComPtr<ID3D12PipelineState> newClaim,newGrid;
  compile(ClaimShader,sizeof(ClaimShader)-1,newClaim);compile(GridShader,sizeof(GridShader)-1,newGrid);
  root=std::move(newRoot);claim=std::move(newClaim);grid=std::move(newGrid);device=dev;
 }
 Capture Create(ID3D12Resource* input,ID3D12DescriptorHeap* views){
  if(!device||!input)throw std::runtime_error("once capture not prepared");
  if(!views)throw std::runtime_error("once views missing");
  {const auto want=ViewHeapDesc(),have=views->GetDesc();
   if(have.Type!=want.Type||have.NumDescriptors<want.NumDescriptors||!(have.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))throw std::runtime_error("once views layout");}
  const auto source=input->GetDesc();
  if(source.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||source.DepthOrArraySize!=1||source.MipLevels!=1||source.SampleDesc.Count!=1)
   throw std::runtime_error("once capture input dimension");
  PixelFormat format;
  if(source.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)format=PixelFormat::Float16;
  else if(source.Format==DXGI_FORMAT_R32G32B32A32_FLOAT)format=PixelFormat::Float32;
  else throw std::runtime_error("once capture input format");
  if(source.Width>8192)throw std::runtime_error("once capture input width");
  Capture value;value.layout=MakeLayout(UINT(source.Width),source.Height,format);
  if(!ValidLayout(value.layout))throw std::runtime_error("once capture layout");
  // Custom heap removes READBACK's COPY_DEST-only resource-state restriction.
  auto heap=device->GetCustomHeapProperties(0,D3D12_HEAP_TYPE_READBACK);
  if(heap.Type!=D3D12_HEAP_TYPE_CUSTOM||heap.CPUPageProperty!=D3D12_CPU_PAGE_PROPERTY_WRITE_BACK||heap.MemoryPoolPreference!=D3D12_MEMORY_POOL_L0)
   throw std::runtime_error("once capture requires CUSTOM WRITE_BACK L0");
  D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=value.layout.bytes;
  bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  auto cd=bd;cd.Width=ControlBytes;
  Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS,&cd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&value.control)),"once control UAV allocation");
  D3D12_HEAP_PROPERTIES local{};local.Type=D3D12_HEAP_TYPE_DEFAULT;
  Require(device->CreateCommittedResource(&local,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&value.buffer)),"once DEFAULT payload UAV allocation");
  // Initialize only CPU-visible control before any recording. DEFAULT payload
  // contents are unspecified; DidRun plus the actual Execute fence gates COPY.
  auto initialize=[](ID3D12Resource* resource,SIZE_T bytes){
   void* mapped=nullptr;D3D12_RANGE noRead{};Require(resource->Map(0,&noRead,&mapped),"once header map");
   std::memset(mapped,0,bytes);D3D12_RANGE written{0,bytes};resource->Unmap(0,&written);
  };
  initialize(value.control.Get(),ControlBytes);
  value.views=views;
  const auto cpu=value.views->GetCPUDescriptorHandleForHeapStart();const auto inc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=source.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  device->CreateShaderResourceView(input,&srv,cpu);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements=ControlBytes/4;uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
  device->CreateUnorderedAccessView(value.control.Get(),nullptr,&uav,{cpu.ptr+inc});
  uav.Buffer.NumElements=UINT(value.layout.bytes/4);
  device->CreateUnorderedAccessView(value.buffer.Get(),nullptr,&uav,{cpu.ptr+2*inc});
  return value;
 }
 // Perform all optional interface/capability failures BEFORE any recording.
 SealCapability PrepareSeal(ID3D12GraphicsCommandList* list)const {
  if(!device||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("once seal requires DIRECT list");
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};
  Require(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&options,sizeof(options)),"once seal capability query");
  if(!(options.WriteBufferImmediateSupportFlags&D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT))throw std::runtime_error("once seal unsupported");
  SealCapability result;Require(list->QueryInterface(IID_PPV_ARGS(&result.list)),"once seal requires list2");return result;
 }
 // Before either Record method, caller MUST retain input, control, buffer, views, root
 // and both PSOs through the ORIGINAL COMPLETE lease. No allocation/failure
 // path follows the first command. The input must already be a readable SRV.
 void Validate(ID3D12GraphicsCommandList* list,const Capture& value)const {
  if(!list||!root||!claim||!grid||!value.control||!value.buffer||!value.views||!ValidLayout(value.layout))throw std::runtime_error("once capture record arguments");
 }
 static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* buffer,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={buffer,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
 }
 // Internal recording body: callers validate first and restore COMMON last.
 void RecordBody(ID3D12GraphicsCommandList* list,const Capture& value)const {
  Transition(list,value.control.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(list,value.buffer.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const auto gpu=value.views->GetGPUDescriptorHandleForHeapStart();const auto inc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  ID3D12DescriptorHeap* heaps[]={value.views.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root.Get());
  const auto& l=value.layout;const UINT sizes[]={l.sourceWidth,l.sourceHeight,l.width,l.height,l.rowPitch,l.pixelBytes};
  list->SetComputeRoot32BitConstants(0,6,sizes,0);list->SetComputeRootDescriptorTable(1,gpu);list->SetComputeRootDescriptorTable(2,{gpu.ptr+inc});
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=value.control.Get();
  list->SetPipelineState(claim.Get());list->Dispatch(1,1,1);list->ResourceBarrier(1,&barrier);
  barrier.UAV.pResource=value.buffer.Get();
  list->SetPipelineState(grid.Get());list->Dispatch(l.groupsX,l.groupsY,1);list->ResourceBarrier(1,&barrier);
 }
 // Unsealed prototype: demonstrates write-once after a successful dispatch;
 // NOT sufficient if a skipped first execution may be followed by a writer.
 void Record(ID3D12GraphicsCommandList* list,const Capture& value)const {
  Validate(list,value);RecordBody(list,value);
  Transition(list,value.control.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
  Transition(list,value.buffer.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
 }
 // Windows GPU validation still required: WBI must execute despite predication.
 // This independent candidate helper does not modify the production lane.
 void RecordSealed(const SealCapability& capability,const Capture& value)const {
  auto* list=capability.list.Get();Validate(list,value);
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER parameters[]={
   {value.control->GetGPUVirtualAddress()+ClaimOffset,1},
   {value.control->GetGPUVirtualAddress()+AllowedOffset,0}};
  const D3D12_WRITEBUFFERIMMEDIATE_MODE modes[]={D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT};
  RecordBody(list,value);
  Transition(list,value.control.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
  list->WriteBufferImmediate(2,parameters,modes);
  Transition(list,value.control.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
  Transition(list,value.buffer.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
 }
};
}
