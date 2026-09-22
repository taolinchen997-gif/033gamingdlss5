// 033 image-only backend for DirectX 12. Records into a caller-owned command
// list. It does not hook Present, wait for the GPU, or create an extra queue.
#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include "framegen_policy.h"
#include "d3d12_identity.h"
#include "framegen_flow_hlsl.inl"
namespace framegen033 {
using Microsoft::WRL::ComPtr;
class FlowDx12 {
    struct Image {ComPtr<ID3D12Resource> resource;};
    struct InFlight {ComPtr<ID3D12DescriptorHeap> descriptors;std::array<ComPtr<ID3D12Resource>,6> borrowed;};
    ComPtr<ID3D12Device> device;
    ComPtr<IUnknown> deviceIdentity;
    ComPtr<ID3D12Fence> completion;
    ComPtr<ID3D12RootSignature> root;
    std::array<ComPtr<ID3D12PipelineState>,3> pipelines;
    std::array<Image,12> images; // A/B/forward/backward at each of three levels
    std::array<InFlight,3> flights;
    Pool pool;
    UINT fw=0,fh=0,step=0;
    uint64_t allocations=0;
    const char* rejection="none";
    struct Params {UINT w,h,fw,fh;float phase;UINT radius,protect,reset;};
    bool Shader(const char* entry,ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> code,error;
        if(FAILED(D3DCompile(kFramegenFlowShader,sizeof(kFramegenFlowShader)-1,"033 image flow",nullptr,nullptr,
            entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)))return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.CS={code->GetBufferPointer(),code->GetBufferSize()};
        return SUCCEEDED(device->CreateComputePipelineState(&d,IID_PPV_ARGS(&pso)));
    }
    bool Same(ID3D12DeviceChild* child)const {
        return identity033::Child(deviceIdentity.Get(),child).equal;
    }
    static bool Colour(DXGI_FORMAT f) {
        return f==DXGI_FORMAT_R32G32B32A32_FLOAT||f==DXGI_FORMAT_R16G16B16A16_FLOAT||
            f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
            f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||f==DXGI_FORMAT_R10G10B10A2_UNORM;
    }
    static bool Texture(const D3D12_RESOURCE_DESC& d) {
        return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.SampleDesc.Count==1 && d.DepthOrArraySize==1 && d.MipLevels==1;
    }
    static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
    }
    void Pass(ID3D12GraphicsCommandList* list,InFlight& slot,UINT pass,UINT shader,const Params& p,
              ID3D12Resource* a,ID3D12Resource* b,ID3D12Resource* output,ID3D12Resource* protect=nullptr,ID3D12Resource* cut=nullptr,ID3D12Resource* seed=nullptr) {
        ID3D12Resource* sources[]={a,b,shader==2?images[10].resource.Get():seed,shader==2?images[11].resource.Get():nullptr,protect,cut};
        auto base=slot.descriptors->GetCPUDescriptorHandleForHeapStart();base.ptr+=size_t(pass)*7*step;
        for(UINT i=0;i<6;++i){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
            d.Format=sources[i]?sources[i]->GetDesc().Format:(i>=4?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT);
            auto handle=base;handle.ptr+=size_t(i)*step;device->CreateShaderResourceView(sources[i],&d,handle);}
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;u.Format=output->GetDesc().Format;
        auto handle=base;handle.ptr+=size_t(6)*step;device->CreateUnorderedAccessView(output,nullptr,&u,handle);
        ID3D12DescriptorHeap* heap=slot.descriptors.Get();list->SetDescriptorHeaps(1,&heap);
        auto gpu=heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(pass)*7*step;
        list->SetComputeRootSignature(root.Get());list->SetComputeRootDescriptorTable(0,gpu);
        list->SetComputeRoot32BitConstants(1,8,&p,0);list->SetPipelineState(pipelines[shader].Get());
        list->Dispatch(((shader==2?p.w:p.fw)+7)/8,((shader==2?p.h:p.fh)+7)/8,1);
    }
public:
    // Signal this fence on the one queue that executes these recordings. The
    // backend pins borrowed images/descriptors until that actual signal retires.
    bool Initialize(ID3D12Device* dev,ID3D12Fence* fence) {
        if(device)return identity033::Equal(device.Get(),dev)&&identity033::Equal(completion.Get(),fence);
        if(!dev||!fence)return false;device=dev;completion=fence;deviceIdentity=identity033::Canonical(dev);
        if(!deviceIdentity||!Same(fence)){device.Reset();completion.Reset();deviceIdentity.Reset();return false;}
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,6,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,6};
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,8};
        D3D12_STATIC_SAMPLER_DESC s{};s.Filter=D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
        s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;rd.NumStaticSamplers=1;rd.pStaticSamplers=&s;
        ComPtr<ID3DBlob> blob,error;bool ok=SUCCEEDED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))&&
            SUCCEEDED(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)))&&
            Shader("pyramidDown",pipelines[0])&&Shader("pyramidEstimate",pipelines[1])&&Shader("interpolate",pipelines[2]);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=98;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(ok)for(auto& f:flights)ok=ok&&SUCCEEDED(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&f.descriptors)));
        step=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if(!ok){device.Reset();completion.Reset();deviceIdentity.Reset();root.Reset();pipelines={};flights={};}return ok;
    }
    // previous/current/protection: NON_PIXEL_SHADER_RESOURCE, output: UAV.
    // The caller must execute recordings in order on the same queue, then signal
    // completion and call Submitted. Cancel is only legal after list discard.
    Ticket Record(ID3D12GraphicsCommandList* list,ID3D12Resource* previous,ID3D12Resource* current,ID3D12Resource* output,
                  float phase,UINT width,UINT height,UINT radius=4,bool reset=false,ID3D12Resource* protection=nullptr,ID3D12Resource* cut=nullptr) {
        ID3D12Resource* outputs[2]={output,nullptr};float phases[2]={phase,0};
        return RecordBatch(list,previous,current,outputs,phases,1,width,height,radius,reset,protection,cut);
    }
    // One pair, one estimation, immutable descriptors for both phases. A batch
    // owns one flight ticket, so 3x does not exhaust the pool twice as quickly.
    Ticket RecordBatch(ID3D12GraphicsCommandList* list,ID3D12Resource* previous,ID3D12Resource* current,
                  ID3D12Resource* const* outputs,const float* phases,UINT count,UINT width,UINT height,
                  UINT radius=4,bool reset=false,ID3D12Resource* protection=nullptr,ID3D12Resource* cut=nullptr) {
        rejection="batch-parameters";if(!outputs||!phases||count<1||count>2||(count>1&&(phases[0]<=0||phases[0]>=1)))return {};
        auto* output=outputs[0];const float phase=phases[0];
        rejection="device-uninitialized";if(!device)return {};
        rejection="device-list";if(!Same(list))return {};
        rejection="device-previous";if(!Same(previous))return {};
        rejection="device-current";if(!Same(current))return {};
        rejection="device-output";if(!Same(output))return {};
        rejection="device-protection";if(protection&&!Same(protection))return {};
        rejection="device-scene";if(cut&&!Same(cut))return {};
        rejection="parameters";if(
            previous==output||current==output||protection==output||!std::isfinite(phase)||phase<0||phase>1||
            !width||!height||width>640||height>640||radius>12)return {};
        if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT&&list->GetType()!=D3D12_COMMAND_LIST_TYPE_COMPUTE)return {};
        auto a=previous->GetDesc(),b=current->GetDesc(),o=output->GetDesc();
        rejection="texture-contract";
        if(!Texture(a)||!Texture(b)||!Texture(o)||!Colour(a.Format)||!Colour(b.Format)||
            (a.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||(b.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||a.Width!=b.Width||a.Height!=b.Height||
            a.Width!=o.Width||a.Height!=o.Height||width>a.Width||height>a.Height||
            (o.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT&&o.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)||
            !(o.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return {};
        for(UINT i=1;i<count;++i){
            if(!Same(outputs[i])||outputs[i]==previous||outputs[i]==current||outputs[i]==protection||outputs[i]==cut||outputs[i]==output||
               !std::isfinite(phases[i])||phases[i]<=0||phases[i]>=1||phases[i]<=phases[i-1])return {};
            auto extra=outputs[i]->GetDesc();if(!Texture(extra)||extra.Width!=o.Width||extra.Height!=o.Height||extra.Format!=o.Format||
                !(extra.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return {};
        }
        rejection="protection-mask";if(protection){auto m=protection->GetDesc();if(!Texture(m)||m.Width!=a.Width||m.Height!=a.Height||m.Format!=DXGI_FORMAT_R32_FLOAT||
            (m.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))return {};}
        rejection="scene-mask";if(cut){auto c=cut->GetDesc();if(!Texture(c)||c.Width!=1||c.Height!=1||c.Format!=DXGI_FORMAT_R32_FLOAT||cut==output)return {};}
        rejection="device-lost";const uint64_t completed=completion->GetCompletedValue();if(completed==UINT64_MAX)return {};
        if(fw!=width||fh!=height){
            rejection="resize-busy";if(!pool.Idle(completed))return {};
            std::array<Image,12> replacement;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
            d.MipLevels=d.DepthOrArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
            rejection="allocation";for(size_t index=0;index<replacement.size();++index){
                const UINT divisor=1u<<(2-unsigned(index/4));
                d.Width=(std::max)(1u,width/divisor);d.Height=(std::max)(1u,height/divisor);
                if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&replacement[index].resource))))return {};
            }
            images=std::move(replacement);fw=width;fh=height;++allocations;
        }
        rejection="descriptors-busy";auto ticket=pool.Acquire(completed);if(!ticket)return {};
        auto& slot=flights[ticket.index];slot.borrowed={previous,current,output,count==2?outputs[1]:nullptr,protection,cut};
        Params p{UINT(a.Width),a.Height,fw,fh,phase,radius,(protection?1u:0u)|(cut?2u:0u),reset?1u:0u};
        const bool calculate=!reset&&phase>0&&phase<1;
        if(calculate){
            // Fine luminance first, then a real 2x box-reduced pyramid.
            for(int level=2;level>=0;--level){
                const UINT base=UINT(level)*4,divisor=1u<<(2-level);
                p.fw=(std::max)(1u,fw/divisor);p.fh=(std::max)(1u,fh/divisor);p.protect=level<2?8u:0u;
                Pass(list,slot,base,0,p,level==2?previous:images[base+4].resource.Get(),nullptr,images[base].resource.Get());
                Barrier(list,images[base].resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Pass(list,slot,base+1,0,p,level==2?current:images[base+5].resource.Get(),nullptr,images[base+1].resource.Get());
                Barrier(list,images[base+1].resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            for(UINT level=0;level<3;++level){
                const UINT base=level*4,divisor=1u<<(2-level);
                p.fw=(std::max)(1u,fw/divisor);p.fh=(std::max)(1u,fh/divisor);
                p.radius=level?1:radius;p.protect=level?4u:0u;
                Pass(list,slot,base+2,1,p,images[base].resource.Get(),images[base+1].resource.Get(),images[base+2].resource.Get(),nullptr,nullptr,level?images[base-2].resource.Get():nullptr);
                Barrier(list,images[base+2].resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Pass(list,slot,base+3,1,p,images[base+1].resource.Get(),images[base].resource.Get(),images[base+3].resource.Get(),nullptr,nullptr,level?images[base-1].resource.Get():nullptr);
                Barrier(list,images[base+3].resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }else for(auto& i:images)Barrier(list,i.resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        p.fw=fw;p.fh=fh;p.protect=(protection?1u:0u)|(cut?2u:0u);
        for(UINT i=0;i<count;++i){p.phase=phases[i];Pass(list,slot,12+i,2,p,previous,current,outputs[i],protection,cut);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=outputs[i];list->ResourceBarrier(1,&barrier);}
        for(auto& i:images)Barrier(list,i.resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        rejection="none";return ticket;
    }
    bool Submitted(Ticket t,uint64_t fenceValue){return pool.Submit(t,fenceValue);}
    bool Discarded(Ticket t){if(!pool.Cancel(t))return false;flights[t.index].borrowed={};return true;}
    bool Retire(){if(!completion||!pool.Idle(completion->GetCompletedValue()))return false;
        images={};fw=fh=0;for(auto& f:flights)f.borrowed={};return true;}
    uint64_t AllocationBatches()const{return allocations;}
    const char* LastRejection()const{return rejection;}
    uint64_t WorkingBytes()const{uint64_t bytes=0;for(UINT d:{4u,2u,1u})bytes+=uint64_t((std::max)(1u,fw/d))*(std::max)(1u,fh/d)*64;return fw?bytes:0;}
};
}
