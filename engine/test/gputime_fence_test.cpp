#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#define K033_TIMING_STANDALONE
#include "../src/gputime_fenced.h"
int main(){try{
    Gpu gpu;int checks=0,failures=0;
    auto check=[&](bool ok,const char*n){++checks;if(!ok){++failures;std::printf("FAIL %s\n",n);}};
    if(!gputime::Create(gpu.dev.Get(),nullptr))throw std::runtime_error("timing create");
    check(!gputime::snapshot().fresh(),"no completed GPU sample is never reported fresh");
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
        gputime::AfterSubmit(cl.Get(),gpu.queue.Get());allocs.push_back(a);lists.push_back(cl);
    }
    gputime::Poll();check(gputime::samples()==0,"no readback before GPU completion");
    gpu.begin();gputime::Begin(gpu.cmd.Get());gputime::End(gpu.cmd.Get());
    check(gputime::dropped()==1,"full ring skips measurement without waiting or reuse");
    OK(gate->Signal(1));gpu.wait();gputime::Poll();
    check(gputime::samples()==gputime::kRing,"all fenced samples published");
    check(gputime::freq_ok()&&std::isfinite(gputime::total()),"actual queue frequency and finite timing");
    const auto timing=gputime::snapshot();
    check(timing.fresh()&&timing.samples==gputime::kRing&&timing.total==gputime::total(),"snapshot reports one coherent completed sample");
    auto stale=timing;stale.age_ms=1001;
    check(!stale.fresh(),"old sample remains distinguishable from current performance");
    const UINT64 before=gputime::samples();
    gpu.begin();gputime::Begin(gpu.cmd.Get());gputime::Abort();
    OK(gpu.cmd->Close());ID3D12CommandList* one[]={gpu.cmd.Get()};gpu.queue->ExecuteCommandLists(1,one);
    gputime::AfterSubmit(gpu.cmd.Get(),gpu.queue.Get());
    OK(gpu.queue->Signal(gpu.fence.Get(),++gpu.serial));OK(gpu.fence->SetEventOnCompletion(gpu.serial,gpu.event));
    if(WaitForSingleObject(gpu.event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("abort completion timeout");
    gputime::Poll();check(gputime::samples()==before,"aborted sample not published");
    gpu.begin();gputime::Begin(gpu.cmd.Get());gputime::End(gpu.cmd.Get());
    OK(gpu.cmd->Close());gpu.queue->ExecuteCommandLists(1,one);gputime::AfterSubmit(gpu.cmd.Get(),gpu.queue.Get());
    OK(gpu.queue->Signal(gpu.fence.Get(),++gpu.serial));OK(gpu.fence->SetEventOnCompletion(gpu.serial,gpu.event));
    if(WaitForSingleObject(gpu.event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("reuse timeout");
    gputime::Poll();check(gputime::samples()==before+1,"completed slots reusable");
    gputime::Destroy();std::printf("fenced GPU timing: %d checks, %d failures\n",checks,failures);return failures?1:0;
}catch(const std::exception&e){std::printf("ERROR %s\n",e.what());return 2;}}
