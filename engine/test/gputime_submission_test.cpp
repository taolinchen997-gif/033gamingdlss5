#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#define K033_TIMING_STANDALONE
#define K033_TIMING_SUBMIT_TEST
#include "../src/gputime_fenced.h"
#include <thread>
int main(){try{
    Gpu gpu;int checks=0,failures=0;
    auto check=[&](bool ok,const char*n){++checks;if(!ok){++failures;std::printf("FAIL %s\n",n);}};
    if(!gputime::Create(gpu.dev.Get(),gpu.queue.Get()))throw std::runtime_error("timing create");
    ComPtr<ID3D12Fence> gate;OK(gpu.dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    OK(gpu.queue->Wait(gate.Get(),1)); // hold GPU; CPU must keep progressing
    std::vector<ComPtr<ID3D12CommandAllocator>> allocs;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> lists;
    for(int i=0;i<gputime::kRing;++i){
        ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>cl;
        OK(gpu.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)));
        OK(gpu.dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&cl)));
        gputime::Begin(cl.Get());gputime::Stamp(cl.Get());gputime::End(cl.Get());OK(cl->Close());
        ID3D12CommandList* submit[]={cl.Get()};gpu.queue->ExecuteCommandLists(1,submit);
        allocs.push_back(a);lists.push_back(cl);
    }
    gputime::Poll();check(gputime::samples()==0,"no readback before GPU completion");
    gpu.begin();gputime::Begin(gpu.cmd.Get());gputime::End(gpu.cmd.Get());
    check(gputime::dropped()==1,"full ring skips measurement without waiting or reuse");
    OK(gate->Signal(1));gpu.wait();gputime::Poll();
    check(gputime::samples()==gputime::kRing,"all fenced samples published");
    check(gputime::freq_ok()&&std::isfinite(gputime::total()),"actual queue frequency and finite timing");
    const UINT64 before=gputime::samples();
    gpu.begin();gputime::Begin(gpu.cmd.Get());gputime::Abort();
    OK(gpu.cmd->Close());ID3D12CommandList* one[]={gpu.cmd.Get()};gpu.queue->ExecuteCommandLists(1,one);
    
    OK(gpu.queue->Signal(gpu.fence.Get(),++gpu.serial));OK(gpu.fence->SetEventOnCompletion(gpu.serial,gpu.event));
    if(WaitForSingleObject(gpu.event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("abort completion timeout");
    gputime::Poll();check(gputime::samples()==before,"aborted sample not published");
    gpu.begin();gputime::Begin(gpu.cmd.Get());gputime::End(gpu.cmd.Get());
    OK(gpu.cmd->Close());gpu.queue->ExecuteCommandLists(1,one);
    OK(gpu.queue->Signal(gpu.fence.Get(),++gpu.serial));OK(gpu.fence->SetEventOnCompletion(gpu.serial,gpu.event));
    if(WaitForSingleObject(gpu.event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("reuse timeout");
    gputime::Poll();check(gputime::samples()==before+1,"completed slots reusable");
    const UINT64 batchBefore=gputime::samples();
    OK(gpu.queue->Wait(gate.Get(),2));
    ID3D12CommandList* batch[2]{};
    for(int i=0;i<2;++i){
        ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>cl;
        OK(gpu.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)));
        OK(gpu.dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&cl)));
        gputime::Begin(cl.Get());gputime::End(cl.Get());OK(cl->Close());
        batch[i]=cl.Get();allocs.push_back(a);lists.push_back(cl);
    }
    std::thread worker([&]{gpu.queue->ExecuteCommandLists(2,batch);});worker.join();
    gputime::Poll();check(gputime::samples()==batchBefore,"cross-thread batch cannot complete before GPU fence");
    OK(gate->Signal(2));
    OK(gpu.queue->Signal(gpu.fence.Get(),++gpu.serial));
    OK(gpu.fence->SetEventOnCompletion(gpu.serial,gpu.event));
    if(WaitForSingleObject(gpu.event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("batch completion timeout");
    gputime::Poll();
    check(gputime::samples()==batchBefore+2,"raw lists submitted as cross-thread batch both measured");
    check(gputime::submit::installed.load() && gputime::submit::calls.load() >= 15,"native submit return observer exercised");
    // The core hands NR a wrapped list; native Execute sees its unwrapped alias.
    const auto aliasBefore=gputime::samples();gpu.begin();gputime::Begin(gpu.cmd.Get());gputime::End(gpu.cmd.Get());
    for(auto& slot:gputime::s_slots)if(slot.phase==gputime::Recorded && slot.cmd==gpu.cmd.Get()){
        slot.native=reinterpret_cast<uint64_t>(gpu.cmd.Get());slot.cmd=reinterpret_cast<ID3D12GraphicsCommandList*>(uintptr_t(0x1234));
    }
    gputime::TrackReset();gpu.wait();gputime::Poll();
    check(gputime::samples()==aliasBefore,"completed but replayable wrapped list keeps its query range");
    gputime::ResetCommand(reinterpret_cast<uint64_t>(gpu.cmd.Get()));gputime::Poll();
    check(gputime::samples()==aliasBefore+1,"native alias submission plus reset publishes wrapped-list sample");
    gputime::submit::Uninstall();check(!gputime::submit::installed.load(),"native observer detached");
    gputime::Destroy();std::printf("native submitted GPU timing: %d checks, %d failures\n",checks,failures);return failures?1:0;
}catch(const std::exception&e){std::printf("ERROR %s\n",e.what());return 2;}}
