#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#include "nr_snapshot.h"
int main(){try{Gpu g;unsigned checks=0,failures=0;auto check=[&](bool b,const char*n){++checks;if(!b){++failures;printf("FAIL %s\n",n);}};
    for(auto fmt:{DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT}){
        auto source=g.texture(8,6,fmt,D3D12_RESOURCE_STATE_COPY_DEST);auto desc=source->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;g.dev->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);
        auto upload=g.buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ),read=g.buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        void* data=nullptr;D3D12_RANGE empty{0,0};OK(upload->Map(0,&empty,&data));std::memset(data,0,size);
        for(unsigned y=0;y<6;++y)reinterpret_cast<float*>(static_cast<char*>(data)+y*fp.Footprint.RowPitch)[0]=float(y+1);
        upload->Unmap(0,nullptr);g.begin();D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=source.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        scale::Barrier(g.cmd.Get(),source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ID3D12Resource* held=nullptr;check(nrsnapshot::Clone(g.dev.Get(),g.cmd.Get(),source.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&held),"copy snapshot");
        check(!nrsnapshot::Clone(g.dev.Get(),g.cmd.Get(),source.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&held),"live snapshot cannot be overwritten");g.wait();
        OK(upload->Map(0,&empty,&data));std::memset(data,0,size);upload->Unmap(0,nullptr);
        g.begin();scale::Barrier(g.cmd.Get(),source.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        scale::Barrier(g.cmd.Get(),held,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;src.pResource=held;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);g.wait();D3D12_RANGE range{0,SIZE_T(size)};OK(read->Map(0,&range,&data));
        bool retained=true;for(unsigned y=0;y<6;++y)retained&=reinterpret_cast<float*>(static_cast<char*>(data)+y*fp.Footprint.RowPitch)[0]==float(y+1);
        read->Unmap(0,&empty);check(retained,"later live input changes cannot alter held guides or exposure");held->Release();
    }
    for(auto fmt:{DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D16_UNORM,DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT}){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=8;desc.Height=6;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=fmt;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> depth;
        OK(g.dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&depth)));
        g.begin();ID3D12Resource* held=nullptr;
        check(nrsnapshot::Clone(g.dev.Get(),g.cmd.Get(),depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&held),"typed depth can be captured as compatible typeless guides");
        g.wait();if(held)held->Release();
    }
    check(SUCCEEDED(g.dev->GetDeviceRemovedReason()),"GPU healthy");printf("complete-frame snapshot primitives: %u checks, %u failures\n",checks,failures);return failures?1:0;
}catch(const std::exception&e){printf("ERROR %s\n",e.what());return 2;}}
