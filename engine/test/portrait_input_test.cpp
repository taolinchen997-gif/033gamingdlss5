#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#define K033_TIMING_STANDALONE
#define K033_TIMING_SUBMIT_TEST
#include "../src/gputime_fenced.h"
#define K033_LEASE_STANDALONE
#include "../src/resolve_leases.h"
#include "../src/portrait_input.h"
#include <fstream>
static std::vector<Pixel> Read(Gpu& g,ID3D12Resource* texture,unsigned w,unsigned h){
    auto desc=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
    g.dev->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);
    auto buffer=g.buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    g.begin();portraitinput::Barrier(g.cmd.Get(),texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=buffer.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=fp;b.pResource=texture;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    g.cmd->CopyTextureRegion(&a,0,0,0,&b,nullptr);portraitinput::Barrier(g.cmd.Get(),texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g.wait();
    void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(size)};OK(buffer->Map(0,&range,&mapped));
    std::vector<Pixel> pixels(size_t(w)*h);for(unsigned y=0;y<h;++y)memcpy(pixels.data()+size_t(y)*w,static_cast<unsigned char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,w*sizeof(Pixel));
    D3D12_RANGE empty{0,0};buffer->Unmap(0,&empty);return pixels;
}
int main(int argc,char** argv){try{
    SetEnvironmentVariableW(L"K033_TEST_HARDWARE",L"0");
    if(argc!=2)throw std::runtime_error("RGBA fixture required");
    std::ifstream file(argv[1],std::ios::binary);unsigned w=0,h=0;file.read(reinterpret_cast<char*>(&w),4);file.read(reinterpret_cast<char*>(&h),4);
    if(!w||!h||w>320||h>320)throw std::runtime_error("fixture dimensions");
    std::vector<unsigned char> rgba(size_t(w)*h*4);file.read(reinterpret_cast<char*>(rgba.data()),rgba.size());if(!file)throw std::runtime_error("fixture data");
    auto detected=portraitdetector::Detect(rgba,w,h,1,GetTickCount64());
    unsigned checks=0,failures=0;auto check=[&](bool good,const char* name){++checks;if(!good){++failures;printf("FAIL %s\n",name);}};
    check(detected.count>0,"real upstream photo detected by statically linked YuNet");
    auto blank=portraitdetector::Detect(std::vector<unsigned char>(size_t(w)*h*4,0),w,h,1,GetTickCount64());
    check(blank.count==0,"black image has no recognized face");
    printf("Detector: faces=%u, thumbnail=%ux%u, CPU=%.3f ms\n",detected.count,w,h,detected.ms);
    Gpu g;constexpr auto fmt=DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto input=g.texture(w,h,fmt,D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<Pixel> source(size_t(w)*h);for(size_t i=0;i<source.size();++i)source[i]={rgba[4*i]/255.f,rgba[4*i+1]/255.f,rgba[4*i+2]/255.f,.7f};
    auto d=input->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;g.dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size);
    auto upload=g.buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* mapped=nullptr;D3D12_RANGE empty{0,0};OK(upload->Map(0,&empty,&mapped));
    for(unsigned y=0;y<h;++y)memcpy(static_cast<unsigned char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,source.data()+size_t(y)*w,w*sizeof(Pixel));upload->Unmap(0,nullptr);
    g.begin();D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=input.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;
    g.cmd->CopyTextureRegion(&a,0,0,0,&b,nullptr);portraitinput::Barrier(g.cmd.Get(),input.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g.wait();
    IUnknown* refs[]={input.Get()};g.begin();resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,1);resolveleases::Pump(g.queue.Get(),true);
    auto* lease=resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,1);if(!lease)throw std::runtime_error("lease unavailable");
    check(portraitinput::Ensure(g.dev.Get(),w,h,fmt,1),"production portrait root signature and shaders compile on WARP");
    detected.epoch=portraitinput::state.epoch;detected.tick=GetTickCount64();portraitinput::state.result=detected;
    portraitdetector::busy=true; // deterministic fixture; no worker launched in this section
    auto* result=portraitinput::Process(g.dev.Get(),g.cmd.Get(),lease,input.Get(),w,h,fmt,1,true,.35f);
    check(result!=input.Get(),"recognized face takes pre-NR enhancement path");
    ComPtr<ID3D12Resource> held=result;
    portraitinput::Clear();
    check(!portraitinput::Ensure(g.dev.Get(),w,h,fmt,1),"rapid recreation waits for the old recorded generation instead of accumulating full-size textures");
    resolveleases::End();g.wait();
    auto pixels=Read(g,result,w,h);unsigned modified=0,escaped=0;float maximum=0;bool bounded=true,alpha=true,hue=true;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){size_t i=size_t(y)*w+x;auto p=pixels[i],s=source[i];float diff=fabs(p.r-s.r)+fabs(p.g-s.g)+fabs(p.b-s.b);
        maximum=(std::max)(maximum,diff);if(diff>1e-5){++modified;bool inside=false;
            for(unsigned f=0;f<detected.count;++f){auto* box=detected.faces[f].box;inside|=(x+.5f)/w>=box[0]&&(x+.5f)/w<=box[0]+box[2]&&(y+.5f)/h>=box[1]&&(y+.5f)/h<=box[1]+box[3];}if(!inside)++escaped;}
        alpha&=p.a==s.a;bounded&=std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&p.r<=s.r*1.1601f&&p.g<=s.g*1.1601f&&p.b<=s.b*1.1601f;
        hue&=fabs(p.r*s.g-p.g*s.r)<1e-5&&fabs(p.r*s.b-p.b*s.r)<1e-5;
    }
    check(modified>20,"nonzero enhancement reaches identified face pixels");check(escaped==0,"pixels outside detected faces unchanged");check(alpha&&bounded&&hue,"alpha and hue preserved with bounded finite brightness");
    printf("Input enhancement: changed=%u/%zu pixels, outside=%u, max RGB difference sum=%.7f\n",modified,source.size(),escaped,maximum);
    g.begin();lease=resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,1);if(!lease)throw std::runtime_error("second lease unavailable");
    check(portraitinput::Ensure(g.dev.Get(),w,h,fmt,1),"completed/reset generation permits a new face context");
    detected.epoch=portraitinput::state.epoch;portraitinput::state.result=detected;
    portraitinput::state.result.tick=GetTickCount64()-portraitinput::MaxAge-1;
    check(portraitinput::Process(g.dev.Get(),g.cmd.Get(),lease,input.Get(),w,h,fmt,1,true,.35f)==input.Get(),"expired face detection bypasses without moving NR output");
    portraitinput::state.result.tick=GetTickCount64();portraitinput::state.result.epoch+=1;
    check(portraitinput::Process(g.dev.Get(),g.cmd.Get(),lease,input.Get(),w,h,fmt,1,true,.35f)==input.Get(),"old stream detection rejected");
    check(portraitinput::Process(g.dev.Get(),g.cmd.Get(),lease,input.Get(),w,h,fmt,1,false,.35f)==input.Get()&&!portraitinput::state.output,"disable releases owner and returns exact source");
    resolveleases::End();g.wait();g.begin();g.wait();portraitdetector::busy=false;
    auto after=Read(g,held.Get(),w,h);check(!memcmp(after.data(),pixels.data(),pixels.size()*sizeof(Pixel)),"leased output survives owner clear until recorded work completes");
    const auto deadline=GetTickCount64()+4000;bool asyncFace=false;unsigned frames=0;
    while(GetTickCount64()<deadline&&!asyncFace){
        g.begin();lease=resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,1);if(!lease)throw std::runtime_error("async lease unavailable");
        portraitinput::Process(g.dev.Get(),g.cmd.Get(),lease,input.Get(),w,h,fmt,2,true,.35f);
        asyncFace=portraitinput::faces>0;resolveleases::End();g.wait();++frames;Sleep(5);
    }
    check(asyncFace,"actual async thumbnail readback and CPU worker publish a fresh face");
    printf("Async detector: %u frames, %.3f ms last CPU inference\n",frames,portraitinput::detectorMs);
    printf("Async result: count=%u epoch=%llu current=%llu age=%llu CPU=%.3f busy=%d\n",portraitinput::state.result.count,portraitinput::state.result.epoch,portraitinput::state.epoch,GetTickCount64()-portraitinput::state.result.tick,portraitinput::state.result.ms,int(portraitdetector::busy.load()));
    for(auto& read:portraitinput::state.reads)printf("Readback tick=%llu complete=%d generation=%llu\n",read.tick,int(resolveleases::Completed(read.ticket)),read.ticket.generation);
    printf("Leases admitted=%llu retired=%llu executed=%llu unavailable=%d\n",resolveleases::admitted,resolveleases::retired,gputime::submit::calls.load(),portraitinput::unavailable?1:0);
    printf("Detector jobs=%u completed=%u failed=%u Win32Error=%lu\n",portraitdetector::jobs.load(),portraitdetector::finished.load(),portraitdetector::failed.load(),portraitdetector::lastError.load());
    while(portraitdetector::busy.load())Sleep(5);
    portraitinput::Clear();g.begin();g.wait();
    printf("PORTRAIT INPUT: %u checks, %u failures; WARP only, no NR/game/hardware GPU\n",checks,failures);return failures?1:0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}}
