// =====================================================================
//  nrfwd.h ——  神经渲染转发器包装层
//
//  ★为什么必须有这一层★
//  运行库 nvngx_dlssnr.dll 会顺着【返回地址】认出调用方模块, 要求那个
//  模块的路径里含 "nvngx.dll", 否则在读第一个参数之前就返回
//  FAIL_PlatformError (0xBAD00002)。我们的 addon 叫 dlss5-033.addon64,
//  怎么写都过不了这道门 —— 这就是整晚 feature 18 建不起来的唯一原因。
//
//  解法: 把 NR 调用放进一个名字带 nvngx.dll 的小 DLL 里 (110KB),
//  由它代我们调。本文件就是调它的那一层。
//
//  实测(nrprobe.exe, RTX 4090): 100/75/60/50/33/25 六个档位
//  CreateFeature + EvaluateFeature 全部返回 0x1。
//
//  转发器源码来自 OptiScaler (GPL-3.0), 见 工具\署名。
// =====================================================================
#pragma once
#include <vector>
#include "nr_contract.h"
#include "nr_feature_params.h"
#include "nr_fault.h"


namespace nrfwd
{

// 转发器导出 (与 nr033fwd.cpp 一一对应 —— 那是我们自己写的, 见该文件顶部注释)
using PFN_Create = void *(__cdecl *)(const wchar_t *snippetPath, const wchar_t *dataPath,
                                     ID3D12Device *dev, ID3D12GraphicsCommandList *cmd,
                                     void *capParams, unsigned w, unsigned h, int preset,
                                     float intensity, int style, float localStructure,
                                     float localTone, float skinStructure, int useAutoMask,
                                     int uiCorrection);
using PFN_Eval = int(__cdecl *)(ID3D12GraphicsCommandList *cmd, void *feature, void *capParams,
                                ID3D12Resource *color, ID3D12Resource *depth,
                                ID3D12Resource *motion, ID3D12Resource *output, unsigned w,
                                unsigned h, unsigned guideW, unsigned guideH, int depthInverted,
                                int reset, float intensity, int style, float localStructure,
                                float localTone, float skinStructure, int useAutoMask,
                                float mvScaleX, float mvScaleY);
using PFN_CreateV2 = void *(__cdecl *)(const wchar_t *snippetPath, const wchar_t *dataPath,
                                     ID3D12Device *dev, ID3D12GraphicsCommandList *cmd,
                                     void *capParams, unsigned w, unsigned h, int preset,
                                     float intensity, int style, float localStructure,
                                     float localTone, float skinStructure, int useAutoMask,
                                     int uiCorrection, float globalTone);
using PFN_EvalV2 = int(__cdecl *)(ID3D12GraphicsCommandList *cmd, void *feature, void *capParams,
                                ID3D12Resource *color, ID3D12Resource *depth,
                                ID3D12Resource *motion, ID3D12Resource *output, unsigned w,
                                unsigned h, unsigned guideW, unsigned guideH, int depthInverted,
                                int reset, float intensity, int style, float localStructure,
                                float localTone, float skinStructure, int useAutoMask,
                                float mvScaleX, float mvScaleY, const nrcontract::Guides *, float globalTone);
using PFN_Release = void(__cdecl *)(void *feature);
using PFN_SetExtras = void(__cdecl *)(void *, float, ID3D12Resource *, ID3D12Resource *,
                                      ID3D12Resource *, unsigned, unsigned, unsigned, unsigned);
using PFN_SetFloatSlot = void(__cdecl *)(int);
using PFN_ProbeFloat = void(__cdecl *)(void *, const char *, float, int);

static HMODULE         s_mod = nullptr;
static PFN_CreateV2    s_create_v2 = nullptr;
static PFN_EvalV2      s_eval_v2 = nullptr;
static PFN_Create      s_create = nullptr;
static PFN_Eval        s_eval = nullptr;
static PFN_Release     s_release = nullptr;
static int(__cdecl* s_release_checked)(void*)=nullptr;
static unsigned long long s_created=0,s_released=0,s_release_failed=0;
static PFN_SetFloatSlot s_set_slot = nullptr;
static PFN_ProbeFloat  s_probe = nullptr;
static int            *s_last_init = nullptr;
static int            *s_last_create = nullptr;
static int            *s_preset_set  = nullptr;   // 诊断: 我们设的预设
static int            *s_preset_back = nullptr;   // 诊断: 运行库读回来的

static NVSDK_NGX_Parameter *s_caps = nullptr;   // ★核心的能力块, 不能新建一个★
static std::wstring         s_snippet;          // 游戏目录里的 nvngx_dlssnr.dll
static std::wstring         s_data;             // 可写目录
static int                  s_float_slot = -1;
static std::string          s_note = "未初始化";

static bool loaded()  { return s_create != nullptr && s_eval != nullptr; }
static const char *note() { return nrfault033::Blocked()?nrfault033::Note():s_note.c_str(); }
static int  float_slot()  { return s_float_slot; }
static int  last_init()   { return s_last_init   ? *s_last_init   : 0; }
static int  last_create() { return s_last_create ? *s_last_create : 0; }
static int  preset_set()  { return s_preset_set  ? *s_preset_set  : -1; }
static int  preset_back() { return s_preset_back ? *s_preset_back : -1; }

static std::wstring widen(const std::string &s)
{
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
    return w;
}

// ---- 找转发器: 先看游戏目录, 再看我们 addon 旁边 ----
static std::wstring find_forwarder(HMODULE self)
{
#ifdef K033_BETA2_RESHADE_HOST
    (void)self;
    const auto directory=engine033::Directory();
    if(directory.empty())return {};
    const auto path=directory+L"\\nvngx.dll_033.dll";
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY)?path:std::wstring{};
#else
    // 两个名字都找: 自研那份优先, 找不到就用过渡期那份。
    // 文件名里必须含 "nvngx.dll" —— 运行库的门禁认的就是这个子串。
    const wchar_t *kNames[] = { L"nvngx.dll_033.dll", L"nvngx.dll_dlssnr.dll" };
    std::vector<std::wstring> dirs;
    const std::wstring gdir = widen(game_dir());
    if (!gdir.empty()) dirs.push_back(gdir);
    wchar_t buf[MAX_PATH] = {};
    if (self != nullptr && GetModuleFileNameW(self, buf, MAX_PATH) > 0)
    {
        std::wstring d(buf);
        const size_t cut = d.find_last_of(L'\\');
        if (cut != std::wstring::npos) dirs.push_back(d.substr(0, cut));
    }
    for (const wchar_t *n : kNames)
        for (const std::wstring &d : dirs)
        {
            const std::wstring p = d + L"\\" + n;
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
        }
    return L"";
#endif
}

