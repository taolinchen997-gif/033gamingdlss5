#pragma once
#include "render_core_abi.h"
#include "../runtime/include/033_runtime.h"
#include <cstddef>
struct ID3D12Device;struct ID3D12GraphicsCommandList;struct IUnknown;
namespace beta2grade {
struct Ticket {void* slot=nullptr;uint64_t generation=0;};
struct Services {
    Ticket(*begin)(ID3D12Device*,ID3D12GraphicsCommandList*,IUnknown* const*,size_t)=nullptr;
    void(*end)()=nullptr;
    void(*cancel)(Ticket)=nullptr;
    bool(*completed)(Ticket)=nullptr;
    bool(*settings_current)(const K033_Settings&)=nullptr;
    void(*armed)()=nullptr;
};
struct Result {int status=K033_BYPASS;bool recorded=false,output_uncertain=false;};
bool SettingsSame(const K033_Settings&,const K033_Settings&);
bool NeedsGrade(const K033_Settings&);
Result Process(const k033core::Frame&,const K033_Settings&,const Services&);
// Owned presentation list, real RT resource. Deliberately no NGX parameter shim.
struct PresentationFrame {
    void* command=nullptr;void* output=nullptr;
    uintptr_t stream=0;uint64_t generation=0;
    uint32_t encoding=0;float diffuseWhite=1;
    bool(*current)(const PresentationFrame&)=nullptr;
};
Result ProcessPresentation(const PresentationFrame&,const K033_Settings&,const Services&);
void PoisonPresentation(const PresentationFrame&);
// Poll this retired runtime only. Never prepare, wait, force-complete or clear
// an in-use reference. K033_BUSY means external references remain retained.
int RetirePresentation(uintptr_t stream,uint64_t generation);
void Pump();
void Poison(const k033core::Frame&);
bool OutputUncertain(const k033core::Frame&);
// These refer to command recording. A successful status is not screen proof.
struct Status {int result=K033_BYPASS;uint32_t pending=0;uint64_t recorded=0;bool output_uncertain=false;};
Status Read();
}
