#pragma once
#include "nr_guide_normalize_policy.h"
#include "nr_guide_normalize_shader.h"
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <array>
#include <utility>
// Included after the existing resolveleases implementation. No new submission
// queue, fence, wait, hook, thread or shared descriptor range is introduced.
namespace nrnormalize033 {
using Microsoft::WRL::ComPtr;
struct Cache {
    ComPtr<ID3D12Resource> depth,motion;
    ComPtr<ID3D12DescriptorHeap> heap;
    resolveleases::Ticket ticket{};
    uint64_t bytes=0;unsigned width=0,height=0;bool used=false,dirty=false;
};
struct Prepared {
    Cache* cache=nullptr;
    explicit operator bool()const{return cache!=nullptr;}
};
inline std::array<Cache,Capacity> cache;
inline ComPtr<ID3D12Device> pipelineDevice;
inline ComPtr<ID3D12RootSignature> signature;
inline ComPtr<ID3D12PipelineState> pipeline;
inline const char* note="等待原生引导格式转换";
inline ULONGLONG retryAfter=0;
inline unsigned failuresLogged=0,recordsLogged=0;
inline bool Fail(const char* text,HRESULT hr=E_FAIL){
    note=text;retryAfter=GetTickCount64()+1000;
    if(failuresLogged<8){++failuresLogged;Log("[033 guide normalize] prepare failed hr=%08X: %s",unsigned(hr),text);}
    return false;
}
inline bool Idle(Cache& c){return !c.used||resolveleases::Completed(c.ticket);}
inline void Drop(Cache& c){c=Cache{};}
inline bool Supports(ID3D12Device* dev,DXGI_FORMAT format,bool output){
    D3D12_FEATURE_DATA_FORMAT_SUPPORT f{};f.Format=format;
    if(FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&f,sizeof(f))))return false;
    if(!(f.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D))return false;
    return output?bool(f.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE):bool(f.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
}
inline bool EnsurePipeline(ID3D12Device* dev){
    if(pipelineDevice&&pipelineDevice.Get()!=dev)return Fail("原生引导转换设备已改变");
    if(pipeline)return true;
    if(!Supports(dev,DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,false)||!Supports(dev,DXGI_FORMAT_R16G16B16A16_SNORM,false)||
       !Supports(dev,DXGI_FORMAT_R32_FLOAT,true)||!Supports(dev,DXGI_FORMAT_R32G32_FLOAT,true))return Fail("设备不支持当前原生引导格式转换");
    // The existing scale stage has already loaded the shader compiler.
    auto dc=GetModuleHandleW(L"d3dcompiler_47.dll"),d12=GetModuleHandleW(L"d3d12.dll");
    auto compile=dc?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(dc,"D3DCompile")):nullptr;
    auto serialize=d12?reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(d12,"D3D12SerializeRootSignature")):nullptr;
    if(!compile||!serialize)return Fail("原生引导转换编译入口不可用");
    ComPtr<ID3DBlob> shader,error,blob;
    auto hr=compile(Shader,sizeof(Shader)-1,"033_native_guides",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&error);
    if(FAILED(hr))return Fail("原生引导转换着色器编译失败",hr);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=2;
    ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=2;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants.Num32BitValues=2;
    for(unsigned i=0;i<2;++i){params[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i+1].DescriptorTable.NumDescriptorRanges=1;params[i+1].DescriptorTable.pDescriptorRanges=&ranges[i];}
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=params;
    error.Reset();hr=serialize(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);
    if(FAILED(hr))return Fail("原生引导转换根签名序列化失败",hr);
    ComPtr<ID3D12RootSignature> rs;ComPtr<ID3D12PipelineState> ps;
    hr=dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&rs));
    if(FAILED(hr))return Fail("原生引导转换根签名创建失败",hr);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=rs.Get();pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
    hr=dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps));
    if(FAILED(hr))return Fail("原生引导转换管线创建失败",hr);
    signature=std::move(rs);pipeline=std::move(ps);pipelineDevice=dev;return true;
}
inline D3D12_RESOURCE_DESC OutputDesc(const Plan& plan,DXGI_FORMAT format){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=plan.width;d.Height=plan.height;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;return d;
}
inline bool Prepare(ID3D12Device* dev,ID3D12Resource* sourceDepth,ID3D12Resource* sourceMotion,
        bool identifiedRe4,bool budgetKnown,uint64_t available,Prepared& prepared,uint64_t budget=0){
    prepared={};
    if(!dev||!sourceDepth||!sourceMotion)return Fail("原生引导转换源不可用");
    const auto plan=Select(identifiedRe4,sourceDepth->GetDesc(),sourceMotion->GetDesc());
    if(!plan)return Fail("原生引导转换契约不匹配");
    if(GetTickCount64()<retryAfter)return false;
    if(!EnsurePipeline(dev))return false;
    Cache* chosen=nullptr;
    for(auto& c:cache)if(Idle(c)&&!c.dirty&&c.width==plan.width&&c.height==plan.height&&c.depth&&c.motion&&c.heap){chosen=&c;break;}
    if(!chosen)for(auto& c:cache)if(Idle(c)){chosen=&c;break;}
    if(!chosen){note="等待原生引导转换资源完成提交与回收";return false;}
    auto& c=*chosen;
    if(c.dirty||!c.depth||!c.motion||!c.heap||c.width!=plan.width||c.height!=plan.height){
        // Every dropped cache entry is confirmed idle, including replay safety.
        Drop(c);uint64_t retained=0;
        for(auto& other:cache){if(&other==chosen)continue;if(Idle(other))Drop(other);else retained+=other.bytes;}
        auto dd=OutputDesc(plan,DXGI_FORMAT_R32_FLOAT),md=OutputDesc(plan,DXGI_FORMAT_R32G32_FLOAT);
        const auto da=dev->GetResourceAllocationInfo(0,1,&dd),ma=dev->GetResourceAllocationInfo(0,1,&md);
        if(da.SizeInBytes==UINT64_MAX||ma.SizeInBytes==UINT64_MAX||da.SizeInBytes>UINT64_MAX-ma.SizeInBytes||
           !BudgetFits(retained,da.SizeInBytes+ma.SizeInBytes,budgetKnown,available,budget))return Fail("原生引导转换显存余量不足");
        ComPtr<ID3D12Resource> depth,motion;ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        auto hr=dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&dd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&depth));
        if(FAILED(hr))return Fail("原生深度转换纹理创建失败",hr);
        hr=dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&md,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&motion));
        if(FAILED(hr))return Fail("原生运动转换纹理创建失败",hr);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=4;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr=dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap));
        if(FAILED(hr))return Fail("原生引导转换描述符创建失败",hr);
        c.depth=std::move(depth);c.motion=std::move(motion);c.heap=std::move(heap);c.width=plan.width;c.height=plan.height;c.bytes=da.SizeInBytes+ma.SizeInBytes;
    }
    // This heap may be overwritten only after its prior exact lease retired.
    const auto step=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle=c.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;srv.Texture2D.PlaneSlice=0;
    srv.Format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;dev->CreateShaderResourceView(sourceDepth,&srv,handle);handle.ptr+=step;
    srv.Format=DXGI_FORMAT_R16G16B16A16_SNORM;dev->CreateShaderResourceView(sourceMotion,&srv,handle);handle.ptr+=step;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Format=DXGI_FORMAT_R32_FLOAT;dev->CreateUnorderedAccessView(c.depth.Get(),nullptr,&uav,handle);handle.ptr+=step;
    uav.Format=DXGI_FORMAT_R32G32_FLOAT;dev->CreateUnorderedAccessView(c.motion.Get(),nullptr,&uav,handle);
    prepared.cache=&c;return true;
}
inline void Attach(const Prepared& frame,resolveleases::Slot* lease){
    if(frame){frame.cache->ticket=resolveleases::GetTicket(lease);frame.cache->used=true;}
}
// Prepare and Begin must have retained BOTH inputs, outputs, heap, RS and PSO.
// No failure-return path remains after this starts emitting GPU commands.
inline void Record(ID3D12GraphicsCommandList* cl,const Prepared& frame){
    auto& c=*frame.cache;D3D12_RESOURCE_BARRIER barriers[2]{};ID3D12Resource* targets[]={c.depth.Get(),c.motion.Get()};
    for(unsigned i=0;i<2;++i){auto& b=barriers[i];b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={targets[i],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};}
    c.dirty=true; // An interrupted recording must never reuse unknown output states.
    cl->ResourceBarrier(2,barriers);
    auto* heap=c.heap.Get();cl->SetDescriptorHeaps(1,&heap);cl->SetComputeRootSignature(signature.Get());cl->SetPipelineState(pipeline.Get());
    const unsigned dims[]={c.width,c.height};cl->SetComputeRoot32BitConstants(0,2,dims,0);
    auto gpu=heap->GetGPUDescriptorHandleForHeapStart();cl->SetComputeRootDescriptorTable(1,gpu);
    gpu.ptr+=2*pipelineDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);cl->SetComputeRootDescriptorTable(2,gpu);
    cl->Dispatch(Groups(c.width),Groups(c.height),1);
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    cl->ResourceBarrier(2,barriers);c.dirty=false;note="原生深度 / 运动已转换，模型效果待验证";
    if(recordsLogged<16){++recordsLogged;Log("[033 guide normalize] recorded depth=19 plane=0 -> 41 motion=13 RG/NDC -> 16 size=%ux%u lease=%llu; output pending GPU completion",c.width,c.height,frame.cache->ticket.generation);}
}
}
