#pragma once
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include "diagnostic_policy.h"

// Local RE4 diagnosis only. Configure DRED before device creation; read it
// after removal. No debug layer, GPU validation, registry or driver changes.
// https://learn.microsoft.com/en-us/windows/win32/direct3d12/use-dred
namespace gpufault033 {
inline constexpr bool Requested() { return diagnostic033::FaultCollection; }
inline FILE* Open() {
    wchar_t path[1024]{};const auto n=GetModuleFileNameW(nullptr,path,1024);
    if(!n || n>=1024)return nullptr;
    auto* leaf=wcsrchr(path,L'\\');if(!leaf)return nullptr;
    if(wcscpy_s(leaf+1,1024-size_t(leaf+1-path),L"033-gpu-fault.log"))return nullptr;
    FILE* file=nullptr;_wfopen_s(&file,path,L"a");return file;
}
// RE4 dump 20260907-024829: allocator Reset waits for the device breadcrumb
// mutex while a game CreateCommandList is inside AllocateBreadcrumbBuffer /
// OpenExistingHeapFromAddress. Do not force that instrumentation on the game.
// The user requested removal of the added diagnostics. All three DRED
// facilities are disabled before device creation. NR/FG lifetime fences remain.
// Existing crash logs are left untouched; no per-frame DRED work or new tracer.
template<class Settings> void Configure(Settings* settings) {
    settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_OFF);
    settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_OFF);
    settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_OFF);
}
inline void BeforeDevice(decltype(&D3D12GetDebugInterface) getDebug) {
    if(!diagnostic033::IsRe4())return;
    static std::atomic<bool> attempted{false};if(attempted.exchange(true))return;
    ID3D12DeviceRemovedExtendedDataSettings1* settings=nullptr;
    const HRESULT hr=getDebug?getDebug(__uuidof(ID3D12DeviceRemovedExtendedDataSettings1),reinterpret_cast<void**>(&settings)):E_NOINTERFACE;
    if(SUCCEEDED(hr)&&settings){Configure(settings);settings->Release();}
}
struct HistoryWindow {uint64_t begin,end;};
inline HistoryWindow Window(uint32_t count,uint32_t completed) {
    const uint64_t last=(std::min)(uint64_t(count),uint64_t(completed));
    const uint64_t oldest=count>65536?uint64_t(count)-65536:0;
    const uint64_t begin=(std::max)(oldest,last>8?last-8:0);
    return {begin,(std::min)(uint64_t(count),(std::max)(begin,last)+16)};
}
inline const char* Op(D3D12_AUTO_BREADCRUMB_OP value) {
    switch(value){
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:return "Draw";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:return "DrawIndexed";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:return "Dispatch";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS:return "DispatchRays";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:return "CopyResource";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:return "CopyTexture";
    case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:return "CopyBuffer";
    case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:return "ResourceBarrier";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEMETACOMMAND:return "ExecuteMetaCommand";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA:return "ResolveQuery";
    case D3D12_AUTO_BREADCRUMB_OP_PRESENT:return "Present";
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME:case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1:
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME2:return "DecodeVideo";
    case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES:case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1:return "ProcessVideo";
    default:return "Other";
    }
}
inline void Name(FILE* f,const char* ansi,const wchar_t* wide) {
    if(ansi)std::fprintf(f,"%.240s",ansi);
    else if(wide)std::fprintf(f,"%.240ls",wide);
    else std::fprintf(f,"<unnamed>");
}
inline void Breadcrumbs(FILE* f,const D3D12_AUTO_BREADCRUMB_NODE1* node) {
    unsigned nodes=0;
    for(;node&&nodes<256;node=node->pNext,++nodes){
        const unsigned completed=node->pLastBreadcrumbValue?*node->pLastBreadcrumbValue:0;
        std::fprintf(f,"list=%p queue=%p completed=%u recorded=%u lastKnown=%d listName=",node->pCommandList,node->pCommandQueue,completed,node->BreadcrumbCount,node->pLastBreadcrumbValue!=nullptr);
        Name(f,node->pCommandListDebugNameA,node->pCommandListDebugNameW);
        std::fprintf(f," queueName=");Name(f,node->pCommandQueueDebugNameA,node->pCommandQueueDebugNameW);std::fprintf(f,"\n");
        if(node->pCommandHistory){
            const auto window=Window(node->BreadcrumbCount,completed);
            for(uint64_t i=window.begin;i<window.end;++i){const auto op=node->pCommandHistory[i%65536];
                std::fprintf(f,"  op[%llu]=%u:%s %s\n",i,unsigned(op),Op(op),node->pLastBreadcrumbValue?(i<completed?"completed":"outstanding"):"completion-unknown");}
        }
        for(unsigned i=0;node->pBreadcrumbContexts && i<(std::min)(node->BreadcrumbContextsCount,256u);++i){
            const auto& ctx=node->pBreadcrumbContexts[i];std::fprintf(f,"  context[%u]=",ctx.BreadcrumbIndex);Name(f,nullptr,ctx.pContextString);std::fprintf(f,"\n");
        }
    }
    std::fprintf(f,"breadcrumb_nodes=%u truncated=%d\n",nodes,node!=nullptr);
}
inline void Allocations(FILE* f,const char* label,const D3D12_DRED_ALLOCATION_NODE1* node) {
    unsigned n=0;
    for(;node && n<256;node=node->pNext,++n){std::fprintf(f,"%s type=%u object=%p name=",label,unsigned(node->AllocationType),node->pObject);
        Name(f,node->ObjectNameA,node->ObjectNameW);std::fprintf(f,"\n");}
    std::fprintf(f,"%s_count=%u truncated=%d\n",label,n,node!=nullptr);
}
inline std::atomic<uintptr_t> nrList{0};
inline std::atomic<uint64_t> nrFrame{0};
inline std::atomic<unsigned> nrWidth{0},nrHeight{0},nrPhase{0};
// CPU recording context only, never presented as a GPU completion breadcrumb.
inline void NR(ID3D12GraphicsCommandList* cl,uint64_t frame,unsigned w,unsigned h,unsigned phase) {
    if(!Requested())return;
    nrList.store(reinterpret_cast<uintptr_t>(cl));nrFrame.store(frame);nrWidth.store(w);nrHeight.store(h);nrPhase.store(phase);
}
inline void ReadDevice(FILE* f,ID3D12Device* device) {
    ID3D12DeviceRemovedExtendedData1* dred=nullptr;
    HRESULT hr=device->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData1),reinterpret_cast<void**>(&dred));
    std::fprintf(f,"DRED interface hr=%08X\n",unsigned(hr));std::fflush(f);
    if(FAILED(hr)||!dred)return;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
    hr=dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);std::fprintf(f,"breadcrumbs hr=%08X\n",unsigned(hr));
    if(SUCCEEDED(hr))Breadcrumbs(f,breadcrumbs.pHeadAutoBreadcrumbNode);std::fflush(f);
    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
    hr=dred->GetPageFaultAllocationOutput1(&fault);std::fprintf(f,"pagefault hr=%08X address=%016llX\n",unsigned(hr),fault.PageFaultVA);
    if(SUCCEEDED(hr)){Allocations(f,"existing",fault.pHeadExistingAllocationNode);Allocations(f,"recently_freed",fault.pHeadRecentFreedAllocationNode);}
    dred->Release();std::fflush(f);
}
inline void Removed(ID3D12Device* device,HRESULT reason) {
    if(!Requested()||!device||SUCCEEDED(reason))return;
    static std::atomic<unsigned> reports{0};if(reports.fetch_add(1)>=4)return;
    FILE* f=Open();if(!f)return;
    std::fprintf(f,"REMOVED tick=%llu device=%p reason=%08X CPU_last_NR list=%p frame=%llu size=%ux%u phase=%u (CPU record only)\n",
        GetTickCount64(),device,unsigned(reason),reinterpret_cast<void*>(nrList.load()),nrFrame.load(),nrWidth.load(),nrHeight.load(),nrPhase.load());
    std::fflush(f);
    __try{ReadDevice(f,device);}__except(EXCEPTION_EXECUTE_HANDLER){std::fprintf(f,"DRED read exception=%08X\n",GetExceptionCode());}
    std::fclose(f);
}
}
