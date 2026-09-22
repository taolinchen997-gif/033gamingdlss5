#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <stdexcept>
#include <limits>
#include "../src/framegen_policy.h"
#include "../src/framegen_compat.h"
#include "../src/framegen_flow_dx11.h"
using namespace framegen033;
using Microsoft::WRL::ComPtr;
static unsigned checks=0;
static void Check(bool yes,const char* name){++checks;std::printf("%s %s\n",yes?"PASS":"FAIL",name);if(!yes)throw std::runtime_error(name);}
static void OK(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D11 failed");}
struct P{float r,g,b,a;};
struct Tex{ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;};
static float Error(P a,P b){return (std::abs(a.r-b.r)+std::abs(a.g-b.g)+std::abs(a.b-b.b))/3;}
int main(){setvbuf(stdout,nullptr,_IONBF,0);try{
    CallbackHistory sequence;
    Check(!sequence.Duplicate(0),"callback frame zero accepted on new context");sequence.Record(0);
    Check(sequence.Duplicate(0),"duplicate callback suppressed");sequence.Reset();
    Check(!sequence.Duplicate(0),"context rebuild clears duplicate history");
    fgcompat033::Observe(100,15);Check(fgcompat033::Ready(fgcompat033::Flags(101)),"complete live input publication");
    Check(!fgcompat033::Ready(fgcompat033::Flags(1601)),"stale input cannot enable a route");
    fgcompat033::Observe(100,7);Check(!fgcompat033::Ready(fgcompat033::Flags(101)),"bridge resources do not claim a native DX12 path");
    Frame a{1,7,1000000000,64,48,1},b{2,7,1016666667,64,48,1};
    Check(Pair(a,b),"real frame pair");b.origin=Origin::Generated;Check(!Pair(a,b),"generated frame never becomes input");b.origin=Origin::Real;
    b.epoch++;Check(!Pair(a,b),"scene epoch invalidates history");b.epoch--;
    b.width++;Check(!Pair(a,b),"resize invalidates history");b.width--;
    b.id=1;Check(!Pair(a,b),"duplicate frame rejected");b.id=2;
    b.timeNs=a.timeNs;Check(!Pair(a,b),"nonmonotonic timestamp rejected");b.timeNs=a.timeNs+200000000;
    Check(!Pair(a,b),"long pause invalidates history");
    Check(InputsMatch(9,9,9,9)&&!InputsMatch(9,8,9,9)&&!InputsMatch(9,9,8,9)&&!InputsMatch(9,9,9,8),"depth MV HUD refer to same real frame");
    Pool pool;auto p0=pool.Acquire(0),p1=pool.Acquire(0),p2=pool.Acquire(0);
    Check(p0&&p1&&p2&&!pool.Acquire(0),"fixed three slots");
    Check(pool.Submit(p0,1)&&pool.Submit(p1,2)&&pool.Submit(p2,3),"ordered queue fences");
    Check(!pool.Acquire(0)&&!pool.Idle(2),"no reuse until queue completes");
    auto again=pool.Acquire(1);Check(again.index==p0.index&&!pool.Cancel(p0),"ABA ticket cannot cancel reused slot");
    Check(!pool.Submit(again,2)&&pool.Cancel(again)&&pool.Idle(3),"monotonic submission and cancel");
    auto plan=MakeSchedule(100,220,100,6,true);Check(plan.count==6&&plan.frames[5].real&&plan.frames[5].dueNs==220,"six phases and fixed real deadline");
    auto late=MakeSchedule(100,220,175,6,true);Check(late.dropped==3&&late.count==3&&late.frames[2].dueNs==220,"late generated frames dropped without postponing real");
    auto overdue=MakeSchedule(100,220,240,6,true);Check(overdue.count==1&&overdue.frames[0].real&&overdue.frames[0].dueNs==240,"overdue real frame goes first");
    Check(MakeSchedule(100,220,100,6,false).count==1&&MakeSchedule(100,220,100,999,true).count==1,"invalid input or multiplier bypass");
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
    OK(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&level,&ctx));
    std::puts("Adapter: WARP (no hardware GPU or game)");FlowDx11 backend;Check(backend.Initialize(dev.Get()),"production flow shaders compile");
    constexpr UINT w=64,h=48;
    auto texture=[&](const void* pixels,DXGI_FORMAT fmt,UINT stride,bool output=false){Tex t;D3D11_TEXTURE2D_DESC d{};
        d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=fmt;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(output?D3D11_BIND_UNORDERED_ACCESS:0);
        D3D11_SUBRESOURCE_DATA s{pixels,stride,0};OK(dev->CreateTexture2D(&d,pixels?&s:nullptr,&t.texture));
        OK(dev->CreateShaderResourceView(t.texture.Get(),nullptr,&t.srv));if(output)OK(dev->CreateUnorderedAccessView(t.texture.Get(),nullptr,&t.uav));return t;};
    auto read=[&](Tex& t){D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stage;OK(dev->CreateTexture2D(&d,nullptr,&stage));ctx->CopyResource(stage.Get(),t.texture.Get());
        D3D11_MAPPED_SUBRESOURCE m{};OK(ctx->Map(stage.Get(),0,D3D11_MAP_READ,0,&m));std::vector<P> out(w*h);
        for(UINT y=0;y<h;++y)std::memcpy(out.data()+y*w,static_cast<char*>(m.pData)+y*m.RowPitch,w*sizeof(P));ctx->Unmap(stage.Get(),0);return out;};
    auto pattern=[](int x,int y){unsigned k=unsigned(x+128)*1973u+unsigned(y+128)*9277u;k=(k^(k>>13))*1274126177u;
        float v=.15f+.6f*float((k>>12)&255)/255;return P{v,.1f+v*.7f,.2f+v*.4f,1};};
    std::vector<P> first(w*h),second(w*h),truth(w*h);std::vector<float> mask(w*h);
    auto output=texture(nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT,w*sizeof(P),true);
    for(auto shift:std::array<std::array<int,2>,3>{{{{4,0}},{{-4,2}},{{0,-4}}}}){
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){first[y*w+x]=pattern(x,y);second[y*w+x]=pattern(int(x)-shift[0],int(y)-shift[1]);truth[y*w+x]=pattern(int(x)-shift[0]/2,int(y)-shift[1]/2);}
        auto ta=texture(first.data(),DXGI_FORMAT_R32G32B32A32_FLOAT,w*sizeof(P));auto tb=texture(second.data(),DXGI_FORMAT_R32G32B32A32_FLOAT,w*sizeof(P));
        Check(backend.Run(ctx.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),.5f,w,h,4)==FlowResult::Submitted,"image-only interpolation submitted");
        auto result=read(output);double err=0,baseline=0;bool finite=true;
        for(UINT y=10;y<h-10;++y)for(UINT x=10;x<w-10;++x){size_t i=y*w+x;err+=Error(result[i],truth[i]);baseline+=Error(second[i],truth[i]);finite&=std::isfinite(result[i].r)&&result[i].a==1;}
        std::printf("translation (%d,%d): error %.6f current-only %.6f\n",shift[0],shift[1],err,baseline);
        Check(finite&&err<baseline*.45,"generated midpoint follows motion, not duplicate or crossfade");
    }
    auto ta=texture(first.data(),DXGI_FORMAT_R32G32B32A32_FLOAT,w*sizeof(P));auto tb=texture(second.data(),DXGI_FORMAT_R32G32B32A32_FLOAT,w*sizeof(P));
    for(auto phase:{0.f,1.f}){Check(backend.Run(ctx.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),phase,w,h)==FlowResult::Submitted,"endpoint submitted");
        auto result=read(output);Check(std::memcmp(result.data(),phase==0?first.data():second.data(),result.size()*sizeof(P))==0,"endpoint exactly preserves real frame");}
    for(UINT y=14;y<24;++y)for(UINT x=20;x<40;++x)mask[y*w+x]=1;
    auto tm=texture(mask.data(),DXGI_FORMAT_R32_FLOAT,w*sizeof(float));
    backend.Run(ctx.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),.5f,w,h,4,false,tm.srv.Get());
    auto protectedFrame=read(output);bool safe=true;for(size_t i=0;i<mask.size();++i)if(mask[i])safe&=std::memcmp(&protectedFrame[i],&second[i],sizeof(P))==0;
    Check(safe,"explicit HUD mask preserves current pixels exactly");
    backend.Run(ctx.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),.5f,w,h,4,true);auto cut=read(output);
    Check(std::memcmp(cut.data(),second.data(),cut.size()*sizeof(P))==0,"scene reset bypasses old flow");
    auto before=backend.AllocationBatches();
    for(int i=0;i<32;++i)backend.Run(ctx.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),i%2?0.f:1.f,w,h);
    read(output);Check(backend.AllocationBatches()==before&&backend.WorkingBytes()==uint64_t(w)*h*64,"32 switches reuse fixed GPU buffers");
    Check(backend.Run(ctx.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),std::numeric_limits<float>::quiet_NaN(),w,h)==FlowResult::Invalid,"NaN rejected");
    Check(backend.Run(ctx.Get(),output.srv.Get(),tb.srv.Get(),output.uav.Get(),.5f,w,h)==FlowResult::Invalid,"input-output alias rejected");
    auto integerInput=texture(nullptr,DXGI_FORMAT_R32G32B32A32_UINT,w*sizeof(P));
    Check(backend.Run(ctx.Get(),integerInput.srv.Get(),tb.srv.Get(),output.uav.Get(),.5f,w,h)==FlowResult::Unsupported,"integer input rejected before shader dispatch");
    Check(backend.Run(ctx.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),.5f,w,h,4,false,tb.srv.Get())==FlowResult::Invalid,"incorrect HUD mask format rejected");
    ComPtr<ID3D11DeviceContext> deferred;OK(dev->CreateDeferredContext(0,&deferred));
    Check(backend.Run(deferred.Get(),ta.srv.Get(),tb.srv.Get(),output.uav.Get(),.5f,w,h)==FlowResult::Unsupported,"deferred lifetime not guessed");
    Check(backend.Retire(ctx.Get())&&backend.WorkingBytes()==0,"completed GPU resources retired");
    std::printf("%u checks passed; output images only, no presented-frame claim.\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
