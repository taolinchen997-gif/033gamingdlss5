#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <limits>
#include "../src/framegen_flow_dx12.h"
#include "../src/framegen_flow_abi.h"
using namespace framegen033;
using Microsoft::WRL::ComPtr;
static unsigned checks=0;
static void Check(bool ok,const char* name){++checks;printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)throw std::runtime_error(name);}
static void OK(HRESULT hr){if(FAILED(hr)){printf("HRESULT %08X\n",unsigned(hr));throw std::runtime_error("D3D12 failure");}}
struct P{float r,g,b,a;};
static float Error(P a,P b){return (std::abs(a.r-b.r)+std::abs(a.g-b.g)+std::abs(a.b-b.b))/3;}
// The identical pixel/lifetime tests can exercise either the local header or
// the exported implementation inside the built 033 engine DLL.
struct Backend {
    FlowDx12 local;const fgflow033abi::Api* api=nullptr;void* handle=nullptr;const wchar_t* enginePath=nullptr;
    explicit Backend(const wchar_t* path):enginePath(path){}
    bool Initialize(ID3D12Device* device,ID3D12Fence* fence){if(!enginePath)return local.Initialize(device,fence);
        auto module=LoadLibraryExW(enginePath,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module)return false;
        auto get=reinterpret_cast<fgflow033abi::GetApi>(GetProcAddress(module,"K033_GetImageFramegen"));if(!get)return false;
        Check(!get(fgflow033abi::Version+1),"built engine rejects incompatible flow ABI");api=get(fgflow033abi::Version);
        if(!api||api->version!=fgflow033abi::Version||api->size!=sizeof(*api))return false;
        Check(!api->createDx12(nullptr,nullptr),"built engine rejects missing device/fence");handle=api->createDx12(device,fence);
        if(!handle)return false;fgflow033abi::Record bad;bad.version++;fgflow033abi::Ticket ticket;
        Check(!api->recordDx12(handle,&bad,&ticket)&&ticket.index==3,"built engine rejects incompatible record ABI");return true;}
    Ticket Record(ID3D12GraphicsCommandList* list,ID3D12Resource* a,ID3D12Resource* b,ID3D12Resource* out,
                  float phase,UINT w,UINT h,UINT radius=4,bool reset=false,ID3D12Resource* mask=nullptr){
        if(!api)return local.Record(list,a,b,out,phase,w,h,radius,reset,mask);
        fgflow033abi::Record r;r.list=list;r.previous=a;r.current=b;r.output=out;r.protection=mask;
        r.phase=phase;r.flowWidth=w;r.flowHeight=h;r.radius=radius;r.reset=reset?1:0;fgflow033abi::Ticket t;
        if(!api->recordDx12(handle,&r,&t))return {};return {t.index,t.generation};}
    bool Submitted(Ticket t,uint64_t value){return api?api->submitted(handle,{t.index,0,t.generation},value)!=0:local.Submitted(t,value);}
    bool Discarded(Ticket t){return api?api->discarded(handle,{t.index,0,t.generation})!=0:local.Discarded(t);}
    uint64_t AllocationBatches(){if(!api)return local.AllocationBatches();fgflow033abi::Status s;if(handle)api->status(handle,&s);return s.allocationBatches;}
    uint64_t WorkingBytes(){if(!api)return local.WorkingBytes();fgflow033abi::Status s;if(handle)api->status(handle,&s);return s.workingBytes;}
    bool Retire(){if(!api)return local.Retire();if(!api->destroy(handle))return false;handle=nullptr;return true;}
};
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> debug;uint64_t serial=0;
    struct List {ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> cmd;};
    Gpu(){ComPtr<ID3D12Debug> layer;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer))))layer->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;OK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        OK(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));OK(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        device.As(&debug);D3D12_COMMAND_QUEUE_DESC q{};OK(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        OK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));puts("Adapter: WARP; no hardware adapter or game");}
    List begin(){List l;OK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&l.allocator)));
        OK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,l.allocator.Get(),nullptr,IID_PPV_ARGS(&l.cmd)));return l;}
    uint64_t submit(List& l){OK(l.cmd->Close());ID3D12CommandList* p=l.cmd.Get();queue->ExecuteCommandLists(1,&p);OK(queue->Signal(fence.Get(),++serial));return serial;}
    void wait(uint64_t value){HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);OK(fence->SetEventOnCompletion(value,e));
        auto result=WaitForSingleObject(e,30000);CloseHandle(e);if(result!=WAIT_OBJECT_0)throw std::runtime_error("WARP timeout");}
    static void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
        D3D12_RESOURCE_BARRIER d{};d.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;d.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&d);}
    ComPtr<ID3D12Resource> resource(D3D12_RESOURCE_DESC d,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=type;ComPtr<ID3D12Resource> r;OK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;}
    ComPtr<ID3D12Resource> texture(UINT w,UINT h,DXGI_FORMAT format,bool output=false){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;
        d.Flags=output?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        return resource(d,D3D12_HEAP_TYPE_DEFAULT,output?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_COPY_DEST);}
    ComPtr<ID3D12Resource> buffer(uint64_t bytes,bool upload){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return resource(d,upload?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_READBACK,upload?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST);}
    void upload(ID3D12Resource* texture,const void* pixels,UINT rowBytes){auto d=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};uint64_t bytes;
        device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto src=buffer(bytes,true);void* p;D3D12_RANGE none{};OK(src->Map(0,&none,&p));
        for(UINT y=0;y<d.Height;++y)memcpy(static_cast<char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,static_cast<const char*>(pixels)+size_t(y)*rowBytes,rowBytes);src->Unmap(0,nullptr);
        auto l=begin();D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=texture;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource=src.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;l.cmd->CopyTextureRegion(&a,0,0,0,&b,nullptr);
        barrier(l.cmd.Get(),texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);wait(submit(l));}
    std::vector<P> read(ID3D12Resource* texture){auto d=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};uint64_t bytes;
        device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto dst=buffer(bytes,false);auto l=begin();
        barrier(l.cmd.Get(),texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=dst.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=fp;
        b.pResource=texture;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l.cmd->CopyTextureRegion(&a,0,0,0,&b,nullptr);
        barrier(l.cmd.Get(),texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);wait(submit(l));
        void* p;D3D12_RANGE range{0,SIZE_T(bytes)};OK(dst->Map(0,&range,&p));std::vector<P> result(size_t(d.Width)*d.Height);
        for(UINT y=0;y<d.Height;++y)memcpy(result.data()+size_t(y)*d.Width,static_cast<char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,size_t(d.Width)*sizeof(P));
        D3D12_RANGE none{};dst->Unmap(0,&none);return result;}
    bool clean(){bool result=true;if(!debug){puts("D3D12 debug layer unavailable");return true;}for(uint64_t i=0;i<debug->GetNumStoredMessages();++i){SIZE_T bytes=0;
        debug->GetMessage(i,nullptr,&bytes);std::vector<char> data(bytes);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());debug->GetMessage(i,m,&bytes);
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING){printf("D3D12: %s\n",m->pDescription);result=false;}}debug->ClearStoredMessages();return result;}
};
#ifndef FRAMEGEN_DX12_TEST_NO_MAIN
int wmain(int argc,wchar_t** argv){setvbuf(stdout,nullptr,_IONBF,0);try{
    Gpu gpu;Backend backend(argc>1?argv[1]:nullptr);Check(backend.Initialize(gpu.device.Get(),gpu.fence.Get()),"DX12 production flow shaders compile");
    constexpr UINT w=64,h=48;auto pattern=[](int x,int y){unsigned k=unsigned(x+128)*1973u+unsigned(y+128)*9277u;k=(k^(k>>13))*1274126177u;
        float v=.15f+.6f*float((k>>12)&255)/255;return P{v,.1f+v*.7f,.2f+v*.4f,1};};
    std::vector<P> first(w*h),second(w*h),truth(w*h);std::vector<float> mask(w*h);
    ComPtr<ID3D12Resource> a,b;auto out=gpu.texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
    auto run=[&](float phase,bool reset=false,ID3D12Resource* protect=nullptr){auto l=gpu.begin();auto t=backend.Record(l.cmd.Get(),a.Get(),b.Get(),out.Get(),phase,w,h,4,reset,protect);
        if(!t)throw std::runtime_error("Record failed");auto value=gpu.submit(l);if(!backend.Submitted(t,value))throw std::runtime_error("Submit failed");gpu.wait(value);return gpu.read(out.Get());};
    for(auto shift:std::array<std::array<int,2>,3>{{{{4,0}},{{-4,2}},{{0,-4}}}}){
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){first[y*w+x]=pattern(x,y);second[y*w+x]=pattern(int(x)-shift[0],int(y)-shift[1]);truth[y*w+x]=pattern(int(x)-shift[0]/2,int(y)-shift[1]/2);}
        a=gpu.texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT);b=gpu.texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT);gpu.upload(a.Get(),first.data(),w*sizeof(P));gpu.upload(b.Get(),second.data(),w*sizeof(P));
        auto result=run(.5f);double error=0,baseline=0;bool finite=true;
        for(UINT y=10;y<h-10;++y)for(UINT x=10;x<w-10;++x){size_t i=y*w+x;error+=Error(result[i],truth[i]);baseline+=Error(second[i],truth[i]);finite&=std::isfinite(result[i].r)&&result[i].a==1;}
        printf("translation (%d,%d): error %.6f current-only %.6f\n",shift[0],shift[1],error,baseline);
        Check(finite&&error<baseline*.45,"DX12 midpoint follows actual image motion");}
    auto exact=[](const std::vector<P>& x,const std::vector<P>& y){return x.size()==y.size()&&memcmp(x.data(),y.data(),x.size()*sizeof(P))==0;};
    Check(exact(run(0.f),first)&&exact(run(1.f),second),"both real endpoints exact");
    for(UINT y=14;y<24;++y)for(UINT x=20;x<40;++x)mask[y*w+x]=1;
    auto m=gpu.texture(w,h,DXGI_FORMAT_R32_FLOAT);gpu.upload(m.Get(),mask.data(),w*sizeof(float));auto protectedFrame=run(.5f,false,m.Get());bool protectedOk=true;
    for(size_t i=0;i<mask.size();++i)if(mask[i])protectedOk&=memcmp(&protectedFrame[i],&second[i],sizeof(P))==0;
    Check(protectedOk,"explicit HUD protection exact");Check(exact(run(.5f,true),second),"scene reset exact current frame");
    auto before=backend.AllocationBatches();for(int i=0;i<32;++i)if(!exact(run(float(i%2)),i%2?second:first))throw std::runtime_error("switch result mismatch");
    Check(backend.AllocationBatches()==before&&backend.WorkingBytes()==uint64_t(w)*h*64,"32 switches keep bounded image storage");
    // Record three command lists before submitting any. Their descriptors must
    // remain distinct; incomplete/reserved work blocks both a fourth slot and resize.
    std::array<Gpu::List,3> lists;std::array<Ticket,3> tickets;std::array<ComPtr<ID3D12Resource>,3> outputs;
    for(unsigned i=0;i<3;++i){lists[i]=gpu.begin();outputs[i]=gpu.texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        tickets[i]=backend.Record(lists[i].cmd.Get(),a.Get(),b.Get(),outputs[i].Get(),float(i%2),w,h);}
    auto rejected=gpu.begin();Check(tickets[0]&&tickets[1]&&tickets[2]&&!backend.Record(rejected.cmd.Get(),a.Get(),b.Get(),out.Get(),1,w,h),"three pending lists bound descriptor storage");
    Check(!backend.Record(rejected.cmd.Get(),a.Get(),b.Get(),out.Get(),1,w/2,h/2)&&!backend.Retire(),"pending lists reject resize and premature release");
    for(unsigned i=0;i<3;++i)Check(backend.Submitted(tickets[i],gpu.submit(lists[i])),"submitted list receives real queue fence");gpu.wait(gpu.serial);
    Check(exact(gpu.read(outputs[0].Get()),first)&&exact(gpu.read(outputs[1].Get()),second)&&exact(gpu.read(outputs[2].Get()),first),"three pending lists retain their own bindings");
    auto t=backend.Record(rejected.cmd.Get(),a.Get(),b.Get(),out.Get(),1,w/2,h/2);Check(bool(t)&&backend.AllocationBatches()==before+1,"resize allowed after actual fence completion");
    rejected.cmd.Reset();rejected.allocator.Reset();Check(backend.Discarded(t)&&!backend.Discarded(t),"discarded list releases its ticket once");
    auto invalid=gpu.begin();Check(!backend.Record(invalid.cmd.Get(),out.Get(),b.Get(),out.Get(),.5f,w,h)&&
        !backend.Record(invalid.cmd.Get(),a.Get(),b.Get(),out.Get(),std::numeric_limits<float>::quiet_NaN(),w,h),"alias and NaN rejected without recording");
    Check(backend.Retire()&&backend.WorkingBytes()==0,"completed resources released");
    if(gpu.debug)Check(gpu.clean(),"D3D12 debug layer has no warnings or errors");else puts("SKIP D3D12 debug layer: not installed on this host");
    printf("%u DX12 checks passed; image generation only, no presentation claim.\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
#endif
