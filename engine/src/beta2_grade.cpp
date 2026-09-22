#include <d3dcompiler.h>
#include "../runtime/src/ngx12_grade_native.h"
#include "../runtime/src/beta2_grade_policy.h"
namespace beta2grade {
bool SettingsSame(const K033_Settings& a,const K033_Settings& b){return k033::inline_grade_same(a,b);}
bool NeedsGrade(const K033_Settings& s){return k033::needs_grade(k033::grade(s));}
namespace {
struct Owner {
    uintptr_t stream=0;bool presentation=false;FaultState fault;k033::Ngx12Grade grade;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    uint32_t width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_R16G16B16A16_FLOAT;int prepared=K033_BUSY;
};
std::array<Owner,8> owners;
std::mutex mutex;
Status status;
}
namespace {
bool ServicesValid(const Services& api){return api.begin&&api.end&&api.cancel&&api.completed&&api.settings_current&&api.armed;}
template<class Reader,class Current>
Result ProcessColor(uintptr_t stream,uint64_t generation,bool presentation,ID3D12GraphicsCommandList* list,
    const K033_Settings& settings,const Services& api,Reader&& reader,Current&& stillCurrent){
    Result result;
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock()){result.status=K033_BUSY;return result;}
    if(!stream||!generation||!list||!ServicesValid(api)||!stillCurrent())return result;
    Owner* owner=nullptr;
    for(auto& item:owners){item.grade.poll();if(item.stream==stream&&item.presentation==presentation)owner=&item;}
    if(!owner)for(auto& item:owners)if(!item.stream){item.stream=stream;item.presentation=presentation;owner=&item;break;}
    if(!owner)for(auto& item:owners)if(recyclable(item.grade.idle(),item.fault)){
        item=Owner{};item.stream=stream;item.presentation=presentation;owner=&item;break;
    }
    if(!owner){result.status=K033_BUSY;return result;}
    owner->fault.feature(generation);result.output_uncertain=owner->fault.output_uncertain;
    if(!owner->fault.admissible())return result;
    if(!k033::valid(settings)){result.status=K033_INVALID;return result;}
    if(!k033::needs_grade(k033::grade(settings)))return result;
    k033::Ngx12Color color;if(!reader(color)){result.status=K033_UNSUPPORTED;return result;}
    if(owner->width!=color.rect.width||owner->height!=color.rect.height||owner->format!=color.format||
        !k033::ngx12_detail::same(owner->device.Get(),color.device.Get()))owner->prepared=K033_BUSY;
    owner->device=color.device;owner->width=color.rect.width;owner->height=color.rect.height;owner->format=color.format;
    auto current=[&](){k033::Ngx12Color fresh;
        return stillCurrent()&&api.settings_current(settings)&&reader(fresh)&&k033::beta2_color_same(color,fresh)&&
            api.settings_current(settings)&&stillCurrent();};
    bool armed=false;
    result.status=owner->grade.record(color,settings,list,api,armed,current);
    if(result.status==K033_BUSY&&owner->prepared<0)result.status=owner->prepared;
    owner->fault.outcome(result.status,armed);result.output_uncertain=owner->fault.output_uncertain;
    result.recorded=result.status==K033_OK;status.result=result.status;status.output_uncertain=result.output_uncertain;
    if(result.recorded)++status.recorded;return result;
}
}
Result Process(const k033core::Frame& frame,const K033_Settings& settings,const Services& api){
    if(!k033core::Valid(&frame)||!frame.current)return {};
    return ProcessColor(frame.stream,frame.featureGeneration,false,static_cast<ID3D12GraphicsCommandList*>(frame.command),settings,api,
        [&](k033::Ngx12Color& out){return k033::beta2_color_read(frame,out);},[&]{return frame.current(&frame)!=0;});
}
Result ProcessPresentation(const PresentationFrame& frame,const K033_Settings& settings,const Services& api){
    if(!frame.current)return {};
    return ProcessColor(frame.stream,frame.generation,true,static_cast<ID3D12GraphicsCommandList*>(frame.command),settings,api,
        [&](k033::Ngx12Color& out){return k033::beta2_present_color_read(frame,out);},[&]{return frame.current(frame);});
}
int RetirePresentation(uintptr_t stream,uint64_t generation){
    if(!stream||!generation)return K033_INVALID;
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return K033_BUSY;
    for(auto& owner:owners)if(owner.presentation&&owner.stream==stream&&owner.fault.generation==generation){
        // Completed() inside poll collects only leases with observed GPU
        // completion and reset/discard proof, then Use drops its extra COM ref.
        // Resize notification alone cannot release either reference owner.
        owner.grade.poll();return owner.grade.idle()?K033_OK:K033_BUSY;
    }
    return K033_BYPASS;
}

void Pump(){
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return;
    status.pending=0;
    for(auto& owner:owners){
        // Unconditional polling includes NR off, neutral grade and poisoned O.
        owner.grade.poll();if(!owner.grade.idle())++status.pending;
        if(owner.fault.admissible()&&owner.device&&owner.width&&owner.height)
            owner.prepared=owner.grade.prepare(owner.device.Get(),owner.width,owner.height,owner.format);
    }
}
Status Read(){std::lock_guard<std::mutex> lock(mutex);return status;}
void Poison(const k033core::Frame& frame){
    std::lock_guard<std::mutex> lock(mutex);
    for(auto& owner:owners)if(!owner.presentation&&owner.stream==frame.stream&&owner.fault.generation==frame.featureGeneration)owner.fault.output_uncertain=true;
    status.output_uncertain=true;status.result=K033_BACKEND_ERROR;
}
bool OutputUncertain(const k033core::Frame& frame){
    if(!frame.current||!frame.current(&frame))return true;
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return true;
    for(const auto& owner:owners)if(!owner.presentation&&owner.stream==frame.stream)return owner.fault.uncertain_for(frame.featureGeneration);
    return false;
}
void PoisonPresentation(const PresentationFrame& frame){
    std::lock_guard<std::mutex> lock(mutex);
    for(auto& owner:owners)if(owner.presentation&&owner.stream==frame.stream&&owner.fault.generation==frame.generation)owner.fault.output_uncertain=true;
    status.output_uncertain=true;status.result=K033_BACKEND_ERROR;
}
}
