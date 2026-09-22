// =====================================================================
//  frmstat.h  ·  帧统计(GPU 侧) —— 给「这一帧跑不跑模型」的控制器供硬数据
//
//  为什么要有它:
//    实测(4090)模型占我们每帧 GPU 开销的 97-99%(3840x1620 要 8-9 ms), 编码/合成/
//    拷贝加起来才 0.3 ms。想省, 只能省模型; 而模型分辨率不许动(重建一次 200 ms,
//    频繁重建驱动会罢工)。唯一允许的动作是「这一帧跑 / 这一帧不跑」。
//    业主的硬约束: 画质不许有可察觉的下降。所以只在细节【物理上看不见】的帧上
//    才允许跳过 —— 判断要靠这三个数, 全部在 GPU 上采, 隔两帧读回来, 不等 GPU:
//      (a) mv_median_px  —— 运动矢量幅值的【中位数】(像素/帧, 渲染分辨率)。
//                            用中位数不用均值: HUD/静止区域不能把整帧的运动稀释掉。
//      (b) dark_frac     —— 亮度低于合成暗膝(线性 0.05, 白点归一后)的像素占比。
//                            代理图是 sRGB 编码的, 先 pow 2.2 线性化再比。
//      (c) residual_mean —— |luma(模型输出) − luma(代理图)| 的均值, 即模型这一帧
//                            到底改了多少(残差能量)。残差小 = 跳过看不出来。
//          residual_mean_static_ref —— 静止帧(中位运动 < 0.5px)上残差的滑动平均,
//                            作为「模型正常出力时改多少」的参照, 让控制器拿当前
//                            残差跟它比, 而不是跟一个拍脑袋的常数比。
//
//  做法:
//    一个 cs_5_0 计算着色器, 一次派发: 按步长(默认每 4 个像素取 1 个)采样,
//    组内先用 groupshared 归并, 再每组一次 InterlockedAdd 进一个 uint 计数缓冲
//    (64 桶直方图 + 暗像素数 + 残差和 + 各种样本数)。开销量级 0.02-0.05 ms。
//    每帧: 从零缓冲 CopyResource 清计数 → 派发 → 拷进 3 帧一环的回读缓冲;
//    CPU 读的是两帧前那一格(跟 gputime.h 同一套, 永远不等 GPU)。
//    ★没有围栏★ 所以每格里写了首尾两个帧标签, 读回来时标签对不上就当没读到,
//    沿用上一帧的数 —— 半新半旧的数不会进控制器。
//
//  ★自带一整套★ 根签名 / PSO / 描述符堆 / UAV 缓冲 / 回读环全是自己的:
//    · 不借 scale.h 的 Blitter: 它 resolve 的根常量钉死在 10 个(改过一次当场
//      掉设备), 不去碰它。
//    · 描述符堆是 shader-visible 的 CBV_SRV_UAV, 6 组一环, 永远不改写还在 GPU 上
//      跑的那组描述符。
//    · 派发跟别的 pass 一样要绑我们自己的堆/根签名/PSO。就地插入路上这条列表
//      是【游戏的】, 绑完不在这里还 —— 还的事归 staterestore::Envelope 管
//      (hostnr::Stage 里它罩着整段), 本文件不重复做。
//
//  状态约定:
//    · 传进来的 mv / proxy / model_out 都在 NON_PIXEL_SHADER_RESOURCE 态, 我们
//      只读, 一个 barrier 都不打在它们身上, 走的时候原样。
//    · 运动矢量资源可能比 gw x gh 大(引导图是子矩形, 在左上角), 只在 gw x gh 内读。
//      格式按资源自己的 GetDesc() 来: TYPELESS 的映射成对应 FLOAT/UNORM。
//    · 计数缓冲在 UAV 态常驻, 每帧 UAV↔COPY_DEST / COPY_SOURCE 来回一次。
//
//  依赖: <Windows.h> <d3d12.h> <d3dcompiler.h> 已由包含者引入(跟 scale.h 一样,
//        编译器走运行时 GetProcAddress, 不加链接依赖)。日志走 FRMSTAT_LOG(可选):
//        #define FRMSTAT_LOG Log   放在 #include "frmstat.h" 之前即可。
//  配置: dlss5-033.cfg 里 frmstat=0 关掉, frmstride=1..16 采样步长(默认 4)。
//        赋值的事归 carrier::load_cfg(跟 gputime::cfg_enabled 一个待遇)。
// =====================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

