#pragma once
// Deliberately invalid COM pointers: a disabled optional timing path must not
// call them at all. A regression raises a caught CPU access violation, never
// creates a D3D device or reaches a graphics runtime/driver.
#define K033_TIMING_STANDALONE
#include "../src/gputime_fenced.h"
#undef K033_TIMING_STANDALONE

static DWORD disabledTimingCalls(unsigned& phase) {
    auto* device=reinterpret_cast<ID3D12Device*>(uintptr_t(1));
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(uintptr_t(1));
    auto* queue=reinterpret_cast<ID3D12CommandQueue*>(uintptr_t(1));
    __try {
        phase=1;if(gputime::Create(device,queue))return 1;
        gputime::s_ready=true;gputime::s_active=-1;
        phase=2;gputime::Begin(list);if(gputime::s_active!=-1)return 2;
        gputime::s_active=0;gputime::s_slots[0].phase=gputime::Recording;
        gputime::s_slots[0].cmd=list;gputime::s_slots[0].count=1;
        phase=3;gputime::Stamp(list);gputime::End(list);gputime::Abort();
        if(gputime::s_slots[0].count!=1||gputime::s_slots[0].phase!=gputime::Recording)return 3;
        gputime::s_slots[0].phase=gputime::Recorded;
        phase=4;gputime::AfterSubmit(list,queue,true);
        if(gputime::s_slots[0].queue!=nullptr)return 4;
        gputime::s_slots[0].phase=gputime::Submitted;
        gputime::s_slots[0].fence=reinterpret_cast<ID3D12Fence*>(uintptr_t(1));
        phase=5;gputime::Poll();gputime::ResetCommand(1);
        if(gputime::s_slots[0].reset)return 5;
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
static void disabledTimingChecks(){
    check(!diagnostic033::StartupTraces&&!gpufault033::Requested(),"added CPU and DRED fault collection disabled");
    check(!diagnostic033::TimingAllowed(true,true),"RE4 clean candidate ignores an old timing-on setting");
    check(!diagnostic033::TimingAllowed(true,false),"every game uses the same diagnostics-off policy");
    check(!diagnostic033::TimingAllowed(false,false)&&!diagnostic033::TimingAllowed(false,true),"explicit timing off honored for either process");
    const int old=gputime::cfg_enabled;
    for(int requested:{0,1}){
        gputime::cfg_enabled=requested;
        unsigned phase=0;const DWORD result=disabledTimingCalls(phase);
        if(result)std::printf("disabled timing requested=%d phase=%u CPU result=%08lX\n",requested,phase,result);
        check(result==0,"even the old default-on setting creates no GPU objects, stamps, submission signals or readbacks");
    // These are raw pointer fields, not COM smart pointers: clear the poison
    // before ordinary teardown. No Release is legal for a poison pointer.
        gputime::s_slots[0]=gputime::Slot{};gputime::s_ready=false;gputime::s_active=-1;
    }
    gputime::cfg_enabled=old;
}
