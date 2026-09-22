#define FRAMEGEN_DX12_TEST_NO_MAIN
#include "framegen_dx12_test.cpp"
#include "../src/universal_fg_abi.h"
#ifdef UNIVERSAL_FG_RESHADE_TEST
#include "universal_fg_ui_probe.h"
#endif
#ifdef UNIVERSAL_FG_HOOK_TEST
#include <timeapi.h>
#endif
int wmain(int argc,wchar_t** argv){setvbuf(stdout,nullptr,_IONBF,0);try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc!=3)throw std::runtime_error("engine and AMD FG library required");
#ifdef UNIVERSAL_FG_HOOK_TEST
    Check(timeGetTime()!=0,"normal winmm import initializes the engine");
#endif
    auto module=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);Check(module!=nullptr,"load built engine");
    Gpu gpu;
    auto get=reinterpret_cast<ufg033abi::GetApi>(GetProcAddress(module,"K033_GetUniversalFramegen"));Check(get&&!get(999),"universal FG ABI validation");auto api=get(ufg033abi::Version);Check(api&&api->size==sizeof(*api),"universal FG API available");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"033UniversalWARP";Check(RegisterClassW(&wc)!=0,"private window class");
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"033 WARP verification",WS_OVERLAPPEDWINDOW,0,0,96,64,nullptr,nullptr,wc.hInstance,nullptr);Check(hwnd&&!IsWindowVisible(hwnd),"test window remains hidden");
    ComPtr<IDXGIFactory4> factory;OK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT}){
        DXGI_SWAP_CHAIN_DESC1 d{};d.Width=64;d.Height=48;d.Format=format;d.SampleDesc.Count=1;d.BufferCount=2;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
#ifdef UNIVERSAL_FG_HOOK_TEST
        d.Width=128;d.Height=112; // Existing factory hooks reject tiny overlay surfaces.
#endif
#ifdef UNIVERSAL_FG_RESHADE_TEST
        d.Width=192;d.Height=144; // ReShade intentionally skips runtimes below 160x120.
#endif
        ComPtr<IDXGISwapChain4> sc;
#ifdef UNIVERSAL_FG_HOOK_TEST
        ComPtr<IDXGISwapChain1> created;OK(factory->CreateSwapChainForHwnd(gpu.queue.Get(),hwnd,&d,nullptr,nullptr,&created));OK(created.As(&sc));created.Reset();
#else
        ufg033abi::Create c;c.factory=factory.Get();c.queue=gpu.queue.Get();c.hwnd=hwnd;c.desc=&d;c.library=argv[2];c.output=reinterpret_cast<void**>(sc.GetAddressOf());OK(api->create(&c));
#endif
        ufg033abi::Status before;Check(api->read(&before)&&before.available==1,"owned swapchain registered");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;ComPtr<ID3D12DescriptorHeap> heap;OK(gpu.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
        auto draw=[&](bool enabled){api->enable(enabled?1:0);ComPtr<ID3D12Resource> target;OK(sc->GetBuffer(sc->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&target)));
            auto list=gpu.begin();Gpu::barrier(list.cmd.Get(),target.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
            auto rtv=heap->GetCPUDescriptorHandleForHeapStart();gpu.device->CreateRenderTargetView(target.Get(),nullptr,rtv);const float colour[]={.5f,.25f,.75f,1};list.cmd->ClearRenderTargetView(rtv,colour,0,nullptr);
            Gpu::barrier(list.cmd.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);gpu.wait(gpu.submit(list));target.Reset();
            auto hr=sc->Present(0,0);if(FAILED(hr)){printf("Present HR=%08X\n",unsigned(hr));throw std::runtime_error("Present failed");}};
        puts("present off");draw(false);
#ifdef UNIVERSAL_FG_FEEDER_EFFECTS_TEST
        auto evals=reinterpret_cast<unsigned(*)()>(GetProcAddress(GetModuleHandleW(L"dlss5-feed.addon64"),"K033_FeedMockEvaluations"));
        Check(evals!=nullptr,"effects test uses the NGX substitute, never a hardware neural model");
        auto initialEvals=evals();auto compileDeadline=GetTickCount64()+10000;while(evals()==initialEvals&&GetTickCount64()<compileDeadline)draw(false);
        Check(evals()>initialEvals,"real Feeder effect copies and flushes before mock evaluation");
#endif
#ifdef UNIVERSAL_FG_RESHADE_TEST
        Check(GetModuleHandleW(L"dlss5-033.addon64")!=nullptr,"actual ReShade loaded the unified registration adapter");
        Check(!GetModuleHandleW(L"renodx-dlss5.addon64"),"no independent RenoDX renderer loaded");
#ifdef UNIVERSAL_FG_FEEDER_TEST
        Check(GetModuleHandleW(L"dlss5-feed.addon64")!=nullptr,"existing RE4 Feeder co-loads with the unified runtime");
#endif
        auto readUi=reinterpret_cast<ReadUniversalUiProbe>(GetProcAddress(GetModuleHandleW(L"universal_fg_ui_probe.addon64"),"Read033UiProbe"));
        UniversalUiProbe ui;Check(readUi&&readUi(&ui)&&ui.live==1&&ui.presents>0,"one actual ReShade runtime draws the main swapchain");
        auto homeToggle=[&](bool enabled=false){
            readUi(&ui);auto opened=ui.opened,closed=ui.closed,overlays=ui.overlays;
            auto key=[&](bool down){PostMessageW(hwnd,down?WM_KEYDOWN:WM_KEYUP,VK_HOME,down?1:LPARAM(0xC0000001));
                MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}draw(enabled);};
            key(true);key(false);readUi(&ui);
            Check(ui.opened==opened+1&&ui.overlays>overlays,"Home sent only to private hidden window opens and draws ReShade overlay");
            key(true);key(false);readUi(&ui);
            Check(ui.closed==closed+1,"second Home closes ReShade overlay");
        };
        homeToggle();
#endif
        puts("present on");for(int i=0;i<6;++i)draw(true);
        ufg033abi::Status live;api->read(&live);printf("format=%u generated=%llu real composition=%llu generated composition=%llu skipped=%llu error=%u\n",unsigned(format),live.generated-before.generated,live.composedReal-before.composedReal,live.composedGenerated-before.composedGenerated,live.skipped-before.skipped,live.error);
        Check(live.generated>before.generated&&live.error==0,"real backbuffer captured and interpolated");
#ifdef UNIVERSAL_FG_RESHADE_TEST
        homeToggle(true);
#endif
        // Engines may reassert the same output colour space every Present.
        // This is metadata, not a scene cut; history must remain usable.
        const auto outputSpace=format==DXGI_FORMAT_R16G16B16A16_FLOAT?DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        api->read(&live);auto colourGenerated=live.generated;
        for(int i=0;i<6;++i){OK(sc->SetColorSpace1(outputSpace));draw(true);}
        api->read(&live);Check(live.generated>colourGenerated,"reasserting the same colour space preserves frame generation history");
        auto allocationBaseline=live.allocations;for(int i=0;i<16;++i)draw(true);api->read(&live);
        Check(live.allocations==allocationBaseline,"steady presentation reuses fixed resources");
        // Disabling waits for outstanding SDK composition before retiring our
        // resources; this also makes the composition counters deterministic.
        draw(false);api->read(&live);Check(live.composedGenerated>before.composedGenerated&&live.composedReal>before.composedReal,"both real and generated frames reach presentation composition");
        for(int round=0;round<4;++round){draw(true);draw(true);draw(false);}api->read(&live);Check(!live.error,"four enable/disable cycles recover");
#ifdef UNIVERSAL_FG_RESHADE_TEST
        OK(sc->ResizeBuffers(2,200,160,format,0));
#else
        OK(sc->ResizeBuffers(2,80,56,format,0));
#endif
        draw(true);draw(true);draw(true);draw(false);api->read(&live);Check(!live.error,"resize and re-enable recover");
#ifdef UNIVERSAL_FG_FEEDER_EFFECTS_TEST
        auto resizedEvals=evals();compileDeadline=GetTickCount64()+10000;while(evals()==resizedEvals&&GetTickCount64()<compileDeadline)draw(false);
        Check(evals()>resizedEvals,"Feeder survives destroyed runtime queue and resumes on the replacement");
#endif
#ifdef UNIVERSAL_FG_RESHADE_TEST
        readUi(&ui);Check(ui.live==1&&ui.created==ui.destroyed+1,"resize replaces runtime without leaking its old resources");homeToggle();
#endif
        sc.Reset();api->read(&live);Check(live.available==0&&live.workingBytes==0,"swapchain shutdown releases history resources");
#ifdef UNIVERSAL_FG_RESHADE_TEST
        readUi(&ui);Check(ui.live==0&&ui.created==ui.destroyed,"swapchain shutdown destroys the ReShade runtime");
#endif
    }
    DestroyWindow(hwnd);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    printf("%u checks passed. Hidden WARP presentation, not a game latency or monitor-FPS measurement.\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s Win32=%lu\n",e.what(),GetLastError());return 1;}}