#ifndef FRMSTAT_LOG
#define FRMSTAT_LOG(...) ((void)0)
#endif

namespace frmstat
{

// ---------------------------------------------------------------- 着色器
//  计数缓冲布局(uint, 共 kCount 个):
//    [0..63]  |MV| 直方图, 桶宽 1 像素, 第 63 桶 = 溢出(>= 63 px)
//    [64] 暗像素数   [65] 代理图样本数
//    [66] 残差和·整数部分   [67] 残差和·小数部分 x4096   [68] 残差样本数
//    [69] 运动矢量样本数    [70] 坏运动矢量数(NaN/Inf, 不进直方图)
//    [71] 帧标签 A          [79] 帧标签 B(首尾各一个, 两个都对上才算这格读完整)
//  残差和拆成整数+小数两个 uint 累加, 是因为 cs_5_0 没有 64 位原子: 直接按
//  定点累加, 8K 全采样时会溢出; 先在组内用 float 归并, 再按组把整数和小数分开
//  加, 8K 满采样(52 万组)也远不到 2^32。
static const char kHlsl[] = R"(
Texture2D<float4>        mv_tex : register(t0);
Texture2D<float4>        proxy  : register(t1);
Texture2D<float4>        model  : register(t2);
RWStructuredBuffer<uint> cnt    : register(u0);
cbuffer C : register(b0)
{
    uint  gw;  uint gh;  uint pw;  uint ph;
    uint  stride; uint flags; uint tag; uint pad0;
    float mvsx; float mvsy; float knee; float pad1;
};

static const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);
static const uint F_MV    = 1u;
static const uint F_PROXY = 2u;
static const uint F_MODEL = 4u;
static const uint F_SRGB  = 8u;
static const uint I_DARK = 64u, I_PX = 65u, I_RES_INT = 66u, I_RES_FRAC = 67u,
                  I_RES_N = 68u, I_MV_N = 69u, I_MV_BAD = 70u, I_TAG_A = 71u, I_TAG_B = 79u;

groupshared uint  g_hist[64];
groupshared float g_res[64];
groupshared uint  g_dark, g_px, g_resn, g_mvn, g_mvbad;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    g_hist[gi] = 0u;
    if (gi == 0u) { g_dark = 0u; g_px = 0u; g_resn = 0u; g_mvn = 0u; g_mvbad = 0u; }
    GroupMemoryBarrierWithGroupSync();

    // 采样点取每个步长格子的中心, 只在 gw x gh / pw x ph 之内读
    uint s = max(stride, 1u);
    uint x = id.x * s + (s >> 1);
    uint y = id.y * s + (s >> 1);

    if ((flags & F_MV) != 0u && x < gw && y < gh)
    {
        float2 v = mv_tex.Load(int3(int(x), int(y), 0)).xy * float2(mvsx, mvsy);
        float  m = length(v);
        if (isnan(m) || isinf(m))
        {
            InterlockedAdd(g_mvbad, 1u);
        }
        else
        {
            uint b = (m < 63.0f) ? uint(m) : 63u;
            InterlockedAdd(g_hist[b], 1u);
            InterlockedAdd(g_mvn, 1u);
        }
    }

    float r = 0.0f;
    if ((flags & F_PROXY) != 0u && x < pw && y < ph)
    {
        float3 p   = max(proxy.Load(int3(int(x), int(y), 0)).rgb, 0.0f);
        // 暗膝在【线性】空间比: 代理图是 sRGB 编码的就先按 2.2 幂线性化
        float3 lin = ((flags & F_SRGB) != 0u) ? pow(max(p, 1e-6f), 2.2f) : p;
        float  yl  = dot(lin, kLuma);
        InterlockedAdd(g_px, 1u);
        if (yl < knee) InterlockedAdd(g_dark, 1u);
        if ((flags & F_MODEL) != 0u)
        {
            // 残差在【编码】空间算: 两张图都是显示参考的, 这里的差就是眼睛看到的差
            float3 mo = max(model.Load(int3(int(x), int(y), 0)).rgb, 0.0f);
            float  d  = abs(dot(mo, kLuma) - dot(p, kLuma));
            if (!(isnan(d) || isinf(d))) { r = d; InterlockedAdd(g_resn, 1u); }
        }
    }
    g_res[gi] = r;
    GroupMemoryBarrierWithGroupSync();

    // 组内归并残差(64 → 1)
    [unroll]
    for (uint k = 32u; k > 0u; k >>= 1u)
    {
        if (gi < k) g_res[gi] += g_res[gi + k];
        GroupMemoryBarrierWithGroupSync();
    }

    // 每组只往全局加一次: 空桶不加, 全局原子操作从每像素一次降到每组几次
    uint hb = g_hist[gi];
    if (hb != 0u) InterlockedAdd(cnt[gi], hb);
    if (gi == 0u)
    {
        if (g_dark  != 0u) InterlockedAdd(cnt[I_DARK],   g_dark);
        if (g_px    != 0u) InterlockedAdd(cnt[I_PX],     g_px);
        if (g_resn  != 0u) InterlockedAdd(cnt[I_RES_N],  g_resn);
        if (g_mvn   != 0u) InterlockedAdd(cnt[I_MV_N],   g_mvn);
        if (g_mvbad != 0u) InterlockedAdd(cnt[I_MV_BAD], g_mvbad);
        float sum = g_res[0];
        if (sum > 0.0f)
        {
            InterlockedAdd(cnt[I_RES_INT],  uint(floor(sum)));
            InterlockedAdd(cnt[I_RES_FRAC], uint(frac(sum) * 4096.0f + 0.5f));
        }
        if (gid.x == 0u && gid.y == 0u) { cnt[I_TAG_A] = tag; cnt[I_TAG_B] = tag; }
    }
}
)";

