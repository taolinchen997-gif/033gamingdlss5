#pragma once
// S20-S22 GPU side of the late-latched, motion-aligned, three-vote person mask.
// Render/NR writer thread only. Every resource here lives for the process: game
// command lists in flight may reference them, and the person device never changes.
#include "yanyun_late_ring.h"
#include "yanyun_heap_pool.h"
#include <tuple>
#include <utility>
namespace yanyundual {
// Game motion (and S22 depth) guides at the capture point. texture==nullptr =>
// no motion recorded for this frame; a mask whose chain crosses it stays in
// place. depth==nullptr => depth unknown for this frame (never rejects).
struct MaskMotion {ID3D12Resource* texture=nullptr;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;float toUvX=0,toUvY=0;
 ID3D12Resource* depth=nullptr;DXGI_FORMAT depthFormat=DXGI_FORMAT_UNKNOWN;bool depthInverted=false;};
namespace late {
using Microsoft::WRL::ComPtr;
// t0 motion, t1 ring, t2 depth | u0 mvRing, u1 mvFrames, u2 latched, u3 aligned[3], u4 tempA, u5 tempB, u6 mask, u7 history (S30)
inline constexpr UINT SrvDescriptors=3,Descriptors=11;
// Readable single-channel view of a depth guide; UNKNOWN => depth not used.
inline DXGI_FORMAT DepthSrvFormat(DXGI_FORMAT f){
 switch(f){
 case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_FLOAT:return DXGI_FORMAT_R32_FLOAT;
 case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
 case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
 case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_UNORM;
 default:return DXGI_FORMAT_UNKNOWN;
 }
}
struct Gpu {ComPtr<ID3D12Device> device;unsigned w=0,h=0;ComPtr<ID3D12Resource> mvRing,mvFrames,latched,aligned,tempA,tempB,mask,history,readback;};
inline Gpu gpu;
inline uint64_t gpuGeneration=0; // S30: a new mask set has no history (the composite restarts it)
inline std::vector<Gpu> superseded; // an earlier mask size; still referenced by lists in flight
struct Pipelines {ComPtr<ID3D12Device> device;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> motion,latch,gather,vote,gate,temporal,store,feather;};
inline Pipelines pipes;
inline void Check(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(what);}
inline void EnsurePipelines(ID3D12Device* dev){
 if(pipes.feather){if(!identity033::Equal(pipes.device.Get(),dev))throw std::runtime_error("late mask device changed");return;}
 D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=SrvDescriptors;
 ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=Descriptors-SrvDescriptors;
 D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants.Num32BitValues=sizeof(LateConstants)/4;
 p[1].ParameterType=p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={1,&ranges[0]};p[2].DescriptorTable={1,&ranges[1]};
 D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
 sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
 D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
 ComPtr<ID3DBlob> blob,error;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"late mask root serialization failed");
 Pipelines next;next.device=dev;
 Check(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&next.root)),"late mask root failed");
 auto make=[&](const char* text,size_t bytes,const char* name,ComPtr<ID3D12PipelineState>& out){
  ComPtr<ID3DBlob> code,err;
  if(FAILED(D3DCompile(text,bytes,name,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&err)))
   throw std::runtime_error(err?static_cast<const char*>(err->GetBufferPointer()):"late mask shader failed");
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=next.root.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};
  Check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&out)),"late mask pipeline failed");
 };
 make(MotionRingShader,sizeof(MotionRingShader)-1,"033_mask_motion",next.motion);
 make(LatchShader,sizeof(LatchShader)-1,"033_mask_latch",next.latch);
 make(GatherShader,sizeof(GatherShader)-1,"033_mask_gather",next.gather);
 make(VoteShader,sizeof(VoteShader)-1,"033_mask_vote",next.vote);
 make(DepthGateShader,sizeof(DepthGateShader)-1,"033_mask_depth_gate",next.gate);
 make(TemporalShader,sizeof(TemporalShader)-1,"033_mask_temporal",next.temporal);
 make(HistoryStoreShader,sizeof(HistoryStoreShader)-1,"033_mask_history",next.store);
 make(FeatherShader,sizeof(FeatherShader)-1,"033_mask_feather",next.feather);
 pipes=std::move(next); // published only when complete
 Log("[033 YY S30 late mask] pipelines compiled once: motion ring, GPU-time latch of the %u newest masks, motion-chain gather, three-vote, depth gate (radius %d, rim cut where q < %.2f x the person's), temporal smoothing (rise %.0f ms, fall %.0f ms, history follows the recorded motion), feather",
  latemask::Votes,GateRadius,1.f-GateTolerance,MaskRiseTauMs,MaskFallTauMs);
}
inline ComPtr<ID3D12Resource> Uav(ID3D12Device* dev,D3D12_RESOURCE_DIMENSION dimension,UINT64 width,UINT height,UINT16 depth,DXGI_FORMAT format){
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};desc.Dimension=dimension;desc.Width=width;desc.Height=height;
 desc.DepthOrArraySize=depth;desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 if(dimension==D3D12_RESOURCE_DIMENSION_BUFFER)desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ComPtr<ID3D12Resource> r;Check(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)),"late mask resource allocation failed");
 return r; // committed resources start zeroed: frame 0 / invalid latch
}
inline void EnsureGpu(ID3D12Device* dev,unsigned w,unsigned h){
 if(gpu.device&&gpu.w==w&&gpu.h==h){if(!identity033::Equal(gpu.device.Get(),dev))throw std::runtime_error("late mask device changed");return;}
 Gpu next;next.device=dev;next.w=w;next.h=h;
 next.mvRing=Uav(dev,D3D12_RESOURCE_DIMENSION_TEXTURE2D,w,h,UINT16(2*latemask::MotionSlots),DXGI_FORMAT_R32_UINT); // motion, then depth
 next.mvFrames=Uav(dev,D3D12_RESOURCE_DIMENSION_BUFFER,256,1,1,DXGI_FORMAT_UNKNOWN);
 next.latched=Uav(dev,D3D12_RESOURCE_DIMENSION_BUFFER,latemask::LatchedBytes,1,1,DXGI_FORMAT_UNKNOWN);
 next.aligned=Uav(dev,D3D12_RESOURCE_DIMENSION_TEXTURE2D,w,h,UINT16(latemask::Votes),DXGI_FORMAT_R32_FLOAT);
 next.tempA=Uav(dev,D3D12_RESOURCE_DIMENSION_TEXTURE2D,w,h,1,DXGI_FORMAT_R32_FLOAT);
 next.tempB=Uav(dev,D3D12_RESOURCE_DIMENSION_TEXTURE2D,w,h,1,DXGI_FORMAT_R32_FLOAT);
 next.mask=Uav(dev,D3D12_RESOURCE_DIMENSION_TEXTURE2D,w,h,1,DXGI_FORMAT_R8_UNORM);
 next.history=Uav(dev,D3D12_RESOURCE_DIMENSION_TEXTURE2D,w,h,1,DXGI_FORMAT_R32_FLOAT);
 {D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width=UINT64(latemask::ReadbackSlots)*latemask::LatchedBytes;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Check(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&next.readback)),"late mask telemetry readback allocation failed");}
 if(gpu.device)superseded.push_back(std::move(gpu));
 gpu=std::move(next);++gpuGeneration;
 Log("[033 YY S22 late mask] GPU mask set %ux%u; %u motion slots; %u vote slices; superseded sets kept=%zu",w,h,latemask::MotionSlots,latemask::Votes,superseded.size());
}
// Fills t0,t1,t2,u0..u7 from start. Unused inputs get null descriptors.
inline void FillDescriptors(ID3D12Device* dev,D3D12_CPU_DESCRIPTOR_HANDLE start,UINT inc,ID3D12Resource* motion,DXGI_FORMAT motionFormat,
 ID3D12Resource* depth=nullptr,DXGI_FORMAT depthFormat=DXGI_FORMAT_UNKNOWN){
 auto at=[&](UINT i){D3D12_CPU_DESCRIPTOR_HANDLE h=start;h.ptr+=SIZE_T(i)*inc;return h;};
 {D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  d.Format=motion?motionFormat:DXGI_FORMAT_R16G16_FLOAT;dev->CreateShaderResourceView(motion,&d,at(0));}
 {D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;d.Format=DXGI_FORMAT_R32_TYPELESS;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  d.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;d.Buffer.NumElements=UINT(latemask::RingBytes/4);dev->CreateShaderResourceView(ring.Buffer(),&d,at(1));}
 {D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  const bool usable=depth&&depthFormat!=DXGI_FORMAT_UNKNOWN;d.Format=usable?depthFormat:DXGI_FORMAT_R32_FLOAT;dev->CreateShaderResourceView(usable?depth:nullptr,&d,at(2));}
 {D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2DARRAY;d.Format=DXGI_FORMAT_R32_UINT;d.Texture2DArray.ArraySize=2*latemask::MotionSlots;
  dev->CreateUnorderedAccessView(gpu.mvRing.Get(),nullptr,&d,at(3));}
 for(auto [resource,index]:{std::pair<ID3D12Resource*,UINT>{gpu.mvFrames.Get(),4},{gpu.latched.Get(),5}}){
  D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;d.Format=DXGI_FORMAT_R32_TYPELESS;d.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;d.Buffer.NumElements=64;
  dev->CreateUnorderedAccessView(resource,nullptr,&d,at(index));}
 {D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2DARRAY;d.Format=DXGI_FORMAT_R32_FLOAT;d.Texture2DArray.ArraySize=latemask::Votes;
  dev->CreateUnorderedAccessView(gpu.aligned.Get(),nullptr,&d,at(6));}
 for(auto [resource,index,format]:{std::tuple<ID3D12Resource*,UINT,DXGI_FORMAT>{gpu.tempA.Get(),7,DXGI_FORMAT_R32_FLOAT},{gpu.tempB.Get(),8,DXGI_FORMAT_R32_FLOAT},{gpu.mask.Get(),9,DXGI_FORMAT_R8_UNORM},{gpu.history.Get(),10,DXGI_FORMAT_R32_FLOAT}}){
  D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d.Format=format;dev->CreateUnorderedAccessView(resource,nullptr,&d,at(index));}
}
inline void Bind(ID3D12GraphicsCommandList* cl,D3D12_GPU_DESCRIPTOR_HANDLE start,UINT inc,ID3D12PipelineState* pso,const LateConstants& c){
 cl->SetComputeRootSignature(pipes.root.Get());cl->SetPipelineState(pso);cl->SetComputeRoot32BitConstants(0,16,c.v,0);
 cl->SetComputeRootDescriptorTable(1,start);cl->SetComputeRootDescriptorTable(2,{start.ptr+UINT64(SrvDescriptors)*inc});
}
inline void UavBarrier(ID3D12GraphicsCommandList* cl,ID3D12Resource* r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;cl->ResourceBarrier(1,&b);}
inline uint32_t Bits(float f){uint32_t u;std::memcpy(&u,&f,4);return u;}
inline std::vector<IUnknown*> Objects(){
 return {ring.Buffer(),gpu.mvRing.Get(),gpu.mvFrames.Get(),gpu.latched.Get(),gpu.aligned.Get(),gpu.tempA.Get(),gpu.tempB.Get(),gpu.mask.Get(),gpu.history.Get(),gpu.readback.Get(),
  pipes.root.Get(),pipes.motion.Get(),pipes.latch.Get(),pipes.gather.Get(),pipes.vote.Get(),pipes.gate.Get(),pipes.temporal.Get(),pipes.store.Get(),pipes.feather.Get()};
}
// At the capture point of every regional frame: this frame's motion at the mask grid.
// Records nothing and returns false when motion is unusable or nothing can be prepared.
inline bool RecordMotion(ID3D12Device* dev,ID3D12GraphicsCommandList* cl,resolveleases::Slot* lease,const MaskMotion& m,const FrameIdentity& frame) noexcept {
 static bool disabled=false;if(disabled||!m.texture||m.format==DXGI_FORMAT_UNKNOWN||!lease||!frame.frame)return false;
 const unsigned mh=latemask::MaskHeight(frame.width,frame.height);if(!mh||mh>latemask::MaxMaskHeight)return false;
 try{
  EnsurePipelines(dev);EnsureGpu(dev,latemask::MaskWidth,mh);
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=Descriptors;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  // S26: pooled heap; a full pool records no motion this frame and never disables the ring.
  auto* heap=yyheappool::motionHeaps.Acquire(dev,hd,lease);if(!heap)return false;
  for(auto* object:Objects())if(object&&!resolveleases::Hold(lease,object))return false;
  if(!resolveleases::Hold(lease,heap))return false;
  const UINT inc=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  const bool depth=m.depth&&m.depthFormat!=DXGI_FORMAT_UNKNOWN;
  if(depth&&!resolveleases::Hold(lease,m.depth))return false;
  FillDescriptors(dev,heap->GetCPUDescriptorHandleForHeapStart(),inc,m.texture,m.format,depth?m.depth:nullptr,depth?m.depthFormat:DXGI_FORMAT_UNKNOWN);
  // No fallible call after the first command.
  ID3D12DescriptorHeap* heaps[]={heap};cl->SetDescriptorHeaps(1,heaps);
  LateConstants c;c.v[0]=latemask::MaskWidth;c.v[1]=mh;c.v[2]=Bits(m.toUvX);c.v[3]=Bits(m.toUvY);
  c.v[4]=uint32_t(frame.frame%latemask::MotionSlots);c.v[5]=uint32_t(frame.frame);
  c.v[6]=depth?(m.depthInverted?2u:1u):0u;c.v[7]=latemask::MotionSlots;
  static bool reported=false;
  if(!reported){reported=true;Log("[033 YY S22 late mask] motion ring records motion%s at the mask grid; masks follow the recorded chain",depth?(m.depthInverted?" and inverted depth":" and depth"):" only (no usable depth guide)");}
  Bind(cl,heap->GetGPUDescriptorHandleForHeapStart(),inc,pipes.motion.Get(),c);
  cl->Dispatch((latemask::MaskWidth+7)/8,(mh+7)/8,1);
  UavBarrier(cl,gpu.mvRing.Get());UavBarrier(cl,gpu.mvFrames.Get());
  return true;
 }catch(const std::exception& e){disabled=true;Log("[033 YY S22 late mask] motion ring disabled: %s; masks stay in place",e.what());return false;}
}
inline void Transition(ID3D12GraphicsCommandList* cl,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
 D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.pResource=r;x.Transition.StateBefore=a;x.Transition.StateAfter=b;
 x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;cl->ResourceBarrier(1,&x);
}
// Latch (3 newest) -> gather each along its recorded motion chain -> vote ->
// S28 depth gate (this frame's depth) -> S30 temporal smoothing (history along this
// frame's motion; step from MaskTemporalStep) -> feather, into gpu.mask (left in UAV state); then the latch/vote words go to
// the telemetry readback slot of this frame. heap/start: Descriptors already
// filled with FillDescriptors(motion=nullptr).
inline void RecordMask(ID3D12GraphicsCommandList* cl,D3D12_GPU_DESCRIPTOR_HANDLE start,UINT inc,const FrameIdentity& frame,float feather,const TemporalStep& step){
 const unsigned w=gpu.w,h=gpu.h;const UINT gx=(w+7)/8,gy=(h+7)/8;
 LateConstants c;c.v[0]=uint32_t(frame.frame);c.v[1]=uint32_t(frame.stream);c.v[2]=uint32_t(frame.stream>>32);c.v[3]=uint32_t(frame.generation);
 c.v[4]=uint32_t(frame.generation>>32);c.v[5]=frame.width;c.v[6]=frame.height;c.v[7]=w;c.v[8]=h;c.v[9]=latemask::MaxFrames;
 c.v[10]=latemask::SlotStride;c.v[11]=latemask::Pitch;c.v[12]=latemask::Votes;
 Bind(cl,start,inc,pipes.latch.Get(),c);cl->Dispatch(1,1,1);UavBarrier(cl,gpu.latched.Get());
 LateConstants s;s.v[0]=w;s.v[1]=h;
 LateConstants g=s;g.v[2]=latemask::MotionSlots;g.v[3]=latemask::Pitch;g.v[4]=Bits(latemask::ChainToleranceTexels);g.v[5]=Bits(latemask::ChainToleranceRelative);
 g.v[6]=Bits(latemask::DepthTolerance);g.v[7]=Bits(latemask::DepthFloor);
 UavBarrier(cl,gpu.mvRing.Get());
 Bind(cl,start,inc,pipes.gather.Get(),g);cl->Dispatch(gx,gy,latemask::Votes);UavBarrier(cl,gpu.aligned.Get());UavBarrier(cl,gpu.latched.Get());
 LateConstants v=s;v.v[2]=latemask::Votes;Bind(cl,start,inc,pipes.vote.Get(),v);cl->Dispatch(gx,gy,1);UavBarrier(cl,gpu.tempB.Get());UavBarrier(cl,gpu.latched.Get());
 LateConstants d=s;d.v[2]=uint32_t(frame.frame);d.v[3]=latemask::MotionSlots;d.v[4]=uint32_t(GateRadius);d.v[5]=GateStride;
 d.v[6]=Bits(GateTolerance);d.v[7]=Bits(GateConfident);
 Bind(cl,start,inc,pipes.gate.Get(),d);cl->Dispatch(gx,gy,1);UavBarrier(cl,gpu.tempA.Get());UavBarrier(cl,gpu.latched.Get());
 LateConstants m=s;m.v[2]=uint32_t(frame.frame);m.v[3]=latemask::MotionSlots;m.v[4]=Bits(step.rise);m.v[5]=Bits(step.fall);m.v[6]=step.reset?1u:0u;
 m.v[7]=Bits(latemask::DepthTolerance);m.v[8]=Bits(latemask::DepthFloor);
 UavBarrier(cl,gpu.history.Get());
 Bind(cl,start,inc,pipes.temporal.Get(),m);cl->Dispatch(gx,gy,1);UavBarrier(cl,gpu.tempB.Get());UavBarrier(cl,gpu.tempA.Get());UavBarrier(cl,gpu.latched.Get());
 Bind(cl,start,inc,pipes.store.Get(),s);cl->Dispatch(gx,gy,1);UavBarrier(cl,gpu.history.Get());
 LateConstants f=s;f.v[3]=uint32_t(latemask::FeatherRadius(feather));f.v[4]=Bits(latemask::FeatherSigma(feather));
 f.v[2]=0;Bind(cl,start,inc,pipes.feather.Get(),f);cl->Dispatch(gx,gy,1);UavBarrier(cl,gpu.tempA.Get());
 f.v[2]=1;Bind(cl,start,inc,pipes.feather.Get(),f);cl->Dispatch(gx,gy,1);UavBarrier(cl,gpu.mask.Get());
 Transition(cl,gpu.latched.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
 cl->CopyBufferRegion(gpu.readback.Get(),UINT64(frame.frame%latemask::ReadbackSlots)*latemask::LatchedBytes,gpu.latched.Get(),0,latemask::LatchedBytes);
 Transition(cl,gpu.latched.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
// Telemetry only (render thread): the latch/vote words of the frame ReadbackLag
// composites ago. Its slot is used only when stamped with exactly that frame,
// so an unexecuted, skipped or overwritten slot is never counted.
struct VoteTotals {uint64_t composites=0,read=0,latched[latemask::Votes+1]{},age[6]{},kinds[5]{},newestPx=0,votedPx=0,trailRejectedPx=0,trailFrames=0,gatedPx=0,gatedFrames=0,heldPx=0,heldFrames=0,historyRejectedPx=0;};
inline VoteTotals voteTotals;
inline void ReadVote(uint64_t frame) noexcept {
 ++voteTotals.composites;
 if(!gpu.readback||frame<=latemask::ReadbackLag)return;
 const uint64_t want=frame-latemask::ReadbackLag;
 const SIZE_T offset=SIZE_T(want%latemask::ReadbackSlots)*latemask::LatchedBytes;
 void* mapped=nullptr;D3D12_RANGE range{offset,offset+latemask::LatchedBytes};
 if(FAILED(gpu.readback->Map(0,&range,&mapped))||!mapped)return;
 uint32_t w[latemask::word::HistoryRejectedPx+1];std::memcpy(w,static_cast<const uint8_t*>(mapped)+offset,sizeof w);
 D3D12_RANGE none{};gpu.readback->Unmap(0,&none);
 if(w[latemask::word::Stamp]!=uint32_t(want))return;
 ++voteTotals.read;
 const unsigned count=(std::min)(w[latemask::word::Count],latemask::Votes);++voteTotals.latched[count];
 if(count)++voteTotals.age[(std::min)(w[latemask::word::Age0],5u)];
 ++voteTotals.kinds[unsigned(latemask::ClassifyVote(w[latemask::word::NewestPx],w[latemask::word::VotedPx],w[latemask::word::FilledPx],w[latemask::word::RemovedPx]))];
 voteTotals.newestPx+=w[latemask::word::NewestPx];voteTotals.votedPx+=w[latemask::word::VotedPx];
 voteTotals.trailRejectedPx+=w[latemask::word::TrailRejectedPx];if(w[latemask::word::TrailRejectedPx]>=16)++voteTotals.trailFrames;
 voteTotals.gatedPx+=w[latemask::word::GatedPx];if(w[latemask::word::GatedPx]>=16)++voteTotals.gatedFrames;
 voteTotals.heldPx+=w[latemask::word::HeldPx];if(w[latemask::word::HeldPx]>=16)++voteTotals.heldFrames;voteTotals.historyRejectedPx+=w[latemask::word::HistoryRejectedPx];
}
}
}
