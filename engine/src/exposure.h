// Game exposure normalization: GPU-only numerical path, no CPU image metering.
// Queue-owned per-frame resources and missing-sample policy: exposure_frames.h.
// Every supplied resource requires an explicit, known arrival state.
#pragma once

namespace exposure
{

struct FrameInput
{
    ID3D12Resource *texture = nullptr; // 只在当前 AfterGameDlss -> Stage 调用期间有效
    float pre = 1.0f;
    float scale = 1.0f;
    bool have_pre = false;
    bool have_scale = false;
};

static thread_local FrameInput s_frame;

static void set_frame(ID3D12Resource *texture, float pre, bool have_pre, float scale, bool have_scale)
{
    s_frame.texture = texture;
    s_frame.pre = pre;
    s_frame.scale = scale;
    s_frame.have_pre = have_pre;
    s_frame.have_scale = have_scale;
}

static void clear_frame() { s_frame = FrameInput{}; }

static const char kHlsl[] = R"(
Texture2D<float4> src : register(t0);
RWTexture2D<float> dst : register(u0);
cbuffer C : register(b0)
{
    uint mode; // 0 game E -> normalization factor; 1 factor -> white; 2 held factor -> factor
    uint unused;
    float slider;
    float pre_exposure;
    float exposure_scale;
    float fallback_white;
};
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float sample = src.Load(int3(0,0,0)).r;
    if(mode==0) {
        float denom=sample*exposure_scale;
        float factor=(sample>1e-6f && sample<1e6f && denom>1e-6f && denom<1e6f)
            ? pre_exposure/denom : -1.0f;
        dst[uint2(0,0)]=(factor>0 && factor<1e12f)?factor:-1.0f;
    } else if(mode==2) {
        dst[uint2(0,0)]=(sample>0 && sample<1e12f)?sample:-1.0f;
    } else {
        float white=slider*sample;
        dst[uint2(0,0)]=clamp((white>0 && white<1e15f)?white:fallback_white,0.01f,4096.0f);
    }
}
)";

typedef HRESULT (WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR,
                                         UINT, UINT, ID3DBlob **, ID3DBlob **);

struct Meter
{
    ID3D12Device *dev = nullptr;
    ID3D12RootSignature *rs = nullptr;
    ID3D12PipelineState *pso = nullptr;
    UINT inc = 0;
    bool ready = false;
    std::string error;
};
template <typename T> static void Rel(T *&p) { if (p) { p->Release(); p = nullptr; } }
// Frame resources live in exposure_frames.h and retain these objects through
// the command-list lease. Destroy only drops this owner's references.
static void Destroy(Meter &m)
{
    Rel(m.pso); Rel(m.rs); Rel(m.dev); m.inc=0; m.ready=false;
}

static bool Make1x1(ID3D12Device *dev, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = 1; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R32_FLOAT; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                    __uuidof(ID3D12Resource),
                                                    reinterpret_cast<void **>(out)));
}

