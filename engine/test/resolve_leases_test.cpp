#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#define K033_TIMING_STANDALONE
#define K033_TIMING_SUBMIT_TEST
#include "../src/gputime_fenced.h"
#define K033_LEASE_STANDALONE
#include "../src/resolve_leases.h"
#include <thread>
static void Drain(Gpu& g,ID3D12CommandQueue* q) {
    OK(q->Signal(g.fence.Get(),++g.serial));OK(g.fence->SetEventOnCompletion(g.serial,g.event));
    if(WaitForSingleObject(g.event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("queue drain timeout");
}
int main(){try {
    Gpu g;unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}};
    IUnknown* refs[]={g.b.rs_rv,g.b.pso_rv};
    check(!resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,2),"unobserved submission/reset bypasses optional pass");
    resolveleases::Pump(nullptr,true);
    check(gputime::submit::installed.load() && resolveleases::reset_installed.load(),"native execution observed even when DX11/Feeder present supplies no D3D12 queue");
    ComPtr<ID3D12Fence> gate;OK(g.dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    OK(g.queue->Wait(gate.Get(),1));
    std::vector<ComPtr<ID3D12CommandAllocator>> allocs,replacements;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> lists;
    std::vector<ID3D12CommandList*> batch;
    bool all=true;
    for(unsigned i=0;i<resolveleases::Capacity;++i) {
        ComPtr<ID3D12CommandAllocator> a,b;ComPtr<ID3D12GraphicsCommandList> cl;
        OK(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)));
        OK(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&b)));
        OK(g.dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&cl)));
        auto* lease=resolveleases::Begin(g.dev.Get(),cl.Get(),refs,2,reinterpret_cast<void*>(uintptr_t(i+1)));all&=lease!=nullptr;resolveleases::End();
        OK(cl->Close());allocs.push_back(a);replacements.push_back(b);lists.push_back(cl);batch.push_back(cl.Get());
    }
    check(all,"bounded pool admits recorded command lists");
    const auto begin=GetTickCount64();auto* excess=resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,2);
    check(!excess && GetTickCount64()-begin<100,"full pool bypasses without GPU wait or overwriting descriptors");
    std::thread worker([&]{g.queue->ExecuteCommandLists(UINT(batch.size()),batch.data());});worker.join();
    for(size_t i=0;i<lists.size();++i){OK(lists[i]->Reset(replacements[i].Get(),nullptr));OK(lists[i]->Close());}
    resolveleases::Pump(g.queue.Get(),false);
    check(resolveleases::retired==0,"successful reset cannot retire in-flight GPU work");
    check(resolveleases::PendingRange(1,resolveleases::Capacity),"retained model includes older incremental generations still on GPU");
    OK(gate->Signal(1));Drain(g,g.queue.Get());resolveleases::Pump(g.queue.Get(),false);
    check(resolveleases::retired==resolveleases::Capacity,"all completed and reset command generations retired");
    check(!resolveleases::PendingRange(1,resolveleases::Capacity),"generation range retires only after every dependent GPU use");
    g.begin();auto* reused=resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,2);resolveleases::End();OK(g.cmd->Close());
    check(reused!=nullptr,"completed heap capacity reusable");
    ID3D12CommandList* one[]={g.cmd.Get()};g.queue->ExecuteCommandLists(1,one);Drain(g,g.queue.Get());
    resolveleases::Pump(g.queue.Get(),false);
    check(reused && reused->active,"GPU completion alone does not release a replayable command list");
    auto realReset=resolveleases::original_reset;
    resolveleases::original_reset=[](ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*)->HRESULT{return E_FAIL;};
    const HRESULT failed=resolveleases::ResetHook(g.cmd.Get(),g.alloc.Get(),nullptr);
    resolveleases::original_reset=realReset;
    check(FAILED(failed) && reused->active && !reused->reset,"failed native Reset cannot retire bindings");
    ComPtr<ID3D12CommandQueue> other;D3D12_COMMAND_QUEUE_DESC desc={};OK(g.dev->CreateCommandQueue(&desc,IID_PPV_ARGS(&other)));
    OK(other->Wait(gate.Get(),2));other->ExecuteCommandLists(1,one);
    ComPtr<ID3D12CommandAllocator> fresh;OK(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&fresh)));
    OK(g.cmd->Reset(fresh.Get(),nullptr));OK(g.cmd->Close());resolveleases::Pump(g.queue.Get(),false);
    check(reused && reused->active,"replay on second queue retains descriptors until second fence completes");
    OK(gate->Signal(2));Drain(g,other.Get());resolveleases::Pump(g.queue.Get(),false);
    check(reused && !reused->active,"all actual queues completed before release");
    // Reset without Execute may also mean an uncovered submission entry.
    OK(g.cmd->Reset(fresh.Get(),nullptr));auto* abandoned=resolveleases::Begin(g.dev.Get(),g.cmd.Get(),refs,2);resolveleases::End();
    OK(g.cmd->Close());OK(g.cmd->Reset(fresh.Get(),nullptr));OK(g.cmd->Close());
    check(abandoned && abandoned->active,"missing Execute evidence pins bounded storage instead of guessing completion");
    check(SUCCEEDED(g.dev->GetDeviceRemovedReason()),"device remains healthy");
    resolveleases::UninstallReset();gputime::submit::Uninstall();
    check(!resolveleases::reset_installed.load() && !gputime::submit::installed.load(),"native hooks detached");
    std::printf("immutable resolve descriptor lifetime: %u checks, %u failures\n",checks,failures);return failures?1:0;
}catch(const std::exception& e){std::printf("ERROR %s\n",e.what());return 2;}}
