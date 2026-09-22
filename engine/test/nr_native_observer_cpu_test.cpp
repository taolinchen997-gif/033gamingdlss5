#include <Windows.h>
#include <vector>
#include <cstdio>
#include "detours/detours.h"
namespace mfgunlock::hook::internal {inline std::vector<HANDLE> OpenOtherThreads(){return {};}}
#include "nr_native_input_observer.h"
static unsigned sequence=0,failures=0,checks=0;
static const D3D12_RESOURCE_BARRIER* expected=nullptr;
static void Check(bool value,const char* name){++checks;if(!value){++failures;printf("FAIL %s\n",name);}}
static void STDMETHODCALLTYPE Original(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    Check(uintptr_t(list)==1&&count==1&&barriers==expected,"original receives unmodified arguments");
    Check(sequence++==0,"original call precedes input state notification");
}
static void Notify(uint64_t list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    Check(list==1&&count==1&&barriers==expected,"observer receives original barrier array");
    Check(sequence++==1,"observation follows original recording");
    Check(barriers[0].Flags==D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY,"split flag remains visible");
    Check(barriers[0].Transition.Subresource==7,"subresource remains visible");
}
static void STDMETHODCALLTYPE Enhanced(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups){
    Check(uintptr_t(list)==2&&count==1&&groups==nullptr,"enhanced commands always forwarded unchanged");
    Check(nrnative033::enhancedSeen.load(),"unsupported state model rejected before enhanced recording");
}
int main(){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Flags=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;barrier.Transition.Subresource=7;
    expected=&barrier;nrnative033::original=Original;nrnative033::barrierNotice.store(Notify);
    nrnative033::BarrierHook(reinterpret_cast<ID3D12GraphicsCommandList*>(1),1,&barrier);
    Check(sequence==2,"exactly one original call and one notification");
    nrnative033::barrierNotice.store(nullptr);sequence=0;
    nrnative033::BarrierHook(reinterpret_cast<ID3D12GraphicsCommandList*>(1),1,&barrier);
    Check(sequence==1,"missing observer never drops graphics commands");
    nrnative033::originalEnhanced=Enhanced;
    nrnative033::EnhancedHook(reinterpret_cast<ID3D12GraphicsCommandList7*>(2),1,nullptr);
    Check(!nrnative033::Request(reinterpret_cast<ID3D12GraphicsCommandList*>(1)),"enhanced barrier gate refuses without touching test object");
    printf("Native observer mocked CPU: %u checks, %u failures; no hooks installed, no GPU created\n",checks,failures);
    return failures?1:0;
}