// ---- float 参数在 vtable 的哪个槽 ----
// 头文件声明 8 个 Set 再 8 个 Get, 顺序 ULL/float/double/uint/int/D3D11*/D3D12*/void*,
// 所以按头文件 float 应该在槽 1。但驱动的这个块不是头文件那份实现:
// 实测这台机器上 float 在【槽 6】。所以必须运行时探。
// 0.375f 是二进制精确值, 往返必须一模一样, 不存在"接近"。
static void discover_float_slot()
{
    if (s_float_slot >= 0 || s_caps == nullptr || s_probe == nullptr || s_set_slot == nullptr)
        return;
    static const char *kKey = "DLSSNR.033FloatProbe";
    const float want = 0.375f;
    const int cand[] = { 1, 2, 5, 6, 7, 4, 3, 0 };
    for (int slot : cand)
    {
        float back = 0.0f;
        s_probe(s_caps, kKey, want, slot);
        if (s_caps->Get(kKey, &back) == NVSDK_NGX_Result_Success && back == want)
        {
            s_float_slot = slot;
            s_set_slot(slot);
            Log("[nrfwd] float 参数走 vtable 槽位 %d", slot);
            return;
        }
    }
    s_float_slot = -2;   // 探过了, 没找到
    Log("[nrfwd] ! 没找到 float 槽位: 强度类参数会失效, uint 类照常");
}

