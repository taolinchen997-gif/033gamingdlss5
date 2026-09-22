#pragma once
#include "beta2_grade.h"
namespace beta2gradehost {
static thread_local staterestore::GradeEnvelope* active=nullptr;
// The game super-resolution entry records on the game's own frame list,
// the same one the NR host uses, so it keeps the ordinary claim.
static beta2grade::Ticket Begin(ID3D12Device* d,ID3D12GraphicsCommandList* c,IUnknown* const* refs,size_t count){
    auto t=resolveleases::GetTicket(resolveleases::Begin(d,c,refs,count,nullptr,leasewait033::Owner::Grade));return {t.slot,t.generation};
}
// The presentation path records on ReShade's immediate list, whose Reset
// entry differs from the game render list the NR host must use.
static beta2grade::Ticket BeginPresentation(ID3D12Device* d,ID3D12GraphicsCommandList* c,IUnknown* const* refs,size_t count){
    auto t=resolveleases::GetTicket(resolveleases::Begin(d,c,refs,count,nullptr,leasewait033::Owner::GradePresent));return {t.slot,t.generation};
}
static void Cancel(beta2grade::Ticket t){resolveleases::CancelUnrecorded({static_cast<resolveleases::Slot*>(t.slot),t.generation});}
static bool Completed(beta2grade::Ticket t){return resolveleases::Completed({static_cast<resolveleases::Slot*>(t.slot),t.generation});}
static bool SettingsCurrent(const K033_Settings& s){
    const auto now=k033beta2::Grade(carrier::cfg);
    return active&&active->Current()&&now.enabled==s.enabled&&now.style==s.style&&now.exposure==s.exposure&&
        now.contrast==s.contrast&&now.saturation==s.saturation&&now.warmth==s.warmth&&now.tint==s.tint&&
        now.highlights==s.highlights&&now.style_strength==s.style_strength;
}
static void Armed(){if(active)active->Arm();}
static beta2grade::Result Process(const k033core::Frame& frame){
    // Preserve a prior partial failure even when this frame cannot obtain a
    // state envelope or has a different configured arrival state.
    if(beta2grade::OutputUncertain(frame))return {K033_BYPASS,false,true};
    if(carrier::OutputArrivalState()!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS)return {};
    staterestore::GradeEnvelope envelope(static_cast<ID3D12GraphicsCommandList*>(frame.command));
    if(!envelope.Current())return {};
    struct Active {staterestore::GradeEnvelope* previous;Active(staterestore::GradeEnvelope* p):previous(active){active=p;}~Active(){active=previous;}} scope(&envelope);
    const beta2grade::Services services{Begin,resolveleases::End,Cancel,Completed,SettingsCurrent,Armed};
    auto result=beta2grade::Process(frame,k033beta2::Grade(carrier::cfg),services);
    if(!envelope.Finish()){beta2grade::Poison(frame);result.status=K033_BACKEND_ERROR;result.output_uncertain=true;result.recorded=false;}
    return result;
}
}
