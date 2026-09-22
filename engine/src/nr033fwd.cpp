#include "nr_contract.h"
// ============================================================================
//  033 神经渲染转发器   nvngx.dll_033.dll
//  整理: B站 @热心网友033      本文件由本项目独立编写, 不含第三方代码。
// ============================================================================
//
//  ★这个 DLL 存在的唯一理由是它的文件名★
//
//  NVIDIA 的神经渲染运行库 nvngx_dlssnr.dll 会顺着【返回地址】认出调用方模块,
//  要求那个模块的路径里含 "nvngx.dll" 这个子串, 否则在读第一个参数之前就直接
//  返回 0xBAD00002 (FAIL_PlatformError)。ReShade 插件也好、别的什么也好, 名字
//  都过不了这道门 —— 所以真正的调用从这里发出, 我们的插件通过下面几个导出
//  隔一层去用它。文件名 "nvngx.dll_033.dll" 里正好含有那个子串。
//
//  ★两条铁律★
//  1) 绝对不能写成尾调用。 return f(...) 会被编译成 jmp, 我们这一帧就没了,
//     运行库顺着返回地址认到的是【我们的调用方】, 照样被拒。所以每个转发都
//     写成 volatile 变量接一下再 return。
//  2) 参数块要用核心给的【能力块】, 不能自己 AllocateParameters 新建一个 ——
//     能力块里带着建 feature 时要用的 snippet/preset 回调, 新块里没有。
//     (这一步在调用方 nrfwd.h 里做, 传进来就是。)
//
//  ★参数块是靠 vtable 驱动的★
//  核心不导出 Set/Get 辅助函数(它们是静态库里的内联)。NVSDK_NGX_Parameter
//  声明了 8 个 Set 重载再 8 个 Get, 顺序是:
//      ULL / float / double / uint / int / ID3D11Resource* / ID3D12Resource* / void*
//  但★实测这台机器上 float 并不在第 1 槽而在第 6 槽★, 所以 float 的槽位由
//  调用方用 probe 逐个试出来再告诉我们。其余(整数、资源指针)一律走第 0 槽的
//  64 位 setter, 实测通吃。
//
//  编译: 见本目录 build_nr033fwd.bat
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>      // 参数块 vtable 里有 ID3D11Resource, 少这个头结构体解析不了
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "ngx/nvsdk_ngx.h"

// ---------------------------------------------------------------------------
// 结果码, 给调用方看
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) int nr033_last_init   = 0;
extern "C" __declspec(dllexport) int nr033_preset_set  = -1;   // 我们设进去的预设
extern "C" __declspec(dllexport) int nr033_preset_back = -1;   // 参数容器回读 (-1=读不到)，不是模型接受证明
extern "C" __declspec(dllexport) int nr033_last_create = 0;

// ---------------------------------------------------------------------------
// ★入口在运行库本体里, 不在驱动核心里★
// nvngx_dlssnr.dll 自己就导出了整套 NVSDK_NGX_D3D12_*(dumpbin 可查)。
// 门禁也在它里面 —— 所以必须调【它的】导出, 从本模块发起。
// 调驱动核心 _nvngx.dll 那套是走不通的: Init 能过, 但 CreateFeature 一律回
// 0xBAD0000B(建不起来), 因为核心根本没装神经渲染这个 feature。
// 核心那边我们只用来拿能力参数块(在调用方 nrfwd.h 里做)。
// ---------------------------------------------------------------------------
namespace
{
// ★这个 Init_Ext 的签名跟核心那个不一样★ 第 5 个参数是【能力参数块】,
// 不是 FeatureCommonInfo。版本号用 0x15。
using PFN_Init = NVSDK_NGX_Result(NVSDK_CONV *)(unsigned long long appId, const wchar_t *dataPath,
                                                ID3D12Device *dev, unsigned version,
                                                NVSDK_NGX_Parameter *params);
using PFN_CreateFeature = NVSDK_NGX_Result(NVSDK_CONV *)(ID3D12GraphicsCommandList *,
                                                         NVSDK_NGX_Feature,
                                                         NVSDK_NGX_Parameter *,
                                                         NVSDK_NGX_Handle **);
using PFN_EvalFeature = NVSDK_NGX_Result(NVSDK_CONV *)(ID3D12GraphicsCommandList *,
                                                       const NVSDK_NGX_Handle *,
                                                       const NVSDK_NGX_Parameter *,
                                                       PFN_NVSDK_NGX_ProgressCallback);
using PFN_ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV *)(NVSDK_NGX_Handle *);