// ★就地插入专用: 游戏自己那块【活的】参数块★
//   交换链那条路走的是核心的能力块 —— 那要求核心被【我们这一方】Init 过,
//   否则拿回来的是个没构造完的空壳(指针非空, 一喂给运行库就 0xC0000005)。
//   而补一次核心 Init 更糟: 游戏进程里本来就有一个活的 NGX 会话, 我们再 Init
//   一次(还漏传 FeatureCommonInfo), 实测直接把显卡整掉线 ——
//   nvidia-smi 报 "GPU is lost", 得重启机器才能恢复。
//   就地插入这条路根本不需要冒这个险: 钩子里游戏刚拿它建完自己的 DLSS,
//   那块参数块必然是活的、构造完整的。直接借用。
//   (对照 OptiScaler: 它敢补 Init 是因为它【自己就是 nvngx】, 游戏的 Init
//    本来就走它那儿, 所以那是进程里的第一次 Init, 不是第二次。)
static NVSDK_NGX_Parameter *s_external_caps = nullptr;
static void use_external_caps(NVSDK_NGX_Parameter *p) {
    // The game can rotate parameter objects between frames. Refresh the active
    // borrowed object too; retaining the first one sends later NR calls to stale
    // parameters. An independently owned core capability block is left alone.
    if (s_external_caps != nullptr && s_caps == s_external_caps) s_caps = p;
    s_external_caps = p;
}

// ★让位时必须把「借来的参数块」还掉★ (2026-09-04 傍晚 龙之信条 2 实测)
//   nrfwd 是个单例: 就地插入把 s_caps 指到了【游戏那块活的参数块】上。
//   让位之后交换链再来建 feature, init() 因为 s_caps 非空直接早退, 于是它是
//   拿【游戏的参数块】在【自己的命令列表】上建 —— 运行库当场 0xC0000005,
//   面板上就是那句「建特性时崩了(renodx 可能不兼容)」。
//   清掉之后下一次 init() 会走核心能力块那条分支, 也就是交换链本来该走的路。
static void drop_external_caps()
{
    s_external_caps = nullptr;
    s_caps          = nullptr;
    s_float_slot    = -1;      // 换了块就得重新探
}

// ---- 一次性初始化 ----
// dev: 游戏的设备。s_external_caps 有值就用它, 否则走核心能力块(交换链那条路)。
static bool init(ID3D12Device *dev, HMODULE self)
{
    if(nrfault033::Blocked()){s_note=nrfault033::Note();return false;}
    if (loaded() && s_caps != nullptr) return true;

    if (s_mod == nullptr)
    {
        const std::wstring p = find_forwarder(self);
        if (p.empty()) { s_note = "找不到 nvngx.dll_033.dll"; return false; }
        s_mod = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (s_mod == nullptr)
        {
            char b[128]; std::snprintf(b, sizeof(b), "转发器载不动 (错误 %lu)", GetLastError());
            s_note = b; return false;
        }
        // 两套导出名都认: nr033_* 是我们自研那份, dlssnr_call_* 是过渡期还在用的那份。
        // 谁在场用谁, 用户目录里是哪个版本都不会瞎。
        const bool own = GetProcAddress(s_mod, "nr033_create") != nullptr;
        auto pick = [&](const char *a, const char *b) { return own ? a : b; };
        s_create    = reinterpret_cast<PFN_Create>(GetProcAddress(s_mod, pick("nr033_create", "dlssnr_call_create")));
        s_eval      = reinterpret_cast<PFN_Eval>(GetProcAddress(s_mod, pick("nr033_evaluate", "dlssnr_call_evaluate")));
        s_release_checked=reinterpret_cast<int(__cdecl*)(void*)>(GetProcAddress(s_mod,"nr033_release_checked"));
        s_release   = reinterpret_cast<PFN_Release>(GetProcAddress(s_mod, pick("nr033_release", "dlssnr_call_release")));
        s_set_slot  = reinterpret_cast<PFN_SetFloatSlot>(GetProcAddress(s_mod, pick("nr033_set_float_slot", "dlssnr_call_set_float_slot")));
        s_probe     = reinterpret_cast<PFN_ProbeFloat>(GetProcAddress(s_mod, pick("nr033_probe_float", "dlssnr_call_probe_float")));
        s_last_init   = reinterpret_cast<int *>(GetProcAddress(s_mod, pick("nr033_last_init", "dlssnr_call_last_init")));
        s_last_create = reinterpret_cast<int *>(GetProcAddress(s_mod, pick("nr033_last_create", "dlssnr_call_last_create")));
        s_preset_set  = reinterpret_cast<int *>(GetProcAddress(s_mod, "nr033_preset_set"));
        s_preset_back = reinterpret_cast<int *>(GetProcAddress(s_mod, "nr033_preset_back"));
        s_create_v2 = reinterpret_cast<PFN_CreateV2>(GetProcAddress(s_mod, "nr033_create_v2"));
        s_eval_v2 = reinterpret_cast<PFN_EvalV2>(GetProcAddress(s_mod, "nr033_evaluate_v2"));
        if (!loaded()) { s_note = "转发器缺导出"; return false; }
    }

    // ★必须用核心的【能力块】, 不能用 AllocateParameters 新建一个★
    // 它带着 feature 创建时要用的 snippet/preset 回调, 新块里没有。
    if (s_caps == nullptr)
    {
        if (rendercore::Integrated())
        {
            s_caps = static_cast<NVSDK_NGX_Parameter*>(rendercore::Api()->capabilities(dev));
            if (!s_caps) { s_note="整合核心尚未取得 NVIDIA 能力块"; return false; }
            Log("[nrfwd] integrated core supplies owned NVIDIA capability block");
        }
        else if (s_external_caps != nullptr)
        {
            s_caps = s_external_caps;          // 就地插入: 借游戏那块活的
            Log("[nrfwd] 用游戏自己的参数块(就地插入路)");
        }
        else
        {
            if (g_ngx.cap == nullptr) { s_note = "核心没有 GetCapabilityParameters"; return false; }
            if (g_ngx.cap(&s_caps) != NVSDK_NGX_Result_Success || s_caps == nullptr)
            {
                s_caps = nullptr; s_note = "核心拒绝给能力块"; return false;
            }
        }
        discover_float_slot();
    }

    if (s_snippet.empty())
    {
#ifdef K033_BETA2_RESHADE_HOST
        const std::wstring gdir=engine033::Directory();
        if(gdir.empty()){s_note="033 模块路径不可用";return false;}
#else
        const std::wstring gdir = widen(game_dir());
#endif
        s_snippet = gdir + L"\\nvngx_dlssnr.dll";
        if (GetFileAttributesW(s_snippet.c_str()) == INVALID_FILE_ATTRIBUTES)
        { s_note = "033 模型目录里没有 nvngx_dlssnr.dll"; s_snippet.clear(); return false; }
    }
    if (s_data.empty())
    {
        wchar_t t[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, t);
        s_data = t;
    }

    s_note = "就位";
    return true;
}

