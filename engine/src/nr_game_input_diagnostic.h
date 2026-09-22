#pragma once
#include "nr_game_input_abi.h"
#include <d3d12.h>
#include <array>
#include <atomic>
#include <cmath>
namespace nrdiag033 {
enum FrameBit:uint32_t {OuterSize=1,OuterVersion=2,Size=4,Version=8,Source=16,Flags=32,
    Serial=64,Stream=128,ScaleX=256,ScaleY=512,JitterX=1024,JitterY=2048};
inline uint32_t FrameIssues(const nrgame033::Frame& f){
    constexpr uint32_t required=nrgame033::DepthReversed|nrgame033::MotionUnjittered|nrgame033::DisplayResolved|nrgame033::FullExtent;
    uint32_t mask=0;
    if(f.size!=sizeof(nrgame033::Frame))mask|=Size;
    if(f.version!=nrgame033::Version)mask|=Version;
    if(f.source!=nrgame033::ReEngineScene)mask|=Source;
    if((f.flags&required)!=required)mask|=Flags;
    if(!f.serial)mask|=Serial;if(!f.stream)mask|=Stream;
    if(!std::isfinite(f.scaleX)||f.scaleX==0)mask|=ScaleX;
    if(!std::isfinite(f.scaleY)||f.scaleY==0)mask|=ScaleY;
    if((f.flags&nrgame033::JitterKnown)&&!std::isfinite(f.jitterX))mask|=JitterX;
    if((f.flags&nrgame033::JitterKnown)&&!std::isfinite(f.jitterY))mask|=JitterY;
    return mask;
}
inline uint32_t OuterIssues(const nrgame033::Frame2& f){
    return (f.size!=sizeof(nrgame033::Frame2)?OuterSize:0u)|(f.version!=2?OuterVersion:0u);
}
enum TextureBit:uint32_t {Dimension=1,Array=2,Mips=4,Samples=8,DenySrv=16,Format=32};
inline uint32_t TextureIssues(const D3D12_RESOURCE_DESC& d,bool depth){
    uint32_t mask=0;
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D)mask|=Dimension;
    if(d.DepthOrArraySize!=1)mask|=Array;if(d.MipLevels!=1)mask|=Mips;
    if(d.SampleDesc.Count!=1)mask|=Samples;
    if(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)mask|=DenySrv;
    const bool accepted=depth?
        (d.Format==DXGI_FORMAT_R32_TYPELESS||d.Format==DXGI_FORMAT_D32_FLOAT||d.Format==DXGI_FORMAT_R32_FLOAT):
        (d.Format==DXGI_FORMAT_R16G16_FLOAT||d.Format==DXGI_FORMAT_R32G32_FLOAT);
    if(!accepted)mask|=Format;return mask;
}
enum class Reason:uint32_t {None,Adapter,OuterSize,OuterVersion,Size,Version,Source,Flags,Serial,Stream,ScaleX,ScaleY,JitterX,JitterY,
    DepthDimension,DepthArray,DepthMips,DepthSamples,DepthDenySrv,DepthFormat,
    MotionDimension,MotionArray,MotionMips,MotionSamples,MotionDenySrv,MotionFormat};