// ---------------------------------------------------------------- 常量
static const int      kBins       = 64;
static const int      kIdxDark    = 64;
static const int      kIdxPx      = 65;
static const int      kIdxResInt  = 66;
static const int      kIdxResFrac = 67;
static const int      kIdxResN    = 68;
static const int      kIdxMvN     = 69;
static const int      kIdxMvBad   = 70;
static const int      kIdxTagA    = 71;
static const int      kIdxTagB    = 79;
static const int      kCount      = 80;                       // 320 字节
static const UINT     kBytes      = static_cast<UINT>(kCount) * 4u;
static const int      kRing       = 3;                        // 回读环: 三帧一环, 读两帧前那格
static const int      kDescRing   = 6;                        // 描述符环: 比回读环深, 更不可能改写在飞的那组
static const int      kDescPerSet = 4;                        // t0 t1 t2 + u0
static const UINT     kConsts     = 12;                       // 根常量 DWORD 数 = 着色器 cbuffer 的 12 个
static const double   kFracScale  = 4096.0;

// ---------------------------------------------------------------- 结果
struct Stats
{
    float    mv_median_px;             // |MV| 中位数, 像素/帧(渲染分辨率); >= 63 表示大半像素都溢出了
    float    mv_p90_px;                // 90 分位, 面板看分布用
    float    mv_overflow_frac;         // 落在溢出桶(>= 63px)的比例
    float    dark_frac;                // 线性亮度 < knee 的像素占比 0..1
    float    residual_mean;            // 本帧模型残差均值(编码空间 luma, 0..1); 只在 has_residual 时有意义
    float    residual_mean_static_ref; // 静止帧上残差的滑动平均; < 0 = 还没建立
    unsigned mv_samples;               // 进了直方图的运动矢量样本数
    unsigned mv_bad;                   // NaN/Inf 的运动矢量样本数
    unsigned px_samples;               // 代理图样本数
    unsigned res_samples;              // 残差样本数
    unsigned frame_tag;                // 这份数据来自第几次派发
    bool     has_mv;                   // 这一帧有运动矢量
    bool     has_residual;             // 这一帧跑了模型(有残差)
    bool     valid;                    // 至少完整读回过一帧
};

static int cfg_enabled = 1;   // dlss5-033.cfg: frmstat=0 关
static int cfg_stride  = 4;   // dlss5-033.cfg: frmstride=1..16 (默认 4: 每 4 个像素取 1 个)
// 静止参考的两个门槛: 中位运动小于这个像素数才算「静止」, 且暗像素占比不过高
static float cfg_static_mv_px   = 0.5f;
static float cfg_static_ema     = 0.05f;   // 滑动平均步长

