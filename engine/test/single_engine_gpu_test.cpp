// Real ReShade registration + one 033 engine + real GPU SR/NR. Hidden test window;
// no game process, no desktop input and no large-resolution stress.
#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#include "ngx/nvsdk_ngx_params.h"
#include "render_core_abi.h"
#include "nr_controls_abi.h"
#include "nr_lifetime_diagnostics.h"
#include "embedded_ui_abi.h"
#include <mmsystem.h>
#include <set>
#include <fstream>
struct NVSDK_NGX_Handle {unsigned int Id;};
template<class T>static T Symbol(HMODULE m,const char* n){auto p=GetProcAddress(m,n);if(!p)throw std::runtime_error(n);return reinterpret_cast<T>(p);}
static unsigned calls=0;
static int children=0,tables=0,disabled=0,ids=0,indents=0,fonts=0,colors=0,trees=0;
static std::set<std::string> sections;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int __cdecl Host(ui033abi::Call* c){
 using namespace ui033abi;
 Require(c&&c->size==sizeof(*c)&&c->op<Count,"UI protocol");++calls;
 switch(c->op){
 case ViewMetrics:c->f[0]=1280;c->f[1]=720;c->f[2]=1.f/60;break;
 case GetFontSize:case GetTextLineHeight:c->f[0]=16;break;
 case GetWindowWidth:c->f[0]=600;break;
 case GetContentRegionAvail:case GetWindowSize:c->f[0]=600;c->f[1]=720;break;
 case TextSize:c->f[0]=80;c->f[1]=16;break;
 case Combo:if(c->text&&std::string(c->text)=="##033_category"){static int category=0;*static_cast<int*>(c->data)=(category++)%7;c->result=1;}break;
 case CollapsingHeader:sections.insert(c->text);c->result=1;break;
 case TreeNode:++trees;c->result=1;break;
 case TreePop:Require(--trees>=0,"tree underflow");break;
 case BeginChild:++children;c->result=1;break;
 case EndChild:Require(--children>=0,"child underflow");break;
 case BeginTable:++tables;c->result=1;break;
 case EndTable:Require(--tables>=0,"table underflow");break;
 case BeginDisabled:++disabled;break;
 case EndDisabled:Require(--disabled>=0,"disabled underflow");break;
 case PushID:case PushIntID:++ids;break;
 case PopID:Require(--ids>=0,"ID underflow");break;
 case Indent:++indents;break;
 case Unindent:Require(--indents>=0,"indent underflow");break;
 case PushColor:++colors;break;
 case PopColor:colors-=c->i[0];Require(colors>=0,"colour underflow");break;
 case PushFontPx:++fonts;break;
 case PopFontPx:Require(--fonts>=0,"font underflow");break;
 default:break; // No simulated setting edits or external actions.
 }return 1;
}

