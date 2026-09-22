// CPU doubles only. The build wrapper extracts the actual BeginCommands body
// from dlss5-feed.cpp into our output directory before compiling this fixture.
#include "../src/gpu_submission.h"
#include <cstdio>
#include <cstddef>
#include <initializer_list>
#include <vector>

using UINT64=uint64_t;
using DWORD=uint32_t;
using HRESULT=int32_t;
constexpr HRESULT S_OK=0, E_FAIL=-1;
constexpr DWORD WAIT_OBJECT_0=0, WAIT_TIMEOUT=258, WAIT_FAILED=0xffffffff;
#define FAILED(hr) ((hr)<0)
#define SUCCEEDED(hr) ((hr)>=0)

struct Fence {
    std::vector<UINT64> samples{0};size_t reads=0;
    HRESULT eventResult=S_OK;unsigned eventCalls=0;
    UINT64 GetCompletedValue(){const auto i=reads++;return samples[i<samples.size()?i:samples.size()-1];}
    HRESULT SetEventOnCompletion(UINT64,void*){++eventCalls;return eventResult;}
};
struct Allocator {HRESULT result=S_OK;unsigned resets=0;HRESULT Reset(){++resets;return result;}};
struct List {HRESULT result=S_OK;unsigned resets=0;HRESULT Reset(Allocator*,void*){++resets;return result;}};
struct Device {HRESULT removed=S_OK;HRESULT GetDeviceRemovedReason(){return removed;}};
static struct Event {DWORD wait=WAIT_OBJECT_0;unsigned resets=0,waits=0;} event;
static void ResetEvent(void*){++event.resets;}
static DWORD WaitForSingleObject(void*,DWORD){++event.waits;return event.wait;}
static void Log(const char*,...){}
static bool g_gpu_quarantined=false;
static bool QuarantineGpu(const char*){g_gpu_quarantined=true;return false;}
static struct Config {int gpu_timeout_ms=500;} g_cfg;
static struct Feed {
    Fence* fence12=nullptr;List* list=nullptr;Device* dev12=nullptr;
    Allocator* alloc[1]{};UINT64 alloc_fence[1]{};int frame_slot=0;void* fence_event=nullptr;
} g;

#include "feeder_begin_commands_under_test.h"

struct Fixture {
    Fence fence;Allocator allocator;List list;Device device;
    Fixture(std::initializer_list<UINT64> samples,UINT64 retire=5) {
        fence.samples=samples;event={};g_gpu_quarantined=false;g_cfg={};
        g={};g.fence12=&fence;g.list=&list;g.dev12=&device;g.alloc[0]=&allocator;
        g.alloc_fence[0]=retire;g.fence_event=&event;
    }
    bool untouched()const{return allocator.resets==0&&list.resets==0;}
};

int main(){
    unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* why){++checks;std::printf("%s %s\n",ok?"PASS":"FAIL",why);if(!ok)++failures;};
    {Fixture f({5});check(BeginCommands()&&f.allocator.resets==1&&f.list.resets==1&&event.waits==0,"completed allocator resets normally without a wait");}
    {Fixture f({0},0);check(BeginCommands()&&f.allocator.resets==1&&f.list.resets==1,"unused allocator starts normally");}
    {Fixture f({UINT64_MAX});check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined,"device already lost never resets allocator or list");}
    {Fixture f({0,UINT64_MAX});check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined,"loss between initial check and retire comparison never resets");}
    {Fixture f({5,5,UINT64_MAX});check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined,"loss after completed comparison but before Reset never resets");}
    {Fixture f({0,UINT64_MAX},0);check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined,"unused allocator still checks device loss before Reset");}
    {Fixture f({0,0,5});check(BeginCommands()&&f.allocator.resets==1&&f.list.resets==1&&event.waits==1,"wait that reaches the target resets normally");}
    {Fixture f({0});check(!BeginCommands()&&f.untouched()&&!g_gpu_quarantined,"stale event wakeup cannot reset pending allocator");}
    {Fixture f({0});event.wait=WAIT_TIMEOUT;check(!BeginCommands()&&f.untouched()&&!g_gpu_quarantined&&event.waits==2,"bounded allocator timeout skips frame without freeing or resetting");}
    {Fixture f({0});event.wait=WAIT_FAILED;check(!BeginCommands()&&f.untouched(),"failed wait cannot reset allocator");}
    {Fixture f({0});f.fence.eventResult=E_FAIL;check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined&&event.waits==0,"failed event registration quarantines before wait");}
    {Fixture f({0,0,UINT64_MAX});check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined,"device loss on event wakeup quarantines");}
    {Fixture f({0,0,5,5,UINT64_MAX});check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined,"device loss after successful wait checks still blocks Reset");}
    {Fixture f({0});event.wait=WAIT_TIMEOUT;f.device.removed=E_FAIL;check(!BeginCommands()&&f.untouched()&&g_gpu_quarantined&&event.waits==1,"removed reason during wait stops immediately");}
    {Fixture f({5});f.allocator.result=E_FAIL;check(!BeginCommands()&&f.allocator.resets==1&&f.list.resets==0,"allocator Reset failure never attempts list Reset");}
    {Fixture f({5});f.list.result=E_FAIL;check(!BeginCommands()&&f.list.resets==1,"list Reset failure is propagated");}
    {Fixture f({5});g.alloc[0]=nullptr;check(!BeginCommands()&&f.untouched(),"missing allocator is rejected");}
    {Fixture f({5});g.fence12=nullptr;check(!BeginCommands()&&f.untouched(),"missing fence is rejected");}
    {Fixture f({5});g.list=nullptr;check(!BeginCommands()&&f.untouched(),"missing list is rejected");}
    {Fixture f({5});g_gpu_quarantined=true;check(!BeginCommands()&&f.untouched()&&f.fence.reads==0,"quarantine blocks retries without further GPU object access");}
    std::printf("FEEDER COMMANDS CPU: %u checks, %u failures; actual BeginCommands body, no graphics device or driver calls\n",checks,failures);
    return failures?1:0;
}