static bool Create(Meter &m, ID3D12Device *dev)
{
    if (m.ready && m.dev == dev) return true;
    Destroy(m);

    HMODULE dc = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (dc == nullptr) dc = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = dc ? reinterpret_cast<PFN_D3DCompile>(GetProcAddress(dc, "D3DCompile")) : nullptr;
    if (compile == nullptr) { m.error = "找不到 D3DCompile"; return false; }

    ID3DBlob *cs = nullptr, *err = nullptr;
    HRESULT hr = compile(kHlsl, sizeof(kHlsl) - 1, "033_exposure", nullptr, nullptr,
                         "main", "cs_5_0", 0, 0, &cs, &err);
    if (FAILED(hr))
    {
        m.error = err ? std::string(static_cast<const char *>(err->GetBufferPointer()), err->GetBufferSize())
                      : "曝光着色器编译失败";
        Rel(err); Rel(cs); return false;
    }
    Rel(err);

    D3D12_DESCRIPTOR_RANGE srv = {};
    srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srv.NumDescriptors = 1;
    D3D12_DESCRIPTOR_RANGE uav = {};
    uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; uav.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER p[3] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants.Num32BitValues = 6; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[1].DescriptorTable.NumDescriptorRanges = 1; p[1].DescriptorTable.pDescriptorRanges = &srv;
    p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[2].DescriptorTable.NumDescriptorRanges = 1; p[2].DescriptorTable.pDescriptorRanges = &uav;
    p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd = {};
    rd.NumParameters = 3; rd.pParameters = p;

    typedef HRESULT (WINAPI *PFN_Serialize)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION,
                                            ID3DBlob **, ID3DBlob **);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    auto serialize = d12 ? reinterpret_cast<PFN_Serialize>(GetProcAddress(d12, "D3D12SerializeRootSignature")) : nullptr;
    if (serialize == nullptr) { m.error = "找不到 D3D12SerializeRootSignature"; Rel(cs); return false; }

    ID3DBlob *sig = nullptr, *se = nullptr;
    hr = serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &se);
    Rel(se);
    if (FAILED(hr)) { m.error = "曝光根签名序列化失败"; Rel(sig); Rel(cs); return false; }
    hr = dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                  __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&m.rs));
    Rel(sig);
    if (FAILED(hr)) { m.error = "曝光根签名创建失败"; Rel(cs); Destroy(m); return false; }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = m.rs; pd.CS.pShaderBytecode = cs->GetBufferPointer();
    pd.CS.BytecodeLength = cs->GetBufferSize();
    hr = dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState),
                                         reinterpret_cast<void **>(&m.pso));
    Rel(cs);
    if (FAILED(hr)) { m.error = "曝光 PSO 创建失败"; Destroy(m); return false; }

    m.inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m.dev = dev; m.dev->AddRef(); m.ready = true; m.error.clear();
    return true;
}

static DXGI_FORMAT ExposureSrvFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R32_TYPELESS:             return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:             return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS:          return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:          return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:    return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
        return f;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

static void Barrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *r,
                    D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    if (r == nullptr || from == to) return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &b);
}

static void UavBarrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *r)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = r;
    cl->ResourceBarrier(1, &b);
}

static void CreateSrv(ID3D12Device *dev, ID3D12Resource *r, DXGI_FORMAT fmt,
                      D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = fmt; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(r, &sv, h);
}

static void CreateUav(ID3D12Device *dev, ID3D12Resource *r, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = DXGI_FORMAT_R32_FLOAT; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    dev->CreateUnorderedAccessView(r, nullptr, &uv, h);
}

static void Dispatch(Meter &m, ID3D12GraphicsCommandList *cl, ID3D12DescriptorHeap *heap,
                     UINT srv_slot, UINT uav_slot, UINT mode, bool have_previous,
                     float slider, float pre, float exp_scale, float fallback_e)
{
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gs = gpu; gs.ptr += static_cast<UINT64>(srv_slot) * m.inc;
    D3D12_GPU_DESCRIPTOR_HANDLE gu = gpu; gu.ptr += static_cast<UINT64>(uav_slot) * m.inc;
    ID3D12DescriptorHeap *heaps[] = { heap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(m.rs);
    cl->SetPipelineState(m.pso);
    UINT c[6] = { mode, have_previous ? 1u : 0u,
                  *reinterpret_cast<UINT *>(&slider), *reinterpret_cast<UINT *>(&pre),
                  *reinterpret_cast<UINT *>(&exp_scale), *reinterpret_cast<UINT *>(&fallback_e) };
    cl->SetComputeRoot32BitConstants(0, 6, c, 0);
    cl->SetComputeRootDescriptorTable(1, gs);
    cl->SetComputeRootDescriptorTable(2, gu);
    cl->Dispatch(1, 1, 1);
}

} // namespace exposure