int wmain(int argc,wchar_t** argv){try{
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(argc!=2)throw std::runtime_error("engine path required");
 if(!timeGetTime())throw std::runtime_error("proxy clock");
 auto core=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(!core)throw std::runtime_error("load engine");
 wchar_t legacy[8]{};
 if(GetEnvironmentVariableW(L"K033_TEST_LEGACY_CRT",legacy,8)&&legacy[0]==L'1'){
  wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring dir(exe);dir.resize(dir.find_last_of(L'\\')+1);
  for(auto name:{L"msvcp140.dll",L"vcruntime140.dll",L"vcruntime140_1.dll"}){
   wchar_t path[MAX_PATH]{};auto mod=GetModuleHandleW(name);Require(mod!=nullptr,"legacy runtime missing");
   GetModuleFileNameW(mod,path,MAX_PATH);Require(!_wcsicmp(path,(dir+name).c_str()),"system runtime masked the legacy regression");
  }
  printf("LEGACY CRT: game-local MSVCP/VCRUNTIME copies are actually loaded PASS\n");
 }
 wchar_t benchValue[8]{};const bool benchmark=GetEnvironmentVariableW(L"K033_TEST_BENCHMARK",benchValue,8)&&benchValue[0]==L'1';
 wchar_t wideValue[8]{};const bool wide=GetEnvironmentVariableW(L"K033_TEST_WIDE",wideValue,8)&&wideValue[0]==L'1';
 wchar_t edgeValue[8]{};const bool edgeAudit=GetEnvironmentVariableW(L"K033_TEST_EDGE_AUDIT",edgeValue,8)&&edgeValue[0]==L'1';
 wchar_t auditValue[8]{};const bool audit=GetEnvironmentVariableW(L"K033_TEST_CONTROL_AUDIT",auditValue,8)&&auditValue[0]==L'1';
 const UINT iw=wide?2560:benchmark?640:64,ih=wide?1080:benchmark?360:48,ow=2*iw,oh=2*ih;
 Gpu g;
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"033IndependentTest";
 RegisterClassW(&wc);
 auto window=CreateWindowExW(0,wc.lpszClassName,L"033 independent validation",WS_POPUP,0,0,960,720,nullptr,nullptr,wc.hInstance,nullptr);
 if(!window)throw std::runtime_error("hidden window creation");
 ComPtr<IDXGIFactory2> factory;OK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=960;desc.Height=720;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
 desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 ComPtr<IDXGISwapChain1> swap;OK(factory->CreateSwapChainForHwnd(g.queue.Get(),window,&desc,nullptr,nullptr,&swap));
 auto present=[&](){OK(swap->Present(0,0));MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(3);};
 present();
 auto adapter=GetModuleHandleW(L"dlss5-033.addon64");if(!adapter)throw std::runtime_error("ReShade did not load registration adapter");
 if(GetProcAddress(adapter,"K033_AfterUpscale"))throw std::runtime_error("adapter contains a second renderer");
 if(GetModuleHandleW(L"renodx-dlss5.addon64")||GetModuleHandleW(L"OptiScaler.dll"))throw std::runtime_error("standalone engine loaded");
 auto get=Symbol<k033core::GetApi>(core,"K033_GetRenderCore");auto api=get(k033core::Version);if(!api||!api->claim(k033core::Host033))throw std::runtime_error("engine owner");
 auto controls=Symbol<nrcontrolsabi::GetApi>(core,"K033_GetNrControls")(nrcontrolsabi::Version);if(!controls)throw std::runtime_error("NR settings");
 controls->set(nrcontrolsabi::Enabled,1);controls->set(nrcontrolsabi::Work,100);controls->set(nrcontrolsabi::Grade,1);controls->set(nrcontrolsabi::Exposure,0.f);
 // Legacy zero-strength/zero-colour settings must not erase the direct model
 // output in the new before-NR workflow.
 controls->set(nrcontrolsabi::Blend,0);controls->set(nrcontrolsabi::Colour,0);controls->set(nrcontrolsabi::Mas,1);present();
 nrcontrolsabi::Snapshot requested;if(!controls->read(&requested)||requested.values[nrcontrolsabi::Enabled]!=1||requested.values[nrcontrolsabi::Exposure] != 0.f)throw std::runtime_error("ReShade did not pump engine settings");
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
    p->Set(NVSDK_NGX_Parameter_Width,iw);p->Set(NVSDK_NGX_Parameter_Height,ih);
    p->Set(NVSDK_NGX_Parameter_OutWidth,ow);p->Set(NVSDK_NGX_Parameter_OutHeight,oh);
    p->Set(NVSDK_NGX_Parameter_PerfQualityValue,static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));
    const unsigned flags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,flags);
    g.begin();NVSDK_NGX_Handle* handle=nullptr;ngx(create(g.cmd.Get(),NVSDK_NGX_Feature_SuperSampling,p,&handle),"create SR");g.wait();
    if(!handle)throw std::runtime_error("null successful handle");
    auto color=g.texture(iw,ih,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto depth=g.texture(iw,ih,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto motion=g.texture(iw,ih,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto output=g.texture(ow,oh,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto upload=[&](ID3D12Resource* t,unsigned channels,float base){auto d=t->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
        g.dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size);auto u=g.buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* map=nullptr;D3D12_RANGE empty{0,0};OK(u->Map(0,&empty,&map));std::memset(map,0,size);
        for(unsigned y=0;y<d.Height;++y){auto row=reinterpret_cast<float*>(static_cast<char*>(map)+fp.Footprint.RowPitch*y);
            for(unsigned x=0;x<d.Width;++x)for(unsigned c=0;c<channels;++c)row[x*channels+c]=channels==4?(c==3?1.f:(edgeAudit?((x>d.Width/3&&x<d.Width*2/3&&y>d.Height/4&&y<d.Height*3/4)?2.f:.03f):(audit?(.04f+.7f*float((x*(c+1)+y*(3-c))%61)/60.f):base+.1f*(x%7)))):base;}
        u->Unmap(0,nullptr);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=u.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        dst.pResource=t;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        scale::Barrier(g.cmd.Get(),t,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(u);};
    g.begin();upload(color.Get(),4,.1f);upload(depth.Get(),1,.5f);upload(motion.Get(),2,0);g.wait();
    p->Set(NVSDK_NGX_Parameter_Color,color.Get());p->Set(NVSDK_NGX_Parameter_Output,output.Get());p->Set(NVSDK_NGX_Parameter_Depth,depth.Get());p->Set(NVSDK_NGX_Parameter_MotionVectors,motion.Get());
    p->Set(NVSDK_NGX_Parameter_MV_Scale_X,float(iw));p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,float(ih));
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,iw);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,ih);
    p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.f);p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.f);
    p->Set("FrameTimeDeltaInMsec",16.67f);p->Set("CameraNear",.1f);p->Set("CameraFar",1000.f);p->Set("CameraFovAngleVertical",1.f);


 if(audit){
    using namespace nrcontrolsabi;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;auto d=output->GetDesc();
    g.dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto rb=g.buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    auto capture=[&](){
        g.begin();scale::Barrier(g.cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        scale::Barrier(g.cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);g.wait();
        void* map=nullptr;D3D12_RANGE rr{0,SIZE_T(bytes)};OK(rb->Map(0,&rr,&map));std::vector<Pixel> result(ow*oh);
        for(unsigned y=0;y<oh;++y)std::memcpy(result.data()+size_t(y)*ow,static_cast<char*>(map)+fp.Footprint.RowPitch*y,ow*sizeof(Pixel));
        D3D12_RANGE empty{0,0};rb->Unmap(0,&empty);return result;
    };
    auto difference=[&](const std::vector<Pixel>& a,const std::vector<Pixel>& b){double sum=0;float mx=0;
        for(size_t i=0;i<a.size();++i){const float av[]={a[i].r,a[i].g,a[i].b},bv[]={b[i].r,b[i].g,b[i].b};
          for(unsigned c=0;c<3;++c){Require(std::isfinite(av[c])&&std::isfinite(bv[c]),"audit nonfinite");float v=std::abs(av[c]-bv[c]);sum+=v;mx=std::max(mx,v);}}
        return std::pair<double,float>(sum/(3*a.size()),mx);
    };
    struct Case{Id id;float value;};
    wchar_t auditStyleText[8]{};GetEnvironmentVariableW(L"K033_TEST_AUDIT_STYLE",auditStyleText,8);
    const float auditStyle=float(_wtoi(auditStyleText));
    const Case baseline[]={{Preset,0},{Style,auditStyle},{Intensity,1},{Structure,1},{GlobalTone,1},{LocalTone,1},{Skin,1},{AutoMask,1},{UiCorrect,1},{Passes,1},{Grade,0},{ApplyModel,1},{Hold,edgeAudit?0.f:1.f}};
    const Case cases[]={{Preset,0},{Preset,1},{Preset,2},{Preset,3},{Style,1},{Style,2},{Style,3},{Intensity,0},{Intensity,2},{Structure,0},{Structure,2},{GlobalTone,0},{GlobalTone,2},{LocalTone,0},{LocalTone,2},{Skin,0},{Skin,2},{AutoMask,0},{UiCorrect,0}};
    std::vector<Pixel> reference;unsigned done=0;
    const Case stackCases[]={{Passes,1},{Passes,2},{Passes,3},{Passes,4},{Style,2},{Style,3}};
    std::vector<Case> selectedCases;
    if(edgeAudit)selectedCases.assign(std::begin(stackCases),std::end(stackCases));else selectedCases.assign(std::begin(cases),std::end(cases));
    for(const auto& item:selectedCases){
        for(const auto& v:baseline)Require(controls->set(v.id,v.value),"audit baseline set");
        if(edgeAudit&&item.id==Style)Require(controls->set(Passes,4),"four-pass style comparison");
        Require(controls->set(item.id,item.value),"audit variant set");present();
        for(unsigned frame=0;frame<70;++frame){p->Set(NVSDK_NGX_Parameter_Reset,1u);g.begin();
            Require(eval(g.cmd.Get(),handle,p,nullptr)==NVSDK_NGX_Result_Success,"audit evaluation");g.wait();present();}
        Snapshot cfg;Require(controls->read(&cfg)&&!cfg.tuningPending&&!cfg.pending,"audit model did not finish rebuild");
        auto a=capture();if(reference.empty())reference=a;
        p->Set(NVSDK_NGX_Parameter_Reset,1u);g.begin();Require(eval(g.cmd.Get(),handle,p,nullptr)==NVSDK_NGX_Result_Success,"audit repeated frame");g.wait();present();auto b=capture();
        auto delta=difference(reference,a),repeat=difference(a,b);
        if(edgeAudit){double red=0;float peak=0;unsigned colored=0;
            for(auto pixel:a){float excess=pixel.r-.5f*(pixel.g+pixel.b);red+=excess;peak=std::max(peak,excess);if(excess>.05f)++colored;}
            printf("EDGE_PROBE passes=%.0f style=%.0f red_mean=%.8f red_peak=%.8f red_pixels=%u/%zu\n",cfg.values[Passes],cfg.values[Style],red/a.size(),peak,colored,a.size());
        }
        printf("CONTROL_AUDIT id=%u label=%s value=%.3f mean=%.9f max=%.9f repeat_mean=%.9f repeat_max=%.9f built=%u parameter_back=%u\n",unsigned(item.id),definitions[item.id].label,item.value,delta.first,delta.second,repeat.first,repeat.second,cfg.presetBuilt,cfg.presetReadback);fflush(stdout);++done;
    }
    printf("CONTROL_AUDIT PASS: %u variants, fixed RGB fixture, baseline style=%.0f; %s; completed model builds\n",done,auditStyle,edgeAudit?"SR reset requested (NR uses its own history policy)":"frozen colour and guides; NR history reset each frame");fflush(stdout);ExitProcess(0);
 }
 wchar_t reconfigValue[8]{};const bool reconfigure=GetEnvironmentVariableW(L"K033_TEST_RECONFIGURE",reconfigValue,8)&&reconfigValue[0]==L'1';
 wchar_t stressValue[8]{};const bool stress=GetEnvironmentVariableW(L"K033_TEST_STRESS",stressValue,8)&&stressValue[0]==L'1';
 auto lifetime=Symbol<nrlifetime::Read>(core,"K033_GetNrLifetime");
 uint64_t memoryFirst=0,memoryLast=0;unsigned cycles=0,heldPendingFrames=0;double maximumBuild=0;
 ComPtr<ID3D12CommandAllocator> heldAlloc;ComPtr<ID3D12GraphicsCommandList> heldList;
 uint64_t previousProcessed=0;unsigned continuousFrames=0;
 double gpuSum=0;unsigned gpuMeasurements=0;
 wchar_t styleValue[8]{};const bool styleSwitch=GetEnvironmentVariableW(L"K033_TEST_STYLE_SWITCH",styleValue,8)&&styleValue[0]==L'1';
 uint64_t styleCreated=0;
 const unsigned frameCount=styleSwitch?240:stress?1840:reconfigure?100:benchmark?180:32;bool sawTwo=false,sawHigh=false,sawRestored=false;
 for(unsigned i=0;i<frameCount;++i){
  if(styleSwitch && i%30==0){Require(controls->set(nrcontrolsabi::Grade,1)&&controls->set(nrcontrolsabi::PreStyle,float((i/30)%4))&&controls->set(nrcontrolsabi::PreStyleStrength,(i/30)%2?.7f:1.f),"input style switch");}
  if(stress && i%40==0){
    unsigned cycle=i/40;
    const unsigned counts[]={1,4,2,3,1};
    const unsigned count=cycle>=45?1:counts[cycle%5];
    Require(controls->set(nrcontrolsabi::Passes,float(count)),"stress pass count");
    // Alternate extra-pass scales, with repeated full appearance rebuilds.
    Require(controls->set(nrcontrolsabi::PassWork,cycle<15?100.f:(cycle%2?75.f:100.f)),"stress extra scale");
    if(cycle==10 || cycle>=15)Require(controls->set(nrcontrolsabi::Preset,float(cycle%2)),"stress preset");
  }
  if(stress && i==360){
    heldAlloc=g.alloc;heldList=g.cmd;g.alloc.Reset();g.cmd.Reset();
    OK(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.alloc)));
    OK(g.dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.alloc.Get(),nullptr,IID_PPV_ARGS(&g.cmd)));OK(g.cmd->Close());
  }
  if(stress && i==480){OK(heldList->Reset(heldAlloc.Get(),nullptr));OK(heldList->Close());heldList.Reset();heldAlloc.Reset();}
  if(reconfigure&&i==10){Require(controls->set(nrcontrolsabi::Passes,2)&&controls->set(nrcontrolsabi::Work,75),"request two-pass rebuild");}
  if(reconfigure&&i==32){Require(controls->set(nrcontrolsabi::Passes,1)&&controls->set(nrcontrolsabi::Work,125),"request high-resolution rebuild");}
  if(reconfigure&&i==54){Require(controls->set(nrcontrolsabi::Work,100),"request baseline rebuild");}
  p->Set(NVSDK_NGX_Parameter_Reset,i==0?1u:0u);g.begin();ngx(eval(g.cmd.Get(),handle,p,nullptr),"evaluate SR");g.wait();present();
  if(styleSwitch){nrlifetime::Snapshot life;Require(lifetime(&life)!=0,"style lifetime");k033core::Status progress;api->status(&progress);
    if(i==29)styleCreated=life.created;
    if(i>=30){Require(life.created==styleCreated && !life.candidatePasses,"input style unnecessarily rebuilt model");Require(progress.processed==previousProcessed+1,"NR frame dropped on input style change");}
    previousProcessed=progress.processed;
  }
  if(stress){
    k033core::Status progress;api->status(&progress);
    if(i>30){Require(progress.processed==previousProcessed+1,"NR dropped a real frame during hot switching");++continuousFrames;}
    previousProcessed=progress.processed;
    nrlifetime::Snapshot life;Require(lifetime(&life)!=0,"per-frame lifetime read");
    Require(life.releaseFailed==0 && life.created>=life.released,"release accounting");
    Require(life.created-life.released==life.cachedPasses+life.candidatePasses+life.parkedFeatures,"allocated feature accounting");
    Require(life.created-life.released<=8,"more than two four-layer banks retained");
    if(i>=80 && i<360)Require(life.created==5 && life.cachedPasses==4,"warm layer changes allocated or discarded cached models");
    if(i>=410 && i<480 && life.parkedFeatures)++heldPendingFrames;
  }
  if(reconfigure){nrcontrolsabi::Snapshot s;if(controls->read(&s)&&!s.tuningPending){sawTwo|=i>=10&&i<32&&s.modelW==96&&s.values[nrcontrolsabi::Passes]==2;sawHigh|=i>=32&&i<54&&s.modelW==160;sawRestored|=i>=54&&s.modelW==128;}}
  if(benchmark && i>=30){nrlifetime::Snapshot life;if(lifetime(&life)&&life.gpuMs>0){gpuSum+=life.gpuMs;++gpuMeasurements;}}
  if(stress && i%40==39){
    nrcontrolsabi::Snapshot cfg;Require(controls->read(&cfg)!=0,"stress settings read");
    nrlifetime::Snapshot life;Require(lifetime(&life)!=0,"lifetime diagnostics");
    if(cfg.tuningPending)printf("PENDING: created=%llu released=%llu failed=%llu parked=%u active=%u\n",life.created,life.released,life.releaseFailed,life.parkedFeatures,life.activePasses);
    if(i<400 || i>=480)Require(!cfg.tuningPending,"stress rebuild still pending");
    Require(life.releaseFailed==0,"NR release failed");
    if(i<400 || i>=480)Require(life.parkedFeatures==0,"retired model accumulation");
    maximumBuild=std::max(maximumBuild,life.lastBuildMs);
    printf("STRESS cycle=%u active=%u extra=%ux%u created=%llu released=%llu parked=%u VRAM_MiB=%llu build_ms=%.2f GPU_ms=%.3f samples=%llu\n",
      cycles,life.activePasses,life.extraW,life.extraH,life.created,life.released,life.parkedFeatures,life.usage>>20,life.lastBuildMs,life.gpuMs,life.timingSamples);
    if(cycles==25)memoryFirst=life.usage;
    if(cycles==45)memoryLast=life.usage;
    ++cycles;
  }
  if(i==20){ui033abi::Api ui{sizeof(ui),ui033abi::Version,Host};auto render=Symbol<ui033abi::Render>(core,"K033_RenderEmbeddedControls");if(render(&ui)!=1||children||tables||disabled||ids||indents||fonts||colors||trees)throw std::runtime_error("active-feature controls");}
 }
    // Verify actual rendered pixels, not only API return codes or counters.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outFp{};UINT64 outBytes=0;auto outDesc=output->GetDesc();
    g.dev->GetCopyableFootprints(&outDesc,0,1,0,&outFp,nullptr,nullptr,&outBytes);
    auto readback=g.buffer(outBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    g.begin();scale::Barrier(g.cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION srcOut{},dstOut{};srcOut.pResource=output.Get();srcOut.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstOut.pResource=readback.Get();dstOut.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dstOut.PlacedFootprint=outFp;
    g.cmd->CopyTextureRegion(&dstOut,0,0,0,&srcOut,nullptr);g.wait();
    void* pixels=nullptr;D3D12_RANGE readRange{0,SIZE_T(outBytes)},noRead{0,0};OK(readback->Map(0,&readRange,&pixels));
    std::vector<Pixel> neuralPixels(ow*oh);
    float minimum=1e30f,maximum=-1e30f;
    for(unsigned y=0;y<oh;++y){const auto row=reinterpret_cast<const Pixel*>(static_cast<const char*>(pixels)+outFp.Footprint.RowPitch*y);
        std::memcpy(neuralPixels.data()+size_t(y)*ow,row,ow*sizeof(Pixel));
        for(unsigned x=0;x<ow;++x)for(float v:{row[x].r,row[x].g,row[x].b}){
            if(!std::isfinite(v)||std::abs(v)>100.f)throw std::runtime_error("invalid rendered pixel");
            minimum=std::min(minimum,v);maximum=std::max(maximum,v);}}
    readback->Unmap(0,&noRead);
    if(maximum-minimum<.01f)throw std::runtime_error("rendered output lost image contrast");
    printf("actual output finite, contrast retained: min %.6f max %.6f\n",minimum,maximum);


 if(styleSwitch){
    Require(controls->action(nrcontrolsabi::Save)!=0,"save input styles");present();
    std::ifstream cfgFile("dlss5-033.cfg");std::string persisted((std::istreambuf_iterator<char>(cfgFile)),{});
    Require(persisted.find("prestyle=3")!=std::string::npos&&persisted.find("prestylestrength=70")!=std::string::npos,"new input controls were not persisted");
    printf("INPUT STYLE SWITCH PASS: 8 states, 210 continuous NR frames, no model creation on style/strength changes; save verified\n");
 }

 if(benchmark){
    nrlifetime::Snapshot life;Require(lifetime(&life)&&gpuMeasurements>=100,"GPU timing sample coverage");
    printf("BENCHMARK output=%ux%u passes=%u extra=%ux%u GPU_mean_ms=%.4f samples=%u VRAM_MiB=%llu\n",ow,oh,life.activePasses,life.extraW,life.extraH,gpuSum/gpuMeasurements,gpuMeasurements,life.usage>>20);
 }
 if(stress){
    Require(cycles==46 && memoryFirst>0 && memoryLast>0,"memory comparison missing");
    Require(memoryLast<=memoryFirst+128ull*1024*1024,"baseline VRAM accumulated after repeated switches");
    nrlifetime::Snapshot life;Require(lifetime(&life)!=0 && life.timingSamples>800,"GPU timing ring stopped advancing");
    Require(life.fullBuilds<=36 && life.passChanges>=8,"cached layer changes unnecessarily rebuilt");
    Require(heldPendingFrames>=60 && continuousFrames==1809,"held-list hot-switch coverage");
    printf("HOT SWITCH PASS: %u consecutive real NR frames; old replayable list held for %u rendered frames; at most eight allocated features\n",continuousFrames,heldPendingFrames);
    printf("46 MODEL STATES PASS: baseline VRAM %llu -> %llu MiB, full builds %llu incremental %llu, max build %.2f ms\n",memoryFirst>>20,memoryLast>>20,life.fullBuilds,life.passChanges,maximumBuild);
 }
 k033core::Status before;api->status(&before);
 if(reconfigure){Require(sawTwo&&sawHigh&&sawRestored,"model/pass reconfiguration did not reach every completed build");printf("MODEL REBUILD: 100%% x1 -> 75%% x2 -> 125%% x1 -> 100%% x1 completed PASS\n");}
 NVSDK_NGX_Handle unknown{};unknown.Id=0x7fff1234;g.begin();eval(g.cmd.Get(),&unknown,p,nullptr);g.wait();present();
 k033core::Status status;api->status(&status);if(status.offered!=before.offered)throw std::runtime_error("unknown frame reached NR");
 if(status.processed==0||status.owner!=k033core::Host033||status.offered!=frameCount)throw std::runtime_error("NR absent or duplicate dispatch");
 // Enabled and disabled use identical SR inputs and neutral source grading.
 Require(controls->set(nrcontrolsabi::Enabled,0)!=0,"disable NR for contribution comparison");present();
 g.begin();scale::Barrier(g.cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 ngx(eval(g.cmd.Get(),handle,p,nullptr),"evaluate SR without NR");
 scale::Barrier(g.cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
 g.cmd->CopyTextureRegion(&dstOut,0,0,0,&srcOut,nullptr);g.wait();
 OK(readback->Map(0,&readRange,&pixels));double difference=0;float maxDifference=0;
 for(unsigned y=0;y<oh;++y){const auto row=reinterpret_cast<const Pixel*>(static_cast<const char*>(pixels)+outFp.Footprint.RowPitch*y);
  for(unsigned x=0;x<ow;++x)for(unsigned c=0;c<3;++c){float delta=std::abs((&row[x].r)[c]-(&neuralPixels[size_t(y)*ow+x].r)[c]);Require(std::isfinite(delta),"invalid NR-off output");difference+=delta;maxDifference=std::max(maxDifference,delta);}}
 readback->Unmap(0,&noRead);difference/=double(ow)*oh*3;
 Require(difference>1e-5 && maxDifference>1e-4,"NR runs but its visible output has been erased");
 nrcontrolsabi::Snapshot contributionCfg;Require(controls->read(&contributionCfg)!=0,"contribution settings");
 printf("%s: same SR input; mean_abs=%.7f max_abs=%.7f\n", contributionCfg.values[nrcontrolsabi::PreStyle]!=0?"NEURAL + INPUT STYLE CONTRIBUTION PASS":"REAL NEURAL CONTRIBUTION PASS: neutral source grade",difference,maxDifference);
 ngx(release(handle),"release");ngx(destroy(p),"destroy");OK(g.dev->GetDeviceRemovedReason());
 printf("ONE ENGINE + REAL RESHADE: %u SR frames, %llu NR frames, owner=%u, finite output, device healthy PASS\n",frameCount,(unsigned long long)status.processed,status.owner);
 printf("Active feature UI: %u callbacks, %zu sections, balanced stacks PASS\n",calls,sections.size());
 auto uiProbe=GetModuleHandleW(L"zz-033-ui-test.addon64");Require(uiProbe!=nullptr,"real UI probe adapter missing");
 auto uiFrames=Symbol<unsigned(__cdecl*)()>(uiProbe,"K033_TestUiFrames")();
 auto uiCalls=Symbol<unsigned(__cdecl*)()>(uiProbe,"K033_TestUiCalls")();
 auto uiHeaders=Symbol<unsigned(__cdecl*)()>(uiProbe,"K033_TestUiHeaders")();
 printf("Actual ReShade UI observations: frames=%u calls=%u headers=%u\n",uiFrames,uiCalls,uiHeaders);
 Require(uiFrames>=3&&uiCalls>300&&uiHeaders>=10,"real ReShade embedded UI or translated IDs failed");
 printf("REAL RESHADE UI: %u frames, %u calls, %u headers, translated IDs preserved PASS\n",uiFrames,uiCalls,uiHeaders);
 for(const auto& name:sections)printf("section: %s\n",name.c_str());
 fflush(stdout);ExitProcess(0);
 }catch(const std::exception& e){printf("FAIL: %s Win32=%lu\n",e.what(),GetLastError());fflush(stdout);ExitProcess(1);}}
