#define FRAMEGEN_DX12_TEST_NO_MAIN
#include "framegen_dx12_test.cpp"
#include "../src/universal_fg_images.h"
static std::vector<unsigned char> ReadRaw(Gpu& g,ID3D12Resource* r,D3D12_RESOURCE_STATES state){auto d=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0,rowBytes=0;
    g.device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,&rowBytes,&bytes);auto rb=g.buffer(bytes,false);auto l=g.begin();Gpu::barrier(l.cmd.Get(),r,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=rb.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=fp;b.pResource=r;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    l.cmd->CopyTextureRegion(&a,0,0,0,&b,nullptr);Gpu::barrier(l.cmd.Get(),r,D3D12_RESOURCE_STATE_COPY_SOURCE,state);g.wait(g.submit(l));void* p;D3D12_RANGE range{0,SIZE_T(bytes)};OK(rb->Map(0,&range,&p));
    std::vector<unsigned char> out(size_t(rowBytes)*d.Height);for(UINT y=0;y<d.Height;++y)memcpy(out.data()+size_t(y)*rowBytes,static_cast<char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,SIZE_T(rowBytes));D3D12_RANGE none{};rb->Unmap(0,&none);return out;}
int main(){setvbuf(stdout,nullptr,_IONBF,0);try{Gpu g;constexpr UINT w=19,h=13;
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT})for(UINT transfer=0;transfer<3;++transfer){
        if(transfer==2&&format!=DXGI_FORMAT_R16G16B16A16_FLOAT)continue;
        const UINT size=format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;std::vector<unsigned char> input(w*h*size);
        const unsigned char p8[4]={128,64,192,255};const uint32_t p10=512u|(256u<<10)|(768u<<20)|(3u<<30);
        const uint16_t p16s[4]={0x3800,0x3400,0x3a00,0x3c00},p16h[4]={0xb000,0x3e00,0x4800,0x3c00};
        const void* pixel=format==DXGI_FORMAT_R8G8B8A8_UNORM?static_cast<const void*>(p8):format==DXGI_FORMAT_R10G10B10A2_UNORM?static_cast<const void*>(&p10):transfer==2?static_cast<const void*>(p16h):static_cast<const void*>(p16s);
        for(size_t i=0;i<w*h;++i)memcpy(input.data()+i*size,pixel,size);
        auto src=g.texture(w,h,format);auto dst=g.texture(w,h,format,true);g.upload(src.Get(),input.data(),w*size);ufg033::Images images;Check(images.Initialize(g.device.Get(),g.fence.Get()),"colour pipeline initialized");
        for(UINT frame=1;frame<=2;++frame){auto l=g.begin();auto generated=images.Record(l.cmd.Get(),src.Get(),dst.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,transfer,frame,false);
            Check(generated==(frame==2?1:0),"one-frame warmup then generation");auto fence=g.submit(l);Check(images.Submitted(fence),"colour submission fenced");g.wait(fence);}
        auto output=ReadRaw(g,dst.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);bool correct=true;
        if(size==8){for(size_t i=0;i<input.size();i+=2){uint16_t a,b;memcpy(&a,input.data()+i,2);memcpy(&b,output.data()+i,2);correct&=abs(int(a)-int(b))<=2;}}
        else if(format==DXGI_FORMAT_R8G8B8A8_UNORM){for(size_t i=0;i<input.size();++i)correct&=abs(int(input[i])-int(output[i]))<=1;}
        else{for(size_t i=0;i<input.size();i+=4){uint32_t a,b;memcpy(&a,input.data()+i,4);memcpy(&b,output.data()+i,4);for(int c=0;c<3;++c)correct&=abs(int((a>>(c*10))&1023)-int((b>>(c*10))&1023))<=1;correct&=(a>>30)==(b>>30);}}
        printf("format=%u transfer=%u\n",unsigned(format),transfer);Check(correct,"SDR/PQ/scRGB roundtrip within target quantization");
        Check(ReadRaw(g,src.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)==input,"real input remains byte-exact");Check(images.Retire(),"colour history retired");
    }
    // A complete scene replacement must never blend the previous scene into
    // the generated output, even when the two scenes have identical luminance.
    {
        auto src=g.texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM);auto dst=g.texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        ufg033::Images images;Check(images.Initialize(g.device.Get(),g.fence.Get()),"cut pipeline initialized");
        std::vector<unsigned char> pixels(w*h*4,0);
        for(UINT frame=1;frame<=2;++frame){for(UINT i=0;i<w*h;++i){pixels[i*4]=frame==1?255:0;pixels[i*4+2]=frame==2?255:0;pixels[i*4+3]=255;}
            if(frame>1){auto uploadTransition=g.begin();Gpu::barrier(uploadTransition.cmd.Get(),src.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);g.wait(g.submit(uploadTransition));}
            g.upload(src.Get(),pixels.data(),w*4);auto l=g.begin();auto result=images.Record(l.cmd.Get(),src.Get(),dst.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,0,frame,false);
            Check(result==(frame==2?1:0),"scene-cut path warmup and output");auto value=g.submit(l);Check(images.Submitted(value),"cut recording fenced");g.wait(value);}
        auto output=ReadRaw(g,dst.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Check(output==pixels,"hard cut uses current scene with no previous colour ghost");
        Check(images.Retire(),"scene-cut buffers retired");
    }
    printf("%u colour checks passed.\n",checks);return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
