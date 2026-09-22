// Calls the actual built core and GPU upscaler. The callback DLL is a contract
// fixture, not the NVIDIA NR model, so this does not assert NR quality.
#ifdef K033_REAL_ADDON_SMOKE
#define DllMain K033_UnusedDllEntry
#include "../src/dlss5_033.cpp"
#undef DllMain
#define Log FixtureLog
#endif
#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#ifdef K033_REAL_ADDON_SMOKE
#undef Log
#endif
#include "ngx/nvsdk_ngx_params.h"
#include "render_core_abi.h"
#include <filesystem>
#include <mmsystem.h>
#ifdef K033_REAL_ADDON_SMOKE
#include "nr_two_pass_probe.h"
#endif
struct NVSDK_NGX_Handle { unsigned int Id; };
template<class T>static T Symbol(HMODULE module,const char* name){auto p=GetProcAddress(module,name);if(!p)throw std::runtime_error(name);return reinterpret_cast<T>(p);}
int wmain(int argc,wchar_t**argv){try{
    if(timeGetTime()==0)throw std::runtime_error("winmm forwarder clock unavailable");
    if(argc!=2)throw std::runtime_error("core DLL path required");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    Gpu g;auto addon=LoadLibraryW(L"dlss5-033.addon64");if(!addon)throw std::runtime_error("callback fixture load failed");
#ifdef K033_REAL_ADDON_SMOKE
    carrier::load_cfg();carrier::cfg.enabled=1;carrier::cfg.work=100;carrier::cfg.modelfull=1;
    carrier::cfg.replica=1;carrier::cfg.preset=3;carrier::cfg.style=2;staterestore::cfg_enabled=0;
    carrier::cfg.pre.enabled=1;carrier::cfg.pre.contrast=1.03f;
    carrier::cfg.pre.saturation=1.02f;carrier::cfg.pre.highlights=.04f;
#endif
    auto core=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!core){printf("load error %lu\n",GetLastError());throw std::runtime_error("core load failed");}
#ifdef K033_REAL_ADDON_SMOKE
    auto controls=Symbol<const nrcontrolsabi::Api*(__cdecl*)()>(core,"K033_GetControlEndpoint")();
    nrcontrolsabi::Snapshot beforeProfile;
    if(!controls || !controls->read(&beforeProfile))throw std::runtime_error("unified controls endpoint unavailable");
    if(controls->set(nrcontrolsabi::Preset,99) || controls->set(nrcontrolsabi::Skin,std::numeric_limits<float>::quiet_NaN()) || controls->set(nrcontrolsabi::Count,1))throw std::runtime_error("controls accepted invalid edits");
    if(!controls->set(nrcontrolsabi::Contrast,1.02f) || carrier::cfg.pre.contrast==1.02f)throw std::runtime_error("controls must enqueue without concurrent config mutation");
    controls->action(nrcontrolsabi::PortraitNatural);nrcontrols::Pump();
    nrcontrolsabi::Snapshot afterProfile;if(!controls->read(&afterProfile) || afterProfile.values[nrcontrolsabi::Skin]!=1.25f || afterProfile.values[nrcontrolsabi::Contrast]!=1.02f
        || afterProfile.values[nrcontrolsabi::Work]!=beforeProfile.values[nrcontrolsabi::Work] || afterProfile.mfgRequested!=beforeProfile.mfgRequested || afterProfile.pending)throw std::runtime_error("unified controls / portrait profile did not reach renderer");
    printf("unified control ABI: %u NR controls; queued edits, portrait profile, invalid values and unchanged work/MFG PASS\n",nrcontrolsabi::Count);
#endif
    auto get=Symbol<k033core::GetApi>(core,"K033_GetRenderCore");
    if(get(0)||!get(1))throw std::runtime_error("ABI version gate");
    const auto* api=get(1);if(api->size!=sizeof(*api))throw std::runtime_error("ABI size");
    auto calls=Symbol<unsigned(__cdecl*)()>(addon,"K033_FixtureCalls");
    auto bad=Symbol<unsigned(__cdecl*)()>(addon,"K033_FixtureBad");
    using Init=NVSDK_NGX_Result(__cdecl*)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
    using Allocate=NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Parameter**);
    using Create=NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
    using Eval=NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,NVSDK_NGX_Parameter*,void*);
    using Release=NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Handle*);
    using Destroy=NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Parameter*);
    auto init=Symbol<Init>(core,"NVSDK_NGX_D3D12_Init_Ext");auto allocate=Symbol<Allocate>(core,"NVSDK_NGX_D3D12_AllocateParameters");
    auto create=Symbol<Create>(core,"NVSDK_NGX_D3D12_CreateFeature");auto eval=Symbol<Eval>(core,"NVSDK_NGX_D3D12_EvaluateFeature");
    auto release=Symbol<Release>(core,"NVSDK_NGX_D3D12_ReleaseFeature");auto destroy=Symbol<Destroy>(core,"NVSDK_NGX_D3D12_DestroyParameters");
    auto ngx=[&](NVSDK_NGX_Result r,const char*n){printf("%s: %08X\n",n,unsigned(r));if(r!=NVSDK_NGX_Result_Success)throw std::runtime_error(n);};
    ngx(init(31337,L".",g.dev.Get(),NVSDK_NGX_Version_API,nullptr),"init");
    NVSDK_NGX_Parameter* p=nullptr;ngx(allocate(&p),"allocate");
    p->Set(NVSDK_NGX_Parameter_Width,64u);p->Set(NVSDK_NGX_Parameter_Height,48u);
    p->Set(NVSDK_NGX_Parameter_OutWidth,128u);p->Set(NVSDK_NGX_Parameter_OutHeight,96u);
    p->Set(NVSDK_NGX_Parameter_PerfQualityValue,static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));
    const unsigned flags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,flags);
    g.begin();NVSDK_NGX_Handle* handle=nullptr;ngx(create(g.cmd.Get(),NVSDK_NGX_Feature_SuperSampling,p,&handle),"create SR");g.wait();
    if(!handle)throw std::runtime_error("null successful handle");
    auto color=g.texture(64,48,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto depth=g.texture(64,48,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto motion=g.texture(64,48,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto output=g.texture(128,96,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto upload=[&](ID3D12Resource* t,unsigned channels,float base){auto d=t->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
        g.dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size);auto u=g.buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* map=nullptr;D3D12_RANGE empty{0,0};OK(u->Map(0,&empty,&map));std::memset(map,0,size);
        for(unsigned y=0;y<d.Height;++y){auto row=reinterpret_cast<float*>(static_cast<char*>(map)+fp.Footprint.RowPitch*y);
            for(unsigned x=0;x<d.Width;++x)for(unsigned c=0;c<channels;++c)row[x*channels+c]=channels==4?(c==3?1.f:base+.1f*(x%7)):base;}
        u->Unmap(0,nullptr);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=u.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        dst.pResource=t;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        scale::Barrier(g.cmd.Get(),t,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(u);};
    g.begin();upload(color.Get(),4,.1f);upload(depth.Get(),1,.5f);upload(motion.Get(),2,0);g.wait();
    p->Set(NVSDK_NGX_Parameter_Color,color.Get());p->Set(NVSDK_NGX_Parameter_Output,output.Get());p->Set(NVSDK_NGX_Parameter_Depth,depth.Get());p->Set(NVSDK_NGX_Parameter_MotionVectors,motion.Get());
    p->Set(NVSDK_NGX_Parameter_MV_Scale_X,64.f);p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,48.f);
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,64u);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,48u);
    p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.f);p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.f);
    p->Set("FrameTimeDeltaInMsec",16.67f);p->Set("CameraNear",.1f);p->Set("CameraFar",1000.f);p->Set("CameraFovAngleVertical",1.f);
    const bool retainedStress=GetEnvironmentVariableW(L"K033_STRESS_RETAINED_LISTS",nullptr,0)>0;
    const unsigned frameCount=retainedStress?160:16;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> retainedLists;
    std::vector<ComPtr<ID3D12CommandAllocator>> retainedAllocators;
    if(retainedStress)for(unsigned index=0;index<48;++index){
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        OK(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        OK(g.dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));OK(list->Close());
        retainedLists.push_back(list);retainedAllocators.push_back(allocator);
    }
    bool warmed=false;
    for(unsigned i=0;i<frameCount;++i){
        if(retainedStress){g.cmd=retainedLists[i%retainedLists.size()];g.alloc=retainedAllocators[i%retainedAllocators.size()];}
#ifdef K033_REAL_ADDON_SMOKE
        if(i==6)carrier::cfg.holdframe=1;
        if(i==8){
            if(!hostnr::s_hold_active || !hostnr::s_hold_depth || !hostnr::s_hold_motion)
                throw std::runtime_error("full frame freeze never became active");
            if(hostnr::s_hold_depth==depth.Get() || hostnr::s_hold_motion==motion.Get())
                throw std::runtime_error("freeze borrowed live guides");
            carrier::cfg.pre.exposure=.1f;
            matchedcapture::Request();
        }
        if(i==12)carrier::cfg.holdframe=0;
#endif
        p->Set(NVSDK_NGX_Parameter_Reset,i==0?1u:0u);g.begin();
        if(i==9){ // Later real input changes must not change the held resources.
            scale::Barrier(g.cmd.Get(),depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            scale::Barrier(g.cmd.Get(),motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            upload(depth.Get(),1,.75f);upload(motion.Get(),2,.01f);
        }
        k033core::Status prior;api->status(&prior);
        ngx(eval(g.cmd.Get(),handle,p,nullptr),"evaluate SR");g.wait();
#ifdef K033_REAL_ADDON_SMOKE
        k033core::Status after;api->status(&after);
        if(retainedStress && warmed && after.processed!=prior.processed+1)throw std::runtime_error("NR blink: a real frame bypassed after warmup with retained command lists");
        if(after.processed>prior.processed)warmed=true;
        hostnr::PumpBuild();
        resolveleases::Pump(g.queue.Get(),true);
        matchedcapture::Pump();
        if(i==12 && hostnr::s_hold_active)throw std::runtime_error("freeze was not released");
#endif
    }
    if(calls()!=frameCount||bad())throw std::runtime_error("not exactly one valid NR callback per rendered SR");
    // Verify actual rendered pixels, not only API return codes or counters.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outFp{};UINT64 outBytes=0;auto outDesc=output->GetDesc();
    g.dev->GetCopyableFootprints(&outDesc,0,1,0,&outFp,nullptr,nullptr,&outBytes);
    auto readback=g.buffer(outBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    g.begin();scale::Barrier(g.cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION srcOut{},dstOut{};srcOut.pResource=output.Get();srcOut.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstOut.pResource=readback.Get();dstOut.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dstOut.PlacedFootprint=outFp;
    g.cmd->CopyTextureRegion(&dstOut,0,0,0,&srcOut,nullptr);g.wait();
    void* pixels=nullptr;D3D12_RANGE readRange{0,SIZE_T(outBytes)},noRead{0,0};OK(readback->Map(0,&readRange,&pixels));
    float minimum=1e30f,maximum=-1e30f;
    for(unsigned y=0;y<96;++y){const auto row=reinterpret_cast<const Pixel*>(static_cast<const char*>(pixels)+outFp.Footprint.RowPitch*y);
        for(unsigned x=0;x<128;++x)for(float v:{row[x].r,row[x].g,row[x].b}){
            if(!std::isfinite(v)||std::abs(v)>100.f)throw std::runtime_error("invalid rendered pixel");
            minimum=std::min(minimum,v);maximum=std::max(maximum,v);}}
    readback->Unmap(0,&noRead);
    if(maximum-minimum<.01f)throw std::runtime_error("rendered output lost image contrast");
    printf("actual output finite, contrast retained: min %.6f max %.6f\n",minimum,maximum);
#ifdef K033_REAL_ADDON_SMOKE
    matchedcapture::Pump();
    const auto captureDeadline=GetTickCount64()+20000;
    while(matchedcapture::status.load()==matchedcapture::Writing && GetTickCount64()<captureDeadline)Sleep(10);
    if(matchedcapture::status.load()!=matchedcapture::Complete)throw std::runtime_error("matched capture did not complete after proven GPU retirement");
    if(retainedStress)printf("48 retained command lists, %u real frames: NO NR bypass after warmup PASS\n",frameCount);
    printf("full freeze, live input mutation, pregrade change, resume and matched raw capture PASS\n");
    controls->set(nrcontrolsabi::Enabled,0);nrcontrols::Pump();
    if(hostnr::s_history.valid)throw std::runtime_error("NR disable retained temporal history");
    controls->set(nrcontrolsabi::Enabled,1);nrcontrols::Pump();
    if(hostnr::s_history.valid || !carrier::cfg.enabled)throw std::runtime_error("NR resume failed to invalidate skipped temporal history");
    printf("unified NR toggle invalidates skipped-frame history PASS\n");
#endif
    NVSDK_NGX_Handle unknown{};unknown.Id=0x7fff1234;const auto before=calls();g.begin();eval(g.cmd.Get(),&unknown,p,nullptr);g.wait();
    if(calls()!=before)throw std::runtime_error("unknown feature reached NR");
    ngx(release(handle),"release");ngx(destroy(p),"destroy");
    if(!api->claim(k033core::Host033)||api->claim(k033core::NativeVulkan))throw std::runtime_error("owner arbitration");
    OK(g.dev->GetDeviceRemovedReason());k033core::Status status;api->status(&status);
#ifdef K033_REAL_ADDON_SMOKE
    if(status.processed==0 || inject::s_frames==0){printf("NR note: %s\n",hostnr::note());throw std::runtime_error("actual NR never processed a frame");}
    printf("ACTUAL NR model processed %llu frames through production 033\n",(unsigned long long)status.processed);
#endif
    printf("production core backend: %u real SR frames/offers, %llu handled NR frames, bad=%u, device healthy PASS\n",calls(),(unsigned long long)status.processed,bad());
#ifdef K033_REAL_ADDON_SMOKE
    if(GetEnvironmentVariableW(L"K033_TWO_PASS_PROBE",nullptr,0))TwoPassProbe(g,depth.Get(),motion.Get());
#endif
    fflush(stdout);ExitProcess(0); // A proxy's detours remain process-lifetime; never FreeLibrary it mid-session.
}catch(const std::exception&e){printf("ERROR %s\n",e.what());fflush(stdout);ExitProcess(1);}}
