#pragma once
#include "beta2_grade_color.h"
#include "../../src/beta2_grade_present_policy.h"

#include "inline_grade_program.h"
#include "grade_use_retirement.h"
#include "beta2_grade_policy.h"
#include "scene_grade_shader.h"
#include "033_scene_grade.h"
#include <functional>
namespace k033 {
// Uses the existing list Reset/Execute/fence ledger. No private queue, wait,
// SDK model, additional SR or final-swapchain substitution is involved.
class Ngx12Grade {
    using Resource=Microsoft::WRL::ComPtr<ID3D12Resource>;
    struct Resources {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        struct Slot {Resource original,graded;Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;} slots[3];
        uint32_t width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;bool ready=false,poisoned=false;
    };
    struct Use : GradeUseRetirement<Ngx12Color,beta2grade::Services,beta2grade::Ticket> {
        std::shared_ptr<Resources> resources;
        unsigned index=0;uint64_t serial=0;
    };
    std::shared_ptr<Resources> resources;
    std::array<std::shared_ptr<Use>,3> uses{};
    uint64_t serial=0;
    static void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* p,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&barrier);
    }
    struct Record {
        std::shared_ptr<Use> use;ID3D12GraphicsCommandList* list;
        const std::function<bool()>& current;
        int validate(){return use->resources->ready&&current()?K033_OK:K033_BYPASS;}
        bool arm(){
            if(!current())return false;
            auto& r=*use->resources;auto& slot=r.slots[use->index];
            IUnknown* refs[]={use->source.output.Get(),r.device.Get(),r.root.Get(),r.pso.Get(),
                slot.original.Get(),slot.graded.Get(),slot.heap.Get()};
            use->ticket=use->api.begin(r.device.Get(),list,refs,7);
            if(!use->ticket.slot)return false;
            // Begin retains COM references; those foreign AddRef calls may reenter.
            if(!current()){
                // Cancellation can be refused if execution was observed. Keep
                // the ticket even though no grade command has been armed.
                use->api.cancel(use->ticket);return false;
            }
            use->api.armed();
            std::lock_guard<std::mutex> lock(use->mutex);use->armed=true;return true;
        }        void capture(){
            auto& slot=use->resources->slots[use->index];const auto& rect=use->source.rect;
            const gradepresentpolicy::Transitions<D3D12_RESOURCE_STATES> plan{use->source.arrival,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST};
            transition(list,use->source.output.Get(),plan.captureBefore(),plan.captureAfter());
            transition(list,slot.original.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=use->source.output.Get();to.pResource=slot.original.Get();
            from.Type=to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_BOX box{rect.x,rect.y,0,rect.x+rect.width,rect.y+rect.height,1};
            list->CopyTextureRegion(&to,0,0,0,&from,&box);
            transition(list,slot.original.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transition(list,use->source.output.Get(),plan.restoreCaptureBefore(),plan.restoreCaptureAfter());
        }
        void grade(const K033_Settings& settings){
            auto& r=*use->resources;auto& slot=r.slots[use->index];
            transition(list,slot.graded.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ID3D12DescriptorHeap* heaps[]={slot.heap.Get()};list->SetDescriptorHeaps(1,heaps);
            list->SetComputeRootSignature(r.root.Get());list->SetPipelineState(r.pso.Get());
            list->SetComputeRootDescriptorTable(0,slot.heap->GetGPUDescriptorHandleForHeapStart());
            const auto constants=scene_grade_constants(r.width,r.height,settings,use->source.encoding,use->source.diffuseWhite);
            list->SetComputeRoot32BitConstants(1,16,&constants,0);list->Dispatch((r.width+7)/8,(r.height+7)/8,1);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=slot.graded.Get();
            list->ResourceBarrier(1,&barrier);
            transition(list,slot.graded.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        void copyback(){
            auto& slot=use->resources->slots[use->index];auto& rect=use->source.rect;
            const gradepresentpolicy::Transitions<D3D12_RESOURCE_STATES> plan{use->source.arrival,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST};
            transition(list,slot.graded.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(list,use->source.output.Get(),plan.writeBefore(),plan.writeAfter());
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=slot.graded.Get();to.pResource=use->source.output.Get();
            from.Type=to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_BOX box{0,0,0,rect.width,rect.height,1};
            list->CopyTextureRegion(&to,rect.x,rect.y,0,&from,&box);
            transition(list,use->source.output.Get(),plan.restoreWriteBefore(),plan.restoreWriteAfter());
            transition(list,slot.graded.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            use->recorded=true;
        }
        void failed(){
            // Completion retires lifetime, not an interrupted private resource
            // transition. Never recycle this generation in an assumed NPSSR
            // state; prepare may replace it only after every Use retires.
            use->resources->poisoned=true;use->resources->ready=false;
        }
    };
public:
    void poll(){for(auto& use:uses)if(use&&use->retire()==K033_OK)use.reset();}
    bool idle()const {for(const auto& use:uses)if(use)return false;return true;}
    bool ready(ID3D12Device* device,uint32_t width,uint32_t height,DXGI_FORMAT format)const {
        return resources&&resources->ready&&!resources->poisoned&&resources->width==width&&resources->height==height&&resources->format==format&&
            ngx12_detail::same(resources->device.Get(),device);
    }
    // Maintenance only; no command recording/submission and no game pointers.
    int prepare(ID3D12Device* device,uint32_t width,uint32_t height,DXGI_FORMAT format) {
        poll();if(ready(device,width,height,format))return K033_OK;if(!idle())return K033_BUSY;
        if(!device||!width||!height||width>16384||height>16384||device->GetNodeCount()!=1)return K033_UNSUPPORTED;
        auto r=std::make_shared<Resources>();r->device=device;r->width=width;r->height=height;r->format=format;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
        if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
           !(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)||!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))return K033_UNSUPPORTED;
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable={2,ranges};parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants={0,0,16};D3D12_ROOT_SIGNATURE_DESC signature{2,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
        Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
        if(FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||
           FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&r->root))))return K033_BACKEND_ERROR;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=r->root.Get();pipeline.CS={k033_scene_grade,sizeof(k033_scene_grade)};
        if(FAILED(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&r->pso))))return K033_BACKEND_ERROR;
        D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=width;image.Height=height;
        image.DepthOrArraySize=image.MipLevels=1;image.SampleDesc.Count=1;image.Format=format;
        image.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto bytes=device->GetResourceAllocationInfo(0,1,&image).SizeInBytes;
        if(!bytes||bytes==UINT64_MAX||bytes>(512ull*1024*1024)/6)return K033_UNSUPPORTED;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for(auto& slot:r->slots) {
            if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&slot.original)))||
               FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&slot.graded))))return K033_BACKEND_ERROR;
            D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
            if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&slot.heap))))return K033_BACKEND_ERROR;
            auto handle=slot.heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format=image.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(slot.original.Get(),&srv,handle);handle.ptr+=step;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=image.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(slot.graded.Get(),nullptr,&uav,handle);
        }
        r->ready=true;resources=std::move(r);return K033_OK;
    }
    int record(const Ngx12Color& source,const K033_Settings& settings,ID3D12GraphicsCommandList* list,
        const beta2grade::Services& api,bool& possibly_recorded,const std::function<bool()>& current) {
        possibly_recorded=false;
        if(!valid(settings))return K033_INVALID;if(!needs_grade(grade(settings)))return K033_BYPASS;
        poll();if(!ready(source.device.Get(),source.rect.width,source.rect.height,source.format))return K033_BUSY;
        unsigned index=0;while(index<3&&uses[index])++index;if(index==3||serial==UINT64_MAX)return K033_BUSY;
        auto use=std::make_shared<Use>();use->resources=resources;use->source=source;use->index=index;use->serial=++serial;use->api=api;
        uses[index]=use;
        Record recorder{use,list,current};const int result=inline_grade_record(recorder,settings);
        possibly_recorded=use->possibly_recorded();if(use->ticket.slot)api.end();return result;
    }
};
}