// ---------------------------------------------------------------- 状态
static ID3D12Device         *s_dev   = nullptr;
static ID3D12RootSignature  *s_rs    = nullptr;
static ID3D12PipelineState  *s_pso   = nullptr;
static ID3D12DescriptorHeap *s_heap  = nullptr;
static ID3D12Resource       *s_cnt   = nullptr;   // 计数缓冲(DEFAULT, UAV 态常驻)
static ID3D12Resource       *s_zero  = nullptr;   // 全零缓冲(UPLOAD, 建时 memset 一次, 每帧从它拷来清零)
static ID3D12Resource       *s_read  = nullptr;   // 回读环(READBACK, kRing 格)
static UINT                  s_inc   = 0;
static bool                  s_ready = false;
static bool                  s_failed = false;    // 建失败就不每帧再试(日志只记一次)
static std::string           s_note  = "未启用";
static int                   s_slot  = 0;         // 回读环当前格
static int                   s_dslot = 0;         // 描述符环当前组
static unsigned              s_tag   = 0;         // 派发计数, 从 1 起(0 = 缓冲被清零后没人写过)
static unsigned              s_slot_tag[kRing] = {};
static unsigned              s_stale = 0;         // 标签对不上(GPU 还没跑到)的次数, 面板/日志看
static unsigned              s_reads = 0;         // 完整读回的次数
static Stats                 s_stats = {};

template <typename T> static void Rel(T *&p) { if (p) { p->Release(); p = nullptr; } }

static void Destroy()
{
    Rel(s_read); Rel(s_zero); Rel(s_cnt); Rel(s_heap); Rel(s_pso); Rel(s_rs);
    s_dev = nullptr;
    s_ready = false; s_failed = false;
    s_slot = 0; s_dslot = 0; s_tag = 0; s_stale = 0; s_reads = 0;
    for (int i = 0; i < kRing; ++i) s_slot_tag[i] = 0;
    std::memset(&s_stats, 0, sizeof(s_stats));
    s_stats.residual_mean_static_ref = -1.0f;
    s_note = "未启用";
}

static bool MakeBuffer(ID3D12Device *dev, ID3D12Resource **out, D3D12_HEAP_TYPE heap, UINT64 bytes,
                       D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = heap;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = bytes;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags            = flags;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initial, nullptr,
                                                  __uuidof(ID3D12Resource), reinterpret_cast<void **>(out)));
}

