// Image-only 033 optical-flow backend. Development API, not yet attached to a
// game's swapchain. Call only at a known rendering boundary on an immediate
// context; the caller owns input/output state and presentation scheduling.
#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "framegen_flow_hlsl.inl"
namespace framegen033 {
using Microsoft::WRL::ComPtr;
enum class FlowResult { Submitted, Busy, Invalid, Unsupported, Failed };
class FlowDx11 {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11ComputeShader> down,flow,blend;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Query> done;
    struct Image {ComPtr<ID3D11Texture2D> tex;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;};
    std::array<Image,4> images;
    UINT fw=0,fh=0;
    bool submitted=false;
    uint64_t allocations=0;
    struct Params {UINT w,h,fw,fh;float phase;UINT radius,protect,reset;};
    bool Shader(const char* entry,ComPtr<ID3D11ComputeShader>& target) {
        ComPtr<ID3DBlob> code,error;
        if(FAILED(D3DCompile(kFramegenFlowShader,sizeof(kFramegenFlowShader)-1,"033 image flow",nullptr,nullptr,
            entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)))return false;
        return SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&target));
    }
    bool Idle(ID3D11DeviceContext* ctx) {
        if(!submitted)return true;
        BOOL complete=FALSE;return ctx->GetData(done.Get(),&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK && complete;
    }
    bool SameDevice(ID3D11DeviceChild* child)const {
        if(!child)return false;ComPtr<ID3D11Device> owner;child->GetDevice(&owner);return owner.Get()==device.Get();
    }
    static bool Description(ID3D11View* view,D3D11_TEXTURE2D_DESC& desc) {
        if(!view)return false;ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return false;texture->GetDesc(&desc);
        return desc.SampleDesc.Count==1 && desc.ArraySize==1 && desc.MipLevels==1;
    }
    static bool Aliases(ID3D11View* a,ID3D11View* b) {
        if(!a||!b)return false;ComPtr<ID3D11Resource> x,y;a->GetResource(&x);b->GetResource(&y);return x.Get()==y.Get();
    }
    static bool ColourView(ID3D11ShaderResourceView* view){
        D3D11_SHADER_RESOURCE_VIEW_DESC d{};view->GetDesc(&d);
        if(d.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||d.Texture2D.MostDetailedMip!=0||d.Texture2D.MipLevels!=1)return false;
        return d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT||d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||
            d.Format==DXGI_FORMAT_R8G8B8A8_UNORM||d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
            d.Format==DXGI_FORMAT_B8G8R8A8_UNORM||d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||d.Format==DXGI_FORMAT_R10G10B10A2_UNORM;
    }
    // Restore only the compute bindings this backend changes. No host ImGui or
    // vendor runtime context crosses this API.
    struct Bindings {
        ID3D11DeviceContext* c;ID3D11ComputeShader* shader=nullptr;
        ID3D11ShaderResourceView* srvs[5]{};ID3D11UnorderedAccessView* uav=nullptr;
        ID3D11Buffer* buffer=nullptr;ID3D11SamplerState* sampler=nullptr;
        ID3D11ClassInstance* classes[256]{};UINT count=256;
        explicit Bindings(ID3D11DeviceContext* ctx):c(ctx){
            c->CSGetShader(&shader,classes,&count);c->CSGetShaderResources(0,5,srvs);
            c->CSGetUnorderedAccessViews(0,1,&uav);c->CSGetConstantBuffers(0,1,&buffer);c->CSGetSamplers(0,1,&sampler);}
        ~Bindings(){ID3D11ShaderResourceView* empty[5]{};ID3D11UnorderedAccessView* none=nullptr;
            c->CSSetShaderResources(0,5,empty);c->CSSetUnorderedAccessViews(0,1,&none,nullptr);
            c->CSSetShader(shader,classes,count);c->CSSetShaderResources(0,5,srvs);
            c->CSSetUnorderedAccessViews(0,1,&uav,nullptr);c->CSSetConstantBuffers(0,1,&buffer);c->CSSetSamplers(0,1,&sampler);
            if(shader)shader->Release();for(auto* x:srvs)if(x)x->Release();if(uav)uav->Release();
            if(buffer)buffer->Release();if(sampler)sampler->Release();for(UINT i=0;i<count;++i)if(classes[i])classes[i]->Release();}
    };
    void Pass(ID3D11DeviceContext* ctx,ID3D11ComputeShader* shader,const Params& p,
              ID3D11ShaderResourceView* a,ID3D11ShaderResourceView* b,ID3D11UnorderedAccessView* out,
              bool generation=false,ID3D11ShaderResourceView* protect=nullptr) {
        ID3D11ShaderResourceView* empty[5]{};ID3D11UnorderedAccessView* none=nullptr;
        ctx->CSSetShaderResources(0,5,empty);ctx->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        ctx->UpdateSubresource(constants.Get(),0,nullptr,&p,0,0);
        ID3D11Buffer* cb=constants.Get();ctx->CSSetConstantBuffers(0,1,&cb);
        ID3D11SamplerState* s=sampler.Get();ctx->CSSetSamplers(0,1,&s);
        ID3D11ShaderResourceView* resources[]={a,b,generation?images[2].srv.Get():nullptr,generation?images[3].srv.Get():nullptr,protect};
        ctx->CSSetShaderResources(0,5,resources);ctx->CSSetUnorderedAccessViews(0,1,&out,nullptr);
        ctx->CSSetShader(shader,nullptr,0);ctx->Dispatch(((generation?p.w:p.fw)+7)/8,((generation?p.h:p.fh)+7)/8,1);
    }
public:
    bool Initialize(ID3D11Device* dev) {
        if(device)return device.Get()==dev;
        if(!dev || dev->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0)return false;device=dev;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(Params);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
        bool ok=Shader("downsample",down)&&Shader("estimate",flow)&&Shader("interpolate",blend)&&
            SUCCEEDED(dev->CreateBuffer(&bd,nullptr,&constants))&&SUCCEEDED(dev->CreateSamplerState(&sd,&sampler))&&
            SUCCEEDED(dev->CreateQuery(&q,&done));
        if(!ok){device.Reset();down.Reset();flow.Reset();blend.Reset();constants.Reset();sampler.Reset();done.Reset();}return ok;
    }
    // Output must be a distinct float RGBA UAV; conversion/presentation is an
    // explicit caller responsibility. Values remain unclipped for linear HDR.
    FlowResult Run(ID3D11DeviceContext* ctx,ID3D11ShaderResourceView* previous,ID3D11ShaderResourceView* current,
                   ID3D11UnorderedAccessView* output,float phase,UINT flowWidth,UINT flowHeight,UINT radius=4,
                   bool reset=false,ID3D11ShaderResourceView* protection=nullptr) {
        if(!device||!ctx||!std::isfinite(phase)||phase<0||phase>1||!flowWidth||!flowHeight||flowWidth>320||flowHeight>320||radius>12)
            return FlowResult::Invalid;
        if(ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return FlowResult::Unsupported;
        if(!SameDevice(ctx)||!SameDevice(previous)||!SameDevice(current)||!SameDevice(output)||
            (protection&&!SameDevice(protection))||Aliases(previous,output)||Aliases(current,output)||Aliases(protection,output))return FlowResult::Invalid;
        if(!ColourView(previous)||!ColourView(current))return FlowResult::Unsupported;
        D3D11_TEXTURE2D_DESC a{},b{},o{},m{};
        if(!Description(previous,a)||!Description(current,b)||!Description(output,o)||a.Width!=b.Width||a.Height!=b.Height||
            a.Width!=o.Width||a.Height!=o.Height||flowWidth>a.Width||flowHeight>a.Height)return FlowResult::Invalid;
        if(o.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT && o.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return FlowResult::Unsupported;
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};output->GetDesc(&u);
        if(u.ViewDimension!=D3D11_UAV_DIMENSION_TEXTURE2D||u.Texture2D.MipSlice!=0||u.Format!=o.Format)return FlowResult::Unsupported;
        if(protection){D3D11_SHADER_RESOURCE_VIEW_DESC v{};protection->GetDesc(&v);
            if(!Description(protection,m)||m.Width!=a.Width||m.Height!=a.Height||v.Format!=DXGI_FORMAT_R32_FLOAT||
                v.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||v.Texture2D.MostDetailedMip!=0||v.Texture2D.MipLevels!=1)return FlowResult::Invalid;}
        if(fw!=flowWidth||fh!=flowHeight){
            if(!Idle(ctx))return FlowResult::Busy;
            std::array<Image,4> replacement;
            D3D11_TEXTURE2D_DESC d{};d.Width=flowWidth;d.Height=flowHeight;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
            d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            for(auto& i:replacement)if(FAILED(device->CreateTexture2D(&d,nullptr,&i.tex))||
                FAILED(device->CreateShaderResourceView(i.tex.Get(),nullptr,&i.srv))||
                FAILED(device->CreateUnorderedAccessView(i.tex.Get(),nullptr,&i.uav)))return FlowResult::Failed;
            images=std::move(replacement);fw=flowWidth;fh=flowHeight;++allocations;
        }
        Bindings restore(ctx);Params p{a.Width,a.Height,fw,fh,phase,radius,protection?1u:0u,reset?1u:0u};
        if(!reset && phase>0 && phase<1){
            Pass(ctx,down.Get(),p,previous,nullptr,images[0].uav.Get());
            Pass(ctx,down.Get(),p,current,nullptr,images[1].uav.Get());
            Pass(ctx,flow.Get(),p,images[0].srv.Get(),images[1].srv.Get(),images[2].uav.Get());
            Pass(ctx,flow.Get(),p,images[1].srv.Get(),images[0].srv.Get(),images[3].uav.Get());
        }
        Pass(ctx,blend.Get(),p,previous,current,output,true,protection);
        ctx->End(done.Get());submitted=true;return FlowResult::Submitted;
    }
    uint64_t AllocationBatches()const{return allocations;}
    uint64_t WorkingBytes()const{return uint64_t(fw)*fh*16*images.size();}
    bool Retire(ID3D11DeviceContext* ctx){
        if(!ctx || !SameDevice(ctx)||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||!Idle(ctx))return false;
        images={};fw=fh=0;submitted=false;return true;
    }
};
}