HMODULE            g_snip    = nullptr;
PFN_Init           g_init    = nullptr;
PFN_CreateFeature  g_create  = nullptr;
PFN_EvalFeature    g_eval    = nullptr;
PFN_ReleaseFeature g_release = nullptr;
bool               g_inited  = false;
int                g_floatSlot = -1;      // 由调用方探出来后告诉我们

bool snippet_ready(const wchar_t *snippetPath)
{
    if (g_init != nullptr) return true;
    if (g_snip == nullptr)
    {
        if (snippetPath != nullptr && *snippetPath != 0)
            g_snip = LoadLibraryExW(snippetPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (g_snip == nullptr) g_snip = GetModuleHandleW(L"nvngx_dlssnr.dll");
    }
    if (g_snip == nullptr) return false;
    g_init    = reinterpret_cast<PFN_Init>(GetProcAddress(g_snip, "NVSDK_NGX_D3D12_Init_Ext"));
    g_create  = reinterpret_cast<PFN_CreateFeature>(GetProcAddress(g_snip, "NVSDK_NGX_D3D12_CreateFeature"));
    g_eval    = reinterpret_cast<PFN_EvalFeature>(GetProcAddress(g_snip, "NVSDK_NGX_D3D12_EvaluateFeature"));
    g_release = reinterpret_cast<PFN_ReleaseFeature>(GetProcAddress(g_snip, "NVSDK_NGX_D3D12_ReleaseFeature"));
    return g_init != nullptr && g_create != nullptr && g_eval != nullptr;
}

// ---- 参数块: 直接走 vtable ------------------------------------------------
// x64 上成员函数的 this 走 RCX, 后面接参数, 所以下面这两个原型就够用。
using PFN_SetULL   = void(__fastcall *)(void *, const char *, unsigned long long);
using PFN_SetFloat = void(__fastcall *)(void *, const char *, float);
using PFN_SetUInt  = void(__fastcall *)(void *, const char *, unsigned int);

inline void **vtbl(void *p) { return p ? *reinterpret_cast<void ***>(p) : nullptr; }

// ★整数往几个候选槽位都写一遍★
// float 实测在第 6 槽而不是头文件说的第 1 槽 —— 这块的 vtable 顺序整个跟头文件
// 对不上, 所以整数的槽位也不能照头文件猜。参数块是【按类型分开存】的, 用错
// setter 存进去, snippet 去读对应类型时就是空的, 表现就是 CreateFeature 回
// 0xBAD0000B(建不起来), 而且一点线索都不给。
// 8 个 Set 重载的形状都是 (this, const char*, 值), 多写几遍不会出事;
// 命中的那个负责生效, 没命中的存在没人读的类型里, 无害。
// 头文件声明的 8 个 Set 重载顺序: ULL / float / double / uint / int / D3D11Res / D3D12Res / void*
// 实测: uint 确实在第 3 槽, 64 位(资源指针)在第 0 槽; ★只有 float 不在第 1 槽而在第 6 槽★,
// 所以 float 的槽位交给调用方探。
constexpr int VT_SET_ULL  = 0;
constexpr int VT_SET_UINT = 3;

void set_u64(void *params, const char *name, unsigned long long v)
{
    void **vt = vtbl(params);
    if (vt == nullptr) return;
    reinterpret_cast<PFN_SetULL>(vt[VT_SET_ULL])(params, name, v);
}
void set_u32(void *params, const char *name, unsigned int v)
{
    void **vt = vtbl(params);
    if (vt == nullptr) return;
    reinterpret_cast<PFN_SetUInt>(vt[VT_SET_UINT])(params, name, v);
}
void set_f32_slot(void *params, const char *name, float v, int slot)
{
    void **vt = vtbl(params);
    if (vt == nullptr || slot < 0 || slot > 15) return;
    reinterpret_cast<PFN_SetFloat>(vt[slot])(params, name, v);
}
inline void set_f32(void *params, const char *name, float v)
{
    if (g_floatSlot >= 0) set_f32_slot(params, name, v, g_floatSlot);
}
inline void set_res(void *params, const char *name, void *res)
{
    set_u64(params, name, reinterpret_cast<unsigned long long>(res));
}
} // namespace

// ===========================================================================
//  导出
// ===========================================================================

