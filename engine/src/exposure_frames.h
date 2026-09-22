#pragma once
#include <array>
#include "exposure_policy.h"
// Included after exposure.h and resolve_leases.h. No frame-count reuse and no
// waits. Commands keep both the bindings and every referenced resource alive.
namespace exposure {
struct FrameSlot {
    ID3D12Resource* factor=nullptr;
    ID3D12Resource* white=nullptr;
    ID3D12DescriptorHeap* heap=nullptr;
    resolveleases::Ticket ticket;
    bool used=false;
};
struct FrameMeter : Meter {
    std::array<FrameSlot,resolveleases::Capacity> slots{};
    int previous=-1;
    uintptr_t stream=0;
    uint64_t sampleTick=0,lastTick=0,offered=0,missing=0,bypass=0;
    bool valid=false;
    exposurepolicy::Status status=exposurepolicy::Status::Waiting;
};
static void DestroyFrames(FrameMeter& m) {
    for(auto& s:m.slots){Rel(s.factor);Rel(s.white);Rel(s.heap);s={};}
    Destroy(m);m.previous=-1;m.sampleTick=0;m.valid=false;
}
static bool EnsureSlot(FrameSlot& s,ID3D12Device* dev) {
    if(!s.factor&&!Make1x1(dev,&s.factor))return false;
    if(!s.white&&!Make1x1(dev,&s.white))return false;
    if(!s.heap){D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=4;
        desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(dev->CreateDescriptorHeap(&desc,__uuidof(ID3D12DescriptorHeap),reinterpret_cast<void**>(&s.heap))))return false;
    }
    return true;
}
static ID3D12Resource* WhiteForFrame(FrameMeter& m,ID3D12Device* dev,ID3D12GraphicsCommandList* cl,
    resolveleases::Slot* lease,uintptr_t stream,bool reset,float gain,float fallbackWhite,bool arrivalKnown,D3D12_RESOURCE_STATES arrival) {
    using S=exposurepolicy::Status;
    const auto now=GetTickCount64();m.lastTick=now;m.valid=false;
    auto reject=[&](S why)->ID3D12Resource*{m.status=why;++m.bypass;return nullptr;};
    if(!dev||!cl||!lease)return reject(S::Failed);
    if(m.dev && m.dev!=dev)DestroyFrames(m);
    if(reset || m.stream!=stream){m.previous=-1;m.sampleTick=0;}
    m.stream=stream;
    const bool fresh=s_frame.texture!=nullptr;
    ID3D12Resource* input=s_frame.texture;
    DXGI_FORMAT inputFormat=DXGI_FORMAT_UNKNOWN;
    if(fresh){
        if(!arrivalKnown)return reject(S::UnknownState);
        const auto d=input->GetDesc();inputFormat=ExposureSrvFormat(d.Format);
        if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width!=1 || d.Height!=1 ||
           d.MipLevels!=1 || d.DepthOrArraySize!=1 || d.SampleDesc.Count!=1 ||
           (d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) || inputFormat==DXGI_FORMAT_UNKNOWN)return reject(S::Unsupported);
        ID3D12Device* owner=nullptr;input->GetDevice(__uuidof(ID3D12Device),reinterpret_cast<void**>(&owner));
        const bool same=owner==dev;Rel(owner);if(!same)return reject(S::Unsupported);
    }else{
        if(m.previous<0 || !exposurepolicy::CanHold(m.stream,stream,m.sampleTick,now,
            resolveleases::Completed(m.slots[m.previous].ticket)))return reject(S::Missing);
        input=m.slots[m.previous].factor;inputFormat=DXGI_FORMAT_R32_FLOAT;
    }
    if(!Create(m,dev))return reject(S::Failed);
    const int index=exposurepolicy::Reusable(m.slots,m.previous,resolveleases::Completed);
    if(index<0)return reject(S::Busy);
    auto& out=m.slots[index];
    if(!EnsureSlot(out,dev))return reject(S::Failed);
    IUnknown* refs[]={out.factor,out.white,out.heap,m.rs,m.pso,input};
    for(auto* ref:refs)if(!resolveleases::Hold(lease,ref))return reject(S::Failed);
    if(!fresh)m.slots[m.previous].ticket=resolveleases::GetTicket(lease);
    // No failure exits after the first barrier: recording owns all bindings.
    if(out.used){
        Barrier(cl,out.factor,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cl,out.white,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    const auto cpu=out.heap->GetCPUDescriptorHandleForHeapStart();
    auto h1=cpu,h2=cpu,h3=cpu;h1.ptr+=m.inc;h2.ptr+=2*m.inc;h3.ptr+=3*m.inc;
    CreateSrv(dev,input,inputFormat,cpu);CreateUav(dev,out.factor,h1);
    CreateSrv(dev,out.factor,DXGI_FORMAT_R32_FLOAT,h2);CreateUav(dev,out.white,h3);
    if(fresh)Barrier(cl,input,arrival,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const float pre=s_frame.have_pre&&std::isfinite(s_frame.pre)&&s_frame.pre>1e-6f&&s_frame.pre<1e6f?s_frame.pre:1.f;
    const float scale=s_frame.have_scale&&std::isfinite(s_frame.scale)&&s_frame.scale>1e-6f&&s_frame.scale<1e6f?s_frame.scale:1.f;
    Dispatch(m,cl,out.heap,0,1,fresh?0:2,false,gain,pre,scale,fallbackWhite);
    UavBarrier(cl,out.factor);
    Barrier(cl,out.factor,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(fresh)Barrier(cl,input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,arrival);
    Dispatch(m,cl,out.heap,2,3,1,false,gain,1.f,1.f,fallbackWhite);
    UavBarrier(cl,out.white);
    Barrier(cl,out.white,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    out.used=true;out.ticket=resolveleases::GetTicket(lease);m.valid=true;
    if(fresh){m.previous=index;m.sampleTick=now;++m.offered;m.status=S::GameRecorded;}
    else{++m.missing;m.status=S::Held;}
    return out.white;
}
static const char* note(const FrameMeter& m) {return exposurepolicy::Note(m.status);}
}
