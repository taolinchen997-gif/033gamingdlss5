#pragma once
#include <memory>
#include "framegen_flow_dx12.h"
#include "framegen_flow_abi.h"
namespace fgflow033service {
inline void* __cdecl Create(void* device,void* fence){
    try{auto p=std::make_unique<framegen033::FlowDx12>();
        if(!p->Initialize(static_cast<ID3D12Device*>(device),static_cast<ID3D12Fence*>(fence)))return nullptr;return p.release();
    }catch(...){return nullptr;}
}
inline int __cdecl Record(void* handle,const fgflow033abi::Record* r,fgflow033abi::Ticket* out){
    if(out)*out={};if(!handle||!r||!out||r->size!=sizeof(*r)||r->version!=fgflow033abi::Version||r->reset>1)return 0;
    auto ticket=static_cast<framegen033::FlowDx12*>(handle)->Record(static_cast<ID3D12GraphicsCommandList*>(r->list),
        static_cast<ID3D12Resource*>(r->previous),static_cast<ID3D12Resource*>(r->current),static_cast<ID3D12Resource*>(r->output),
        r->phase,r->flowWidth,r->flowHeight,r->radius,r->reset!=0,static_cast<ID3D12Resource*>(r->protection));
    *out={ticket.index,0,ticket.generation};return ticket?1:0;
}
inline int __cdecl Submitted(void* handle,fgflow033abi::Ticket t,uint64_t value){return handle&&!t.reserved&&
    static_cast<framegen033::FlowDx12*>(handle)->Submitted({t.index,t.generation},value);}
inline int __cdecl Discarded(void* handle,fgflow033abi::Ticket t){return handle&&!t.reserved&&
    static_cast<framegen033::FlowDx12*>(handle)->Discarded({t.index,t.generation});}
inline int __cdecl Status(void* handle,fgflow033abi::Status* s){
    if(!handle||!s||s->size!=sizeof(*s)||s->version!=fgflow033abi::Version)return 0;
    auto* p=static_cast<framegen033::FlowDx12*>(handle);s->allocationBatches=p->AllocationBatches();s->workingBytes=p->WorkingBytes();return 1;
}
inline int __cdecl Destroy(void* handle){if(!handle)return 1;auto* p=static_cast<framegen033::FlowDx12*>(handle);
    if(!p->Retire())return 0;delete p;return 1;}
inline const fgflow033abi::Api api{sizeof(fgflow033abi::Api),fgflow033abi::Version,Create,Record,Submitted,Discarded,Status,Destroy};
}
extern "C" __declspec(dllexport) const fgflow033abi::Api* __cdecl K033_GetImageFramegen(uint32_t version){
    return version==fgflow033abi::Version?&fgflow033service::api:nullptr;
}
