#pragma once
// Capture owned guide/white textures in the same command list as held colour.
// The caller retains both resources until its observed submission completes.
namespace nrsnapshot {
inline bool Clone(ID3D12Device* dev,ID3D12GraphicsCommandList* cmd,ID3D12Resource* source,
                  D3D12_RESOURCE_STATES sourceState,ID3D12Resource** target){
    if(!dev||!cmd||!source||!target||*target)return false;
    auto desc=source->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1)return false;
    desc.Alignment=0;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // Depth-only typed resources cannot be recreated as ordinary copy/SRV
    // textures. A compatible typeless copy retains all original depth bits.
    if(desc.Format==DXGI_FORMAT_D32_FLOAT)desc.Format=DXGI_FORMAT_R32_TYPELESS;
    else if(desc.Format==DXGI_FORMAT_D16_UNORM)desc.Format=DXGI_FORMAT_R16_TYPELESS;
    else if(desc.Format==DXGI_FORMAT_D24_UNORM_S8_UINT)desc.Format=DXGI_FORMAT_R24G8_TYPELESS;
    else if(desc.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)desc.Format=DXGI_FORMAT_R32G8X24_TYPELESS;
    // Copy-only snapshot, read as the source's original format by the consumer.
    desc.Flags=D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    if(FAILED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        __uuidof(ID3D12Resource),reinterpret_cast<void**>(target))))return false;
    scale::Barrier(cmd,source,sourceState,D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyResource(*target,source);
    scale::Barrier(cmd,source,D3D12_RESOURCE_STATE_COPY_SOURCE,sourceState);
    scale::Barrier(cmd,*target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return true;
}
}
