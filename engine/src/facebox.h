// Archived, unused skin-colour bounding-box prototype. Not a face detector.
// Frame-count-based readback here is not GPU completion proof: do not wire it into rendering.
// Two independent NR features were exercised only by test/nr_two_pass_probe.h (2026-09-06).
#pragma once

namespace facebox
{

static const char kHlsl[] = R"(
Texture2D<float4>    src : register(t0);
RWByteAddressBuffer  box : register(u0);
cbuffer C : register(b0) { uint2 size; float thresh; float pad; };

float SkinW(float3 c)
{
    float sum = c.r + c.g + c.b;
    if (sum < 1e-4f) return 0.0f;
    float rn = c.r / sum, gn = c.g / sum;
    float wr = smoothstep(0.33f, 0.36f, rn) * (1.0f - smoothstep(0.47f, 0.52f, rn));
    float wg = smoothstep(0.26f, 0.29f, gn) * (1.0f - smoothstep(0.36f, 0.39f, gn));
    float wo = smoothstep(0.0f, 0.02f, c.r - c.g) * smoothstep(0.0f, 0.02f, c.g - c.b);
    return saturate(wr * wg * wo);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= size.x || id.y >= size.y) return;
    float3 c = src.Load(int3(id.xy, 0)).rgb;
    if (SkinW(c) < thresh) return;
    uint old;
    box.InterlockedMin(0,  id.x, old);
    box.InterlockedMin(4,  id.y, old);
    box.InterlockedMax(8,  id.x, old);
    box.InterlockedMax(12, id.y, old);
    box.InterlockedAdd(16, 1u, old);
}
)";

static const int kRing = 3;
struct Box { UINT x0, y0, x1, y1, count; bool valid; };

static ID3D12RootSignature *s_rs    = nullptr;
static ID3D12PipelineState *s_pso   = nullptr;
static ID3D12Resource      *s_buf   = nullptr;
static ID3D12Resource      *s_reset = nullptr;
static ID3D12Resource      *s_read  = nullptr;
static bool s_armed[kRing] = {};
static int  s_slot = 0;
static bool s_ready = false;
static Box  s_last = {};

static void Destroy()
{
    if (s_read)  { s_read->Release();  s_read  = nullptr; }
    if (s_reset) { s_reset->Release(); s_reset = nullptr; }
    if (s_buf)   { s_buf->Release();   s_buf   = nullptr; }
    if (s_pso)   { s_pso->Release();   s_pso   = nullptr; }
    if (s_rs)    { s_rs->Release();    s_rs    = nullptr; }
    s_ready = false; s_slot = 0; s_last = Box{};
    for (int i = 0; i < kRing; ++i) s_armed[i] = false;
}

static bool Create(ID3D12Device *dev)
{
    if (s_ready) return true;
    if (dev == nullptr) return false;

    D3D12_DESCRIPTOR_RANGE rr = {}; rr.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rr.NumDescriptors = 1;
    D3D12_DESCRIPTOR_RANGE ru = {}; ru.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ru.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER p[3] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants.Num32BitValues = 4;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[1].DescriptorTable.NumDescriptorRanges = 1; p[1].DescriptorTable.pDescriptorRanges = &rr;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[2].DescriptorTable.NumDescriptorRanges = 1; p[2].DescriptorTable.pDescriptorRanges = &ru;
    D3D12_ROOT_SIGNATURE_DESC rd = {}; rd.NumParameters = 3; rd.pParameters = p;

    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    typedef HRESULT (WINAPI *PFN_Ser)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);
    PFN_Ser ser = d12 ? reinterpret_cast<PFN_Ser>(GetProcAddress(d12, "D3D12SerializeRootSignature")) : nullptr;
    if (ser == nullptr) return false;
    ID3DBlob *sig = nullptr, *err = nullptr;
    if (FAILED(ser(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)))
    { if (sig) sig->Release(); if (err) err->Release(); return false; }
    HRESULT hr = dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                          __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&s_rs));
    sig->Release(); if (err) err->Release();
    if (FAILED(hr)) return false;

    // 编译器跟 scale.h 一样是运行时取的(不加链接依赖)
    HMODULE dc = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (dc == nullptr) dc = LoadLibraryW(L"d3dcompiler_47.dll");
    scale::PFN_D3DCompile comp = dc ? reinterpret_cast<scale::PFN_D3DCompile>(GetProcAddress(dc, "D3DCompile")) : nullptr;
    if (comp == nullptr) { Destroy(); return false; }

    ID3DBlob *cs = nullptr, *ce = nullptr;
    if (FAILED(comp(kHlsl, sizeof(kHlsl) - 1, "033_facebox", nullptr, nullptr,
                    "main", "cs_5_0", 0, 0, &cs, &ce)) || cs == nullptr)
    { if (ce) ce->Release(); Destroy(); return false; }
    if (ce) ce->Release();

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = s_rs;
    pd.CS.pShaderBytecode = cs->GetBufferPointer();
    pd.CS.BytecodeLength  = cs->GetBufferSize();
    hr = dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&s_pso));
    cs->Release();
    if (FAILED(hr)) { Destroy(); return false; }

    const UINT kOne = sizeof(UINT) * 5;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Height = 1; bd.DepthOrArraySize = 1;
    bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES hDef = {}; hDef.Type = D3D12_HEAP_TYPE_DEFAULT;
    bd.Width = kOne; bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(dev->CreateCommittedResource(&hDef, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&s_buf)))) { Destroy(); return false; }

    D3D12_HEAP_PROPERTIES hUp = {}; hUp.Type = D3D12_HEAP_TYPE_UPLOAD;
    bd.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(dev->CreateCommittedResource(&hUp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&s_reset)))) { Destroy(); return false; }
    {
        void *pm = nullptr; D3D12_RANGE none = { 0, 0 };
        if (SUCCEEDED(s_reset->Map(0, &none, &pm)) && pm != nullptr)
        {
            UINT v[5] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0u, 0u };
            memcpy(pm, v, sizeof(v));
            s_reset->Unmap(0, nullptr);
        }
    }

    D3D12_HEAP_PROPERTIES hRb = {}; hRb.Type = D3D12_HEAP_TYPE_READBACK;
    bd.Width = static_cast<UINT64>(kOne) * kRing;
    if (FAILED(dev->CreateCommittedResource(&hRb, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&s_read)))) { Destroy(); return false; }

    s_ready = true;
    return true;
}

