#pragma once
#include "../src/nr_runtime_state.h"
#include <stdexcept>

namespace nrruntimetest033 {
// Model the real ReShade DX12 cache boundary: API calls update a cache; native
// NR calls change the underlying list without changing that cache. No GPU or
// D3D/driver entry point is called. Production uses the same Run scope below.
struct Commands {
    unsigned apiHeap=10,nativeHeap=10,apiGraphics=20,nativeGraphics=20;
    unsigned apiCompute=30,nativeCompute=30,restores=0,heapBinds=0;
    bool contract=true;
    void NativeNr() { nativeHeap=99;nativeCompute=98; }
    void bind_descriptor_tables(reshade::api::shader_stage stages,
        reshade::api::pipeline_layout layout,uint32_t first,uint32_t count,
        const reshade::api::descriptor_table* tables) {
        contract &= stages==reshade::api::shader_stage::all && layout.handle==0 &&
                    first==0 && count==0 && tables==nullptr;
        ++restores;
        // The upstream zero-count branch forces heap/root updates even when
        // their cached values have not changed; a nonempty bind can skip them.
        if(count==0 && apiHeap){nativeHeap=apiHeap;++heapBinds;}
        apiGraphics=nativeGraphics=unsigned(layout.handle);
        apiCompute=nativeCompute=unsigned(layout.handle);
    }
    bool DrawOverlay() {
        if(apiHeap!=10){apiHeap=nativeHeap=10;++heapBinds;}
        if(apiGraphics!=20)apiGraphics=nativeGraphics=20;
        return nativeHeap==10 && nativeGraphics==20;
    }
    bool DispatchEffect() {
        if(apiHeap!=10){apiHeap=nativeHeap=10;++heapBinds;}
        if(apiCompute!=30)apiCompute=nativeCompute=30;
        return nativeHeap==10 && nativeCompute==30;
    }
};
}
static void nrRuntimeStateChecks() {
    using namespace nrruntimetest033;
    Commands before;before.NativeNr();
    check(!before.DrawOverlay(),"old raw NR path reproduces stale heap on next overlay draw");
    check(!before.DispatchEffect(),"old raw NR path reproduces stale compute root on next effect");
    for(int initialHeap : {0,10,45})for(int result : {0,1,-1}) {
        Commands commands;commands.apiHeap=commands.nativeHeap=initialHeap;
        int calls=0;
        int actual=nrruntimestate033::Run(&commands,[&]{++calls;commands.NativeNr();return result;});
        check(actual==result && calls==1,"state scope preserves stage return and does not rerun NR");
        check(commands.contract && commands.restores==1,"all stage returns force the runtime cache synchronization");
        check(commands.DrawOverlay(),"overlay uses its own descriptor heap after raw NR");
        check(commands.DispatchEffect(),"next compute effect rebinds its own root signature");
    }
    Commands throwing;
    try {nrruntimestate033::Run(&throwing,[&]() -> int {throwing.NativeNr();throw std::runtime_error("mock stage failure");});}
    catch(const std::runtime_error&) {}
    check(throwing.restores==1 && throwing.DrawOverlay() && throwing.DispatchEffect(),
          "C++ exception path also returns runtime binding ownership");
    Commands disabled;
    check(disabled.restores==0 && disabled.DrawOverlay(),"unentered callback does not change runtime bindings");
}
