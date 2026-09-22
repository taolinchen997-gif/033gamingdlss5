#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include "../src/beta2_grade.h"
#include "../runtime/src/inline_grade_program.h"
#include "../runtime/src/scene_grade_shader.h"
// Exact production adapter below, with inert API/COM/service values. No D3D
// header/device, DLL loading, OS window, GPU command list or runtime execution.
namespace reshade::api {
enum class device_api{d3d12,d3d11};enum class color_space{unknown,srgb,scrgb,hdr10_pq,hdr10_hlg};
enum class shader_stage{all};struct resource{uint64_t handle=0;};struct resource_view{uint64_t handle=0;};struct pipeline_layout{};
struct command_list{uintptr_t native=10;unsigned restores=0;bool failRestore=false;
 uintptr_t get_native(){return native;}void bind_descriptor_tables(shader_stage,pipeline_layout,unsigned,unsigned,const void*){++restores;if(failRestore)throw 1;}};
struct device{device_api kind=device_api::d3d12;device_api get_api(){return kind;}resource get_resource_from_view(resource_view v){return {v.handle};}};
struct command_queue{device* dev;command_list* list;uintptr_t native=20;
 device* get_device(){return dev;}command_list* get_immediate_command_list(){return list;}uintptr_t get_native(){return native;}};
struct effect_runtime{device* dev;command_queue* queue;resource backbuffer{30};
 device* get_device(){return dev;}command_queue* get_command_queue(){return queue;}resource get_current_back_buffer(){return backbuffer;}};
struct swapchain{resource backbuffer{30};color_space space=color_space::srgb;
 resource get_current_back_buffer(){return backbuffer;}color_space get_color_space(){return space;}};
}
namespace carrier{struct Config{int enabled=0;float diffuse_white=203;}cfg;K033_Settings grade=k033::defaults();}
namespace k033beta2{static K033_Settings Grade(const carrier::Config&){return carrier::grade;}}
namespace safemode{static bool paused=false;static bool off(){return paused;}}
namespace rendercore{struct Snapshot{uint64_t offered=0;};static Snapshot state;static Snapshot Status(){return state;}}
static void Log(const char*,const char*){}
namespace resolveleases{static void End(){}}
namespace beta2gradehost{static beta2grade::Ticket Begin(ID3D12Device*,ID3D12GraphicsCommandList*,IUnknown*const*,size_t){return {};}
 static void Cancel(beta2grade::Ticket){}static bool Completed(beta2grade::Ticket){return false;}}