// ---- 建 feature 18, 尺寸就是我们要的【小尺寸】 ----
// 调校参数必须在这里给: 模型只在建 feature 时读一次, evaluate 时再设会被忽略。
// dev 会被转发器拿去做 snippet 的 Init_Ext(第一次调用时), 不能传空。

// ★核心路★ 跟 RenoDX 完全同一条: 自己申请参数板 + 驱动核心的 CreateFeature/
// EvaluateFeature, 不经过我们那个转发器 DLL。
// RenoDX 的 NGX 入口表一共 8 个(从它二进制数的): Init_Ext / AllocateParameters /
// CreateFeature / EvaluateFeature(_C) / ReleaseFeature / DestroyParameters / Shutdown1。
// 【没有 GetCapabilityParameters】—— 它从不管游戏借参数板, 一律自己申请。
// OptiScaler 那条卡在 0xBAD0000B 的路, 借的正是游戏那块。
// 再加上 Init_Ext 时交上「去哪找 snippet」的清单(见 dlss5_033.cpp), 两件事凑齐,
// 核心才可能建得起 feature 18。★失败自动退回转发器, 一行都不少现有效果。★
static bool                 s_core_ok     = false;
static nrlayers::FeatureParams<NVSDK_NGX_Parameter> s_core_models;

static void core_enable(bool on) { s_core_ok = on; }
static bool core_in_use()        { return s_core_models.Any(); }

