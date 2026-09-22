#pragma once
#include "../src/gpu_fault.h"
#include <string>
static void gpuFaultChecks() {
    using namespace gpufault033;
    struct Settings {
        unsigned sequence=0;
        void SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT v){check(v==D3D12_DRED_ENABLEMENT_FORCED_OFF,"RE4 startup must not allocate forced breadcrumb buffers");sequence=sequence*10+1;}
        void SetPageFaultEnablement(D3D12_DRED_ENABLEMENT v){check(v==D3D12_DRED_ENABLEMENT_FORCED_OFF,"RE4 page-fault instrumentation disabled too");sequence=sequence*10+2;}
        void SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT v){check(v==D3D12_DRED_ENABLEMENT_FORCED_OFF,"RE4 breadcrumb context stays disabled with breadcrumb buffers");sequence=sequence*10+3;}
    } settings;
    Configure(&settings);check(settings.sequence==123,"all added DRED instrumentation disabled before device creation");
    for(uint32_t count:{0u,1u,65535u,65536u,65537u,0xffffffffu})for(uint32_t done:{0u,1u,37u,65536u,0xffffffffu}){
        const auto w=Window(count,done);
        check(w.begin<=w.end && w.end<=count && w.end-w.begin<=24,"history window is bounded even at uint maximum");
        check(w.begin>= (count>65536?uint64_t(count)-65536:0),"history never reads overwritten ring entries");
    }
    FILE* f=nullptr;check(tmpfile_s(&f)==0 && f,"CPU diagnostic output can be inspected");if(!f)return;
    D3D12_AUTO_BREADCRUMB_OP history[]={D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE,D3D12_AUTO_BREADCRUMB_OP_DISPATCH,D3D12_AUTO_BREADCRUMB_OP_PRESENT};
    UINT done=1;D3D12_AUTO_BREADCRUMB_NODE1 node{};node.pCommandListDebugNameA="033 CPU fixture";node.BreadcrumbCount=3;node.pLastBreadcrumbValue=&done;node.pCommandHistory=history;
    Breadcrumbs(f,&node);Allocations(f,"existing",nullptr);
    D3D12_DRED_ALLOCATION_NODE1 allocation{};allocation.ObjectNameA="033 NR full";allocation.AllocationType=D3D12_DRED_ALLOCATION_TYPE_RESOURCE;
    Allocations(f,"recently_freed",&allocation);std::rewind(f);
    std::string result;char buffer[512];while(std::fgets(buffer,sizeof(buffer),f))result+=buffer;std::fclose(f);
    check(result.find("op[0]=9:CopyResource completed")!=std::string::npos,"last completed operation printed correctly");
    check(result.find("op[1]=6:Dispatch outstanding")!=std::string::npos,"first outstanding operation distinguished from completed");
    check(result.find("033 NR full")!=std::string::npos && result.find("recently_freed_count=1")!=std::string::npos,"freed resource evidence survives report formatting");
    check(result.find("existing_count=0")!=std::string::npos,"missing allocation evidence is explicit");
    check(!Requested(),"CPU test process does not enable RE4 diagnostics or create a GPU device");
}