// 调用方探花点槽位: 往指定槽位写一个 float, 它那边再 Get 回来对一下。
extern "C" __declspec(dllexport)
void __cdecl nr033_probe_float(void *params, const char *name, float value, int slot)
{
    set_f32_slot(params, name, value, slot);
}

// 探出来之后固定下来
extern "C" __declspec(dllexport)
void __cdecl nr033_set_float_slot(int slot)
{
    g_floatSlot = slot;
}

// ---------------------------------------------------------------------------
// 建 feature 18 (神经渲染)
// snippetPath = 游戏目录里的 nvngx_dlssnr.dll; 它所在目录要进 NGX 的搜索路径,
// 否则核心找不到这份运行库。
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport)
void *__cdecl nr033_create_v2(const wchar_t *snippetPath, const wchar_t *dataPath,
                           ID3D12Device *dev, ID3D12GraphicsCommandList *cmd,
                           void *capParams, unsigned w, unsigned h, int preset,
                           float intensity, int style, float localStructure,
                           float localTone, float skinStructure, int useAutoMask,
                           int uiCorrection, float globalTone)
{
    // ★逐步哨兵★ 崩了就看停在哪个值上 —— 那一步就是崩的那一步。
    //   还是 0 = 这个函数一步都没进(调用本身就炸, 说明函数指针/参数传递有问题)。
    nr033_last_init = -10; nr033_last_create = 0;
    if (dev == nullptr || cmd == nullptr)     { nr033_last_init = -3; return nullptr; }
    if (capParams == nullptr)                 { nr033_last_init = -2; return nullptr; }
    nr033_last_init = -11;                    // 过了空检查, 准备加载运行库
    if (!snippet_ready(snippetPath))          { nr033_last_init = -1; return nullptr; }
    nr033_last_init = -12;                    // 运行库就位, 准备 Init_Ext

    if (!g_inited)
    {
        // ★这个 Init_Ext 是运行库自己的, 不是驱动核心那个★
        //   第 5 个参数是【能力参数块】(不是 FeatureCommonInfo), 版本号 0x15。
        //   传错了核心回 0xBAD0000C(FAIL_OutOfDate), 一点线索不给。
        volatile NVSDK_NGX_Result ir =
            g_init(0x24480451ull, dataPath ? dataPath : L".", dev, 0x15,
                   reinterpret_cast<NVSDK_NGX_Parameter *>(capParams));
        nr033_last_init = static_cast<int>(ir);
        // ★就地插入(游戏进程内)：NGX 已被游戏 Init 过，我们再 Init 常回 0★
        //   0 不是文档里的任何结果码。实测这只是"已经起来了"的一种表达 ——
        //   把 0 也当成放行，继续去 CreateFeature；真出错会是 0xBAD.. 那才拦。
        //   桌面/帮手路第一次 Init 会返 0x1，照走。
        if (ir != NVSDK_NGX_Result_Success &&
            ir != NVSDK_NGX_Result_FAIL_FeatureAlreadyExists &&
            static_cast<unsigned>(ir) != 0u) return nullptr;
        g_inited = true;
    }

    // ★调节类参数是建 feature 时读一次的★ 之后在 evaluate 里再设也不会生效,
    // 所以改任何一项都必须重建 feature。
    // ★NGX 通用的建 feature 参数, 少了就是 0xBAD0000B(建不起来)★
    // 这两个名字在运行库的字符串表里能扒到, 不是 DLSSNR. 前缀的。
    // A capability block survives model replacement. Previous Evaluate calls
    // leave resource pointers and subrects in it, including freed startup-sized
    // textures. Preserve its capability callbacks, but do not expose those
    // frame bindings to creation of a different-sized model.
    for (const char* family : {"Color", "Output", "Depth", "MVec", "UI", "UIAlpha", "Backbuffer"}) {
        char key[96];
        std::snprintf(key,sizeof(key),"DLSSNR.%s",family);set_res(capParams,key,nullptr);
        const bool image=std::strcmp(family,"Color")==0 || std::strcmp(family,"Output")==0;
        for (const char* suffix : {"BaseX", "BaseY", "Width", "Height"}) {
            unsigned value=0;
            if(image && std::strcmp(suffix,"Width")==0)value=w;
            if(image && std::strcmp(suffix,"Height")==0)value=h;
            std::snprintf(key,sizeof(key),"DLSSNR.%sSubrect%s",family,suffix);set_u32(capParams,key,value);
        }
    }
    set_u32(capParams,"DLSSNR.Reset",1u);
    set_u32(capParams,"DLSSNR.DepthInverted",0u);
    set_f32(capParams,"DLSSNR.MVecScaleX",0.f);set_f32(capParams,"DLSSNR.MVecScaleY",0.f);
    nr033_last_create = -20;   // Init 过了, 准备写参数
    set_u32(capParams, "CreationNodeMask", static_cast<unsigned int>(1ull));
    set_u32(capParams, "VisibilityNodeMask", static_cast<unsigned int>(1ull));
    set_u32(capParams, "DLSSNR.Width", static_cast<unsigned int>(w));
    set_u32(capParams, "DLSSNR.Height", static_cast<unsigned int>(h));
    // ★几何信息必须喚全★
    //   主插件(renodx)除了 Width/Height, 还会喚 Input/Output 尺寸、Scale、Upscaling。
    //   不告诉模型输入输出各多大, 它只能按自己的默认假设走 —— 一旦跟实际不符,
    //   出来的就是“对不齐”的软画面。我们始终 1:1(放大自己做), 所以输入=输出。
    set_f32(capParams, "DLSSNR.ScalingRatio", 1.0f);   // 模型只肯 1:1, 放大我们自己做
    set_u32(capParams, "DLSSNR.Style", static_cast<unsigned int>(static_cast<unsigned long long>(style)));
    set_u32(capParams, "DLSSNR.UseAutoMask", static_cast<unsigned int>(static_cast<unsigned long long>(useAutoMask)));
    set_u32(capParams, "DLSSNR.UICorrection", static_cast<unsigned int>(static_cast<unsigned long long>(uiCorrection)));
    set_u32(capParams, "DLSSNR.Enabled", static_cast<unsigned int>(1ull));
    // ★总是发, 不能只在 preset>0 时发★
    // 能力参数块是共用且持久的: 之前设过 3, 切回「默认」时不发,
    // 那个 3 就一直留在里面 —— 表现是“选了也白选”。
    set_u32(capParams, "DLSSNR.Hint.Render.Preset", static_cast<unsigned int>(preset));
    // This reads our parameter container BEFORE CreateFeature. It proves only
    // storage, not that a preset is recognized, accepted, or visually distinct.
    {
        unsigned int back = 0xFFFFFFFFu;
        auto *p = reinterpret_cast<NVSDK_NGX_Parameter *>(capParams);
        const NVSDK_NGX_Result gr = p->Get("DLSSNR.Hint.Render.Preset", &back);
        nr033_preset_set  = static_cast<int>(preset);
        nr033_preset_back = (gr == NVSDK_NGX_Result_Success) ? static_cast<int>(back) : -1;
    }
    set_f32(capParams, "DLSSNR.Intensity",              intensity);
    set_f32(capParams, "DLSSNR.LocalStructureStrength", localStructure);
    set_f32(capParams, "DLSSNR.LocalToneStrength",      localTone);
    set_f32(capParams, "DLSSNR.GlobalToneStrength",     globalTone);
    set_f32(capParams, "DLSSNR.SkinStructureStrength",  skinStructure);

    nr033_last_create = -21;   // 参数写完了, 准备 CreateFeature
    NVSDK_NGX_Handle *feature = nullptr;
    volatile NVSDK_NGX_Result cr =
        g_create(cmd, static_cast<NVSDK_NGX_Feature>(18),
                 reinterpret_cast<NVSDK_NGX_Parameter *>(capParams), &feature);
    nr033_last_create = static_cast<int>(cr);
    if (cr != NVSDK_NGX_Result_Success) return nullptr;
    return feature;
}