// 建全套。已经就绪就直接返回 true(幂等); 失败会记 s_failed, 之后不再重试 —— 想重试先 Destroy()。
static bool Create(ID3D12Device *dev)
{
    if (s_ready) return true;
    if (s_failed) return false;
    if (dev == nullptr) { s_note = "没有设备"; return false; }
    if (!cfg_enabled) { s_note = "cfg 关掉了"; return false; }
    Destroy();

    typedef HRESULT (WINAPI *PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR,
                                          UINT, UINT, ID3DBlob **, ID3DBlob **);
    typedef HRESULT (WINAPI *PFN_Serialize)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION,
                                            ID3DBlob **, ID3DBlob **);

    // 编译器和根签名序列化都走运行时取地址, 不加静态链接(理由见 scale.h / build.bat)
    HMODULE dc = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (dc == nullptr) dc = LoadLibraryW(L"d3dcompiler_47.dll");
    PFN_Compile compile = dc ? reinterpret_cast<PFN_Compile>(GetProcAddress(dc, "D3DCompile")) : nullptr;
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    PFN_Serialize serialize = d12 ? reinterpret_cast<PFN_Serialize>(GetProcAddress(d12, "D3D12SerializeRootSignature")) : nullptr;
    if (compile == nullptr)   { s_failed = true; s_note = "找不到 d3dcompiler_47.dll / D3DCompile"; FRMSTAT_LOG("[frmstat] %s", s_note.c_str()); return false; }
    if (serialize == nullptr) { s_failed = true; s_note = "进程里没有 d3d12.dll / D3D12SerializeRootSignature"; FRMSTAT_LOG("[frmstat] %s", s_note.c_str()); return false; }

    ID3DBlob *cs = nullptr, *err = nullptr;
    HRESULT hr = compile(kHlsl, sizeof(kHlsl) - 1, "033_frmstat", nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs, &err);
    if (FAILED(hr))
    {
        s_note = err ? std::string(static_cast<const char *>(err->GetBufferPointer()), err->GetBufferSize()) : "着色器编译失败";
        Rel(err); s_failed = true;
        FRMSTAT_LOG("[frmstat] 着色器编译失败: %s", s_note.c_str());
        return false;
    }
    Rel(err);

    // 根签名: [0] 常量 12 个 DWORD, [1] 表 SRV t0..t2, [2] 表 UAV u0。没有采样器(全部 Load)。
    D3D12_DESCRIPTOR_RANGE r_srv = {}; r_srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r_srv.NumDescriptors = 3;
    D3D12_DESCRIPTOR_RANGE r_uav = {}; r_uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; r_uav.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER p[3] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants.Num32BitValues = kConsts; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable.NumDescriptorRanges = 1; p[1].DescriptorTable.pDescriptorRanges = &r_srv; p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable.NumDescriptorRanges = 1; p[2].DescriptorTable.pDescriptorRanges = &r_uav; p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 3; rsd.pParameters = p;

    ID3DBlob *sig = nullptr;
    hr = serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    Rel(err);
    if (FAILED(hr)) { Rel(cs); s_failed = true; s_note = "根签名序列化失败"; FRMSTAT_LOG("[frmstat] %s 0x%08X", s_note.c_str(), static_cast<unsigned>(hr)); return false; }
    hr = dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&s_rs));
    Rel(sig);
    if (FAILED(hr)) { Rel(cs); s_failed = true; s_note = "根签名创建失败"; FRMSTAT_LOG("[frmstat] %s 0x%08X", s_note.c_str(), static_cast<unsigned>(hr)); return false; }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = s_rs; pd.CS.pShaderBytecode = cs->GetBufferPointer(); pd.CS.BytecodeLength = cs->GetBufferSize();
    hr = dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&s_pso));
    Rel(cs);
    if (FAILED(hr)) { Destroy(); s_failed = true; s_note = "PSO 创建失败"; FRMSTAT_LOG("[frmstat] %s 0x%08X", s_note.c_str(), static_cast<unsigned>(hr)); return false; }

    // 描述符堆: shader-visible, kDescRing 组 x (3 SRV + 1 UAV)
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = static_cast<UINT>(kDescRing * kDescPerSet);
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&s_heap));
    if (FAILED(hr)) { Destroy(); s_failed = true; s_note = "描述符堆创建失败"; FRMSTAT_LOG("[frmstat] %s 0x%08X", s_note.c_str(), static_cast<unsigned>(hr)); return false; }
    s_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 三块缓冲: 计数(UAV 态常驻) / 全零(UPLOAD) / 回读环(READBACK)
    if (!MakeBuffer(dev, &s_cnt, D3D12_HEAP_TYPE_DEFAULT, kBytes,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeBuffer(dev, &s_zero, D3D12_HEAP_TYPE_UPLOAD, kBytes,
                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ) ||
        !MakeBuffer(dev, &s_read, D3D12_HEAP_TYPE_READBACK, static_cast<UINT64>(kBytes) * kRing,
                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST))
    { Destroy(); s_failed = true; s_note = "缓冲创建失败"; FRMSTAT_LOG("[frmstat] %s", s_note.c_str()); return false; }

    // 全零缓冲真的写成零(不依赖「堆默认清零」那条约定, 驱动各有各的脾气)
    {
        D3D12_RANGE none = { 0, 0 };
        void *pz = nullptr;
        if (FAILED(s_zero->Map(0, &none, &pz)) || pz == nullptr)
        { Destroy(); s_failed = true; s_note = "零缓冲映射失败"; FRMSTAT_LOG("[frmstat] %s", s_note.c_str()); return false; }
        std::memset(pz, 0, kBytes);
        s_zero->Unmap(0, nullptr);
    }

    // 计数缓冲的 UAV 描述符永远不变, 每组都先放好, 每帧只写三个 SRV
    for (int i = 0; i < kDescRing; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = DXGI_FORMAT_UNKNOWN;
        uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uv.Buffer.FirstElement = 0;
        uv.Buffer.NumElements = static_cast<UINT>(kCount);
        uv.Buffer.StructureByteStride = 4;
        D3D12_CPU_DESCRIPTOR_HANDLE h = s_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(i * kDescPerSet + 3) * s_inc;
        dev->CreateUnorderedAccessView(s_cnt, nullptr, &uv, h);
    }

    s_dev = dev;
    s_stats.residual_mean_static_ref = -1.0f;
    s_ready = true;
    s_note = "就绪";
    FRMSTAT_LOG("[frmstat] 就绪: 步长 %d, 计数 %u 字节 x %d 格", cfg_stride, kBytes, kRing);
    return true;
}

