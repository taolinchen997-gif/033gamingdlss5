// A presentation input adapter for the existing host NR pipeline. No second
// model implementation, proxy DLL, render queue or frame-generation backend.
#pragma once
#include "d3d12_identity.h"
#include "nr_input_policy.h"
#include "nr_runtime_state.h"
#include "nr_zero_guides.h"
#ifdef K033_BETA2_RESHADE_HOST
#include "beta2_grade_present.h"
#endif
namespace nrbackbuffer {
using namespace reshade::api;
using Microsoft::WRL::ComPtr;
static ComPtr<ID3D12Device> device;
static ComPtr<IUnknown> identity;
static nrzero033::Guides zeroGuides;
static ComPtr<ID3D12CommandQueue> boundQueue;
static std::atomic<effect_runtime*> boundRuntime{nullptr};
static unsigned width=0,height=0;
static uint64_t processed=0,lastFrame=0;
static unsigned framesAtSize=0;
static std::atomic<uint64_t> realFrame{0};
static bool sawFrame=false,stopped=false;
static const char* reason="等待画面接入";
static void Note(const char* text){
    if(std::strcmp(reason,text)!=0){reason=text;Log("[033 NR input] %s",reason);}
}
static bool Claimed(){return nrinput033::ownership.Get()==nrinput033::Route::Presentation;}
static void OnFinish(effect_runtime* runtime,command_list* commands,resource_view rtv,resource_view){
    if(!rendercore::Integrated() || !runtime || !commands)return;
    const bool trace=diagnostic033::StartupTraces && realFrame.load()<=8;
    // Declared before the writer/COM locals: this marker also proves their
    // destructors returned, not merely that the function reached a return.
    struct End {bool active;~End(){if(active)Log("[033 startup] NR finish callback returned tid=%lu",GetCurrentThreadId());}} end{trace};
    if(trace)Log("[033 startup] NR finish callback begin tid=%lu",GetCurrentThreadId());
    nrdispatch::AfterScope writer;if(!writer.entered)return;
#ifdef K033_BETA2_RESHADE_HOST
    // Independent scene grade precedes NR enable/native/model gates.
    const auto graded=yanyundual::enabled?beta2grade::Result{}:beta2gradepresent::Process(runtime,commands,rtv);
    if(graded.output_uncertain){Note(beta2gradepresent::Reason());return;}
#endif
    if(nrfault033::Blocked()){Note(nrfault033::Note());return;}
    const auto core=rendercore::Status();
    auto* api=runtime->get_device();auto* queue=runtime->get_command_queue();
    // Only the runtime's immediate list is ours to augment. Never weaken the
    // strict state-restoration gate for a game's borrowed rendering list.
    const bool valid=api&&api->get_api()==device_api::d3d12&&queue&&
        commands==queue->get_immediate_command_list();
    const bool nativeFg=GetModuleHandleW(L"nvngx_dlssg.dll")||GetModuleHandleW(L"sl.dlss_g.dll");
    nrgame033::Discover();
    if(!nrinput033::CanPresent(carrier::cfg.enabled!=0,safemode::off(),true,
        nrinput033::PreferUpscale(carrier::cfg.inject!=0,nrgame033::automaticAdapter.load()),
        core.offered,nativeFg,valid,nrinput033::ownership.Get())){
        if(Claimed()&&(!carrier::cfg.enabled||safemode::off()))nrgame033::Pause();
        if(carrier::cfg.enabled&&!carrier::cfg.inject&&!Claimed())Note("等待可独占的 D3D12 画面通道");
        return;
    }
    if(stopped)return;
    if(nrgame033::waitingForRenderer.load()){
        // Request input independently of NR/model initialization: the adapter
        // must be able to complete its first EndRendering callback while NR is
        // waiting. No GPU work, fixed sleep, or config/counter mutation here.
        nrgame033::wanted.store(true);
        Note("正在等待游戏渲染器就绪，随后自动启动神经渲染");return;
    }
    auto* input=reinterpret_cast<ID3D12Resource*>(api->get_resource_from_view(rtv).handle);
    const auto actualBackbuffer=runtime->get_current_back_buffer();
    if(!input||!actualBackbuffer.handle||reinterpret_cast<uint64_t>(input)!=actualBackbuffer.handle){
        Note("当前不是已确认的实际后缓冲，神经渲染未录制");return;}
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(commands->get_native());
    auto* dev=reinterpret_cast<ID3D12Device*>(api->get_native());
    if(!input||!list||!dev){Note("画面资源不可用");return;}
    const auto desc=input->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.DepthOrArraySize!=1||desc.SampleDesc.Count!=1||
       !desc.Width||!desc.Height||desc.Width>8192||desc.Height>8192||desc.Width*desc.Height>16777216||
       carrier::TypedColorFormat(desc.Format)==DXGI_FORMAT_UNKNOWN){Note("当前画面尺寸或格式尚不支持");return;}
    auto currentIdentity=identity033::Canonical(dev);
    if(!currentIdentity||!identity033::Child(currentIdentity.Get(),list).equal||
       !identity033::Child(currentIdentity.Get(),input).equal){Note("画面与命令列表的设备校验失败");return;}
    auto* nativeQueue=reinterpret_cast<ID3D12CommandQueue*>(queue->get_native());
    if(!identity033::Child(currentIdentity.Get(),nativeQueue).equal){Note("画面提交队列的设备校验失败");return;}
    if(device&&(!identity033::Equal(device.Get(),dev)||!identity033::Equal(boundQueue.Get(),nativeQueue))){
        stopped=true;Note("图形设备或队列已更换，请重启游戏后恢复神经渲染");return;
    }
    if(boundRuntime.load()&&boundRuntime.load()!=runtime){Note("已绑定另一游戏画面，本窗口不重复渲染");return;}
#ifdef K033_BETA2_RESHADE_HOST
    // Preserve the independent runtime's no-zero-placeholder contract. Scene
    // adapter availability and actual guide consumption are separate facts.
    auto native=nrgame033::Read(dev,nativeQueue,list,UINT(desc.Width),desc.Height);
    if(!native.ready){nrbeta2::Note(nrbeta2::State::WaitingScene,nrbeta2::Source::SceneDeclared);
        Note(nrgame033::Note());return;}
#endif
    if(!rendercore::Api()->claim(k033core::Host033)||!nrinput033::ownership.Claim(nrinput033::Route::Presentation))return;
    if(!device){device=dev;identity=currentIdentity;boundQueue=nativeQueue;
        Log("[033 NR input] route=presentation selected; guide origin and consumption are reported per recorded frame");}
    if(!boundRuntime.load()){boundRuntime.store(runtime);sawFrame=false;}
    const auto serial=realFrame.load();
    if(!serial||(sawFrame&&lastFrame==serial))return;
    lastFrame=serial;sawFrame=true;
    // The present hook supplies the swapchain's color space (including the
    // explicit universal-FG runtime); effect_runtime exposes only a setter.
#ifndef K033_BETA2_RESHADE_HOST
    const auto zeroState=zeroGuides.Poll(dev,list,commands,UINT(desc.Width),desc.Height);
    if(zeroState==nrzero033::Result::Failed){
        stopped=true;Note("神经渲染输入资源准备失败，游戏画面继续显示，详见 zero upload 日志");return;
    }
    if(zeroState!=nrzero033::Result::Ready){
        Note("输入资源正在随画面提交，等待完成确认");return;
    }
#endif
    if(width!=desc.Width||height!=desc.Height){
        width=UINT(desc.Width);height=desc.Height;hostnr::invalidate_history();framesAtSize=0;
        Log("[033 NR input] presentation=%ux%u format=%u space=%u; native guide readiness is recorded separately",width,height,unsigned(desc.Format),unsigned(carrier::source_space));
    }
    struct CallbackScope{bool previous=rendercore::in_callback;CallbackScope(){rendercore::in_callback=true;}~CallbackScope(){rendercore::in_callback=previous;}} callback;
#ifndef K033_BETA2_RESHADE_HOST
    auto native=nrgame033::Read(dev,nativeQueue,list,width,height);
#endif
    nrinput033::PresentScope inputScope(native.ready,native.depthState,native.motionState,
        native.depthPlan==nrgame033::GuideSubresource::DepthPlane0,native.normalizeRe4);
#ifdef K033_BETA2_RESHADE_HOST
    // Keeps the actual scene PresentScope above; no fabricated NGX epoch.
    hostnr::InputScope gradeInput(nullptr,graded.recorded);
#endif
    hostnr::s_defer_build=true;
    hostnr::clear_guide_rects();hostnr::set_output_rect({0,0,width,height});
    if(native.ready){
        hostnr::set_guide_rects(nrgame033::GuideRects(native.frame,native.motionWidth,native.motionHeight));
    }
    hostnr::set_stream_key(native.ready?native.frame.stream:reinterpret_cast<uintptr_t>(runtime));exposure::clear_frame();
    const int result=nrruntimestate033::Run(commands,[&]{
        return hostnr::Stage(list,device.Get(),input,
            native.ready?native.depth.Get():zeroGuides.Depth(),native.ready?native.motion.Get():zeroGuides.Motion(),
            width,height,native.ready?native.frame.scaleX:0,native.ready?native.frame.scaleY:0,native.ready?1:0,native.reset);
    });
    hostnr::clear_guide_rects();hostnr::set_stream_key(0);exposure::clear_frame();
    if(result==2){nrgame033::Consume(native);Note(yanyundual::note.load());}
    else if(result==1){nrgame033::Consume(native);++processed;++framesAtSize;Note(native.ready?"画面和 RE 场景引导已送入神经渲染":nrgame033::Note());
#ifdef K033_BETA2_RESHADE_HOST
        // RE scene has its own serial. It does not establish a same-Evaluate
        // relation to this backbuffer; do not claim that stronger property.
        nrbeta2::Recorded(nrbeta2::Source::SceneDeclared,false,native.frame.serial);
#endif
        if(framesAtSize<=3||processed%300==0)Log("[033 NR input] route=presentation recorded=%llu host_frames=%llu size=%ux%u size_frames=%u GPU_completed_samples=%llu native_guides=%d reset=%d input_serial=%llu jitter_known=%d runtime_bindings_resynced=1",processed,hostnr::frames(),width,height,framesAtSize,gputime::samples(),native.ready?1:0,native.reset,native.ready?native.frame.serial:0,native.ready&&(native.frame.flags&nrgame033::JitterKnown)?1:0);
    }else{hostnr::invalidate_history();Note(hostnr::s_failed?"神经模型准备或求值失败，详见日志":"正在准备模型或等待资源提交确认");}
}
static void OnDestroyRuntime(effect_runtime* runtime){
#ifdef K033_BETA2_RESHADE_HOST
    beta2gradepresent::Destroy(runtime);
#endif
    // Notification must not be lost when model building owns the writer.
    // All texture/history changes remain on the next serialized real frame.
    boundRuntime.compare_exchange_strong(runtime,nullptr);
}
}
