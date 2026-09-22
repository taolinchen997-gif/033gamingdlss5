#pragma once
#include "nr_zero_upload_policy.h"
// Preparation for the presentation-only route. These are still zero
// placeholders, never native guides. No initialized ReShade resource call,
// queue selection, Execute, flush, or CPU fence wait is permitted here.
namespace nrzero033 {
class Guides {
    using Resource=Microsoft::WRL::ComPtr<ID3D12Resource>;
    Gate gate;
    Resource depth,motion,pendingDepth,pendingMotion,upload;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[2]{};
    resolveleases::Ticket ticket;
    ID3D12Device* device=nullptr;
    ID3D12GraphicsCommandList* list=nullptr;
    reshade::api::command_list* commands=nullptr;
    HRESULT failure=S_OK;
    bool Checked(HRESULT hr,const char* stage){
        if(SUCCEEDED(hr))return true;
        failure=hr;Log("[033 zero upload] stage=%s failed hr=%08X",stage,unsigned(hr));return false;
    }
public:
    Result Poll(ID3D12Device* dev,ID3D12GraphicsCommandList* native,
                reshade::api::command_list* api,unsigned w,unsigned h){
        device=dev;list=native;commands=api;return gate.Poll(*this,w,h);
    }
    ID3D12Resource* Depth()const{return depth.Get();}
    ID3D12Resource* Motion()const{return motion.Get();}
    bool Prepare(unsigned w,unsigned h){
        Log("[033 zero upload] prepare size=%ux%u; recording-only initialization",w,h);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R32_FLOAT;
        if(!Checked(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(&pendingDepth)),"allocate-depth"))return false;
        UINT64 depthBytes=0,motionBytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&layout[0],nullptr,nullptr,&depthBytes);
        desc.Format=DXGI_FORMAT_R16G16_FLOAT;
        if(!Checked(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(&pendingMotion)),"allocate-motion"))return false;
        device->GetCopyableFootprints(&desc,0,1,0,&layout[1],nullptr,nullptr,&motionBytes);
        const UINT64 bytes=(std::max)(depthBytes,motionBytes);
        if(!bytes||bytes>SIZE_MAX){failure=E_INVALIDARG;return false;}
        heap.Type=D3D12_HEAP_TYPE_UPLOAD;desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(!Checked(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,IID_PPV_ARGS(&upload)),"allocate-upload"))return false;
        void* memory=nullptr;D3D12_RANGE read{0,0};
        if(!Checked(upload->Map(0,&read,&memory),"map-upload"))return false;
        if(!memory){upload->Unmap(0,nullptr);failure=E_POINTER;return false;}
        std::memset(memory,0,size_t(bytes));D3D12_RANGE written{0,size_t(bytes)};upload->Unmap(0,&written);
        pendingDepth->SetName(L"033 presentation zero depth");pendingMotion->SetName(L"033 presentation zero motion");
        upload->SetName(L"033 presentation zero upload");
        Log("[033 zero upload] allocated size=%ux%u upload_bytes=%llu; awaiting observed command lease",w,h,bytes);
        return true;
    }
    bool Record(){
        IUnknown* refs[]={pendingDepth.Get(),pendingMotion.Get(),upload.Get()};
        auto* lease=resolveleases::Begin(device,list,refs,3,this);
        if(!lease)return false;
        struct End {~End(){resolveleases::End();}} end;
        ticket=resolveleases::GetTicket(lease);
        const reshade::api::resource src{reinterpret_cast<uint64_t>(upload.Get())};
        const reshade::api::resource dst[2]={{reinterpret_cast<uint64_t>(pendingDepth.Get())},{reinterpret_cast<uint64_t>(pendingMotion.Get())}};
        for(unsigned i=0;i<2;++i)
            commands->copy_buffer_to_texture(src,layout[i].Offset,layout[i].Footprint.RowPitch/4,
                layout[i].Footprint.Height,dst[i],0,nullptr);
        const auto before=reshade::api::resource_usage::copy_dest;
        const auto after=reshade::api::resource_usage::shader_resource_non_pixel;
        commands->barrier(dst[0],before,after);commands->barrier(dst[1],before,after);
        Log("[033 zero upload] copies recorded on runtime list=%p; no nested flush or CPU wait",list);
        return true;
    }
    bool Complete(){return resolveleases::Completed(ticket);}
    void Publish(){
        depth=std::move(pendingDepth);motion=std::move(pendingMotion);upload.Reset();ticket={};
        Log("[033 zero upload] GPU submission and command retirement verified; placeholders ready");
    }
    void Discard(){pendingDepth.Reset();pendingMotion.Reset();upload.Reset();ticket={};}
};
}
