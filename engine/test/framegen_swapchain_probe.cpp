// Offline ABI probe: a private, never-shown WARP window. No game injection,
// screen capture or hardware rendering. Generates into the SDK-owned texture;
// never calls Present and does not claim displayed frame generation.
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <cstring>
#include "../src/framegen_flow_dx12.h"
#include "../third_party/OptiScaler033/external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h"
#include "../third_party/OptiScaler033/external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.h"
using Microsoft::WRL::ComPtr;
struct CallbackState {framegen033::FlowDx12 flow;framegen033::Ticket pending;ID3D12Resource* a=nullptr;ID3D12Resource* b=nullptr;unsigned calls=0;};
static ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* p,void* context){
    auto& s=*static_cast<CallbackState*>(context);++s.calls;
    auto* output=static_cast<ID3D12Resource*>(p->outputs[0].resource);
    printf("SDK callback: output=%p state=%u count=%u\n",output,p->outputs[0].state,p->numGeneratedFrames);
    if(!output||p->outputs[0].state!=FFX_API_RESOURCE_STATE_UNORDERED_ACCESS){p->numGeneratedFrames=0;return FFX_API_RETURN_ERROR_PARAMETER;}
    s.pending=s.flow.Record(static_cast<ID3D12GraphicsCommandList*>(p->commandList),s.a,s.b,output,.5f,64,48);
    printf("033 recording ticket=%u\n",s.pending.index);
    p->numGeneratedFrames=s.pending?1:0;return FFX_API_RETURN_OK;
}
static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER d{};d.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;d.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&d);}
int wmain(int argc,wchar_t** argv){
    setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc!=2)return 2;
    auto module=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!module){printf("LoadLibrary error=%lu\n",GetLastError());return 2;}
    auto create=reinterpret_cast<decltype(&ffxCreateContext)>(GetProcAddress(module,"ffxCreateContext"));
    auto destroy=reinterpret_cast<decltype(&ffxDestroyContext)>(GetProcAddress(module,"ffxDestroyContext"));
    if(!create||!destroy)return 2;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))||
       FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 2;
    D3D12_COMMAND_QUEUE_DESC q{};if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))))return 2;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"033PrivateSwapchainProbe";
    if(!RegisterClassW(&wc))return 2;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"033 private WARP probe",WS_OVERLAPPEDWINDOW,0,0,64,48,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 2;
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=64;desc.Height=48;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 sc{};sc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    sc.hwnd=window;sc.dxgiFactory=factory.Get();sc.gameQueue=queue.Get();sc.desc=&desc;
    IDXGISwapChain4* swapchain=nullptr;sc.swapchain=&swapchain;
    ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version{};version.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
    version.version=FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;sc.header.pNext=&version.header;
    ffxContext context=nullptr;auto result=create(&context,&sc.header,nullptr);
    printf("WARP swapchain create=%u, SDK header version=%u, hidden=%d\n",result,version.version,!IsWindowVisible(window));
    HRESULT query=E_NOINTERFACE;
    bool callbackOk=false;
    if(result==FFX_API_RETURN_OK&&swapchain){
        ComPtr<IFrameInterpolationSwapChainDX12> stable;query=swapchain->QueryInterface(IID_PPV_ARGS(&stable));
        printf("Stable callback-capable COM interface=%08X\n",unsigned(query));
        if(SUCCEEDED(query)){
            FfxFrameGenerationConfig config{};config.header.type=FFX_API_FRAME_GENERATION_CONFIG;
            config.swapChain=swapchain;config.frameGenerationEnabled=false;stable->setFrameGenerationConfig(&config);
            printf("Disabled configuration accepted; underlying swapchain=%s\n",stable->real()?"present":"missing");
            ComPtr<ID3D12Fence> fence;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
            if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))||
               FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
               FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))))return 2;
            UINT64 signal=0;auto wait=[&](){if(FAILED(queue->Signal(fence.Get(),++signal)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);
                auto hr=fence->SetEventOnCompletion(signal,e);bool ok=SUCCEEDED(hr)&&WaitForSingleObject(e,10000)==WAIT_OBJECT_0;CloseHandle(e);return ok;};
            auto make=[&](D3D12_RESOURCE_DESC d,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES state){D3D12_HEAP_PROPERTIES hp{};hp.Type=h;ComPtr<ID3D12Resource> r;
                if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))))return ComPtr<ID3D12Resource>{};return r;};
            D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=64;td.Height=48;
            td.DepthOrArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            auto input=make(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);if(!input)return 2;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;device->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);
            D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            auto upload=make(bd,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);if(!upload)return 2;
            void* data=nullptr;D3D12_RANGE none{};if(FAILED(upload->Map(0,&none,&data)))return 2;
            const float pixel[4]={.5f,.25f,.75f,1.f};for(UINT y=0;y<48;++y)for(UINT x=0;x<64;++x)memcpy(static_cast<char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch+x*16,pixel,16);upload->Unmap(0,nullptr);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=input.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            Barrier(list.Get(),input.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if(FAILED(list->Close()))return 2;ID3D12CommandList* command=list.Get();queue->ExecuteCommandLists(1,&command);if(!wait())return 2;
            CallbackState callback;callback.a=callback.b=input.Get();if(!callback.flow.Initialize(device.Get(),fence.Get()))return 2;
            puts("Input upload and flow initialization complete");
            config.frameGenerationCallback=Generate;config.frameGenerationCallbackContext=&callback;config.frameGenerationEnabled=true;
            // The public SDK uses the supplied game queue when async is false.
            // Signalling our fence after dispatch therefore covers this work.
            config.allowAsyncWorkloads=false;config.interpolationRect={0,0,64,48};stable->setFrameGenerationConfig(&config);
            printf("Callback configuration applied; buffers changed=%d\n",stable->verifyBackbufferDuplicateResources());
            FfxApiResource generated{},real{};stable->dispatchInterpolationCommands(&generated,&real);
            printf("Dispatch returned: generated=%p callback count=%u\n",generated.resource,callback.calls);
            if(!callback.pending||!generated.resource||!wait()||!callback.flow.Submitted(callback.pending,signal))return 2;
            auto* output=static_cast<ID3D12Resource*>(generated.resource);auto od=output->GetDesc();device->GetCopyableFootprints(&od,0,1,0,&fp,nullptr,nullptr,&bytes);
            bd.Width=bytes;auto readback=make(bd,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);if(!readback)return 2;
            if(FAILED(allocator->Reset())||FAILED(list->Reset(allocator.Get(),nullptr)))return 2;
            Barrier(list.Get(),output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            src={};src.pResource=output;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst={};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
            list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(list.Get(),output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if(FAILED(list->Close()))return 2;queue->ExecuteCommandLists(1,&command);if(!wait())return 2;
            D3D12_RANGE readRange{0,SIZE_T(bytes)};if(FAILED(readback->Map(0,&readRange,&data)))return 2;
            const uint16_t expected[4]={0x3800,0x3400,0x3a00,0x3c00};callbackOk=callback.calls==1;
            for(UINT y=0;y<48;++y)for(UINT x=0;x<64;++x)callbackOk&=memcmp(static_cast<char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch+x*8,expected,8)==0;
            readback->Unmap(0,&none);
            config.frameGenerationEnabled=false;config.frameGenerationCallback=nullptr;config.frameGenerationCallbackContext=nullptr;stable->setFrameGenerationConfig(&config);
            callbackOk&=callback.flow.Retire();printf("033 optical flow into SDK-owned interpolation output: %s (callbacks=%u)\n",callbackOk?"PASS":"FAIL",callback.calls);
        }
    }
    if(context)printf("Swapchain destroy=%u\n",destroy(&context,nullptr));
    DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    puts("One image generated through the SDK callback; zero Present calls. Display pacing and games remain untested.");
    return result==FFX_API_RETURN_OK&&SUCCEEDED(query)&&callbackOk?0:1;
}
