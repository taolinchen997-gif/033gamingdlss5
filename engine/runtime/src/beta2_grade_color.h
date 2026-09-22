#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <mutex>
#include <cmath>
#include "../../sdk/ngx/nvsdk_ngx.h"
#include "../../sdk/ngx/nvsdk_ngx_params.h"
#include "../../src/beta2_grade.h"
#include "../../src/beta2_grade_present_policy.h"
#include "../../src/d3d12_identity.h"
namespace k033 {
namespace ngx12_detail {
inline bool same(IUnknown* a,IUnknown* b){
    return identity033::Equal(a,b);
}
}
// Adapted from S54 v5 ngx12_color_reader: typed O, HDR FP16 and one exact
// successful Evaluate. Grade needs neither depth/MV nor an NR model.
struct Ngx12Color {
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Resource> output;
    struct Rect {uint32_t x=0,y=0,width=0,height=0;} rect;
    DXGI_FORMAT format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D12_RESOURCE_STATES arrival=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    uint32_t encoding=0;float diffuseWhite=1;
};
inline bool beta2_color_read(const k033core::Frame& f,Ngx12Color& out){
    out={};if(!f.current||!f.current(&f)||f.source!=k033core::Dx12||!f.haveFlags||
        !(f.flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)||!f.outputW||!f.outputH)return false;
    auto list=static_cast<ID3D12GraphicsCommandList*>(f.command);
    auto params=static_cast<const NVSDK_NGX_Parameter*>(f.parameters);
    if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    Ngx12Color value;ID3D12Resource* raw=nullptr;
    if(FAILED(list->GetDevice(IID_PPV_ARGS(&value.device)))||!value.device||
       params->Get(NVSDK_NGX_Parameter_Output,&raw)!=NVSDK_NGX_Result_Success||!raw||
       FAILED(raw->QueryInterface(IID_PPV_ARGS(&value.output))))return false;
    Microsoft::WRL::ComPtr<ID3D12Device> child;
    if(FAILED(value.output->GetDevice(IID_PPV_ARGS(&child)))||!ngx12_detail::same(value.device.Get(),child.Get()))return false;
    value.rect={0,0,f.outputW,f.outputH};
    if(params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,&value.rect.x)!=NVSDK_NGX_Result_Success||
       params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y,&value.rect.y)!=NVSDK_NGX_Result_Success)return false;
    const auto d=value.output->GetDesc();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1||d.MipLevels!=1||
       d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||!(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
       uint64_t(value.rect.x)+value.rect.width>d.Width||uint64_t(value.rect.y)+value.rect.height>d.Height||
       FAILED(value.output->GetHeapProperties(&heap,&flags))||(flags&D3D12_HEAP_FLAG_HARDWARE_PROTECTED)||!f.current(&f))return false;
    out=std::move(value);return true;
}
inline bool beta2_present_color_read(const beta2grade::PresentationFrame& f,Ngx12Color& out){
    out={};if(!f.current||!f.current(f)||!f.command||!f.output||!f.stream||!f.generation||
        f.encoding>3||!std::isfinite(f.diffuseWhite)||f.diffuseWhite<1||f.diffuseWhite>10000)return false;
    auto* list=static_cast<ID3D12GraphicsCommandList*>(f.command);
    auto* raw=static_cast<ID3D12Resource*>(f.output);
    Ngx12Color value;Microsoft::WRL::ComPtr<ID3D12Device> child;
    if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||FAILED(list->GetDevice(IID_PPV_ARGS(&value.device)))||
       !value.device||FAILED(raw->QueryInterface(IID_PPV_ARGS(&value.output)))||
       FAILED(value.output->GetDevice(IID_PPV_ARGS(&child)))||!ngx12_detail::same(value.device.Get(),child.Get()))return false;
    const auto d=value.output->GetDesc();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};
    // Copy resources preserve the exact bit-compatible format; the shader
    // applies the existing transfer function. No UAV flag is required on RT.
    const bool format=gradepresentpolicy::color(f.encoding,d.Format==DXGI_FORMAT_R8G8B8A8_UNORM,
        d.Format==DXGI_FORMAT_R10G10B10A2_UNORM,d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT);
    if(!format||d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1||
       d.MipLevels!=1||!(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)||!d.Width||!d.Height||
       d.Width>8192||d.Height>8192||d.Width*d.Height>16777216||
       FAILED(value.output->GetHeapProperties(&heap,&flags))||(flags&D3D12_HEAP_FLAG_HARDWARE_PROTECTED)||!f.current(f))return false;
    value.rect={0,0,uint32_t(d.Width),d.Height};value.format=d.Format;
    value.arrival=D3D12_RESOURCE_STATE_RENDER_TARGET;value.encoding=f.encoding;value.diffuseWhite=f.diffuseWhite;
    out=std::move(value);return true;
}
inline bool beta2_color_same(const Ngx12Color& a,const Ngx12Color& b){
    return ngx12_detail::same(a.device.Get(),b.device.Get())&&ngx12_detail::same(a.output.Get(),b.output.Get())&&
        a.rect.x==b.rect.x&&a.rect.y==b.rect.y&&a.rect.width==b.rect.width&&a.rect.height==b.rect.height&&
        a.format==b.format&&a.arrival==b.arrival&&a.encoding==b.encoding&&a.diffuseWhite==b.diffuseWhite;
}
}