// 在【我们自己的】命令列表上跑。src 必须处于可被 SRV 读的状态。
static void Dispatch(ID3D12Device *dev, ID3D12GraphicsCommandList *cl,
                     ID3D12DescriptorHeap *heap, UINT inc, UINT slotSrv, UINT slotUav,
                     ID3D12Resource *src, DXGI_FORMAT fmt, UINT w, UINT h, float thresh)
{
    if (!s_ready || cl == nullptr || src == nullptr || heap == nullptr || w == 0 || h == 0) return;
    const UINT kOne = sizeof(UINT) * 5;

    // 复位: 拿 upload 缓冲直接拷, 比 ClearUAV 少一堆非着色器可见描述符的麻烦
    scale::Barrier(cl, s_buf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->CopyBufferRegion(s_buf, 0, s_reset, 0, kOne);
    scale::Barrier(cl, s_buf, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = fmt; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE hs = cpu; hs.ptr += static_cast<SIZE_T>(slotSrv) * inc;
    dev->CreateShaderResourceView(src, &sv, hs);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = DXGI_FORMAT_R32_TYPELESS; uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uv.Buffer.NumElements = 5; uv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    D3D12_CPU_DESCRIPTOR_HANDLE hu = cpu; hu.ptr += static_cast<SIZE_T>(slotUav) * inc;
    dev->CreateUnorderedAccessView(s_buf, nullptr, &uv, hu);

    D3D12_GPU_DESCRIPTOR_HANDLE gs = gpu; gs.ptr += static_cast<UINT64>(slotSrv) * inc;
    D3D12_GPU_DESCRIPTOR_HANDLE gu = gpu; gu.ptr += static_cast<UINT64>(slotUav) * inc;

    ID3D12DescriptorHeap *heaps[] = { heap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(s_rs);
    cl->SetPipelineState(s_pso);
    UINT c[4] = { w, h, *reinterpret_cast<const UINT *>(&thresh), 0u };
    cl->SetComputeRoot32BitConstants(0, 4, c, 0);
    cl->SetComputeRootDescriptorTable(1, gs);
    cl->SetComputeRootDescriptorTable(2, gu);
    cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

    scale::Barrier(cl, s_buf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->CopyBufferRegion(s_read, static_cast<UINT64>(s_slot) * kOne, s_buf, 0, kOne);
    scale::Barrier(cl, s_buf, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    s_armed[s_slot] = true;

    // 读【三帧前】那一格 —— 永远不等 GPU
    const int older = (s_slot + 1) % kRing;
    if (s_armed[older])
    {
        D3D12_RANGE rg;
        rg.Begin = static_cast<SIZE_T>(older) * kOne;
        rg.End   = static_cast<SIZE_T>(older + 1) * kOne;
        void *pr = nullptr;
        if (SUCCEEDED(s_read->Map(0, &rg, &pr)) && pr != nullptr)
        {
            const UINT *v = reinterpret_cast<const UINT *>(pr) + older * 5;
            Box b = {};
            b.x0 = v[0]; b.y0 = v[1]; b.x1 = v[2]; b.y1 = v[3]; b.count = v[4];
            // 有效: 真有像素命中, 且盒子没退化
            b.valid = (b.count > 64u) && (b.x1 > b.x0) && (b.y1 > b.y0);
            s_last = b;
            D3D12_RANGE none = { 0, 0 };
            s_read->Unmap(0, &none);
        }
    }
    s_slot = (s_slot + 1) % kRing;
}

static const Box &last()  { return s_last; }
static bool       ready() { return s_ready; }

} // namespace facebox
