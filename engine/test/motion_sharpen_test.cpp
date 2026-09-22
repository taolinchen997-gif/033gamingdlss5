#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#include <limits>
struct Vec {float x,y;};
static ComPtr<ID3D12Resource> Motion(Gpu& g,UINT w,UINT h,const std::vector<Vec>& v) {
    auto t=g.texture(w,h,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={};UINT rows;UINT64 row,total;
    auto desc=t->GetDesc();g.dev->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&row,&total);
    auto up=g.buffer(total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* p=nullptr;D3D12_RANGE empty={};OK(up->Map(0,&empty,&p));
    for(UINT y=0;y<h;++y)std::memcpy(static_cast<char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,v.data()+y*w,w*sizeof(Vec));
    up->Unmap(0,nullptr);g.begin();
    D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=t.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=up.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
    g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    scale::Barrier(g.cmd.Get(),t.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g.wait();return t;
}
static bool Equal(const std::vector<Pixel>& a,const std::vector<Pixel>& b){return a.size()==b.size() && !std::memcmp(a.data(),b.data(),a.size()*sizeof(Pixel));}
int main(){try{
    Gpu g;unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* s){++checks;if(!ok){++failures;std::printf("FAIL %s\n",s);}};
    constexpr UINT w=19,h=13;std::vector<Pixel> input(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){float v=0.1f+float((x*3+y*7)%17)*0.02f;input[y*w+x]={v,v*.8f,v*.6f,float(x%7)/7.f};}
    auto run=[&](const scale::MotionSharpen* m,int mode=0){return g.run(input,w,h,mode,1,1,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,nullptr,1,2,1,m);};
    auto zero=Motion(g,w,h,std::vector<Vec>(w*h,{0,0}));
    auto fast=Motion(g,w,h,std::vector<Vec>(w*h,{8,0}));
    auto invalid=Motion(g,w,h,std::vector<Vec>(w*h,{std::numeric_limits<float>::quiet_NaN(),0}));
    auto medium=Motion(g,w,h,std::vector<Vec>(w*h,{4,0}));
    scale::MotionSharpen m;m.texture=zero.Get();m.format=DXGI_FORMAT_R32G32_FLOAT;
    m.extent[0]=w;m.extent[1]=h;m.scale[0]=m.scale[1]=1;m.moving=0;
    auto off=run(nullptr);check(Equal(off,input),"off preserves exact identity at full resolution");
    auto still=run(&m);check(!Equal(still,off),"static motion sharpens at full resolution including replica");
    m.texture=fast.Get();auto moving=run(&m);check(Equal(moving,off),"fast motion reduces sharpening to configured zero");
    m.texture=medium.Get();auto mid=run(&m);
    double staticDelta=0,midDelta=0;bool alpha=true,finite=true;
    for(size_t i=0;i<input.size();++i){staticDelta+=std::abs(still[i].r-off[i].r);midDelta+=std::abs(mid[i].r-off[i].r);alpha&=still[i].a==input[i].a;finite&=std::isfinite(still[i].r);}
    check(midDelta>0 && midDelta<staticDelta,"motion strength changes continuously");
    check(alpha && finite,"alpha preserved and output finite");
    m.texture=invalid.Get();check(Equal(run(&m),off),"NaN motion bypasses sharpening per pixel");
    m.texture=zero.Get();m.extent[0]=w+1;check(Equal(run(&m),off),"out-of-bounds guide rectangle rejected");m.extent[0]=w;
    m.scale[0]=std::numeric_limits<float>::infinity();check(Equal(run(&m),off),"invalid guide scale bypassed");m.scale[0]=1;
    std::vector<Vec> crop(9*7,{999,999});
    for(int y=2;y<5;++y)for(int x=2;x<7;++x)crop[y*9+x]={2,0};
    auto offset=Motion(g,9,7,crop);m.texture=offset.Get();m.origin[0]=2;m.origin[1]=2;m.extent[0]=5;m.extent[1]=3;m.scale[0]=2;
    check(Equal(run(&m),mid),"guide crop and normalized scale produce same output-pixel motion");
    m.texture=zero.Get();m.origin[0]=m.origin[1]=0;m.extent[0]=w;m.extent[1]=h;m.scale[0]=1;
    for(int mode=1;mode<=3;++mode) {
        for(size_t i=0;i<input.size();++i){float v=float(i%31)/2.f;input[i]={v,v*.7f,v*.3f,float(i%5)/5.f};if(mode==2){input[i].r=pq(v*100);input[i].g=pq(v*70);input[i].b=pq(v*30);}}
        auto out=run(&m,mode);bool sane=true;
        for(size_t i=0;i<out.size();++i){sane&=std::isfinite(out[i].r)&&std::isfinite(out[i].g)&&std::isfinite(out[i].b)&&out[i].a==input[i].a;}
        check(sane,"HDR motion sharpen is finite and preserves alpha");
    }
    check(Equal(run(nullptr,3),input),"toggle off returns to bit-exact baseline after enabled frames");
    std::printf("production motion-adaptive resolve: %u checks, %u failures\n",checks,failures);return failures?1:0;
}catch(const std::exception& e){std::printf("ERROR %s\n",e.what());return 2;}}