// ---------------------------------------------------------------------------
// 每帧求值
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport)
int __cdecl nr033_evaluate_v2(ID3D12GraphicsCommandList *cmd, void *feature, void *capParams,
                           ID3D12Resource *color, ID3D12Resource *depth,
                           ID3D12Resource *motion, ID3D12Resource *output, unsigned w,
                           unsigned h, unsigned guideW, unsigned guideH, int depthInverted,
                           int reset, float intensity, int style, float localStructure,
                           float localTone, float skinStructure, int useAutoMask,
                           float mvScaleX, float mvScaleY, const nrcontract::Guides *guides, float globalTone)
{
    if (!snippet_ready(nullptr) || feature == nullptr || capParams == nullptr) return 0;
    if (guides != nullptr && guides->size != sizeof(nrcontract::Guides)) return 0;
    nrcontract::Guides rects;
    rects.depth = {0, 0, guideW, guideH}; rects.motion = rects.depth;
    if (guides != nullptr) rects = *guides;

    // ★★把 UI / 后缓冲那一族显式清空 —— 每帧都写★★ (2026-09-04 夜)
    //   参考实现的 SetExtras 每帧无条件写 nullptr + 尺寸 0(两个调用点都是
    //   SetExtras(cfg, nullptr, nullptr, 0,0,0,0))。我们一个都没写过。
    //   为什么这很要命: 就地插入这条路借的是【游戏那块活的参数板】, 里面可能留着
    //   Streamline 或游戏自己塞进去的旧指针; 而「界面保护」(UICorrection)是开着的,
    //   等于让模型拿一张来路不明的图去做界面校正 —— 出来什么样谁也不知道。
    set_res(capParams, "DLSSNR.UI",         nullptr);
    set_res(capParams, "DLSSNR.UIAlpha",    nullptr);
    set_res(capParams, "DLSSNR.Backbuffer", nullptr);
    set_u32(capParams, "DLSSNR.UISubrectBaseX",  0u);
    set_u32(capParams, "DLSSNR.UISubrectBaseY",  0u);
    set_u32(capParams, "DLSSNR.UISubrectWidth",  0u);
    set_u32(capParams, "DLSSNR.UISubrectHeight", 0u);
    set_u32(capParams, "DLSSNR.UIAlphaSubrectBaseX",  0u);
    set_u32(capParams, "DLSSNR.UIAlphaSubrectBaseY",  0u);
    set_u32(capParams, "DLSSNR.UIAlphaSubrectWidth",  0u);
    set_u32(capParams, "DLSSNR.UIAlphaSubrectHeight", 0u);
    set_u32(capParams, "DLSSNR.BackbufferSubrectBaseX",  0u);
    set_u32(capParams, "DLSSNR.BackbufferSubrectBaseY",  0u);
    set_u32(capParams, "DLSSNR.BackbufferSubrectWidth",  0u);
    set_u32(capParams, "DLSSNR.BackbufferSubrectHeight", 0u);

    set_res(capParams, "DLSSNR.Color",  color);
    set_res(capParams, "DLSSNR.Depth",  depth);
    set_res(capParams, "DLSSNR.MVec",   motion);
    set_res(capParams, "DLSSNR.Output", output);

    // ★每帧都得重设一遍: 这块参数是跟游戏自己的 DLSS 共用的★
    //   参考实现原话: "The block is shared with the game's own DLSS, which
    //   overwrites these between frames" —— 就地插入这条路借的正是游戏那块参数块,
    //   游戏每帧都会往里写。所以凡是模型会读的键, 求值前一律重设, 不能指望
    //   建 feature 时留下的值还在。
    set_u32(capParams, "DLSSNR.Enabled", 1u);
    // 起点一律归零 —— 别人留在块里的偏移会让模型从半张图开始读
    set_u32(capParams, "DLSSNR.ColorSubrectBaseX",  0u);
    set_u32(capParams, "DLSSNR.ColorSubrectBaseY",  0u);
    set_u32(capParams, "DLSSNR.OutputSubrectBaseX", 0u);
    set_u32(capParams, "DLSSNR.OutputSubrectBaseY", 0u);
    set_u32(capParams, "DLSSNR.DepthSubrectBaseX",  rects.depth.x);
    set_u32(capParams, "DLSSNR.DepthSubrectBaseY",  rects.depth.y);
    set_u32(capParams, "DLSSNR.MVecSubrectBaseX",   rects.motion.x);
    set_u32(capParams, "DLSSNR.MVecSubrectBaseY",   rects.motion.y);
    // 颜色/输出按处理尺寸, 深度和运动矢量可能是另一个尺寸(引导图分辨率)
    set_u32(capParams, "DLSSNR.ColorSubrectWidth", static_cast<unsigned int>(w));
    set_u32(capParams, "DLSSNR.ColorSubrectHeight", static_cast<unsigned int>(h));
    set_u32(capParams, "DLSSNR.OutputSubrectWidth", static_cast<unsigned int>(w));
    set_u32(capParams, "DLSSNR.OutputSubrectHeight", static_cast<unsigned int>(h));
    set_u32(capParams, "DLSSNR.DepthSubrectWidth", rects.depth.width);
    set_u32(capParams, "DLSSNR.DepthSubrectHeight", rects.depth.height);
    set_u32(capParams, "DLSSNR.MVecSubrectWidth", rects.motion.width);
    set_u32(capParams, "DLSSNR.MVecSubrectHeight", rects.motion.height);
    set_u32(capParams, "DLSSNR.Width", static_cast<unsigned int>(w));
    set_u32(capParams, "DLSSNR.Height", static_cast<unsigned int>(h));
    // ★几何信息必须喚全★
    //   主插件(renodx)除了 Width/Height, 还会喚 Input/Output 尺寸、Scale、Upscaling。
    //   不告诉模型输入输出各多大, 它只能按自己的默认假设走 —— 一旦跟实际不符,
    //   出来的就是“对不齐”的软画面。我们始终 1:1(放大自己做), 所以输入=输出。
    set_u32(capParams, "DLSSNR.DepthInverted", static_cast<unsigned int>(static_cast<unsigned long long>(depthInverted)));
    set_u32(capParams, "DLSSNR.Reset", static_cast<unsigned int>(static_cast<unsigned long long>(reset)));
    set_u32(capParams, "DLSSNR.Style", static_cast<unsigned int>(static_cast<unsigned long long>(style)));
    set_u32(capParams, "DLSSNR.UseAutoMask", static_cast<unsigned int>(static_cast<unsigned long long>(useAutoMask)));

    set_f32(capParams, "DLSSNR.MVecScaleX", mvScaleX);
    set_f32(capParams, "DLSSNR.MVecScaleY", mvScaleY);
    set_f32(capParams, "DLSSNR.Intensity",              intensity);
    set_f32(capParams, "DLSSNR.LocalStructureStrength", localStructure);
    set_f32(capParams, "DLSSNR.LocalToneStrength",      localTone);
    set_f32(capParams, "DLSSNR.GlobalToneStrength",     globalTone);
    set_f32(capParams, "DLSSNR.SkinStructureStrength",  skinStructure);

    volatile NVSDK_NGX_Result r =
        g_eval(cmd, reinterpret_cast<const NVSDK_NGX_Handle *>(feature),
               reinterpret_cast<const NVSDK_NGX_Parameter *>(capParams), nullptr);
    return static_cast<int>(r);
}