inline Reason FrameReason(uint32_t mask){
    for(unsigned i=0;i<12;++i)if(mask&(1u<<i))return Reason(uint32_t(Reason::OuterSize)+i);
    return Reason::None;
}
inline Reason TextureReason(uint32_t depth,uint32_t motion){
    for(unsigned i=0;i<6;++i)if(depth&(1u<<i))return Reason(uint32_t(Reason::DepthDimension)+i);
    for(unsigned i=0;i<6;++i)if(motion&(1u<<i))return Reason(uint32_t(Reason::MotionDimension)+i);
    return Reason::None;
}
struct Text {const char* current;const char* recent;};
#define K033_NR_DIAG_TEXT(value) {value,"最近一次：" value}
inline constexpr Text Texts[]={
    K033_NR_DIAG_TEXT("原生输入格式或语义不匹配 · NR 等待"),
    K033_NR_DIAG_TEXT("场景适配兼容状态未确认 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入扩展协议长度不匹配 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入扩展协议版本不匹配 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入协议长度不匹配 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入协议版本不匹配 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入来源语义不匹配 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入缺少必要语义标记 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入没有有效帧号 · NR 等待"),
    K033_NR_DIAG_TEXT("场景输入没有有效场景标识 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动横向换算无效 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动纵向换算无效 · NR 等待"),
    K033_NR_DIAG_TEXT("场景横向抖动数据无效 · NR 等待"),
    K033_NR_DIAG_TEXT("场景纵向抖动数据无效 · NR 等待"),
    K033_NR_DIAG_TEXT("原生深度不是二维纹理 · NR 等待"),
    K033_NR_DIAG_TEXT("原生深度数组范围尚不支持 · NR 等待"),
    K033_NR_DIAG_TEXT("原生深度mip层数尚不支持 · NR 等待"),
    K033_NR_DIAG_TEXT("原生深度采样数尚不支持 · NR 等待"),
    K033_NR_DIAG_TEXT("原生深度禁止创建着色器视图 · NR 等待"),
    K033_NR_DIAG_TEXT("原生深度格式尚不支持 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动不是二维纹理 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动数组范围尚不支持 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动mip层数尚不支持 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动采样数尚不支持 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动禁止创建着色器视图 · NR 等待"),
    K033_NR_DIAG_TEXT("原生运动格式尚不支持 · NR 等待")};
#undef K033_NR_DIAG_TEXT
// Writes occur in the existing nrdispatch::AfterScope. UI reads only display.
// A short absent snapshot retains the last concrete reason. Once marked recent,
// repeated identical rejection stays recent instead of alternating log strings.
class DisplayState {
    static constexpr uint32_t Recent=0x100;
    std::atomic<uint32_t> display{0};
public:
    void Clear(){display.store(0,std::memory_order_release);}
    void Reject(Reason reason){
        const uint32_t next=uint32_t(reason),old=display.load(std::memory_order_relaxed);
        display.store(next|((old&0xffu)==next?(old&Recent):0u),std::memory_order_release);
    }
    void Unpublished(){const auto old=display.load(std::memory_order_relaxed);if(old&0xffu)display.store(old|Recent,std::memory_order_release);}
    const char* Note()const{
        const auto value=display.load(std::memory_order_acquire),index=value&0xffu;
        if(!index||index>=std::size(Texts))return nullptr;
        return value&Recent?Texts[index].recent:Texts[index].current;
    }
};
using Signature=std::array<uint64_t,32>;
inline Signature Key(Reason reason,uint32_t frameMask,uint32_t depthMask,uint32_t motionMask,
                     const nrgame033::Frame& f,const nrgame033::Frame2& outer,
                     const D3D12_RESOURCE_DESC& d,const D3D12_RESOURCE_DESC& m){
    return {uint32_t(reason),frameMask,depthMask,motionMask,f.size,f.version,f.source,f.flags,outer.size,outer.version,
        f.width,f.height,outer.motionWidth,outer.motionHeight,outer.outputWidth,outer.outputHeight,
        uint32_t(d.Dimension),d.Width,d.Height,d.DepthOrArraySize,d.MipLevels,uint32_t(d.Format),d.SampleDesc.Count,uint32_t(d.Flags),
        uint32_t(m.Dimension),m.Width,m.Height,m.DepthOrArraySize,m.MipLevels,uint32_t(m.Format),m.SampleDesc.Count,uint32_t(m.Flags)};
}
// Bounded process-lifetime deduplication. No allocation, pointers, frame serial,
// timestamps or repeated Missing observations contribute to the signature.
class LogBook {
    std::array<Signature,16> seen{};size_t count=0;
public:
    bool New(const Signature& key){for(size_t i=0;i<count;++i)if(seen[i]==key)return false;
        if(count==seen.size())return false;seen[count++]=key;return true;}
    size_t Count()const{return count;}
};
}
