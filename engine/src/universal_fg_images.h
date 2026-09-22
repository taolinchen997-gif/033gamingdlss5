// Real-frame history and colour conversion for image-only frame generation.
// The swapchain owner executes all recordings on its game queue, signals the
// completion fence after submission, and calls Submitted once per callback.
#pragma once
#include "framegen_flow_dx12.h"
#include "universal_fg_policy.h"
namespace ufg033 {
using Microsoft::WRL::ComPtr;
enum class RecordReason : uint32_t { output, warmup, busy, frameGap, lateFrame, flowUnavailable, incompatible, lostDevice };
static constexpr char colourShader[]=R"(
Texture2D<float4> Src:register(t0);Texture2D<float4> Prev:register(t1);RWTexture2D<float4> Dst:register(u0);
cbuffer C:register(b0){uint w,h,transfer,protect;};
float3 srgbToLinear(float3 x){return lerp(x/12.92,pow(max(0,(x+.055)/1.055),2.4),step(.04045,x));}
float3 linearToSrgb(float3 x){return lerp(x*12.92,1.055*pow(max(0,x),1/2.4)-.055,step(.0031308,x));}
float3 pqToLinear(float3 x){float3 p=pow(saturate(x),1/(2523.0/32));return pow(max(p-3424.0/4096,0)/max(2413.0/128-2392.0/128*p,1e-6),1/(2610.0/16384))*125;}
float3 linearToPq(float3 x){float3 p=pow(max(0,x/125),2610.0/16384);return pow((3424.0/4096+2413.0/128*p)/(1+2392.0/128*p),2523.0/32);}
[numthreads(8,8,1)]void capture(uint3 p:SV_DispatchThreadID){if(p.x>=w||p.y>=h)return;float4 c=Src.Load(int3(p.xy,0));
 c.rgb=transfer==0?srgbToLinear(c.rgb):transfer==1?pqToLinear(c.rgb):c.rgb;Dst[p.xy]=c;}
[numthreads(8,8,1)]void resolve(uint3 p:SV_DispatchThreadID){if(p.x>=w||p.y>=h)return;float4 c=Src.Load(int3(p.xy,0));
 c.rgb=transfer==0?linearToSrgb(c.rgb):transfer==1?linearToPq(c.rgb):c.rgb;Dst[p.xy]=c;}
[numthreads(8,8,1)]void mask(uint3 p:SV_DispatchThreadID){if(p.x>=w||p.y>=h)return;int2 xy=p.xy;
 float3 c=Src.Load(int3(xy,0)).rgb,old=Prev.Load(int3(xy,0)).rgb;
 float delta=max(max(abs(c.r-old.r),abs(c.g-old.g)),abs(c.b-old.b));float edge=0;
 [unroll]for(int k=-1;k<=1;k+=2){float3 n=Src.Load(int3(clamp(xy+int2(k,0),int2(0,0),int2(w,h)-1),0)).rgb;
 edge=max(edge,length(c-n));n=Src.Load(int3(clamp(xy+int2(0,k),int2(0,0),int2(w,h)-1),0)).rgb;edge=max(edge,length(c-n));}
 // Conservative stationary high-contrast protection. This is not a semantic
 // HUD detector; confidence rejection in the flow shader remains necessary.
 Dst[p.xy]=float4(protect!=0?saturate(1-delta*200)*saturate((edge-.08)*12):0,0,0,0);}
[numthreads(1,1,1)]void scene(uint3 p:SV_DispatchThreadID){uint hits=0;
 [loop]for(uint y=0;y<9;++y)[loop]for(uint x=0;x<16;++x){int2 xy=int2((x+.5)*w/16,(y+.5)*h/9);
 float3 a=Src.Load(int3(xy,0)).rgb,b=Prev.Load(int3(xy,0)).rgb;
 float diff=length(a-b)/max(1,max(length(a),length(b)));if(diff>.6)++hits;}
 Dst[int2(0,0)]=float4(hits>108?1:0,0,0,0);}
)";
class Images {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12Fence> fence;
    ComPtr<IUnknown> deviceIdentity;identity033::Result identityResult;
    const char* identityFailure=nullptr;uint64_t normalizedDevices=0;
    bool Same(ID3D12DeviceChild* child,const char* name){
        identityResult=identity033::Child(deviceIdentity.Get(),child);
        if(identityResult.normalized)++normalizedDevices;
        if(!identityResult.equal)identityFailure=name;
        return identityResult.equal;
    }
    ComPtr<ID3D12RootSignature> root;std::array<ComPtr<ID3D12PipelineState>,4> pipelines;
    struct Slot {ComPtr<ID3D12DescriptorHeap> heap;uint64_t fence=0;};std::array<Slot,3> slots;
    ComPtr<ID3D12Resource> history[2],middle[2],protection,sceneCut;
    framegen033::FlowDx12 flow;framegen033::Ticket ticket;
    UINT width=0,height=0,step=0,write=0,nextSlot=0,pendingSlot=3;
    uint64_t lastId=0,lastTick=0,allocations=0;bool have=false,pending=false;
    RecordReason reason=RecordReason::warmup;uint64_t interval=0;
    static void Barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
        if(a==b)return;D3D12_RESOURCE_BARRIER d{};d.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;d.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&d);}
    bool Shader(const char* entry,ComPtr<ID3D12PipelineState>& target){ComPtr<ID3DBlob> code,error;
        if(FAILED(D3DCompile(colourShader,sizeof(colourShader)-1,"033 FG colours",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)))return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.CS={code->GetBufferPointer(),code->GetBufferSize()};return SUCCEEDED(device->CreateComputePipelineState(&d,IID_PPV_ARGS(&target)));}
    bool Buffers(UINT w,UINT h){if(width==w&&height==h)return true;if(!Idle())return false;
        ComPtr<ID3D12Resource> next[6];D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
        d.MipLevels=d.DepthOrArraySize=d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(int i=0;i<6;++i){d.Format=i>=4?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;if(i==5)d.Width=d.Height=1;
            if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&next[i]))))return false;}
        history[0]=next[0];history[1]=next[1];middle[0]=next[2];middle[1]=next[3];protection=next[4];sceneCut=next[5];width=w;height=h;have=false;write=0;++allocations;return true;}
    void Pass(ID3D12GraphicsCommandList* l,UINT slot,UINT pass,UINT shader,ID3D12Resource* src,ID3D12Resource* prev,ID3D12Resource* out,UINT transfer,bool protect){
        auto handle=slots[slot].heap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=size_t(pass)*3*step;
        ID3D12Resource* inputs[2]={src,prev};for(UINT i=0;i<2;++i){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=inputs[i]?inputs[i]->GetDesc().Format:DXGI_FORMAT_R16G16B16A16_FLOAT;
            v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Texture2D.MipLevels=1;
            auto h=handle;h.ptr+=size_t(i)*step;device->CreateShaderResourceView(inputs[i],&v,h);}
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=out->GetDesc().Format;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;auto uh=handle;uh.ptr+=size_t(2)*step;device->CreateUnorderedAccessView(out,nullptr,&u,uh);
        auto* heap=slots[slot].heap.Get();l->SetDescriptorHeaps(1,&heap);l->SetComputeRootSignature(root.Get());auto gh=heap->GetGPUDescriptorHandleForHeapStart();gh.ptr+=UINT64(pass)*3*step;
        l->SetComputeRootDescriptorTable(0,gh);UINT values[4]={width,height,transfer,protect?1u:0u};l->SetComputeRoot32BitConstants(1,4,values,0);l->SetPipelineState(pipelines[shader].Get());l->Dispatch(shader==3?1:(width+7)/8,shader==3?1:(height+7)/8,1);
    }
