#pragma once
#include "beta2_grade.h"
#include "beta2_grade_present_policy.h"
namespace beta2gradepresent {
using namespace reshade::api;
struct Tick {uint64_t serial=0,backbuffer=0;void* queue=nullptr;color_space space=color_space::unknown;};
static thread_local Tick tick;
static std::atomic<effect_runtime*> bound{nullptr};
static std::atomic<uint64_t> generation{1};
static gradepresentpolicy::Once once;
static beta2grade::Result last;
static std::atomic<const char*> note{"等待呈现调色入口"};
static void Note(const char* text){const auto previous=note.exchange(text);if(std::strcmp(previous,text))Log("[033 grade presentation] %s",text);}
static const char* Reason(){return note.load();}
static void Capture(command_queue* queue,resource backbuffer,color_space space,uint64_t serial){
    tick={};if(!queue||queue->get_device()->get_api()!=device_api::d3d12||!backbuffer.handle||!serial)return;
    tick={serial,backbuffer.handle,reinterpret_cast<void*>(queue->get_native()),space};
}
static void Capture(command_queue* queue,swapchain* sc,uint64_t serial){
    if(!sc){tick={};return;}Capture(queue,sc->get_current_back_buffer(),sc->get_color_space(),serial);
}
static void Destroy(effect_runtime* runtime){
    const uint64_t epoch=generation.load();
    auto* expected=runtime;if(!bound.compare_exchange_strong(expected,nullptr))return;
    generation.fetch_add(1); // Invalidate before any COM release/reentrant callback.
    const int retired=beta2grade::RetirePresentation(reinterpret_cast<uintptr_t>(runtime),epoch);
    if(retired==K033_BUSY)Note("画面重建：调色引用尚未满足回收条件，已保留");
    else if(retired==K033_OK)Note("画面重建：已回收确认完成的调色引用");
}
struct Call {
    effect_runtime* runtime;command_list* commands;resource_view rtv;
    Tick original;uint64_t epoch;K033_Settings settings;float white;bool armed=false;
};
static thread_local Call* active=nullptr;
static bool Current(const beta2grade::PresentationFrame& f){
    if(!active||bound.load()!=active->runtime||generation.load()!=active->epoch||f.generation!=active->epoch||
       tick.serial!=active->original.serial||tick.backbuffer!=active->original.backbuffer||tick.queue!=active->original.queue||
       tick.space!=active->original.space||f.command!=reinterpret_cast<void*>(active->commands->get_native()))return false;
    auto* runtime=active->runtime;auto* queue=runtime->get_command_queue();
    return queue&&active->commands==queue->get_immediate_command_list()&&reinterpret_cast<void*>(queue->get_native())==tick.queue&&
        runtime->get_current_back_buffer().handle==tick.backbuffer&&
        runtime->get_device()->get_resource_from_view(active->rtv).handle==reinterpret_cast<uint64_t>(f.output);
}
static bool SettingsCurrent(const K033_Settings& settings){
    if(!active)return false;const auto now=k033beta2::Grade(carrier::cfg);
    return beta2grade::SettingsSame(now,settings)&&carrier::cfg.diffuse_white==active->white;
}
static void Armed(){if(active)active->armed=true;}
static beta2grade::Result Process(effect_runtime* runtime,command_list* commands,resource_view rtv){
    beta2grade::Result result;
    if(!runtime||!commands||safemode::off()){Note("调色等待可用的呈现通道");return result;}
    if(bound.load()==runtime&&once.generation==generation.load()&&once.uncertain)return last;
    auto* api=runtime->get_device();auto* queue=runtime->get_command_queue();
    const bool immediate=api&&api->get_api()==device_api::d3d12&&queue&&commands==queue->get_immediate_command_list();
    const bool same=immediate&&tick.serial&&tick.queue==reinterpret_cast<void*>(queue->get_native())&&
        tick.backbuffer==runtime->get_current_back_buffer().handle;
    const auto offered=rendercore::Status().offered;
    if(!gradepresentpolicy::admit(immediate,same,offered)){
        Note(offered?"调色由游戏超分入口接管；呈现端不重复处理":"等待同一画面的 D3D12 自有列表");return result;
    }
    auto* expected=static_cast<effect_runtime*>(nullptr);
    if(!bound.compare_exchange_strong(expected,runtime)&&expected!=runtime){Note("已绑定另一画面，不重复调色");return result;}
    const uint64_t epoch=generation.load();
    if(!once.enter(reinterpret_cast<uintptr_t>(runtime),epoch,tick.serial)){
        if(once.current(reinterpret_cast<uintptr_t>(runtime),epoch,tick.serial)||once.uncertain)return last;
        return result;
    }
    last={};
    uint32_t encoding=0;
    if(tick.space==color_space::srgb)encoding=1;
    else if(tick.space==color_space::hdr10_pq)encoding=2;
    else if(tick.space==color_space::scrgb)encoding=3;
    else{Note("当前画面色彩编码未知，调色未录制");last=result;return result;}
    const auto input=api->get_resource_from_view(rtv);
    if(input.handle!=tick.backbuffer){Note("当前不是已确认的实际后缓冲，调色未录制");last=result;return result;}
    Call call{runtime,commands,rtv,tick,epoch,k033beta2::Grade(carrier::cfg),carrier::cfg.diffuse_white};
    struct Active {Call* previous;Active(Call* c):previous(active){active=c;}~Active(){active=previous;}} scope(&call);
    beta2grade::PresentationFrame frame{reinterpret_cast<void*>(commands->get_native()),reinterpret_cast<void*>(input.handle),
        reinterpret_cast<uintptr_t>(runtime),epoch,encoding,call.white,Current};
    {
        // One atomic grade-owner admission. A busy lock or stale pre-record
        // observation is a transient bypass, never persistent output poison.
        const beta2grade::Services services{beta2gradehost::BeginPresentation,resolveleases::End,beta2gradehost::Cancel,
            beta2gradehost::Completed,SettingsCurrent,Armed};
        try{result=beta2grade::ProcessPresentation(frame,call.settings,services);}
        catch(...){if(call.armed)beta2grade::PoisonPresentation(frame);result={K033_BACKEND_ERROR,false,call.armed};}
        // Only this owned immediate list may use the runtime resync contract.
        // A borrowed game list still requires the strict original envelope.
        if(call.armed){try{commands->bind_descriptor_tables(shader_stage::all,pipeline_layout{},0,0,nullptr);}
            catch(...){beta2grade::PoisonPresentation(frame);result={K033_BACKEND_ERROR,false,true};}}
        if(result.output_uncertain)Note("调色录制中断，已暂停此画面处理");
        else if(result.recorded)Note("调色已录制，等待游戏画面验收");
        else if(result.status==K033_BUSY)Note("调色资源准备或回收中");
        else if(result.status==K033_UNSUPPORTED)Note("当前画面格式或资源条件尚未接通调色");
        else if(result.status<0)Note("调色参数或资源准备失败，未录制");
        else if(!beta2grade::NeedsGrade(call.settings))Note("调色关闭或中性参数，无需处理");
        else Note("本帧状态已变化，调色未录制");
    }
    once.finish(result.recorded,result.output_uncertain);last=result;return result;
}
}