// TYPELESS → 能建 SRV 的对应格式; 其余原样
static DXGI_FORMAT ViewFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16_TYPELESS:       return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:       return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R8G8_TYPELESS:         return DXGI_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16_TYPELESS:          return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS:          return DXGI_FORMAT_R32_FLOAT;
    default:                                return f;
    }
}

static bool IsTypeless(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G8X24_TYPELESS:     case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:     case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R32_TYPELESS:          case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R8G8_TYPELESS:         case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R8_TYPELESS:           case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC2_TYPELESS:          case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC4_TYPELESS:          case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:     case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_BC6H_TYPELESS:         case DXGI_FORMAT_BC7_TYPELESS:
        return true;
    default:
        return false;
    }
}

// 资源自己的格式优先(TYPELESS 映射成对应的 FLOAT/UNORM); 映射不了才用调用者给的提示
static DXGI_FORMAT PickFormat(ID3D12Resource *r, DXGI_FORMAT hint)
{
    if (r == nullptr) return DXGI_FORMAT_UNKNOWN;
    const D3D12_RESOURCE_DESC d = r->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) return DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT f = d.Format;
    if (IsTypeless(f)) f = ViewFormat(f);
    if (IsTypeless(f) || f == DXGI_FORMAT_UNKNOWN)
    {
        f = ViewFormat(hint);
        if (IsTypeless(f)) f = DXGI_FORMAT_UNKNOWN;
    }
    return f;
}

static void MakeSrv(ID3D12Resource *r, DXGI_FORMAT f, int desc_index)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = (r != nullptr) ? f : DXGI_FORMAT_R8G8B8A8_UNORM;   // 空描述符也要个合法格式
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE h = s_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(desc_index) * s_inc;
    s_dev->CreateShaderResourceView(r, &sv, h);                  // r 为空 = 空描述符(合法, 读出来全 0)
}

static void Barrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &b);
}

// 直方图分位数: 桶内线性插值。q=0.5 是中位数。n=0 返回 0。
static float Quantile(const uint32_t *hist, uint32_t n, double q)
{
    if (n == 0) return 0.0f;
    const double target = q * static_cast<double>(n);
    double cum = 0.0;
    for (int b = 0; b < kBins; ++b)
    {
        const double h = static_cast<double>(hist[b]);
        if (cum + h >= target)
        {
            const double within = (h > 0.0) ? (target - cum) / h : 0.0;
            return static_cast<float>(static_cast<double>(b) + ((within < 0.0) ? 0.0 : (within > 1.0) ? 1.0 : within));
        }
        cum += h;
    }
    return static_cast<float>(kBins);
}

// 把一格回读解析成 Stats。标签对不上 = GPU 还没跑到那格, 不动 s_stats。
static void Parse(const uint32_t *c, unsigned want_tag)
{
    if (c[kIdxTagA] != want_tag || c[kIdxTagB] != want_tag) { ++s_stale; return; }
    Stats st = s_stats;                       // 静止参考值要继承
    st.mv_samples   = c[kIdxMvN];
    st.mv_bad       = c[kIdxMvBad];
    st.px_samples   = c[kIdxPx];
    st.res_samples  = c[kIdxResN];
    st.frame_tag    = want_tag;
    st.has_mv       = st.mv_samples > 0;
    st.mv_median_px = Quantile(c, st.mv_samples, 0.5);
    st.mv_p90_px    = Quantile(c, st.mv_samples, 0.9);
    st.mv_overflow_frac = st.has_mv ? static_cast<float>(static_cast<double>(c[kBins - 1]) / static_cast<double>(st.mv_samples)) : 0.0f;
    st.dark_frac    = (st.px_samples > 0) ? static_cast<float>(static_cast<double>(c[kIdxDark]) / static_cast<double>(st.px_samples)) : 0.0f;
    st.has_residual = st.res_samples > 0;
    if (st.has_residual)
    {
        const double sum = static_cast<double>(c[kIdxResInt]) + static_cast<double>(c[kIdxResFrac]) / kFracScale;
        st.residual_mean = static_cast<float>(sum / static_cast<double>(st.res_samples));
        // 静止参考: 画面基本没动、也不是一片黑的帧, 模型改了多少就是它「正常出力」的量
        if (st.has_mv && st.mv_median_px < cfg_static_mv_px && st.dark_frac < 0.9f)
        {
            if (st.residual_mean_static_ref < 0.0f) st.residual_mean_static_ref = st.residual_mean;
            else st.residual_mean_static_ref += cfg_static_ema * (st.residual_mean - st.residual_mean_static_ref);
        }
    }
    st.valid = true;
    s_stats = st;
    ++s_reads;
}