static NVSDK_NGX_Result core_create_guarded(ID3D12GraphicsCommandList *cmd,
                                            NVSDK_NGX_Parameter *p,
                                            NVSDK_NGX_Handle **out, DWORD *seh)
{
    *seh = 0;
    __try { return g_ngx.create(cmd, static_cast<NVSDK_NGX_Feature>(18), p, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh = GetExceptionCode(); nrfault033::Record(*seh,nrfault033::Site::CoreCreate); return NVSDK_NGX_Result_Fail; }
}
static NVSDK_NGX_Result core_eval_guarded(ID3D12GraphicsCommandList *cmd,
                                          NVSDK_NGX_Handle *h,
                                          NVSDK_NGX_Parameter *p, DWORD *seh)
{
    *seh = 0;
    __try { return g_ngx.evaluate(cmd, h, p, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh = GetExceptionCode(); nrfault033::Record(*seh,nrfault033::Site::CoreEvaluate); return NVSDK_NGX_Result_Fail; }
}

static void *create_inner(ID3D12Device *dev, ID3D12GraphicsCommandList *cmd, unsigned w, unsigned h,
                    int preset, float intensity, int style, float localStructure, float localTone,
                    float skinStructure, int useAutoMask, int uiCorrection, DWORD *seh, float globalTone = 1.0f)
{
    *seh = 0;
    if(nrfault033::Blocked()){*seh=nrfault033::Code();return nullptr;}
    // ── 先试核心路 ──
    {
        static bool synced = false;
        if (!synced) { synced = true; s_core_ok = g_core_probe_ok; }   // 探针说了算
    }
    if (s_core_ok && g_ngx.alloc != nullptr && g_ngx.create != nullptr && cmd != nullptr)
    {
        auto* slot=s_core_models.Free();
        if(!slot){s_note="核心模型参数记录已满，保留现有模型";return nullptr;}
        NVSDK_NGX_Parameter *p = nullptr;
        if (g_ngx.alloc(&p) == NVSDK_NGX_Result_Success && p != nullptr)
        {
            p->Set("CreationNodeMask", 1u);
            p->Set("VisibilityNodeMask", 1u);
            p->Set("DLSSNR.Width",  w);
            p->Set("DLSSNR.Height", h);
            p->Set("DLSSNR.Enabled", 1u);
            p->Set("DLSSNR.Style",              static_cast<unsigned>(style));
            p->Set("DLSSNR.UseAutoMask",        static_cast<unsigned>(useAutoMask));
            p->Set("DLSSNR.UICorrection",       static_cast<unsigned>(uiCorrection));
            p->Set("DLSSNR.Hint.Render.Preset", static_cast<unsigned>(preset));
            p->Set("DLSSNR.ScalingRatio",  1.0f);
            p->Set("DLSSNR.Intensity",              intensity);
            p->Set("DLSSNR.LocalStructureStrength", localStructure);
            p->Set("DLSSNR.LocalToneStrength",      localTone);
            p->Set("DLSSNR.GlobalToneStrength",     globalTone);
            p->Set("DLSSNR.SkinStructureStrength",  skinStructure);
            NVSDK_NGX_Handle *hh = nullptr;
            DWORD cseh = 0;
            const NVSDK_NGX_Result cr = core_create_guarded(cmd, p, &hh, &cseh);
            if(cseh || nrfault033::Blocked()){
                *slot={hh,p}; // Unknown consumer ownership: retain parameters and any returned handle.
                *seh=cseh?cseh:nrfault033::Code();return nullptr;
            }
            if (cr == NVSDK_NGX_Result_Success && hh != nullptr)
            {
                *slot={hh,p};
                Log("[nrfwd] 走核心路: CreateFeature(18) 成功 —— 跟 RenoDX 同一条");
                return hh;
            }
            Log("[nrfwd] 核心路建不起(0x%08X, SEH 0x%08lX), 退回转发器",
                static_cast<unsigned>(cr), cseh);
            if (g_ngx.destroy != nullptr) g_ngx.destroy(p);
            s_core_ok = false;
        }
    }
    if (!loaded() || s_caps == nullptr || dev == nullptr) return nullptr;
    __try
    {
        if (s_create_v2 != nullptr)
            return s_create_v2(s_snippet.c_str(), s_data.c_str(), dev, cmd,
                s_caps, w, h, preset, intensity, style, localStructure, localTone,
                skinStructure, useAutoMask, uiCorrection, globalTone);
        if (s_probe != nullptr && s_float_slot >= 0)
            s_probe(s_caps, "DLSSNR.GlobalToneStrength", globalTone, s_float_slot);
        else if (globalTone != 1.0f) { s_note = "Global Tone 需要新版 033 转发器"; return nullptr; }
        return s_create(s_snippet.c_str(), s_data.c_str(), dev, cmd,
                        s_caps, w, h, preset, intensity, style, localStructure, localTone,
                        skinStructure, useAutoMask, uiCorrection);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh = GetExceptionCode(); nrfault033::Record(*seh,nrfault033::Site::ForwardCreate); return nullptr; }
}

// guideW/guideH 可以跟 w/h 不一样 —— 引导图(深度/运动矢量)常常是满分辨率而
// 模型跑在缩小的尺寸上, 这是正常用法(renodx 日志里
// "NR input 5120x2160 (guides 3413x1440)" 就是这个)。
// mvScaleX/Y 是运动矢量的单位: Feed 给的是 UV 空间, 要乘引导图宽高换成像素;
// 全零占位图无所谓给多少。★不能拿分辨率去推★ —— 推出来常常正好是 1.0,
// 等于告诉模型"什么都没动"(转发器源码里明写了这个坑)。
static int evaluate(ID3D12GraphicsCommandList *cmd, void *feature, ID3D12Resource *color,
                    ID3D12Resource *depth, ID3D12Resource *motion, ID3D12Resource *output,
                    unsigned w, unsigned h, unsigned guideW, unsigned guideH,
                    int depthInverted, int reset, float intensity,
                    int style, float localStructure, float localTone, float skinStructure,
                    int useAutoMask, float mvScaleX, float mvScaleY, DWORD *seh,
                    const nrcontract::Guides *guides = nullptr, float globalTone = 1.0f)
{
    *seh = 0;
    if(nrfault033::Blocked()){*seh=nrfault033::Code();return 0;}
    if (feature == nullptr) return 0;
    if (guideW == 0) guideW = w;
    if (guideH == 0) guideH = h;
    nrcontract::Guides rects;
    rects.depth = {0, 0, guideW, guideH}; rects.motion = rects.depth;
    if (guides != nullptr) {
        if (guides->size != sizeof(rects)) return 0;
        rects = *guides;
    }
    // ── 核心路: 参数写我们自己那块板, 求值也走核心 ──
    auto* core=s_core_models.Find(feature);
    if (core != nullptr)
    {
        if(core->params==nullptr || g_ngx.evaluate==nullptr)return 0;
        NVSDK_NGX_Parameter *p = core->params;
        p->Set("DLSSNR.Color",  color);
        p->Set("DLSSNR.Depth",  depth);
        p->Set("DLSSNR.MVec",   motion);
        p->Set("DLSSNR.Output", output);
        p->Set("DLSSNR.Enabled", 1u);
        p->Set("DLSSNR.Width",  w);
        p->Set("DLSSNR.Height", h);
        p->Set("DLSSNR.ColorSubrectBaseX", 0u);
        p->Set("DLSSNR.ColorSubrectBaseY", 0u);
        p->Set("DLSSNR.ColorSubrectWidth", w);
        p->Set("DLSSNR.ColorSubrectHeight", h);
        p->Set("DLSSNR.OutputSubrectBaseX", 0u);
        p->Set("DLSSNR.OutputSubrectBaseY", 0u);
        p->Set("DLSSNR.OutputSubrectWidth", w);
        p->Set("DLSSNR.OutputSubrectHeight", h);
        p->Set("DLSSNR.DepthSubrectBaseX", rects.depth.x);
        p->Set("DLSSNR.DepthSubrectBaseY", rects.depth.y);
        p->Set("DLSSNR.DepthSubrectWidth", rects.depth.width);
        p->Set("DLSSNR.DepthSubrectHeight", rects.depth.height);
        p->Set("DLSSNR.MVecSubrectBaseX", rects.motion.x);
        p->Set("DLSSNR.MVecSubrectBaseY", rects.motion.y);
        p->Set("DLSSNR.MVecSubrectWidth", rects.motion.width);
        p->Set("DLSSNR.MVecSubrectHeight", rects.motion.height);
        p->Set("DLSSNR.MVecScaleX", mvScaleX);
        p->Set("DLSSNR.MVecScaleY", mvScaleY);
        p->Set("DLSSNR.DepthInverted", static_cast<unsigned>(depthInverted));
        p->Set("DLSSNR.Reset",         static_cast<unsigned>(reset));
        p->Set("DLSSNR.Style",         static_cast<unsigned>(style));
        p->Set("DLSSNR.UseAutoMask",   static_cast<unsigned>(useAutoMask));
        p->Set("DLSSNR.Intensity",              intensity);
        p->Set("DLSSNR.LocalStructureStrength", localStructure);
        p->Set("DLSSNR.LocalToneStrength",      localTone);
            p->Set("DLSSNR.GlobalToneStrength",     globalTone);
        p->Set("DLSSNR.SkinStructureStrength",  skinStructure);
        DWORD eseh = 0;
        const NVSDK_NGX_Result er =
            core_eval_guarded(cmd, reinterpret_cast<NVSDK_NGX_Handle *>(feature), p, &eseh);
        *seh = eseh;
        return static_cast<int>(er);
    }
    if (!loaded()) return 0;
    __try
    {
        if (s_eval_v2 != nullptr)
            return s_eval_v2(cmd, feature, s_caps, color, depth, motion, output, w, h,
                guideW, guideH, depthInverted, reset, intensity, style, localStructure,
                localTone, skinStructure, useAutoMask, mvScaleX, mvScaleY, &rects, globalTone);
        if (rects.offset() || rects.depth.width != rects.motion.width ||
            rects.depth.height != rects.motion.height) {
            s_note = "引导图区域需要新版 033 转发器"; return 0;
        }
        if (s_probe != nullptr && s_float_slot >= 0)
            s_probe(s_caps, "DLSSNR.GlobalToneStrength", globalTone, s_float_slot);
        else if (globalTone != 1.0f) return 0;
        return s_eval(cmd, feature, s_caps, color, depth, motion, output, w, h,
                      rects.depth.width, rects.depth.height,
                      depthInverted, reset, intensity, style, localStructure, localTone,
                      skinStructure, useAutoMask, mvScaleX, mvScaleY);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh = GetExceptionCode(); nrfault033::Record(*seh,nrfault033::Site::ForwardEvaluate); return 0; }
}

static bool release_core_guarded(void *f)
{
    __try { return g_ngx.release(reinterpret_cast<NVSDK_NGX_Handle *>(f))==NVSDK_NGX_Result_Success; }
    __except (EXCEPTION_EXECUTE_HANDLER) {nrfault033::Record(GetExceptionCode(),nrfault033::Site::CoreRelease);return false;}
}

static void release_inner(void *&feature, bool dying)
{
    if(nrfault033::Blocked())return; // Preserve the opaque handle; no consumer cleanup after a fault.
    if (feature == nullptr) return;
    auto* core=s_core_models.Find(feature);
    if (core != nullptr)
    {
        if (!dying && (!g_ngx.release || !release_core_guarded(feature))) {++s_release_failed;return;}
        if (!dying && core->params != nullptr && g_ngx.destroy != nullptr)
            g_ngx.destroy(core->params);
        *core={};feature=nullptr;
        return;
    }
    if (!dying) {
        __try {
            if(s_release_checked) {const int result=s_release_checked(feature);if(result!=NVSDK_NGX_Result_Success){if(s_release_failed++<8)Log("[nrfwd] release failed feature=%p result=0x%08X",feature,result);return;}}
            else if(s_release) s_release(feature);
            else {++s_release_failed;return;}
        } __except (EXCEPTION_EXECUTE_HANDLER) {nrfault033::Record(GetExceptionCode(),nrfault033::Site::ForwardRelease);++s_release_failed;return;}
    }
    feature = nullptr;
}

// ── 「这是我们自己发的调用」标记 ────────────────────────────────
//   nrscale 的 feature 18 账本靠它把自己人摘出去 —— 不然我们自己建的会被
//   记成「别人拿着」, 换引擎时永远等不到账本清零。
//   ★不能靠句柄比对★: 建的那一刻句柄还没交回给 hostnr, 比对必然落空。
//   ★也不能用 RAII 写在里面★: 里面有 __try/__except, 跟带析构的局部对象
//     不能同居(MSVC C2712)。所以套一层没有 __try 的薄壳。
static void *create(ID3D12Device *dev, ID3D12GraphicsCommandList *cmd, unsigned w, unsigned h,
                    int preset, float intensity, int style, float localStructure, float localTone,
                    float skinStructure, int useAutoMask, int uiCorrection, DWORD *seh, float globalTone = 1.0f)
{
    nrscale::self_begin();
    void *f = create_inner(dev, cmd, w, h, preset, intensity, style, localStructure, localTone,
                           skinStructure, useAutoMask, uiCorrection, seh, globalTone);
    nrscale::self_end();
    if(f)++s_created;
    return f;
}

static void release(void *&feature, bool dying)
{
    nrscale::self_begin();
    const bool had=feature!=nullptr;
    release_inner(feature, dying);
    if(had && !feature && !dying)++s_released;
    nrscale::self_end();
}

} // namespace nrfwd