namespace mock{static int calls=0;static bool poison=false;static int mode=0;static uint32_t encoding=0;static int retires=0,retireResult=K033_OK;static uintptr_t retireStream=0;static uint64_t retireGeneration=0;}
#include "../src/beta2_grade_present.h"
namespace beta2grade {
bool SettingsSame(const K033_Settings& a,const K033_Settings& b){return k033::inline_grade_same(a,b);}
bool NeedsGrade(const K033_Settings& s){return k033::needs_grade(k033::grade(s));}
Result ProcessPresentation(const PresentationFrame& f,const K033_Settings& settings,const Services& api){
 ++mock::calls;mock::encoding=f.encoding;
 if(!f.current(f)||!api.settings_current(settings))return {};
 if(mock::poison)return {K033_BYPASS,false,true};
 if(mock::mode==1)return {K033_UNSUPPORTED,false,false};
 if(mock::mode==2)return {K033_BUSY,false,false};
 if(mock::mode==6){auto nested=beta2gradepresent::Process(beta2gradepresent::active->runtime,beta2gradepresent::active->commands,beta2gradepresent::active->rtv);if(nested.recorded)throw std::runtime_error("stale duplicate result");}
 if(mock::mode==3){beta2gradepresent::Destroy(beta2gradepresent::active->runtime);if(f.current(f))throw std::runtime_error("stale runtime accepted");return {};}
 if(!k033::needs_grade(k033::grade(settings)))return {};
 api.armed();if(mock::mode==4)return {K033_BACKEND_ERROR,false,true};if(mock::mode==5)throw 1;
 return {K033_OK,true,false};
}
void PoisonPresentation(const PresentationFrame&){mock::poison=true;}
int RetirePresentation(uintptr_t stream,uint64_t generation){++mock::retires;mock::retireStream=stream;mock::retireGeneration=generation;
 if(beta2gradepresent::bound.load()!=nullptr||beta2gradepresent::generation.load()==generation)throw std::runtime_error("retirement before invalidation");return mock::retireResult;}
}
static int checks=0,failures=0;static void check(bool value,const char* what){++checks;if(!value){++failures;printf("FAIL %s\n",what);}}
struct Case{
 reshade::api::device dev;reshade::api::command_list commands;reshade::api::command_queue queue{&dev,&commands};
 reshade::api::effect_runtime runtime{&dev,&queue};reshade::api::swapchain swap;
 Case(){using namespace beta2gradepresent;bound=nullptr;generation=1;once={};last={};tick={};active=nullptr;note="test";
  mock::calls=0;mock::poison=false;mock::mode=0;mock::retires=0;mock::retireResult=K033_OK;mock::retireStream=0;mock::retireGeneration=0;rendercore::state={};safemode::paused=false;carrier::cfg={};carrier::grade=k033::defaults();carrier::grade.exposure=.2f;}
 void present(uint64_t s=1){beta2gradepresent::Capture(&queue,&swap,s);}
 beta2grade::Result finish(uint64_t view=30){return beta2gradepresent::Process(&runtime,&commands,{view});}
};
int main(){
 using namespace gradepresentpolicy;
 for(int nr:{0,1})for(int native:{0,1}){Case c;carrier::cfg.enabled=nr;(void)native;c.present();auto r=c.finish();
  check(r.recorded&&!r.output_uncertain,"grade independent of NR/native readiness");check(mock::calls==1&&c.commands.restores==1,"owned adapter records then resyncs");
  auto duplicate=c.finish();check(duplicate.recorded&&mock::calls==1&&c.commands.restores==1,"same real frame not graded twice");
  c.present(2);check(c.finish().recorded&&mock::calls==2,"next real frame records once");}
 {Case c;auto r=c.finish();check(!r.recorded&&mock::calls==0,"no present snapshot bypass");c.present();check(!c.finish(31).recorded&&mock::calls==0,"foreign RTV refused");}
 {Case c;c.dev.kind=reshade::api::device_api::d3d11;c.present();check(!c.finish().recorded&&mock::calls==0,"non DX12 refused");}
 {Case c;c.present();reshade::api::command_list foreign;check(!beta2gradepresent::Process(&c.runtime,&foreign,{30}).recorded&&mock::calls==0,"borrowed list refused");}
 {Case c;c.present();rendercore::state.offered=1;check(!c.finish().recorded&&mock::calls==0,"NGX offer yields presentation");}
 {Case c;c.present();c.finish();reshade::api::effect_runtime other{&c.dev,&c.queue};check(!beta2gradepresent::Process(&other,&c.commands,{30}).recorded&&mock::calls==1,"second runtime refused");}
 for(auto space:{reshade::api::color_space::srgb,reshade::api::color_space::hdr10_pq,reshade::api::color_space::scrgb}){
  Case c;c.swap.space=space;c.present();check(c.finish().recorded,"known encoding records");
  check(mock::encoding==(space==reshade::api::color_space::srgb?1:space==reshade::api::color_space::hdr10_pq?2:3),"encoding comes from exact captured swapchain");}
 {Case c;c.swap.space=reshade::api::color_space::unknown;c.present();check(!c.finish().recorded&&!mock::calls,"unknown encoding refused");}
 {Case c;c.present();mock::mode=3;check(!c.finish().recorded&&c.commands.restores==0,"runtime destroyed before arm cannot record");}
 for(int mode:{4,5}){Case c;c.present();mock::mode=mode;auto r=c.finish();check(r.output_uncertain&&!r.recorded&&c.commands.restores==1,"partial recording restores bindings and poisons output");
  c.present(2);rendercore::state.offered=1;check(c.finish().output_uncertain&&mock::calls==1,"poison survives next frame and NGX yield");
  beta2gradepresent::Destroy(&c.runtime);mock::poison=false;mock::mode=0;rendercore::state={};c.present(3);check(c.finish().recorded,"actual runtime destroy creates new generation");}
 {Case c;c.present();c.commands.failRestore=true;auto r=c.finish();check(r.output_uncertain&&mock::poison&&!r.recorded,"resync failure poisons even after full recording");}
 {Case c;carrier::grade.exposure=0;c.present();check(!c.finish().recorded&&c.commands.restores==0,"neutral no native binding mutation");}
 {Case c;c.present();mock::mode=6;check(c.finish().recorded&&mock::calls==1&&c.commands.restores==1,"same frame reentry cannot record twice");}
 {Case c;c.present();mock::mode=2;check(!c.finish().output_uncertain,"mutex busy is transient");mock::mode=0;c.present(2);check(c.finish().recorded&&!beta2gradepresent::once.uncertain,"busy does not permanently poison runtime");}
 for(int mode:{1,2}){Case c;c.present();mock::mode=mode;auto r=c.finish();check(!r.recorded&&!r.output_uncertain&&c.commands.restores==0,"unsupported or busy does not arm");}
 // This is the same transition plan used in native capture/copyback.
 for(int arrival:{4,8}){Transitions<int> t{arrival,2048,1024};check(t.captureBefore()==arrival&&t.captureAfter()==2048,"capture starts at declared RT/UAV");
  check(t.restoreCaptureBefore()==2048&&t.restoreCaptureAfter()==arrival,"capture restores RT/UAV");
  check(t.writeBefore()==arrival&&t.writeAfter()==1024,"copyback starts at RT/UAV");check(t.restoreWriteBefore()==1024&&t.restoreWriteAfter()==arrival,"copyback restores arrival");}
 {auto s=k033::defaults();auto c=k033::scene_grade_constants(10,20,s,2,203);
  check(sizeof(c)==64&&c.encoding==2&&c.diffuseWhite==203&&c.width==10&&c.height==20,"native/HLSL constants preserve real encoding and white");
}
 for(unsigned encoding=0;encoding<=4;++encoding)for(unsigned format=0;format<4;++format){
  const bool expected=(encoding==1&&(format==1||format==2))||(encoding==2&&format==2)||(encoding==3&&format==3);
  check(color(encoding,format==1,format==2,format==3)==expected,"actual supported format and transfer pair policy");}
 {Case c;c.present();c.finish();auto epoch=beta2gradepresent::generation.load();
  reshade::api::effect_runtime foreign{&c.dev,&c.queue};beta2gradepresent::Destroy(&foreign);check(mock::retires==0,"foreign destroy cannot retire bound stream");
  beta2gradepresent::Destroy(&c.runtime);check(mock::retires==1&&mock::retireStream==reinterpret_cast<uintptr_t>(&c.runtime)&&mock::retireGeneration==epoch,"destroy retires exact stream and old generation");
  beta2gradepresent::Destroy(&c.runtime);check(mock::retires==1,"duplicate destroy cannot retire again");}
 {Case c;c.present();c.finish();mock::retireResult=K033_BUSY;beta2gradepresent::Destroy(&c.runtime);
  check(mock::retires==1&&std::strstr(beta2gradepresent::Reason(),"尚未满足")!=nullptr,"retirement busy is reported without force release");}
 printf("presentation grade CPU checks=%d failed=%d (mock adapter, no GPU)\n",checks,failures);return failures?1:0;
}