// 每帧一次。往 cl 里录: 清零 → 派发 → 拷进回读环; 顺手把两帧前那格解析掉(不等 GPU)。
//   mv        运动矢量(游戏的, 渲染分辨率 gw x gh 在左上角; 资源可以更大), 可空
//   proxy     模型看到的代理图(s_small), pw x ph, 可空
//   model_out 模型输出(s_out, 跟 proxy 同尺寸同格式); 这帧没跑模型就传空 —— 残差那一项就没有
//   proxy_is_srgb_encoded  代理图是不是 sRGB 编码(就地插入路上两种分支都是: 1)
//   mv_scale_x/y  把运动矢量数值换算成像素的系数 —— 传 Stage 拿到的 mvScaleX/Y(我们的转发器原样透传给模型的那对)
//   dark_knee  暗膝(线性, 白点归一后), 跟 scale.h 合成里的 kDarkKnee 一致
//   ★绑定★ 跟别的 pass 一样绑我们自己的堆/根签名/PSO, 派发完不还 —— 还给游戏是 staterestore::Envelope 的事。
//   ★状态★ mv/proxy/model_out 必须已在 NON_PIXEL_SHADER_RESOURCE 态, 这里一个 barrier 都不打在它们身上。
static void Dispatch(ID3D12GraphicsCommandList *cl,
                     ID3D12Resource *mv, DXGI_FORMAT mv_fmt, unsigned gw, unsigned gh,
                     ID3D12Resource *proxy, DXGI_FORMAT proxy_fmt, unsigned pw, unsigned ph,
                     ID3D12Resource *model_out, int proxy_is_srgb_encoded,
                     float mv_scale_x = 1.0f, float mv_scale_y = 1.0f, float dark_knee = 0.05f)
{
    if (!s_ready || !cfg_enabled || cl == nullptr) return;

    int stride = cfg_stride;
    if (stride < 1) stride = 1; else if (stride > 16) stride = 16;
    const unsigned s = static_cast<unsigned>(stride);

    const DXGI_FORMAT mvf = PickFormat(mv, mv_fmt);
    const DXGI_FORMAT pxf = PickFormat(proxy, proxy_fmt);
    const bool has_mv    = (mv != nullptr) && mvf != DXGI_FORMAT_UNKNOWN && gw > 0 && gh > 0;
    const bool has_proxy = (proxy != nullptr) && pxf != DXGI_FORMAT_UNKNOWN && pw > 0 && ph > 0;
    const bool has_model = has_proxy && (model_out != nullptr);
    if (!has_mv && !has_proxy) return;          // 什么都没有, 这帧不录(格子不前进)

    // 派发格子覆盖两张图里采样点更多的那张; 每个线程自己按尺寸剪
    const unsigned nx_mv = has_mv    ? (gw + s - 1) / s : 0;
    const unsigned ny_mv = has_mv    ? (gh + s - 1) / s : 0;
    const unsigned nx_px = has_proxy ? (pw + s - 1) / s : 0;
    const unsigned ny_px = has_proxy ? (ph + s - 1) / s : 0;
    const unsigned nx = (nx_mv > nx_px) ? nx_mv : nx_px;
    const unsigned ny = (ny_mv > ny_px) ? ny_mv : ny_px;

    // 描述符: 这一组三个 SRV(u0 那个建堆时就放好了)
    const int base = s_dslot * kDescPerSet;
    MakeSrv(has_mv    ? mv        : nullptr, mvf, base + 0);
    MakeSrv(has_proxy ? proxy     : nullptr, pxf, base + 1);
    MakeSrv(has_model ? model_out : nullptr, pxf, base + 2);
    D3D12_GPU_DESCRIPTOR_HANDLE g_srv = s_heap->GetGPUDescriptorHandleForHeapStart();
    g_srv.ptr += static_cast<UINT64>(base) * s_inc;
    D3D12_GPU_DESCRIPTOR_HANDLE g_uav = s_heap->GetGPUDescriptorHandleForHeapStart();
    g_uav.ptr += static_cast<UINT64>(base + 3) * s_inc;

    // 清零: 从全零缓冲整块拷过来
    Barrier(cl, s_cnt, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->CopyResource(s_cnt, s_zero);
    Barrier(cl, s_cnt, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // 派发
    if (++s_tag == 0) s_tag = 1;                // 绕回来了也不用 0(0 = 没写过)
    unsigned flags = 0;
    if (has_mv)    flags |= 1u;
    if (has_proxy) flags |= 2u;
    if (has_model) flags |= 4u;
    if (proxy_is_srgb_encoded) flags |= 8u;
    UINT c[kConsts] = {};
    c[0] = gw; c[1] = gh; c[2] = pw; c[3] = ph;
    c[4] = s;  c[5] = flags; c[6] = s_tag; c[7] = 0;
    std::memcpy(&c[8],  &mv_scale_x, 4);
    std::memcpy(&c[9],  &mv_scale_y, 4);
    std::memcpy(&c[10], &dark_knee,  4);
    c[11] = 0;

    ID3D12DescriptorHeap *heaps[] = { s_heap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(s_rs);
    cl->SetPipelineState(s_pso);
    cl->SetComputeRoot32BitConstants(0, kConsts, c, 0);
    cl->SetComputeRootDescriptorTable(1, g_srv);
    cl->SetComputeRootDescriptorTable(2, g_uav);
    cl->Dispatch((nx + 7) / 8, (ny + 7) / 8, 1);

    // 拷进回读环这一格(UAV → COPY_SOURCE 的转换本身就把 UAV 写完的事同步掉了)
    Barrier(cl, s_cnt, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->CopyBufferRegion(s_read, static_cast<UINT64>(kBytes) * static_cast<UINT64>(s_slot), s_cnt, 0, kBytes);
    Barrier(cl, s_cnt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    s_slot_tag[s_slot] = s_tag;

    // 两帧前那格读回来(环长 3, (slot+1)%3 就是最老的一格)。没有围栏, 靠首尾标签认完整。
    const int old = (s_slot + 1) % kRing;
    if (s_slot_tag[old] != 0)
    {
        D3D12_RANGE rg = { static_cast<SIZE_T>(kBytes) * static_cast<SIZE_T>(old), static_cast<SIZE_T>(kBytes) * static_cast<SIZE_T>(old + 1) };
        void *p = nullptr;
        if (SUCCEEDED(s_read->Map(0, &rg, &p)) && p != nullptr)
        {
            uint32_t local[kCount];
            std::memcpy(local, static_cast<const uint8_t *>(p) + rg.Begin, kBytes);   // 先整块抄下来, 再解析(免得读一半被 GPU 改)
            D3D12_RANGE none = { 0, 0 };
            s_read->Unmap(0, &none);
            Parse(local, s_slot_tag[old]);
        }
    }

    s_slot  = (s_slot + 1) % kRing;
    s_dslot = (s_dslot + 1) % kDescRing;
}

// 最近一次完整读回的统计(落后约两帧)。valid=false 表示还没有任何一帧读回。
static const Stats &Read()      { return s_stats; }
static bool         ready()     { return s_ready; }
static bool         failed()    { return s_failed; }
static const char  *note()      { return s_note.c_str(); }
static unsigned     stale()     { return s_stale; }     // 标签对不上的次数(偶尔有正常; 一直涨说明 CPU 领先太多)
static unsigned     reads()     { return s_reads; }
static unsigned     dispatches(){ return s_tag; }

} // namespace frmstat
