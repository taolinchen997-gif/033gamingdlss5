// Executes the production encode/resolve shaders, with an identity model, on WARP.
// Tests the colour bridge only: this is not an NVIDIA NR model quality test.
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <string>
using Microsoft::WRL::ComPtr;
static void Log(const char *, ...) {}
#include "../src/scale.h"
static void OK(HRESULT hr) { if (FAILED(hr)) { std::printf("HRESULT %08X\n", unsigned(hr)); throw std::runtime_error("D3D12 failure"); } }
struct Pixel { float r,g,b,a; };
static float pq(double nits) {
    const double p = std::pow(nits / 10000.0, 2610.0 / 16384.0);
    return float(std::pow((3424.0/4096.0 + 2413.0/128.0*p)/(1 + 2392.0/128.0*p), 2523.0/32.0));
}
struct Gpu {
    ComPtr<ID3D12Device> dev;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    UINT64 serial=0;
    HANDLE event = CreateEventW(nullptr,FALSE,FALSE,nullptr);
    scale::Blitter b;
    Gpu() {
        if(GetEnvironmentVariableW(L"K033_TEST_D3D_DEBUG",nullptr,0)){
            ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
        }
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> adapter;
        OK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        wchar_t hardware[8]={};GetEnvironmentVariableW(L"K033_TEST_HARDWARE",hardware,8);
        if(hardware[0]==L'1'){
            ComPtr<IDXGIFactory6> modern;OK(factory.As(&modern));
            OK(modern->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
            DXGI_ADAPTER_DESC d={};OK(adapter->GetDesc(&d));
            std::printf("Hardware adapter vendor=%04X device=%04X\n",d.VendorId,d.DeviceId);
        }else{OK(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));std::puts("Adapter: WARP");}
        OK(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
        D3D12_COMMAND_QUEUE_DESC q={}; OK(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        OK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
        OK(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cmd)));
        OK(cmd->Close()); OK(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        if (!scale::Create(b,dev.Get())) throw std::runtime_error(b.error);
    }
    ~Gpu(){ scale::Destroy(b); CloseHandle(event); }
    void begin() { OK(alloc->Reset()); OK(cmd->Reset(alloc.Get(),nullptr)); }
    void wait() {
        if(GetEnvironmentVariableW(L"K033_TEST_D3D_DEBUG",nullptr,0)){
            ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(dev.As(&info))){
                for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<char> data(bytes);
                    auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,message,&bytes);fprintf(stderr,"D3D12: %s\n",message->pDescription);}
                info->ClearStoredMessages();}
        }
        OK(cmd->Close()); ID3D12CommandList *lists[]={cmd.Get()}; queue->ExecuteCommandLists(1,lists);
        OK(queue->Signal(fence.Get(),++serial)); OK(fence->SetEventOnCompletion(serial,event));
        if (WaitForSingleObject(event,30000)!=WAIT_OBJECT_0) throw std::runtime_error("GPU timeout");
    }
    ComPtr<ID3D12Resource> texture(UINT w,UINT h,DXGI_FORMAT fmt,D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=w;d.Height=h;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=fmt;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> t; OK(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&t))); return t;
    }
    ComPtr<ID3D12Resource> buffer(UINT64 size,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC d={};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;
        d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES hp={};hp.Type=heap;
        ComPtr<ID3D12Resource> t; OK(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&t)));return t;
    }
    std::vector<Pixel> run(const std::vector<Pixel>&input,UINT w,UINT h,int mode,int curve,int compose,
                          int apply,float split,DXGI_FORMAT modelFmt,bool changed=false,
                          const std::vector<Pixel>* suppliedModel=nullptr,
                          float strength=1.f,float guard=2.f,float colour=1.f,
                          const scale::MotionSharpen* motion=nullptr,const pregrade::Settings* grade=nullptr,float skinProtect=0.f,bool inputOnly=false) {
        const DXGI_FORMAT fmt=DXGI_FORMAT_R32G32B32A32_FLOAT;
        auto full=texture(w,h,fmt,D3D12_RESOURCE_STATE_COPY_DEST);
        auto encoded=texture(w,h,modelFmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto output=texture(w,h,fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={};UINT rows=0;UINT64 rowBytes=0,total=0;
        auto desc=full->GetDesc();dev->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&rowBytes,&total);
        auto upload=buffer(total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        auto readback=buffer(total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        void *data=nullptr; D3D12_RANGE noRead={0,0}; OK(upload->Map(0,&noRead,&data));
        for(UINT y=0;y<h;++y) std::memcpy(static_cast<char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,input.data()+size_t(y)*w,w*sizeof(Pixel));
        upload->Unmap(0,nullptr);
        ComPtr<ID3D12Resource> modelUpload,modelSource,modelOutput;
        if(suppliedModel) {
            if(suppliedModel->size()!=input.size()) throw std::runtime_error("Model size mismatch");
            modelUpload=buffer(total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
            modelSource=texture(w,h,fmt,D3D12_RESOURCE_STATE_COPY_DEST);
            modelOutput=texture(w,h,modelFmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            OK(modelUpload->Map(0,&noRead,&data));
            for(UINT y=0;y<h;++y) std::memcpy(static_cast<char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,suppliedModel->data()+size_t(y)*w,w*sizeof(Pixel));
            modelUpload->Unmap(0,nullptr);
        }
        begin();
        D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=full.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        scale::Barrier(cmd.Get(),full.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        scale::Dispatch(b,dev.Get(),cmd.Get(),full.Get(),fmt,encoded.Get(),modelFmt,w,h,0,3.16f,mode,nullptr,curve,203.f,grade);
        scale::UavBarrier(cmd.Get(),encoded.Get());
        scale::Barrier(cmd.Get(),encoded.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(suppliedModel) {
            dst.pResource=modelSource.Get();src.pResource=modelUpload.Get();
            cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            scale::Barrier(cmd.Get(),modelSource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            // The two dispatches are pending on one list: keep distinct descriptor slots.
            scale::Dispatch(b,dev.Get(),cmd.Get(),modelSource.Get(),fmt,modelOutput.Get(),modelFmt,w,h,1,1.f,0,nullptr,curve,203.f);
            scale::UavBarrier(cmd.Get(),modelOutput.Get());
            scale::Barrier(cmd.Get(),modelOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        scale::DispatchResolve(b,dev.Get(),cmd.Get(),full.Get(),encoded.Get(),suppliedModel?modelOutput.Get():changed?full.Get():encoded.Get(),fmt,output.Get(),w,h,
                              strength,3.16f,guard,mode==0?1:mode>=2?mode:0,colour,0.f,nullptr,compose,0,curve,apply,split,203.f,motion,grade,skinProtect,0.f,UINT(4));
        scale::UavBarrier(cmd.Get(),output.Get());
        scale::Barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        if(inputOnly) {if(modelFmt!=fmt)throw std::runtime_error("input readback requires FP32");scale::Barrier(cmd.Get(),encoded.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);}
        src={};src.pResource=inputOnly?encoded.Get():output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst={};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);wait();
        D3D12_RANGE readRange={0,SIZE_T(total)};OK(readback->Map(0,&readRange,&data));std::vector<Pixel> result(w*h);
        for(UINT y=0;y<h;++y) std::memcpy(result.data()+size_t(y)*w,static_cast<char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,w*sizeof(Pixel));
        readback->Unmap(0,&noRead);return result;
    }
};
#ifndef NR_COLOR_TEST_NO_MAIN
int main() { try {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    Gpu gpu; int failures=0,checks=0;
    constexpr UINT w=19,h=13;
    for (int mode=0;mode<4;++mode) for(int half=0;half<2;++half) for(int curve=1;curve<=2;++curve) {
        std::vector<Pixel> input(w*h);
        const float levels[]={0.f,0.001f,0.1f,1.f,4.f,16.f};
        for(size_t i=0;i<input.size();++i){
            float a=levels[i%6],b=levels[(i/6)%6],c=levels[(i/36)%6];
            if(mode==0) {a/=16;b/=16;c/=16;}
            if(mode==2) {a=pq(a*62.5);b=pq(b*62.5);c=pq(c*62.5);}
            input[i]={a,b,c,float(i%11)/10.f};
        }
        auto fmt=half?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
        for(int apply=0;apply<2;++apply) {
            auto result=gpu.run(input,w,h,mode,curve,1,apply,0.f,fmt);
            double worst=0,alphaError=0; bool finite=true;
            for(size_t i=0;i<input.size();++i){
                const float *a=&input[i].r,*b=&result[i].r;
                for(int c=0;c<3;++c){finite &= std::isfinite(b[c]);worst=std::max(worst,double(std::abs(a[c]-b[c]))/(mode==1?std::max(1.f,std::abs(a[c])):1.0));}
                alphaError=std::max(alphaError,double(std::abs(a[3]-b[3])));
            }
            // Identity must be exact in source colour space; loose tolerances would
            // hide the very FP16/inverse-curve drift this regression test targets.
            const double tolerance=0.0;
            bool pass=finite && worst<=tolerance && alphaError==0;
            std::printf("mode=%d FP%d curve=%d apply=%d max_error=%.7f alpha=%.7f %s\n",mode,half?16:32,curve,apply,worst,alphaError,pass?"PASS":"FAIL");
            ++checks;if(!pass)++failures;
        }
    }
    std::vector<Pixel> editInput(w*h,Pixel{0.2f,0.3f,0.4f,0.7f});
    auto edited=gpu.run(editInput,w,h,1,1,1,1,0.f,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    bool editApplied=std::isfinite(edited[0].r) && std::abs(edited[0].r-editInput[0].r)>0.01f;
    ++checks;if(!editApplied)++failures;
    std::printf("model edit reaches result: %s\n",editApplied?"PASS":"FAIL");
    auto split=gpu.run(editInput,w,h,1,1,1,1,0.5f,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    bool splitOk=split[0].r==editInput[0].r && std::abs(split[w-1].r-editInput[w-1].r)>0.01f;
    ++checks;if(!splitOk)++failures;
    std::printf("split original/processed: %s\n",splitOk?"PASS":"FAIL");
    std::printf("production colour shaders: %d cases, %d failures\n",checks,failures);
    return failures?1:0;
} catch(const std::exception &e){std::printf("FAIL: %s\n",e.what());return 2;} }
#endif
