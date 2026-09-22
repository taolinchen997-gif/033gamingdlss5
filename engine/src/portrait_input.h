// Optional face conditioning before NR. Detection is asynchronous CPU work on
// a small thumbnail. Readback reuse requires the same verified leases as NR.
#pragma once
#include <wrl/client.h>
#include "portrait_detector.h"
#include "portrait_age_policy.h"
namespace portraitinput {
using Microsoft::WRL::ComPtr;
constexpr unsigned DescriptorBase=48,DescriptorEnd=54;
constexpr uint64_t CaptureInterval=150,MaxAge=portraitage::MaxAge;
static const char* Shader=
#include "portrait_input_hlsl.inl"
;
struct Constants {unsigned width=0,height=0;float amount=0;unsigned count=0;
    float boxes[4][4]{},eyes[4][4]{},mouths[4][4]{};};
static_assert(sizeof(Constants)==52*4,"portrait root constants layout");
struct Readback {ComPtr<ID3D12Resource> thumb,buffer;resolveleases::Ticket ticket;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};uint64_t tick=0;};
struct Context {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> thumbnail,enhance;
    ComPtr<ID3D12Resource> output,reference;
    Readback reads[3];portraitdetector::Result result;
    unsigned w=0,h=0,tw=0,th=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    uint64_t stream=0,epoch=0,lastCapture=0,referenceTick=0;
    std::vector<resolveleases::Ticket> uses;
};
static Context state;
static uint64_t epoch=0;
static unsigned faces=0;
static double detectorMs=0;
static uint64_t lastProcessed=0;
static bool unavailable=false;
static bool appliedThisFrame=false;
struct Diagnostics {uint64_t captures=0,read=0,expired=0,busy=0,submitted=0,readAge=0;};
static Diagnostics diagnostics;
static void Report(uint64_t now) {
    static uint64_t reported=0;if(now-reported<5000)return;reported=now;
    unsigned pending=0;for(const auto& read:state.reads)if(read.tick)++pending;
    Log("[033 skin prelight] captures=%llu reads=%llu expired=%llu busy=%llu submitted=%llu last_read_age_ms=%llu pending=%u detected=%u result_age_ms=%llu",
        diagnostics.captures,diagnostics.read,diagnostics.expired,diagnostics.busy,diagnostics.submitted,diagnostics.readAge,
        pending,state.result.count,state.result.tick?now-state.result.tick:0);
}
static std::vector<resolveleases::Ticket> retiring;
static unsigned Signature(int enabled,float strength){return enabled?0xface0001u^unsigned(strength*1000.f):0;}
static void CollectRetired(){retiring.erase(std::remove_if(retiring.begin(),retiring.end(),[](auto ticket){return resolveleases::Completed(ticket);}),retiring.end());}
static void Clear(){
    for(auto ticket:state.uses)if(!resolveleases::Completed(ticket))retiring.push_back(ticket);
    state={};faces=0;appliedThisFrame=false;detectorMs=0;lastProcessed=0;unavailable=false;++epoch;CollectRetired();
}
static void Barrier(ID3D12GraphicsCommandList* cl,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};cl->ResourceBarrier(1,&b);
}
static bool Texture(ID3D12Device* dev,ComPtr<ID3D12Resource>& out,unsigned w,unsigned h,DXGI_FORMAT fmt,D3D12_RESOURCE_STATES initial,bool uav){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=fmt;
    d.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
    return SUCCEEDED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,initial,nullptr,IID_PPV_ARGS(&out)));
}
static bool Buffer(ID3D12Device* dev,ComPtr<ID3D12Resource>& out,UINT64 size,D3D12_HEAP_TYPE type){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
        type==D3D12_HEAP_TYPE_READBACK?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&out)));
}
static bool Pipelines(Context& s){
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,52};
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    D3D12_ROOT_SIGNATURE_DESC desc{2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)) ||
        FAILED(s.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&s.root))))return false;
    const char* entries[]={"Thumbnail","Enhance"};ID3D12PipelineState** targets[]={s.thumbnail.GetAddressOf(),s.enhance.GetAddressOf()};
    for(unsigned i=0;i<2;++i){blob.Reset();error.Reset();
        if(FAILED(D3DCompile(Shader,strlen(Shader),"033 portrait",nullptr,nullptr,entries[i],"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=s.root.Get();pso.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
        if(FAILED(s.device->CreateComputePipelineState(&pso,__uuidof(ID3D12PipelineState),reinterpret_cast<void**>(targets[i]))))return false;
    }return true;
}
static bool Ensure(ID3D12Device* dev,unsigned w,unsigned h,DXGI_FORMAT format,uint64_t stream){
    if(state.device.Get()==dev && state.w==w && state.h==h && state.format==format && state.stream==stream)return !unavailable;
    Clear();
    // Rapid off/on or resolution changes must not allocate another full-size
    // face surface while the previous generation can still execute on the GPU.
    if(!retiring.empty())return false;
    state.device=dev;state.w=w;state.h=h;state.format=format;state.stream=stream;state.epoch=epoch;
    const double ratio=(std::min)(1.0,double(portraitdetector::MaxEdge)/(std::max)(w,h));
    state.tw=(std::max)(1u,unsigned(w*ratio));state.th=(std::max)(1u,unsigned(h*ratio));
    if(!Pipelines(state)){unavailable=true;return false;}
    for(auto& r:state.reads){
        if(!Texture(dev,r.thumb,state.tw,state.th,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true)){unavailable=true;return false;}
        auto d=r.thumb->GetDesc();UINT64 bytes=0;dev->GetCopyableFootprints(&d,0,1,0,&r.footprint,nullptr,nullptr,&bytes);
        if(!Buffer(dev,r.buffer,bytes,D3D12_HEAP_TYPE_READBACK)){unavailable=true;return false;}
    }return true;
}
static bool Hold(resolveleases::Slot* lease,std::initializer_list<IUnknown*> resources){
    for(auto resource:resources)if(!resolveleases::Hold(lease,resource))return false;return true;
}
static void Dispatch(ID3D12GraphicsCommandList* cl,resolveleases::Slot* lease,ID3D12PipelineState* pso,
    ID3D12Resource* source,ID3D12Resource* reference,ID3D12Resource* out,const Constants& c,unsigned base){
    auto* dev=state.device.Get();auto cpu=lease->heap->GetCPUDescriptorHandleForHeapStart();auto gpu=lease->heap->GetGPUDescriptorHandleForHeapStart();
    const auto stride=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);cpu.ptr+=base*stride;gpu.ptr+=base*stride;
    for(auto resource:{source,reference}){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=resource->GetDesc().Format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;dev->CreateShaderResourceView(resource,&d,cpu);cpu.ptr+=stride;}
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=out->GetDesc().Format;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;dev->CreateUnorderedAccessView(out,nullptr,&u,cpu);
    cl->SetDescriptorHeaps(1,&lease->heap);cl->SetComputeRootSignature(state.root.Get());cl->SetPipelineState(pso);
    cl->SetComputeRootDescriptorTable(0,gpu);cl->SetComputeRoot32BitConstants(1,52,&c,0);cl->Dispatch((c.width+7)/8,(c.height+7)/8,1);
}
static void ReadCompleted(uint64_t now){
    for(auto& r:state.reads)if(r.tick && resolveleases::Completed(r.ticket)){
        const auto captured=r.tick;r.tick=0;
        diagnostics.readAge=now-captured;
        if(portraitage::Weight(now,captured)==0){++diagnostics.expired;continue;}
        if(portraitdetector::busy.load()){++diagnostics.busy;continue;}
        // The final row may be shorter than RowPitch. Map only the allocation
        // returned by GetCopyableFootprints, including its actual last row.
        void* mapped=nullptr;const SIZE_T size=SIZE_T(r.buffer->GetDesc().Width);
        const D3D12_RANGE range{0,size};
        if(FAILED(r.buffer->Map(0,&range,&mapped)))continue;
        std::vector<unsigned char> rgba(size_t(state.tw)*state.th*4);
        for(unsigned y=0;y<state.th;++y)memcpy(rgba.data()+size_t(y)*state.tw*4,static_cast<unsigned char*>(mapped)+r.footprint.Offset+size_t(y)*r.footprint.Footprint.RowPitch,state.tw*4);
        const D3D12_RANGE written{0,0};r.buffer->Unmap(0,&written);
        ++diagnostics.read;portraitdetector::Start(std::move(rgba),state.tw,state.th,state.epoch,captured);
    }
    portraitdetector::Read(state.result);
    if(state.result.epoch==state.epoch)detectorMs=state.result.ms;
}
static bool UploadReference(ID3D12GraphicsCommandList* cl,resolveleases::Slot* lease){
    auto& s=state;
    ComPtr<ID3D12Resource> next,upload;
    if(!Texture(s.device.Get(),next,s.tw,s.th,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COPY_DEST,false))return false;
    auto d=next->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    s.device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    if(!Buffer(s.device.Get(),upload,bytes,D3D12_HEAP_TYPE_UPLOAD))return false;
    void* mapped=nullptr;D3D12_RANGE empty{0,0};if(FAILED(upload->Map(0,&empty,&mapped)))return false;
    for(unsigned y=0;y<s.th;++y)memcpy(static_cast<unsigned char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,s.result.rgba.data()+size_t(y)*s.tw*4,s.tw*4);
    upload->Unmap(0,nullptr);
    if(!Hold(lease,{next.Get(),upload.Get()}))return false;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=next.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
    cl->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(cl,next.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    s.reference=next;s.referenceTick=s.result.tick;return true;
}
static ID3D12Resource* Process(ID3D12Device* dev,ID3D12GraphicsCommandList* cl,resolveleases::Slot* lease,
    ID3D12Resource* source,unsigned w,unsigned h,DXGI_FORMAT format,uint64_t stream,bool enabled,float strength){
    appliedThisFrame=false;
    if(!enabled || strength<=0){if(state.device)Clear();return source;}
    const auto now=GetTickCount64();faces=0;
    if(!Ensure(dev,w,h,format,stream) || !Hold(lease,{state.root.Get(),state.thumbnail.Get(),state.enhance.Get()}))return source;
    auto ticket=resolveleases::GetTicket(lease);bool found=false;
    for(auto& used:state.uses)if(used.slot==ticket.slot){used=ticket;found=true;break;}
    if(!found)state.uses.push_back(ticket);
    lastProcessed=now;ReadCompleted(now);Report(now);
    if(now-state.lastCapture>=CaptureInterval && !portraitdetector::busy.load())for(auto& r:state.reads)if(!r.tick){
        if(!Hold(lease,{r.thumb.Get(),r.buffer.Get()}))break;
        Constants c;c.width=state.tw;c.height=state.th;
        Dispatch(cl,lease,state.thumbnail.Get(),source,source,r.thumb.Get(),c,DescriptorBase);
        Barrier(cl,r.thumb.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=r.buffer.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=r.footprint;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=r.thumb.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cl->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Barrier(cl,r.thumb.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        r.ticket=resolveleases::GetTicket(lease);r.tick=now;state.lastCapture=now;++diagnostics.captures;break;
    }
    const auto& r=state.result;
    bool protectFace=r.epoch==state.epoch && r.count && portraitage::Weight(now,r.tick)>0;
    if(protectFace && r.tick!=state.referenceTick && !UploadReference(cl,lease))protectFace=false;
    // Skin-colour processing is live even with no detected face, stale/busy
    // detection or unavailable reference upload. No face is required on screen.
    ID3D12Resource* reference=protectFace?state.reference.Get():source;
    if(!state.output && !Texture(dev,state.output,w,h,format,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,true))return source;
    if(!Hold(lease,{state.output.Get(),reference}))return source;
    Constants c;c.width=w;c.height=h;c.count=protectFace?r.count:0;c.amount=strength;
    for(unsigned i=0;i<c.count;++i){memcpy(c.boxes[i],r.faces[i].box,16);memcpy(c.eyes[i],r.faces[i].eyes,16);memcpy(c.mouths[i],r.faces[i].mouth,16);
        c.mouths[i][2]*=portraitage::Weight(now,r.tick);}
    Barrier(cl,state.output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Dispatch(cl,lease,state.enhance.Get(),source,reference,state.output.Get(),c,DescriptorBase+3);
    Barrier(cl,state.output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ++diagnostics.submitted;appliedThisFrame=true;faces=c.count;detectorMs=r.ms;return state.output.Get();
}
}
