#pragma once
#include <chrono>
#include "yanyun_semantic_worker.h"
#include "yanyun_late_mask.h"
#include "nr_stack_finish.h"
namespace yanyundual {
struct Bank {
 ComPtr<ID3D12Device> device;carrier::Cfg cfg;uint64_t key=0,lastFrame=0;unsigned w=0,h=0,gw=0,gh=0;int count=1;
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN,modelFormat=DXGI_FORMAT_R16G16B16A16_FLOAT;
 Dimensions size[3]{};void* feature[3]{};ComPtr<ID3D12Resource> input[3],output[3],refined[2],finish,resolved,clarity,combined;
 exposure::FrameMeter exposure;
 scale::Blitter blit;ComPtr<ID3D12RootSignature> blendSignature;ComPtr<ID3D12PipelineState> blendPipeline; // borrowed from Pipelines (S19)
 unsigned inputSignature=0;bool inputSignatureValid=false; // person-only history key (S18)
 ~Bank(){for(auto& f:feature)nrfwd::release(f,false);exposure::DestroyFrames(exposure);}
};
// S19: the compiled person pipelines (scale/resolve set + blend) are created once
// and shared by every person bank and the mask view for the process lifetime;
// the person device can never change (Want() refuses). S18's log showed every
// person-model apply recompiling the whole shader set on the present thread:
// ~4 s of stalled frames and ~10 NR frames skipped per apply.
struct Pipelines {ComPtr<ID3D12Device> device;scale::Blitter blit;bool blitReady=false;ComPtr<ID3D12RootSignature> blendSignature;ComPtr<ID3D12PipelineState> blendPipeline;};
inline Pipelines shared;
// The person chain's own input encoding (its pre-grade and white). A change here
// resets ONLY the person NR history; scene grade/white/model changes no longer do.
inline unsigned PersonInputSignature(const carrier::Cfg& cfg){
 auto grade=pregrade::Checked(&cfg.pre);grade.skinProtection=0;
 unsigned h=pregrade::Signature(grade);
 h=(h*16777619u)^unsigned(cfg.white_source);
 h=(h*16777619u)^unsigned(exposurepolicy::FixedWhite(cfg.white_source,cfg.replica!=0,cfg.whitepoint,cfg.white_trim)*1000.f);
 h=(h*16777619u)^unsigned(cfg.replica?1:cfg.curve);
 return h;
}
inline Bank* active=nullptr;inline Bank* building=nullptr;inline Bank* maskView=nullptr;inline std::vector<Bank*> retired;
inline ComPtr<ID3D12Device> wantedDevice;inline unsigned wantW=0,wantH=0,wantGw=0,wantGh=0;inline DXGI_FORMAT wantFormat=DXGI_FORMAT_UNKNOWN;
inline ComPtr<ID3D12CommandQueue> buildQueue;inline ComPtr<ID3D12CommandAllocator> buildAllocator;
inline ComPtr<ID3D12GraphicsCommandList> buildList;inline ComPtr<ID3D12Fence> buildFence;inline UINT64 buildValue=0;
inline UINT64 memoryUsage=0,memoryBudget=0;
inline bool buildPoisoned=false;inline ULONGLONG retryAfter=0;
inline uint64_t ConfigKey(const carrier::Cfg& cfg,unsigned w,unsigned h,unsigned gw,unsigned gh,DXGI_FORMAT fmt){
 uint64_t hash=14695981039346656037ull;auto mix=[&](uint32_t u){hash=(hash^u)*1099511628211ull;};
 mix(w);mix(h);mix(gw);mix(gh);mix(unsigned(fmt));
 mix(cfg.work);mix(cfg.modelfull);mix(cfg.passwork);mix(cfg.passwork3);mix(cfg.passes);
 mix(nrlayers::Signature(cfg)); // Only model creation settings require another NR bank.
 return hash;
}
inline void Want(ID3D12Device* dev,unsigned w,unsigned h,unsigned gw,unsigned gh,DXGI_FORMAT fmt,UINT64 usage,UINT64 budget){
 memoryUsage=usage;memoryBudget=budget;
 if(!RecognitionRequested(enabled,previewMask.load()))return;
 if(wantedDevice&&!identity033::Equal(wantedDevice.Get(),dev)){note="人物处理设备已变化，请重启游戏";return;}
 wantedDevice=dev;wantW=w;wantH=h;wantGw=gw;wantGh=gh;wantFormat=fmt;
}
inline ComPtr<ID3D12Resource> Texture(ID3D12Device* dev,unsigned w,unsigned h,DXGI_FORMAT fmt,D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS){
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};
 desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=fmt;desc.SampleDesc.Count=1;
 desc.Flags=state==D3D12_RESOURCE_STATE_COPY_DEST?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 ComPtr<ID3D12Resource> texture;Require(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&texture)),"person texture allocation failed");return texture;
}
inline void EnsureBlend(ID3D12Device* dev){
 if(shared.blendPipeline){if(!identity033::Equal(shared.device.Get(),dev))throw std::runtime_error("person pipeline device changed");return;}
 D3D12_DESCRIPTOR_RANGE srv{},uav{};srv.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;srv.NumDescriptors=4; // original, person, scene, GPU-latched mask
 uav.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;uav.NumDescriptors=2; // result, latch result (S20)
 D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants.Num32BitValues=sizeof(BlendConstants)/4;
 p[1].ParameterType=p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 p[1].DescriptorTable={1,&srv};p[2].DescriptorTable={1,&uav};
 D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
 sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
 D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
 ComPtr<ID3DBlob> blob,error;Require(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"person blend root serialization failed");
 ComPtr<ID3D12RootSignature> signature;ComPtr<ID3D12PipelineState> pipeline;
 Require(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&signature)),"person blend root failed");
 blob.Reset();Require(D3DCompile(BlendShader,sizeof(BlendShader)-1,"033_person_scene",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error),"person blend shader failed");
 D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=signature.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
 Require(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline)),"person blend pipeline failed");
 shared.device=dev;shared.blendSignature=signature;shared.blendPipeline=pipeline; // published only when complete
 Log("[033 YY S19 pipelines] person blend pipeline compiled once; shared by all person banks");
}
inline void UseBlend(Bank& bank){EnsureBlend(bank.device.Get());bank.blendSignature=shared.blendSignature;bank.blendPipeline=shared.blendPipeline;}
inline void EnsureBlit(ID3D12Device* dev){
 EnsureBlend(dev);if(shared.blitReady)return;
 scale::Blitter blit;
 if(!scale::Create(blit,dev)){scale::Destroy(blit);throw std::runtime_error("person scale pipeline failed");}
 shared.blit=blit;shared.blitReady=true; // never destroyed: banks and in-flight leases borrow it
 Log("[033 YY S19 pipelines] person scale/resolve pipelines compiled once; shared by all person banks");
}
inline void BuildBank(Bank& bank){
 auto* dev=bank.device.Get();bank.count=std::clamp(bank.cfg.passes,1,3);
 bank.modelFormat=bank.format==DXGI_FORMAT_R32G32B32A32_FLOAT?bank.format:DXGI_FORMAT_R16G16B16A16_FLOAT;
 EnsureBlit(dev);bank.blit=shared.blit;UseBlend(bank);
 for(int i=0;i<bank.count;++i){
  bank.size[i]=ModelSize(bank.w,bank.h,bank.gw,bank.gh,bank.cfg.modelfull!=0,bank.cfg.work,i==0?100:i==1?bank.cfg.passwork:bank.cfg.passwork3);
  const auto size=bank.size[i];if(!size.w||!size.h)throw std::runtime_error("person model dimensions outside contract");
  bank.input[i]=Texture(dev,size.w,size.h,bank.modelFormat);bank.output[i]=Texture(dev,size.w,size.h,bank.modelFormat);
  auto model=nrlayers::Get(bank.cfg,i);DWORD seh=0;
  bank.feature[i]=nrfwd::create(dev,buildList.Get(),size.w,size.h,model.preset,model.intensity,model.style,model.structure,model.tone,model.skin,model.autoMask,model.uiCorrect,&seh,model.globalTone);
  if(!bank.feature[i]||seh||nrfault033::Blocked())throw std::runtime_error("person NR feature creation failed");
 }
 for(auto& r:bank.refined)r=Texture(dev,bank.size[0].w,bank.size[0].h,bank.modelFormat);
 bank.finish=Texture(dev,bank.size[0].w,bank.size[0].h,bank.modelFormat);
 bank.resolved=Texture(dev,bank.w,bank.h,bank.format);bank.clarity=Texture(dev,bank.w,bank.h,bank.format);bank.combined=Texture(dev,bank.w,bank.h,bank.format);
}
inline void PumpBuild(){
 nrdispatch::WriterAccess writer;if(!writer.entered)return;
 if(nrfault033::Blocked()||buildPoisoned)return;
 for(auto it=retired.begin();it!=retired.end();){if(!resolveleases::Pending(*it)){delete *it;it=retired.erase(it);}else ++it;}
 if(building){
  const auto done=buildFence->GetCompletedValue();if(done==UINT64_MAX){buildPoisoned=true;note="人物模型初始化设备失效";return;}
  if(done<buildValue)return;
  if(active)retired.push_back(active);active=building;building=nullptr;note="人物模型已就绪，等待识别结果";
  Log("[033 YY person] bank initialized; %d independent NR layers; %ux%u; not game-accepted",active->count,active->w,active->h);
 }
 if(!enabled&&active){retired.push_back(active);active=nullptr;}
 if(!RecognitionRequested(enabled,previewMask.load())&&maskView){retired.push_back(maskView);maskView=nullptr;}
 if(RecognitionRequested(enabled,previewMask.load())&&wantedDevice&&wantW&&wantH){
  if(maskView&&(maskView->w!=wantW||maskView->h!=wantH||maskView->format!=wantFormat)){retired.push_back(maskView);maskView=nullptr;}
  if(!maskView&&retired.empty()&&GetTickCount64()>=retryAfter){
   try{auto next=std::make_unique<Bank>();next->device=wantedDevice;next->w=wantW;next->h=wantH;next->format=wantFormat;next->count=0;
    UseBlend(*next);next->combined=Texture(wantedDevice.Get(),wantW,wantH,wantFormat);maskView=next.release();}
   catch(const std::exception& e){note="人物预览资源准备失败";Log("[033 YY mask] resource preparation failed: %s",e.what());retryAfter=GetTickCount64()+2000;return;}
  }
  if(Worker().Status()<0){note="人物识别失败，分区未生效，详见日志";return;}
 }
 if(!enabled||!wantedDevice||!wantW||!wantH||!retired.empty()||GetTickCount64()<retryAfter)return;
 const auto key=ConfigKey(person,wantW,wantH,wantGw,wantGh,wantFormat);
 if(active&&active->key==key)return;
 const auto estimate=EstimateBytes(wantW,wantH,wantGw,wantGh,person.modelfull!=0,person.work,person.passwork,person.passwork3,person.passes);
 if(!estimate){note="人物 SR 精度超出模型尺寸上限，请降低精度后应用";return;}
 if(!memoryBudget){note="等待显存预算信息，分区尚未生效";return;}
 if(!FitsBudget(memoryUsage,memoryBudget,estimate)){
  // The mismatched bank cannot be published. Retire it by exact lease proof,
  // instead of waiting forever for enough memory for two complete banks.
  if(active){retired.push_back(active);active=nullptr;note="等待旧人物模型安全回收，分区尚未生效";return;}
  note="人物模型显存余量不足，分区尚未生效";
  static ULONGLONG logged=0;if(GetTickCount64()-logged>5000){logged=GetTickCount64();Log("[033 YY person] budget blocked usage_MiB=%llu budget_MiB=%llu estimate_MiB=%llu output=%ux%u work=%d layers=%d",memoryUsage>>20,memoryBudget>>20,estimate>>20,wantW,wantH,person.work,person.passes);}return;}
 // Do not retire a live bank until a complete replacement is initialized.
 // A failed command submission is quarantined permanently, never guessed idle.
 Bank* bank=nullptr;try{bank=new Bank;}catch(...){note="人物参数内存分配失败";return;}bank->device=wantedDevice;bank->cfg=person;bank->w=wantW;bank->h=wantH;bank->gw=wantGw;bank->gh=wantGh;bank->format=wantFormat;bank->key=key;
 bool armed=false;
 try{
  if(!buildQueue){D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
   Require(wantedDevice->CreateCommandQueue(&q,IID_PPV_ARGS(&buildQueue)),"person init queue failed");
   Require(wantedDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&buildAllocator)),"person init allocator failed");
   Require(wantedDevice->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,buildAllocator.Get(),nullptr,IID_PPV_ARGS(&buildList)),"person init list failed");
   Require(buildList->Close(),"person initial close failed");Require(wantedDevice->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&buildFence)),"person init fence failed");}
  Require(buildAllocator->Reset(),"person init allocator reset failed");Require(buildList->Reset(buildAllocator.Get(),nullptr),"person init list reset failed");
  nrdispatch::LongStep longBuild;
  armed=true;BuildBank(*bank);Require(buildList->Close(),"person init close failed");
  ID3D12CommandList* lists[]={buildList.Get()};buildQueue->ExecuteCommandLists(1,lists);
  Require(buildQueue->Signal(buildFence.Get(),++buildValue),"person init signal failed");building=bank;
  note="正在初始化人物模型";
 }catch(const std::exception& e){
  Log("[033 YY person] build failed: %s; commands_armed=%d",e.what(),armed?1:0);note="人物模型准备失败，详见日志";retryAfter=GetTickCount64()+2000;
  if(armed){buildPoisoned=true;building=bank;}else delete bank;
 }
}
struct NestedLease {
 resolveleases::Slot* previous=resolveleases::recording;resolveleases::Slot* slot=nullptr;
 ~NestedLease(){if(slot)resolveleases::End();resolveleases::recording=previous;}
};
inline scale::MotionSharpen MotionSettings(const carrier::Cfg& cfg,ID3D12Resource* resource,const nrcontract::Guides* guides,float sx,float sy){
 scale::MotionSharpen m;if(!cfg.mas||!resource)return m;const auto desc=resource->GetDesc();
 m.texture=resource;m.format=desc.Format;m.still=cfg.mas_still;m.moving=cfg.mas_moving;m.threshold=cfg.mas_threshold;m.scale[0]=sx;m.scale[1]=sy;
 if(guides){m.origin[0]=guides->motion.x;m.origin[1]=guides->motion.y;m.extent[0]=guides->motion.width;m.extent[1]=guides->motion.height;}
 else {m.extent[0]=UINT(desc.Width);m.extent[1]=desc.Height;}return m;
}
inline ID3D12Resource* RenderPerson(Bank& b,ID3D12GraphicsCommandList* cl,resolveleases::Slot* lease,
 ID3D12Resource* original,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* white,
 const nrcontract::Guides* guides,int inverted,bool reset,float sx,float sy,int encode,int resolveMode,uint64_t frame,uintptr_t stream){
 auto* dev=b.device.Get();scale::Blitter blit=b.blit;blit.heap=lease->heap;blit.white_bound=nullptr;
 const auto& cfg=b.cfg;auto grade=pregrade::Checked(&cfg.pre);grade.skinProtection=0;
 const float fixedWhite=exposurepolicy::FixedWhite(cfg.white_source,cfg.replica!=0,cfg.whitepoint,cfg.white_trim);
 const int curve=cfg.replica?1:cfg.curve;
 white=nullptr;
 if(exposurepolicy::UsesGame(cfg.white_source,cfg.replica!=0,encode)){
  D3D12_RESOURCE_STATES arrival=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;const bool known=carrier::ExposureArrivalState(arrival);
  white=exposure::WhiteForFrame(b.exposure,dev,cl,lease,stream,reset,exposurepolicy::ExposureGain(cfg.white_source,cfg.whitepoint,cfg.white_trim),fixedWhite,known,arrival);
 }
 auto motionSharpen=MotionSettings(cfg,motion,guides,sx,sy);
 scale::Dispatch(blit,dev,cl,original,b.format,b.input[0].Get(),b.modelFormat,b.size[0].w,b.size[0].h,0,fixedWhite,encode,white,curve,cfg.diffuse_white,&grade);
 scale::Barrier(cl,b.input[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 ID3D12Resource* result=nullptr;
 for(int i=0;i<b.count;++i){
  const unsigned base=i?nrstack::PassBase(i):0;
  if(i){
   scale::Barrier(cl,result,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   scale::DispatchResolve(blit,dev,cl,b.input[0].Get(),b.input[0].Get(),result,b.modelFormat,b.refined[0].Get(),b.size[0].w,b.size[0].h,
    1.f,1.f,3.f,1,1.f,0.f,nullptr,4,0,0,1,0.f,203.f,nullptr,nullptr,nrskin::Protection(b.count),0.f,UINT(base));
   scale::Barrier(cl,result,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   scale::Barrier(cl,b.refined[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   scale::Dispatch(blit,dev,cl,b.refined[0].Get(),b.modelFormat,b.input[i].Get(),b.modelFormat,b.size[i].w,b.size[i].h,int((base+4)/2));
   scale::Barrier(cl,b.refined[0].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   scale::Barrier(cl,b.input[i].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  const auto tune=nrlayers::Get(cfg,i);DWORD seh=0;
  int code=nrfwd::evaluate(cl,b.feature[i],b.input[i].Get(),depth,motion,b.output[i].Get(),b.size[i].w,b.size[i].h,b.gw,b.gh,
   inverted,reset||b.lastFrame+1!=frame,tune.intensity,tune.style,tune.structure,tune.tone,tune.skin,tune.autoMask,sx,sy,&seh,guides,tune.globalTone);
  if(seh||code!=NVSDK_NGX_Result_Success){buildPoisoned=true;note="人物模型求值失败，分区效果未提交";Log("[033 YY person] evaluate layer=%d result=0x%08X seh=0x%08X",i+1,unsigned(code),unsigned(seh));return nullptr;}
  result=b.output[i].Get();
  if(i){
   scale::Barrier(cl,result,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   scale::Barrier(cl,b.refined[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   scale::DispatchResolve(blit,dev,cl,b.refined[0].Get(),b.input[i].Get(),result,b.modelFormat,b.refined[1].Get(),b.size[0].w,b.size[0].h,
    1.f,1.f,3.f,1,1.f,0.f,nullptr,3,0,0,1,0.f,203.f,nullptr,nullptr,0.f,0.f,UINT(base+6));
   scale::Barrier(cl,b.refined[0].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   scale::Barrier(cl,result,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   scale::Barrier(cl,b.input[i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);result=b.refined[1].Get();
  }
 }
 scale::Barrier(cl,result,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 scale::DispatchResolve(blit,dev,cl,b.input[0].Get(),b.input[0].Get(),result,b.modelFormat,b.finish.Get(),b.size[0].w,b.size[0].h,
  1.f,1.f,3.f,1,1.f,0.f,nullptr,4,0,0,1,0.f,203.f,nullptr,nullptr,nrskin::Protection(b.count),0.f,UINT(nrstack::FinalBase));
 scale::Barrier(cl,result,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 scale::Barrier(cl,b.finish.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 scale::DispatchResolve(blit,dev,cl,original,b.input[0].Get(),b.finish.Get(),b.format,b.resolved.Get(),b.w,b.h,
  cfg.blend/100.f,fixedWhite,cfg.guard,resolveMode,cfg.colour,0.f,white,cfg.replica?1:cfg.compose,cfg.resample,curve,1,0.f,cfg.diffuse_white,&motionSharpen,&grade,
  nrskin::Protection(b.count),cfg.skin_lift,UINT(4));
 scale::Barrier(cl,b.finish.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 scale::Barrier(cl,b.input[0].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 scale::Barrier(cl,b.resolved.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 scale::DispatchResolve(blit,dev,cl,b.resolved.Get(),b.resolved.Get(),b.resolved.Get(),b.format,b.clarity.Get(),b.w,b.h,
  cfg.natural_look,1.f,cfg.guard,resolveMode,1.f,cfg.sharpen,nullptr,5,0,0,1,0.f,cfg.diffuse_white,nullptr,nullptr,0.f,0.f,UINT(nrstack::ClarityBase));
 scale::Barrier(cl,b.resolved.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 b.lastFrame=frame;return b.clarity.Get();
}
inline ID3D12Resource* Composite(ID3D12Device* dev,ID3D12GraphicsCommandList* cl,
 ID3D12Resource* original,ID3D12Resource* scene,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* white,
 const nrcontract::Guides* guides,int inverted,bool reset,float sx,float sy,int encode,int resolveMode,const FrameIdentity& frame) noexcept {
 const bool preview=previewMask.load();
 if(!RecognitionRequested(enabled,preview))return scene;
 if(buildPoisoned)return nullptr;
 // S18: a person bank awaiting replacement keeps rendering with the settings it
 // was built with (as the scene's hot switch does) instead of withholding the
 // whole regional result for the entire rebuild. Only a missing bank or a
 // geometry/format mismatch withholds composition.
 const bool bankFits=active&&active->w==wantW&&active->h==wantH&&active->gw==wantGw&&active->gh==wantGh&&
  active->format==wantFormat&&frame.width==wantW&&frame.height==wantH;
 // S30: nothing to compose (no person bank for this geometry yet) shows the scene
 // look, not the game's original picture.
 if(!CanComposite(preview,maskView&&maskView->w==frame.width&&maskView->h==frame.height,bankFits))return scene;
 auto& b=*(preview?maskView:active);
 const bool bankCurrent=preview||b.key==ConfigKey(person,wantW,wantH,wantGw,wantGh,wantFormat);
 if(!preview&&bankCurrent)b.cfg=person; // Dynamic grade/mix/sharpen values do not rebuild model memory.
 // The CPU still decides WHETHER to compose (identity + age of the newest mask it
 // can see) and the fade weight; WHICH mask is used is decided later on the GPU
 // (S20 late latch; S21 votes over it and the two publications before it; S22
 // aligns all three along the recorded motion).
 const auto snapshot=Worker().Snapshot();const auto now=GetTickCount64();
 const auto reason=snapshot?MaskStatus(snapshot->source,frame,now):MaskReason::Empty;
 const uint64_t age=snapshot&&now>=snapshot->source.capturedMs?now-snapshot->source.capturedMs:UINT64_MAX;
 const uint64_t framesOld=snapshot&&frame.frame>=snapshot->source.frame?frame.frame-snapshot->source.frame:UINT64_MAX;
 // S30 (user: 「渐变可不可以调更自然点，就是更慢一点会更好感觉」): the person mask is
 // smoothed in time on the GPU (TemporalShader), so an old or missing recognition
 // fades the person look out THERE; the CPU only decides whether to compose. It
 // keeps composing for MaskHistoryHoldMs after the last usable recognition of this
 // stream/generation/size (the GPU latch then finds nothing newer and the history
 // fades); any identity change restarts from zero. The shown weight only ramps the
 // person look in (MaskFadeInMs). When nothing is composed the SCENE look is shown,
 // never the game's original picture (S18-S29 returned the original).
 static uint64_t lastUsableMs=0,lastUsableStream=0,lastUsableGeneration=0;static unsigned lastUsableW=0,lastUsableH=0;
 if(reason==MaskReason::Ready){lastUsableMs=now;lastUsableStream=frame.stream;lastUsableGeneration=frame.generation;lastUsableW=frame.width;lastUsableH=frame.height;}
 const bool sameIdentity=lastUsableMs&&lastUsableStream==frame.stream&&lastUsableGeneration==frame.generation&&lastUsableW==frame.width&&lastUsableH==frame.height;
 const bool warm=reason==MaskReason::Ready||(sameIdentity&&now>=lastUsableMs&&now-lastUsableMs<=MaskHistoryHoldMs);
 // Every call (composed or not) advances the clock so a gap restarts the ramp.
 static float shown=0.f;static uint64_t lastCall=0;
 const uint64_t elapsed=lastCall&&now>lastCall?now-lastCall:0;lastCall=now;
 shown=ShownMaskWeight(shown,elapsed,warm?1.f:0.f);
 const float weight=preview?(warm?1.f:0.f):shown;
 const bool ringReady=late::ring.Buffer()!=nullptr;
 // Bounded counters: admission and weights, not proof of displayed pixels.
 static uint64_t maskChecks=0,maskFull=0,maskFaded=0,maskAged=0,bankHeld=0,ringMissing=0,historyFrames=0,sceneFallback=0;
 ++maskChecks;if(weight>=1.f)++maskFull;else if(weight>0.f)++maskFaded;if(reason==MaskReason::Age)++maskAged;
 if(weight>0.f&&!bankCurrent)++bankHeld;if(!ringReady)++ringMissing;if(warm&&reason!=MaskReason::Ready)++historyFrames;
 if(!(weight>0.f)||!ringReady)++sceneFallback;
 if(maskChecks>=128&&maskChecks<=8192&&(maskChecks&(maskChecks-1))==0)
  Log("[033 YY S30 mask delivery] checked=%llu full=%llu faded=%llu age_rejected=%llu other_withheld=%llu person_bank_hold=%llu ring_missing=%llu history_frames=%llu scene_fallback=%llu cpu_latest_source=%llu cpu_age_ms=%llu cpu_age_frames=%llu shown=%.2f; GPU latches the three newest finished masks at execution, aligns, votes and smooths them in time; admission only, not display acceptance",
   maskChecks,maskFull,maskFaded,maskAged,maskChecks-maskFull-maskFaded-maskAged,bankHeld,ringMissing,historyFrames,sceneFallback,snapshot?snapshot->source.frame:0,age,framesOld,shown);
 if(!(weight>0.f)||!ringReady){
  note=Worker().Status()<0?"人物识别启动失败，请看日志":reason==MaskReason::Age?"人物识别延迟过高，暂用场景效果":snapshot?"画面已变化，暂用场景效果，等待重新识别":"等待当前画面的人物识别结果，暂用场景效果";
  static unsigned rejectedLogs=0;static uint64_t lastRejectedSource=0;
  if(snapshot&&snapshot->source.frame!=lastRejectedSource&&rejectedLogs<12){++rejectedLogs;lastRejectedSource=snapshot->source.frame;Log("[033 YY mask rejected] reason=%u source=%llu current=%llu source_generation=%llu current_generation=%llu age_ms=%llu age_frames=%llu hold_ms=%llu ring=%d; scene look shown (S30)",unsigned(reason),snapshot->source.frame,frame.frame,snapshot->source.generation,frame.generation,age,framesOld,MaskHistoryHoldMs,ringReady?1:0);}
  return scene;
 }
 bool armed=false;
 try{
  const unsigned mh=latemask::MaskHeight(frame.width,frame.height);
  if(!mh||mh>latemask::MaxMaskHeight){note="画面比例超出人物识别范围，暂用场景效果";return scene;}
  late::EnsurePipelines(dev);late::EnsureGpu(dev,latemask::MaskWidth,mh);
  // 0..8 late-mask set (t0 unused here), 9..12 blend SRVs, 13..14 blend UAVs.
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=late::Descriptors+6;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  // S26: pooled heap, bound to this pass's lease once it exists (yanyun_heap_pool.h).
  yyheappool::Reservation blend(yyheappool::blendHeaps,yyheappool::blendHeaps.Reserve(dev,hd));
  if(!blend.heap){note="等待人物处理资源退休，暂用场景效果";return scene;}
  ID3D12DescriptorHeap* const blendHeap=blend.heap;
  std::vector<IUnknown*> refs{original,scene,depth,motion,white,b.resolved.Get(),b.clarity.Get(),b.combined.Get(),b.refined[0].Get(),b.refined[1].Get(),b.finish.Get(),
   b.blit.rs,b.blit.pso,b.blit.rs_rv,b.blit.pso_rv,b.blit.pso_stack,b.blendSignature.Get(),b.blendPipeline.Get(),blendHeap};
  for(auto* object:late::Objects())refs.push_back(object);
  for(int i=0;i<b.count;++i){refs.push_back(b.input[i].Get());refs.push_back(b.output[i].Get());}
  NestedLease scope;scope.slot=resolveleases::Begin(dev,cl,refs.data(),refs.size(),&b,leasewait033::Owner::NR);
  if(!scope.slot){note="等待人物处理资源退休，暂用场景效果";return scene;}
  blend.Bind(resolveleases::GetTicket(scope.slot));
  armed=true;
  // Person history resets on shared discontinuities (passed in) or on a change
  // of the person chain's OWN input encoding; never on scene-only changes.
  const unsigned inputSignature=preview?0u:PersonInputSignature(b.cfg);
  const bool personReset=reset||!b.inputSignatureValid||b.inputSignature!=inputSignature;
  // S21: does the person layer really restart? Every history reset it receives,
  // by cause: shared stream/rectangle/hold change, its own input encoding, or
  // a composite missing in between (RenderPerson resets on a frame gap).
  static uint64_t personFrames=0,resetShared=0,resetOwnInput=0,resetGap=0;
  if(!preview){++personFrames;if(reset)++resetShared;else if(personReset)++resetOwnInput;if(b.lastFrame&&b.lastFrame+1!=frame.frame)++resetGap;}
  auto* character=preview?original:RenderPerson(b,cl,scope.slot,original,depth,motion,white,guides,inverted,personReset,sx,sy,encode,resolveMode,frame.frame,frame.stream);
  if(!character)return nullptr;
  if(!preview){b.inputSignature=inputSignature;b.inputSignatureValid=true;}
  const auto inc=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  const auto cpu=blendHeap->GetCPUDescriptorHandleForHeapStart();const auto gpu=blendHeap->GetGPUDescriptorHandleForHeapStart();
  late::FillDescriptors(dev,cpu,inc,nullptr,DXGI_FORMAT_UNKNOWN);
  ID3D12Resource* inputs[]={original,character,scene,late::gpu.mask.Get()};
  for(unsigned i=0;i<4;++i){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=i==3?DXGI_FORMAT_R8_UNORM:b.format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
   dev->CreateShaderResourceView(inputs[i],&d,{cpu.ptr+SIZE_T(late::Descriptors+i)*inc});}
  {D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=b.format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;dev->CreateUnorderedAccessView(b.combined.Get(),nullptr,&uv,{cpu.ptr+SIZE_T(late::Descriptors+4)*inc});}
  {D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Format=DXGI_FORMAT_R32_TYPELESS;uv.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;uv.Buffer.NumElements=64;
   dev->CreateUnorderedAccessView(late::gpu.latched.Get(),nullptr,&uv,{cpu.ptr+SIZE_T(late::Descriptors+5)*inc});}
  ID3D12DescriptorHeap* heaps[]={blendHeap};cl->SetDescriptorHeaps(1,heaps);
  // GPU: take the three newest finished masks NOW, follow each along the motion
  // recorded since its frame (S22), vote per texel, cut what this frame's depth
  // puts behind the person (S28), smooth in time along this frame's motion (S30),
  // feather. The history continues only from the previous frame of this stream
  // on this mask set; otherwise it restarts from this frame's vote.
  static uint64_t lastComposedFrame=0,lastComposedStream=0,lastComposedGeneration=0,lastComposedSet=0;
  static std::chrono::steady_clock::time_point lastComposedAt{};
  const auto composedAt=std::chrono::steady_clock::now();
  const bool continuous=lastComposedFrame&&frame.frame==lastComposedFrame+1&&frame.stream==lastComposedStream&&frame.generation==lastComposedGeneration&&late::gpuGeneration==lastComposedSet;
  const TemporalStep step=MaskTemporalStep(lastComposedFrame?std::chrono::duration<double,std::milli>(composedAt-lastComposedAt).count():0.,continuous);
  late::RecordMask(cl,gpu,inc,frame,feather,step);
  lastComposedFrame=frame.frame;lastComposedStream=frame.stream;lastComposedGeneration=frame.generation;lastComposedSet=late::gpuGeneration;lastComposedAt=composedAt;
  static uint64_t temporalSteps=0,temporalResets=0;if(step.reset)++temporalResets;else ++temporalSteps;
  late::ReadVote(frame.frame);
  {const auto& t=late::voteTotals;
   if(t.composites==128||t.composites%512==0){
    Log("[033 YY S30 gpu vote] composites=%llu read=%llu latched none/1/2/3=%llu/%llu/%llu/%llu newest_age_frames 1/2/3/4/5+=%llu/%llu/%llu/%llu/%llu frames steady=%llu edge_fixed=%llu dropout_fixed=%llu extra_removed=%llu no_person=%llu person_texels newest=%llu voted=%llu trail_rejected_texels=%llu trail_frames=%llu depth_gated_texels=%llu gated_frames=%llu history_held_texels=%llu held_frames=%llu history_rejected_texels=%llu temporal_steps=%llu temporal_restarts=%llu; person_layer frames=%llu reset_shared=%llu reset_own_input=%llu frame_gap=%llu; GPU words read back %u composites later",
     t.composites,t.read,t.latched[0],t.latched[1],t.latched[2],t.latched[3],t.age[1],t.age[2],t.age[3],t.age[4],t.age[5],
     t.kinds[unsigned(latemask::VoteKind::Steady)],t.kinds[unsigned(latemask::VoteKind::Edge)],t.kinds[unsigned(latemask::VoteKind::Dropout)],
     t.kinds[unsigned(latemask::VoteKind::Extra)],t.kinds[unsigned(latemask::VoteKind::None)],t.newestPx,t.votedPx,t.trailRejectedPx,t.trailFrames,t.gatedPx,t.gatedFrames,t.heldPx,t.heldFrames,t.historyRejectedPx,temporalSteps,temporalResets,
     personFrames,resetShared,resetOwnInput,resetGap,latemask::ReadbackLag);
    yyheappool::LogPools();}}
  scale::Barrier(cl,late::gpu.mask.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  if(!preview)scale::Barrier(cl,character,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  scale::Barrier(cl,scene,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  cl->SetComputeRootSignature(b.blendSignature.Get());cl->SetPipelineState(b.blendPipeline.Get());
  BlendConstants constants{b.w,b.h,RegionalPersonFidelity,RegionalSceneStrength,preview?1u:0u,1u,weight};
  cl->SetComputeRoot32BitConstants(0,sizeof(constants)/4,&constants,0);cl->SetComputeRootDescriptorTable(1,{gpu.ptr+UINT64(late::Descriptors)*inc});
  cl->SetComputeRootDescriptorTable(2,{gpu.ptr+UINT64(late::Descriptors+4)*inc});cl->Dispatch((b.w+7)/8,(b.h+7)/8,1);
  scale::UavBarrier(cl,b.combined.Get());
  scale::Barrier(cl,late::gpu.mask.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  scale::Barrier(cl,scene,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(!preview)scale::Barrier(cl,character,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  static unsigned acceptedLogs=0;
  if(acceptedLogs<3){++acceptedLogs;Log("[033 YY S30 mask accepted] cpu_source=%llu current=%llu cpu_age_ms=%llu weight=%.2f mask=%ux%u feather=%.2f person_bank_current=%d person_reset=%d preview=%d temporal=%s; GPU votes over the three newest finished masks at execution; composite recorded, display pending",snapshot?snapshot->source.frame:0,frame.frame,age,weight,latemask::MaskWidth,mh,feather,bankCurrent?1:0,personReset?1:0,preview?1:0,step.reset?"restart":"continue");}
  if(!preview)++recorded;
  note=preview?"正在显示人物识别区域（白色为人物）":!bankCurrent?"人物新参数准备中，暂用上一套人物参数":weight<1.f?"人物分区平滑过渡中":reason!=MaskReason::Ready?"人物识别暂缺，人物效果正在淡出":"人物与场景分区已录制";
  return b.combined.Get();
 }catch(const std::exception& e){if(armed)buildPoisoned=true;note="分区处理失败，详见日志";Log("[033 YY composite] failed: %s; armed=%d",e.what(),armed?1:0);return nullptr;}
 catch(...){if(armed)buildPoisoned=true;note="分区处理异常";return nullptr;}
}
}