public:
    bool Initialize(ID3D12Device* d,ID3D12Fence* f){device=d;fence=f;deviceIdentity=identity033::Canonical(d);if(!deviceIdentity||!flow.Initialize(d,f))return false;
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2}};
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,4};
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;ComPtr<ID3DBlob> blob,error;
        if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||FAILED(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)))||
           !Shader("capture",pipelines[0])||!Shader("mask",pipelines[1])||!Shader("resolve",pipelines[2])||!Shader("scene",pipelines[3]))return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=15;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        for(auto& s:slots)if(FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&s.heap))))return false;step=d->GetDescriptorHandleIncrementSize(hd.Type);return true;}
    bool Idle()const{if(pending||!fence)return false;auto n=fence->GetCompletedValue();if(n==UINT64_MAX)return false;for(const auto& s:slots)if(s.fence>n)return false;return true;}
    void ResetHistory(){have=false;}
    bool Retire(){if(!Idle()||!flow.Retire())return false;history[0].Reset();history[1].Reset();middle[0].Reset();middle[1].Reset();protection.Reset();sceneCut.Reset();width=height=0;have=false;return true;}
    // 1 = output, 0 = warmup/busy, -1 = incompatible resources/format.
    int Record(ID3D12GraphicsCommandList* list,ID3D12Resource* colour,ID3D12Resource* output,D3D12_RESOURCE_STATES colourState,
               D3D12_RESOURCE_STATES outputState,UINT transfer,uint64_t id,bool reset,bool protect=true){
        ID3D12Resource* outputs[2]={output,nullptr};D3D12_RESOURCE_STATES states[2]={outputState,outputState};
        return RecordBatch(list,colour,outputs,states,1,colourState,transfer,id,reset,protect);
    }
    // Capture and advance real history exactly once, regardless of multiplier.
    int RecordBatch(ID3D12GraphicsCommandList* list,ID3D12Resource* colour,ID3D12Resource* const* outputs,
               const D3D12_RESOURCE_STATES* outputStates,UINT count,D3D12_RESOURCE_STATES colourState,
               UINT transfer,uint64_t id,bool reset,bool protect=true){
        if(!outputs||!outputStates||count<1||count>2)return -1;auto* output=outputs[0];
        reason=RecordReason::busy;
        if(pending)return 0;reason=RecordReason::incompatible;
        if(!list||!colour||!output||transfer>2)return -1;
        // Validate before allocating history or recording even the capture pass.
        // Same adapter is not sufficient: different D3D12 devices remain invalid.
        identityFailure=nullptr;
        if(!Same(list,"device-list")||!Same(colour,"device-colour")||!Same(output,"device-output")||
           (count==2&&!Same(outputs[1],"device-second-output")))return -1;
        auto d=colour->GetDesc(),o=output->GetDesc();
        if(d.Width!=o.Width||d.Height!=o.Height||d.Format!=o.Format||d.SampleDesc.Count!=1||d.DepthOrArraySize!=1||d.MipLevels!=1||
           (d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&d.Format!=DXGI_FORMAT_R10G10B10A2_UNORM&&d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)||
           !(o.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return -1;
        if(count==2){if(!outputs[1]||outputs[1]==output||outputs[1]==colour)return -1;
            auto extra=outputs[1]->GetDesc();if(extra.Width!=o.Width||extra.Height!=o.Height||extra.Format!=o.Format||extra.MipLevels!=1||
                extra.SampleDesc.Count!=1||extra.DepthOrArraySize!=1||!(extra.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return -1;}
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{d.Format};if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))return -1;
        auto complete=fence->GetCompletedValue();if(complete==UINT64_MAX){reason=RecordReason::lostDevice;return -1;}
        reason=RecordReason::busy;UINT slot=nextSlot%3;if(slots[slot].fence>complete)return 0;
        if(!Buffers(UINT(d.Width),d.Height))return 0;
        const uint64_t now=GetTickCount64();const bool paired=have&&!reset&&id==lastId+1&&now>=lastTick&&now-lastTick<=100;
        interval=lastTick&&now>=lastTick?now-lastTick:0;
        reason=!have||reset?RecordReason::warmup:id!=lastId+1?RecordReason::frameGap:!paired?RecordReason::lateFrame:RecordReason::flowUnavailable;
        auto* current=history[write].Get();auto* previous=history[write^1].Get();
        Barrier(list,colour,colourState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Barrier(list,current,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Pass(list,slot,0,0,colour,nullptr,current,transfer,protect);Barrier(list,current,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list,colour,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,colourState);
        int generated=0;ticket={};
        if(paired){
            Barrier(list,sceneCut.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Pass(list,slot,3,3,current,previous,sceneCut.Get(),transfer,protect);
            Barrier(list,sceneCut.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(list,protection.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Pass(list,slot,1,1,current,previous,protection.Get(),transfer,protect);
            Barrier(list,protection.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ID3D12Resource* intermediate[2]={middle[0].Get(),middle[1].Get()};
            for(UINT i=0;i<count;++i)Barrier(list,intermediate[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            UINT fw=std::min(width,640u),fh=std::max(1u,UINT(uint64_t(height)*fw/width));
            if(fh>640){fh=640;fw=std::max(1u,UINT(uint64_t(width)*fh/height));}
            const float phases[2]={ufgpolicy033::Phase(0,count),ufgpolicy033::Phase(1,count)};
            ticket=flow.RecordBatch(list,previous,current,intermediate,phases,count,fw,fh,4,false,protection.Get(),sceneCut.Get());
            for(UINT i=0;i<count;++i){Barrier(list,intermediate[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if(ticket){Barrier(list,outputs[i],outputStates[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    Pass(list,slot,i==0?2:4,2,intermediate[i],nullptr,outputs[i],transfer,protect);
                    Barrier(list,outputs[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,outputStates[i]);}}
            if(ticket){generated=int(count);reason=RecordReason::output;}
        }
        pending=true;pendingSlot=slot;++nextSlot;have=true;write^=1;lastId=id;lastTick=now;return generated;
    }
    bool Pending()const{return pending;}
    bool Submitted(uint64_t value){if(!pending)return true;slots[pendingSlot].fence=value;pending=false;pendingSlot=3;return !ticket||flow.Submitted(ticket,value);}
    uint64_t Allocations()const{return allocations+flow.AllocationBatches();}
    RecordReason LastReason()const{return reason;}
    const char* FlowRejection()const{return identityFailure?identityFailure:reason==RecordReason::flowUnavailable?flow.LastRejection():"none";}
    uint64_t NormalizedDevices()const{return normalizedDevices;}
    const identity033::Result& LastDeviceCheck()const{return identityResult;}
    uint64_t Interval()const{return interval;}
    uint64_t Bytes()const{return uint64_t(width)*height*36+(sceneCut?4:0)+flow.WorkingBytes();}
};
}