// Preserve the original ABI for older add-ons and Feeder hosts.
extern "C" __declspec(dllexport)
void *__cdecl nr033_create(const wchar_t *snippetPath, const wchar_t *dataPath,
    ID3D12Device *dev, ID3D12GraphicsCommandList *cmd, void *capParams,
    unsigned w, unsigned h, int preset, float intensity, int style,
    float localStructure, float localTone, float skinStructure, int useAutoMask, int uiCorrection)
{
    return nr033_create_v2(snippetPath, dataPath, dev, cmd, capParams, w, h, preset,
        intensity, style, localStructure, localTone, skinStructure, useAutoMask, uiCorrection, 1.0f);
}
extern "C" __declspec(dllexport)
int __cdecl nr033_evaluate(ID3D12GraphicsCommandList *cmd, void *feature, void *capParams,
    ID3D12Resource *color, ID3D12Resource *depth, ID3D12Resource *motion, ID3D12Resource *output,
    unsigned w, unsigned h, unsigned guideW, unsigned guideH, int depthInverted,
    int reset, float intensity, int style, float localStructure, float localTone,
    float skinStructure, int useAutoMask, float mvScaleX, float mvScaleY)
{
    return nr033_evaluate_v2(cmd, feature, capParams, color, depth, motion, output, w, h,
        guideW, guideH, depthInverted, reset, intensity, style, localStructure, localTone,
        skinStructure, useAutoMask, mvScaleX, mvScaleY, nullptr, 1.0f);
}

extern "C" __declspec(dllexport)
void __cdecl nr033_release(void *feature)
{
    if (!snippet_ready(nullptr) || feature == nullptr || g_release == nullptr) return;
    volatile NVSDK_NGX_Result r = g_release(reinterpret_cast<NVSDK_NGX_Handle *>(feature));
    (void)r;
}

extern "C" __declspec(dllexport)
int __cdecl nr033_release_checked(void *feature)
{
    if (!snippet_ready(nullptr) || feature == nullptr || g_release == nullptr) return 0;
    // Keep this module on the call stack: the runtime validates its caller.
    // A tail call would expose the engine's address and return PlatformError.
    volatile NVSDK_NGX_Result result=g_release(reinterpret_cast<NVSDK_NGX_Handle *>(feature));
    return static_cast<int>(result);
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { /* 什么都别做: 这里跑代码容易死锁 */ }
    return TRUE;
}
