#pragma once
#include "nr_game_input_abi.h"
#include "nr_contract.h"
#include "nr_game_input_diagnostic.h"
#include <cmath>
#include <array>
#include <cstddef>
#include <utility>
namespace nrgame033 {
inline bool NeedsReadTransition(uint32_t state){return state!=0x40u;}
enum class GuideSubresource : uint8_t { Whole, DepthPlane0 };
// A read plan is not format admission. The existing diagnostic gate still
// rejects this source until the separately reviewed normalization is wired.
inline GuideSubresource DepthReadPlan(const D3D12_RESOURCE_DESC& d){
    return d.Format==DXGI_FORMAT_R32G8X24_TYPELESS&&d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&
        d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&
        !(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)?GuideSubresource::DepthPlane0:GuideSubresource::Whole;
}
inline unsigned BuildReadTransitions(ID3D12Resource* depth,ID3D12Resource* motion,
        uint32_t depthState,uint32_t motionState,GuideSubresource depthPlan,
        D3D12_RESOURCE_BARRIER (&before)[2],D3D12_RESOURCE_BARRIER (&reverse)[2]){
    ID3D12Resource* resources[]={depth,motion};const uint32_t states[]={depthState,motionState};
    unsigned count=0;
    for(unsigned i=0;i<2;++i){if(!NeedsReadTransition(states[i]))continue;
        auto& b=before[count];b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        const UINT subresource=i==0&&depthPlan==GuideSubresource::DepthPlane0?0u:D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition={resources[i],subresource,D3D12_RESOURCE_STATES(states[i]),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        reverse[count]=b;std::swap(reverse[count].Transition.StateBefore,reverse[count].Transition.StateAfter);++count;
    }
    return count;
}
struct InputExports {uintptr_t acquire=0,acquire2=0,describe=0;};
template<class Match> bool SelectInputExports(const Match& base,const Match& extended,const Match& descriptor,InputExports& out){
    out={};
    if(base.ambiguous||!base.module||!base.address||extended.ambiguous||descriptor.ambiguous||
       (extended.module&&extended.module!=base.module)||(descriptor.module&&descriptor.module!=base.module))return false;
    out={base.address,extended.address,descriptor.address};return true;
}
inline bool Extent(uint64_t w,uint32_t h){return w&&h&&w<=8192&&h<=8192;}
inline bool FullTargetRect(float left,float top,float right,float bottom,uint64_t w,uint32_t h){
    return Extent(w,h)&&left==0.f&&top==0.f&&right==float(w)&&bottom==float(h);
}
inline Result Check(const Frame& f,uint32_t w,uint32_t h,bool device,bool queue,
                    bool states,uint64_t consumed) {
    if(!f.depth||!f.motion||!f.queue)return Result::Missing;
    if(nrdiag033::FrameIssues(f))return Result::Contract;
    if(!w||!h||w>8192||h>8192||f.width!=w||f.height!=h)return Result::Extent;
    if(!device)return Result::Device;
    if(!queue)return Result::Queue;
    if(f.serial<=consumed)return Result::Stale;
    if(!states)return Result::State;
    return Result::Ready;
}
inline Result Check(const Frame2& f,uint32_t w,uint32_t h,bool device,bool queue,
                    bool states,uint64_t consumed){
    if(nrdiag033::OuterIssues(f))return Result::Contract;
    if(!Extent(w,h)||f.outputWidth!=w||f.outputHeight!=h||
       !Extent(f.frame.width,f.frame.height)||!Extent(f.motionWidth,f.motionHeight))return Result::Extent;
    // Current scene NDC scale is defined in depth/render pixels. The model's
    // normalization with a differently sized motion texture is not verified.
    // Keep the true dimensions, but do not guess a conversion or admit it.
    if(f.motionWidth!=f.frame.width||f.motionHeight!=f.frame.height)return Result::MotionExtent;
    // V2 supplies independently verified full semantic resources. Reuse all
    // original source, flags, serial, device, queue and state checks; only the
    // false equation between output and guide dimensions is removed.
    return Check(f.frame,f.frame.width,f.frame.height,device,queue,states,consumed);
}
inline bool ResourceExtents(const Frame2& f,uint64_t dw,uint32_t dh,uint64_t mw,uint32_t mh){
    return dw==f.frame.width&&dh==f.frame.height&&mw==f.motionWidth&&mh==f.motionHeight;
}
inline nrcontract::Guides GuideRects(const Frame& f,uint32_t mw,uint32_t mh){
    nrcontract::Guides result;result.depth={0,0,f.width,f.height};result.motion={0,0,mw,mh};return result;
}
// Only the small set of explicitly identified input resources is watched.
// Recording order is not submission order. Never publish a recorded barrier
// until its command list is actually offered to the selected queue.
class StateBook {
public:
    static constexpr size_t Resources=8,Lists=256;
private:
    struct Resource {uintptr_t id=0;uint64_t generation=0,age=0;uint32_t state=0;bool known=false;GuideSubresource plan=GuideSubresource::Whole;};
    struct End {uint64_t generation=0;uint32_t state=0;bool seen=false,valid=false;};
    struct List {uintptr_t id=0;std::array<End,Resources> end{};};
    std::array<Resource,Resources> resources{};
    std::array<List,Lists> lists{};
    uint64_t clock=0,generation=0;
    uintptr_t queue=0;
    bool overflow=false;
public:
    static bool State(uint32_t usage,uint32_t& native) {
        if(usage==0x80000000u){native=0;return true;} // ReShade GENERAL = DX12 COMMON.
        constexpr uint32_t allowed=0x4|0x8|0x10|0x20|0x40|0x80|0x400|0x800;
        if(!usage||(usage&~allowed))return false;
        const uint32_t writes=usage&(0x4|0x8|0x10|0x400);
        if(writes && (usage!=writes || (writes&(writes-1))))return false;
        native=usage;return true;
    }
    void Invalidate(){for(auto& r:resources)r.known=false;for(auto& l:lists)l={};overflow=false;}
    void Watch(uintptr_t id,uintptr_t submitQueue,GuideSubresource plan=GuideSubresource::Whole){
        if(!id||!submitQueue)return;
        if(queue!=submitQueue){Invalidate();queue=submitQueue;}
        for(auto& r:resources)if(r.id==id){
            if(r.plan!=plan)r={id,++generation,++clock,0,false,plan};else r.age=++clock;
            return;
        }
        auto* slot=&resources[0];
        for(auto& r:resources)if(!r.id){slot=&r;break;}else if(r.age<slot->age)slot=&r;
        *slot={id,++generation,++clock,0,false,plan};
    }
    bool Watched(uintptr_t id)const{for(const auto& r:resources)if(r.id==id&&id)return true;return false;}
    std::array<uintptr_t,Resources> Ids()const{std::array<uintptr_t,Resources> ids{};for(size_t i=0;i<Resources;++i)ids[i]=resources[i].id;return ids;}
    void Forget(uintptr_t id){for(auto& r:resources)if(r.id==id)r={};}
    void Reset(uintptr_t list){for(auto& l:lists)if(l.id==list){l={};return;}}
    void Barrier(uintptr_t list,uintptr_t resource,uint32_t usage,uint32_t flags=0,
                 uint32_t subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES){
        if(!list)return;
        size_t index=Resources;
        for(size_t i=0;i<Resources;++i)if(resources[i].id==resource&&resource){index=i;break;}
        if(index==Resources)return;
        // In the exact one-mip/one-array depth plan, subresource 1 is only
        // stencil. Its transitions neither establish nor poison plane 0.
        if(resources[index].plan==GuideSubresource::DepthPlane0&&subresource==1)return;
        // Keep the former single-plane behavior for every other read plan.
        // Out-of-plan subresources cannot establish a known arrival state.
        if(subresource!=0&&subresource!=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)flags=3;
        List* found=nullptr;
        for(auto& l:lists)if(l.id==list){found=&l;break;}
        if(!found)for(auto& l:lists)if(!l.id){found=&l;l.id=list;break;}
        if(!found){overflow=true;for(auto& r:resources)r.known=false;return;}
        auto& end=found->end[index];end.generation=resources[index].generation;end.seen=true;
        end.valid=(flags==0||flags==2)&&State(usage,end.state); // BEGIN_ONLY is not readable; END_ONLY completes it.
    }
    void Submit(uintptr_t list,uintptr_t submitQueue){
        if(overflow){Invalidate();return;}
        for(const auto& l:lists)if(l.id==list){
            for(size_t i=0;i<Resources;++i){const auto& end=l.end[i];auto& r=resources[i];
                if(end.seen&&r.id&&end.generation==r.generation){
                    r.known=end.valid&&submitQueue==queue;r.state=end.state;
                }
            }return;
        }
    }
    bool Read(uintptr_t id,uint32_t& state)const{
        if(overflow)return false;
        for(const auto& r:resources)if(r.id==id&&id&&r.known){state=r.state;return true;}
        return false;
    }
};
}
