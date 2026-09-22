// =====================================================================
//  nrscale.h  ——  神经渲染「模型分辨率」杠杆
//
//  目标: 给直挂路线(ReShade + renodx)补上 OptiScaler 才有的降开销旋钮。
//        实测背书: 燕云 100%->75% = +26% 帧数; 审判之眼 NR 开/关 = 142/313 帧。
//        renodx 二十个配置键里一个尺寸旋钮都没有 —— 所以只能我们自己加。
//
//  ── 手法 ────────────────────────────────────────────────────────────
//  用 Microsoft Detours 内联钩住【NGX 核心导出函数本体】:
//      NVSDK_NGX_D3D12_CreateFeature
//      NVSDK_NGX_D3D12_EvaluateFeature
//  钩的是函数入口的机器码, 不是谁的导入表 —— 所以:
//    · renodx 什么时候解析的、怎么解析的, 全都无所谓
//    · 它拿到的就是真地址, 一调就落进我们的钩子
//
//  ★为什么不是改 IAT★
//    第一版就是改 renodx 的 GetProcAddress 导入项, 结果一次都没拦到 ——
//    renodx 在我们打补丁之前早就把 NGX 函数取走了。IAT 补丁对时序极度
//    敏感, 内联钩子不敏感。OptiScaler 用的也是 Detours(源码里带着
//    detours.lib), 这是被验证过的路子。
//
//  ── 阶段 ────────────────────────────────────────────────────────────
//  mode = 0  只观察: 读参数、写日志, 原样转发。零副作用。
//  mode = 1  真改:   在 CreateFeature(18) 时把模型尺寸改小。
//  默认 0 —— 先摸清 renodx 到底怎么建这个特性, 再谈改。
// =====================================================================
#pragma once
#include "render_core_client.h"

#include <detours/detours.h>
#include <intrin.h>
#include "nr_call_scope.h"
#include "mfg/ngx_hook.hpp"

namespace nrscale
{

// ---------- 状态 ----------
// 两套钩子:
//   core_*  = 驱动 NGX 核心 (_nvngx.dll) —— 游戏自己的 DLSS 走这里
//   snip_*  = nvngx_dlssnr.dll 运行库本身 —— ★renodx 建 feature 18 走这里★
// 第一版只钩了核心, 结果拦到的是我们自己面板点的那次, 不是 renodx 的。
// 古墓丽影的 ReShade 日志写着 "feature 18 created via the signed snippet",
// 那个 snippet 就是运行库自己的导出函数, 绕过核心。
static PFN_CreateFeature   o_create = nullptr;   // 主钩(薄壳优先)
static PFN_CreateFeature   o_create_core = nullptr;
static PFN_EvaluateFeature o_eval   = nullptr;
// ★★ 得钩两扇门 ★★
//   nvngx.dll  薄壳 —— 普通游戏走这里(古墓丽影暗影)
//   _nvngx.dll 核心 —— ★用 Streamline 的游戏走这里★(燕云实测: 整套
//                      sl.interposer/sl.dlss 都在, 薄壳压根没加载,
//                      链路是 游戏→sl.interposer→sl.dlss→_nvngx.dll)
//   只钩薄壳会漏掉所有 Streamline 游戏; 只钩核心又会把我们自己的调用也拦下来。
//   两边都钩, 然后【按 feature 句柄】认谁的调用 —— 这也是 OptiScaler 的做法。
static PFN_EvaluateFeature o_eval_core = nullptr;
// 装钩那一刻薄壳入口的【原地址】(Detours 之后 o_eval 就成了跳板, 不能再拿来比)。
// 之后补钩核心时靠它判断「核心那扇门其实是不是薄壳这同一个函数」(转发导出)。
static PVOID s_shell_eval_target = nullptr;
static PVOID s_shell_create_target = nullptr;
static PVOID s_shell_release_target = nullptr;
static bool  s_core_late_tried = false;

// 由主文件在所有头都包完之后填上(那时才看得见 carrier/hostnr 的句柄)
typedef bool (*PFN_IsOurFeature)(const void *);
static PFN_IsOurFeature g_is_ours = nullptr;
static PFN_CreateFeature   s_create = nullptr;   // 运行库(snippet)
static PFN_EvaluateFeature s_eval   = nullptr;
static bool  snip_hooked    = false;

static bool  installed      = false;
// ★就地插入的回调★ 游戏自己的 DLSS 求值跑完之后调它,
// 在同一条命令列表上对刚写出来的 Output 做神经渲染。
// 实现在 inject.h(它包含在本文件之后, 所以这里用函数指针隔一层)。
using PFN_AfterEval = int(__cdecl *)(ID3D12GraphicsCommandList *, const NVSDK_NGX_Parameter *, const NVSDK_NGX_Handle *);
static PFN_AfterEval g_after_eval = nullptr;
// ★重入闸★ 我们自己的 feature-18 求值走的是同一个被 Detour
// 补过的 EvaluateFeature 入口, 会再次进来。不拦的话 = 无限递归→卡死。
// 只有游戏自己那一次求值才触发插入, 插入里面的求值一律直通。
// Reentry is per-thread; the shared NR writer has a separate nonblocking guard.
static std::atomic<int> create_total{0};
static int   create18_count = 0;
static std::atomic<int> eval_total{0};
// ★只统计【游戏那扇门】的求值★
//   eval_total 是三个钩子(薄壳/核心/运行库)共用的, 我们自己的 NR 求值也会
//   把它加上去 —— 拿它当「游戏在用 DLSS」的判据会误判, 导致交换链把
//   feature 18 白让出去。这个计数器只在薄壳钩子里加, 才是干净的信号。
static std::atomic<int> game_eval{0};
static char  status[256]    = "未安装";

// 0 = 只观察; 1 = 真的改尺寸
static int   mode        = 0;
static float want_scale  = 0.75f;

// 观察到的实际尺寸(界面显示用)
static unsigned int seen_w = 0, seen_h = 0;
static unsigned int used_w = 0, used_h = 0;

// ---------- 独立日志 ----------
static void NLog(const char *fmt, ...)
{
    static SRWLOCK log_lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&log_lock);

    char line[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st; GetLocalTime(&st);
    char stamp[32];
    _snprintf_s(stamp, sizeof(stamp), _TRUNCATE, "%02d:%02d:%02d.%03d  ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    const std::string path = game_dir() + "\\dlss5-033-nrscale.log";
    FILE *f = nullptr;
    if (fopen_s(&f, path.c_str(), "a") == 0 && f)
    {
        fputs(stamp, f); fputs(line, f); fputs("\n", f);
        fclose(f);
    }
    ReleaseSRWLockExclusive(&log_lock);
}

// ---------- 参数转储 ----------
static void dump_uint(const NVSDK_NGX_Parameter *p, const char *key)
{
    unsigned int v = 0;
    if (const_cast<NVSDK_NGX_Parameter *>(p)->Get(key, &v) == NVSDK_NGX_Result_Success)
        NLog("    %-40s = %u", key, v);
}
static void dump_float(const NVSDK_NGX_Parameter *p, const char *key)
{
    float v = 0.0f;
    if (const_cast<NVSDK_NGX_Parameter *>(p)->Get(key, &v) == NVSDK_NGX_Result_Success)
        NLog("    %-40s = %.4f", key, v);
}
static void dump_ptr(const NVSDK_NGX_Parameter *p, const char *key)
{
    void *v = nullptr;
    if (const_cast<NVSDK_NGX_Parameter *>(p)->Get(key, &v) == NVSDK_NGX_Result_Success && v)
        NLog("    %-40s = %p", key, v);
}

static void dump_params(const NVSDK_NGX_Parameter *p, const char *tag)
{
    NLog("  [%s] renodx 设进来的参数:", tag);
    for (const char *k : { "DLSSNR.Width", "DLSSNR.Height", "Width", "Height",
                           "DLSSNR.ColorSubrectWidth",  "DLSSNR.ColorSubrectHeight",
                           "DLSSNR.DepthSubrectWidth",  "DLSSNR.DepthSubrectHeight",
                           "DLSSNR.MVecSubrectWidth",   "DLSSNR.MVecSubrectHeight",
                           "DLSSNR.OutputSubrectWidth", "DLSSNR.OutputSubrectHeight",
                           "DLSSNR.Style", "DLSSNR.Hint.Render.Preset",
                           "DLSSNR.DepthInverted", "DLSSNR.UseAutoMask", "DLSSNR.UICorrection",
                           "CreationNodeMask", "VisibilityNodeMask" })
        dump_uint(p, k);

    for (const char *k : { "DLSSNR.ScalingRatio", "DLSSNR.Intensity",
                           "DLSSNR.LocalStructureStrength", "DLSSNR.LocalToneStrength",
                           "DLSSNR.SkinStructureStrength",
                           "DLSSNR.MVecScaleX", "DLSSNR.MVecScaleY" })
        dump_float(p, k);

    for (const char *k : { "DLSSNR.Color", "DLSSNR.Depth", "DLSSNR.MVec", "DLSSNR.Output",
                           "DLSSNR.Backbuffer", "DLSSNR.UI", "DLSSNR.ControlMask" })
        dump_ptr(p, k);
}

// ---------- 只读: 看一眼 ScalingRatio 有没有被设过 ----------
// ★这里绝不写入★
//
// 血泪教训: 上一版为了试探"这个键能不能写", 往 renodx 的参数表里写了
// 0.75, 然后只在"原来有值"时才还原 —— 而它原来【没有】这个键, 于是那个
// 0.75 就永久留在里面了。运行库拿着 ScalingRatio=0.75 配全尺寸纹理,
// 直接 0xBAD00002, 把玩家本来好好的神经渲染搞坏了。
//
// 结论: 观察模式必须是真观察。试探性写入要么别做, 要么在我们【自己的】
// 参数对象上做, 绝不碰别人正在用的那份。
static void peek_ratio(const NVSDK_NGX_Parameter *cp)
{
    auto *p = const_cast<NVSDK_NGX_Parameter *>(cp);
    float v = -1.0f;
    const bool had = p->Get("DLSSNR.ScalingRatio", &v) == NVSDK_NGX_Result_Success;
    NLog("  [只读] ScalingRatio %s", had ? ("= " + std::to_string(v)).c_str()
                                        : "未被设置(renodx 不用这个键)");
}

// ═════════════════════════════════════════════════════════════════════
//  feature 18 归属账本 —— 游戏内换引擎的地基
// ═════════════════════════════════════════════════════════════════════
//  两次崩溃(2026-09-04 23:56 燕云 / 2026-09-05 00:13 燕云)是同一件事:
//  【交接的时候, 在位的那个从来没撒手】, 后来的那个直接建第二个 -> 设备移除。
//
//  以前没法做交接, 是因为我们根本不知道"现在有没有人拿着"。这个账本就是
//  那双眼睛: 每一次 feature 18 的建/放都过一遍这里, 记下【不是我们的】那些。
//
//  ★怎么分清是不是我们自己发的调用★
//    不能用句柄比对 —— 建的那一刻句柄还没交回给 hostnr, 比对必然落空。
//    改成深度计数: nrfwd 在自己调 create/release 前后 self_begin/self_end,
//    钩子里看见 s_self_depth>0 就知道"这是自己人", 不记账。
//
//  ★钩得到谁★
//    薄壳 nvngx.dll + 核心 _nvngx.dll —— 这两扇门可以钩(就地插入天天在用)。
//    运行库 nvngx_dlssnr.dll ★钩不得★(社区签名版自校验代码完整性, 一改就挂),
//    所以 renodx 走运行库那条路我们【看不见】。看不见的时候账本是 0, 于是
//    下面的交接必须靠"我们自己先撒手 + 静置"来保证安全, 不能反过来赌它。
typedef NVSDK_NGX_Result (__cdecl *PFN_ReleaseFeature18)(NVSDK_NGX_Handle *);
static PFN_ReleaseFeature18 o_release      = nullptr;   // 薄壳的 ReleaseFeature
static PFN_ReleaseFeature18 o_release_core = nullptr;   // 核心的 ReleaseFeature

static void    *s_other18[8] = {};
static int      s_other_n    = 0;
static thread_local int s_self_depth = 0;
static unsigned s_tick       = 0;     // 每帧 +1
static unsigned s_free_tick  = 0;     // 账本最后一次归零是哪一帧

static void self_begin() { ++s_self_depth; }
static void self_end()   { if (s_self_depth > 0) --s_self_depth; }

// 别人手上还有没有 feature 18。★看不见运行库那扇门, 所以 false 只代表
// "我们没看见别人拿着", 不等于"一定没人拿着"★ —— 交接逻辑必须记着这一条。
static bool     other_owns()     { return s_other_n > 0; }
static int      other_count()    { return s_other_n; }
static unsigned frames_since_free() { return s_tick - s_free_tick; }
static void     tick()           { ++s_tick; }

static void other_created(void *h)
{
    if (h == nullptr || s_other_n >= 8) return;
    for (int i = 0; i < s_other_n; ++i) if (s_other18[i] == h) return;
    s_other18[s_other_n++] = h;
    NLog("★账本: 别人建了 feature 18 (句柄 %p) —— 别人手上现在有 %d 个★", h, s_other_n);
}

static void other_released(void *h)
{
    for (int i = 0; i < s_other_n; ++i)
        if (s_other18[i] == h)
        {
            s_other18[i] = s_other18[s_other_n - 1];
            --s_other_n;
            if (s_other_n == 0) s_free_tick = s_tick;
            NLog("★账本: 别人放了 feature 18 (句柄 %p) —— 还剩 %d 个★", h, s_other_n);
            return;
        }
}

static void forget_feature(const void *h);
// ---------- 钩子: ReleaseFeature (两扇门共用) ----------
static NVSDK_NGX_Result __cdecl My_ReleaseFeature(NVSDK_NGX_Handle *h)
{
    const auto result = o_release ? o_release(h) : NVSDK_NGX_Result_Fail;
    if (result == NVSDK_NGX_Result_Success) {
        forget_feature(h);
        if (s_self_depth == 0) other_released(h);
    }
    return result;
}
static NVSDK_NGX_Result __cdecl Core_ReleaseFeature(NVSDK_NGX_Handle *h)
{
    const auto result = o_release_core ? o_release_core(h) : NVSDK_NGX_Result_Fail;
    if (result == NVSDK_NGX_Result_Success) {
        forget_feature(h);
        if (s_self_depth == 0) other_released(h);
    }
    return result;
}

// ── 句柄 → feature id 的小账本 ────────────────────────────────────
//   ★为什么必须有它★ (2026-09-05, 评论区「又在幻想的爽鱼」报的鸣潮)
//   他贴的诊断自相矛盾:
//       结论: 已经挂到游戏的 DLSS 上了, 但游戏一次都没调用它
//       插入位置: 参数块里没有 Output
//       已处理: 1452 帧            ← 明明处理了 1452 帧
//       深度与运动: 实时(来自游戏) ← 明明拿到了游戏的真数据
//   真相: 它【在工作】, 只是状态文字被覆盖了。
//   鸣潮开着 DLSS 帧生成, 于是每帧有两次 NGX 求值:
//       feature 1  (DLSS)      有 Output -> 我们插进去, 成功
//       feature 11 (帧生成)    没有 Output -> 我们报「参数块里没有 Output」
//   而 eval_common 以前【根本不看是哪个 feature】, 逮谁往谁里插, 于是每一帧
//   都被第二次求值把 s_note 覆盖成失败 —— 用户看到的就是那份自相矛盾的诊断。
//
//   现在建 feature 的时候把 句柄->feature id 记下来, 求值时只往
//   DLSS/DLAA(1) 和 光线重建(13) 里插, 帧生成(11) 之类一律只转发不碰。
struct FeatureInfo {
    const void *handle = nullptr;
    int id = -1;
    unsigned flags = 0, outputW = 0, outputH = 0;
    bool haveFlags = false;
};
static FeatureInfo s_features[256];
static SRWLOCK s_feature_lock = SRWLOCK_INIT;
static void remember_feature(const void *h, int fid, const NVSDK_NGX_Parameter *cp)
{
    if (h == nullptr) return;
    FeatureInfo item; item.handle = h; item.id = fid;
    auto *p = const_cast<NVSDK_NGX_Parameter *>(cp);
    if (p != nullptr) {
        item.haveFlags = p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &item.flags) == NVSDK_NGX_Result_Success;
        p->Get(NVSDK_NGX_Parameter_OutWidth, &item.outputW);
        p->Get(NVSDK_NGX_Parameter_OutHeight, &item.outputH);
    }
    AcquireSRWLockExclusive(&s_feature_lock);
    int freeIndex = -1;
    for (int i = 0; i < 256; ++i) {
        if (s_features[i].handle == h) { freeIndex = i; break; }
        if (s_features[i].handle == nullptr && freeIndex < 0) freeIndex = i;
    }
    if (freeIndex >= 0) s_features[freeIndex] = item;
    ReleaseSRWLockExclusive(&s_feature_lock);
}
static FeatureInfo feature_info(const void *h)
{
    FeatureInfo result;
    AcquireSRWLockShared(&s_feature_lock);
    for (const auto &v : s_features) if (v.handle == h && h != nullptr) { result = v; break; }
    ReleaseSRWLockShared(&s_feature_lock);
    return result;
}
static void forget_feature(const void *h)
{
    AcquireSRWLockExclusive(&s_feature_lock);
    for (auto &v : s_features) if (v.handle == h) { v = {}; break; }
    ReleaseSRWLockExclusive(&s_feature_lock);
}
static int feature_of(const void *h) { return feature_info(h).id; }

// 这个 feature 值不值得我们往里插神经渲染
//   1 = DLSS/DLAA(正主), 13 = 光线重建(也出 Output); 12 是 DeepDVC(调色滤镜), 不插
//   11 = 帧生成, 别的 = 不认识 —— 一律只转发
static bool worth_injecting(int fid) { return nrdispatch::image_candidate(fid, false); }

// ---------- 钩子: CreateFeature ----------
static NVSDK_NGX_Result create_common(
    PFN_CreateFeature orig, const char *entry, ID3D12GraphicsCommandList *cmd, NVSDK_NGX_Feature id,
    const NVSDK_NGX_Parameter *params, NVSDK_NGX_Handle **out)
{
    ++create_total;
    const int fid = static_cast<int>(id);

    // 18 = DLSSNR(神经渲染)。1 = DLSS/DLAA 是游戏自己的, 不碰。
    if (fid == 18 && params != nullptr)
    {
        ++create18_count;
        NLog("======== 拦到 feature 18 (DLSSNR) 创建 · 第 %d 次 ========", create18_count);
        dump_params(params, "创建");

        auto *p = const_cast<NVSDK_NGX_Parameter *>(params);
        unsigned int w = 0, h = 0;
        p->Get("DLSSNR.Width", &w);
        p->Get("DLSSNR.Height", &h);
        if (w == 0 || h == 0) { p->Get("Width", &w); p->Get("Height", &h); }
        seen_w = w; seen_h = h;

        peek_ratio(params);

        // ★核心这一层永远不改尺寸★
        // renodx 的调用是"核心 -> 运行库"两层串起来的, 两层都改会缩两次
        // (实测 5120x2160 -> 2560x1080 -> 1280x540)。真正落地的是运行库那层,
        // 所以尺寸只在 Snip_CreateFeature 里动, 这里纯观察。
        used_w = w; used_h = h;
        NLog("  (核心层, 尺寸原样放行 %ux%u)", w, h);
    }
    else if (create_total <= 8)
    {
        NLog("  (放行 feature %d)", fid);
    }

    NVSDK_NGX_Result r = orig ? orig(cmd, id, params, out) : NVSDK_NGX_Result_Fail;
    // ★记账★ 建成功的 feature 18, 只要不是我们自己发的调用, 就记下来 ——
    //   换引擎时靠它判断"对面撒手了没有"。
    if (r == NVSDK_NGX_Result_Success && out != nullptr && *out != nullptr)
    {
        remember_feature(*out, fid, params);        // 求值时要靠它认出帧生成
        NLog("[feature-ledger] entry=%s feature=%d handle=%p self=%d", entry, fid, *out, s_self_depth > 0 ? 1 : 0);
        if (fid == 18 && s_self_depth == 0) other_created(static_cast<void *>(*out));
    }
    if (fid == 18)
        NLog("  [核心] 转发 CreateFeature(18) -> 0x%08X %s",
             static_cast<unsigned>(r),
             r == NVSDK_NGX_Result_Success ? "成功" : "★失败, 改回观察模式★");
    return r;
}

static NVSDK_NGX_Result __cdecl My_CreateFeature(
    ID3D12GraphicsCommandList *cmd, NVSDK_NGX_Feature id,
    const NVSDK_NGX_Parameter *params, NVSDK_NGX_Handle **out)
{ return create_common(o_create, "primary", cmd, id, params, out); }

static NVSDK_NGX_Result __cdecl Core_CreateFeature(
    ID3D12GraphicsCommandList *cmd, NVSDK_NGX_Feature id,
    const NVSDK_NGX_Parameter *params, NVSDK_NGX_Handle **out)
{ return create_common(o_create_core, "core", cmd, id, params, out); }

// ---------- 钩子: 运行库(snippet) 的 CreateFeature ----------
// ★renodx 的神经渲染走这条★
static NVSDK_NGX_Result __cdecl Snip_CreateFeature(
    ID3D12GraphicsCommandList *cmd, NVSDK_NGX_Feature id,
    const NVSDK_NGX_Parameter *params, NVSDK_NGX_Handle **out)
{
    const int fid = static_cast<int>(id);
    if (fid == 18 && params != nullptr)
    {
        ++create18_count;
        NLog("######## [运行库] 拦到 renodx 建 feature 18 · 第 %d 次 ########", create18_count);
        dump_params(params, "创建");

        auto *p = const_cast<NVSDK_NGX_Parameter *>(params);
        unsigned int w = 0, h = 0;
        p->Get("DLSSNR.Width", &w);
        p->Get("DLSSNR.Height", &h);
        if (w == 0 || h == 0) { p->Get("Width", &w); p->Get("Height", &h); }
        seen_w = w; seen_h = h;

        peek_ratio(params);

        if (mode == 1 && w > 0 && h > 0 && want_scale > 0.2f && want_scale < 0.999f)
        {
            const unsigned int nw = static_cast<unsigned int>(w * want_scale) & ~1u;
            const unsigned int nh = static_cast<unsigned int>(h * want_scale) & ~1u;
            p->Set("DLSSNR.Width",  nw);
            p->Set("DLSSNR.Height", nh);
            used_w = nw; used_h = nh;
            NLog("  ★改尺寸★ %ux%u -> %ux%u (%.0f%%, 开销约 %.0f%%)",
                 w, h, nw, nh, want_scale * 100.0f, want_scale * want_scale * 100.0f);
        }
        else
        {
            used_w = w; used_h = h;
            NLog("  (观察模式, 尺寸原样放行 %ux%u)", w, h);
        }
    }

    NVSDK_NGX_Result r = s_create ? s_create(cmd, id, params, out) : NVSDK_NGX_Result_Fail;

    if (fid == 18)
    {
        NLog("  [运行库] 转发 CreateFeature(18) -> 0x%08X %s",
             static_cast<unsigned>(r),
             r == NVSDK_NGX_Result_Success ? "★成功★" : "失败");

        // ★安全网★: 是我们改了尺寸才失败的话, 立刻退回观察模式并原样重试一次。
        // 不能让玩家因为拉了个滑块就把神经渲染搞没了 —— 宁可这个功能不生效,
        // 也不能把本来好好的 NR 弄坏。
        if (r != NVSDK_NGX_Result_Success && mode == 1)
        {
            mode = 0;
            NLog("  ★改尺寸导致创建失败 -> 已自动退回观察模式, 并按原尺寸重试★");
            auto *p2 = const_cast<NVSDK_NGX_Parameter *>(params);
            p2->Set("DLSSNR.Width",  seen_w);
            p2->Set("DLSSNR.Height", seen_h);
            used_w = seen_w; used_h = seen_h;
            r = s_create ? s_create(cmd, id, params, out) : r;
            NLog("  重试(原尺寸 %ux%u) -> 0x%08X %s", seen_w, seen_h,
                 static_cast<unsigned>(r),
                 r == NVSDK_NGX_Result_Success ? "救回来了" : "仍失败");
            strcpy_s(status, "改尺寸不被接受, 已自动退回(见下方说明)");
        }
    }
    return r;
}

static NVSDK_NGX_Result __cdecl Snip_EvaluateFeature(
    ID3D12GraphicsCommandList *cmd, const NVSDK_NGX_Handle *h,
    const NVSDK_NGX_Parameter *params, void *callback)
{
    ++eval_total;
    if (params != nullptr && (eval_total <= 2 || (eval_total % 1800) == 0))
    {
        NLog("---- [运行库] Evaluate 第 %d 次 ----", eval_total.load());
        dump_params(params, "求值");
    }
    return s_eval ? s_eval(cmd, h, params, callback) : NVSDK_NGX_Result_Fail;
}

// ---------- 钩子: EvaluateFeature (两扇门共用一套逻辑) ----------
static void call_after_guarded(ID3D12GraphicsCommandList *cmd, const NVSDK_NGX_Parameter *params, const NVSDK_NGX_Handle *h)
{
    // Keep SEH separate from the C++ scope objects used by eval_common.
    __try { g_after_eval(cmd, params, h); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_after_eval = nullptr; NLog("[inject] 抛异常, 已断开"); }
}
static NVSDK_NGX_Result eval_common(
    PFN_EvaluateFeature orig, ID3D12GraphicsCommandList *cmd, const NVSDK_NGX_Handle *h,
    const NVSDK_NGX_Parameter *params, void *callback, uintptr_t caller)
{
    // 重入(我们自己 feature-18 的求值)直接原样放行, 绝不再触发插入
    if (nrdispatch::context.after)
        return orig ? orig(cmd, h, params, callback) : NVSDK_NGX_Result_Fail;

    // 这是我们自己建的 feature(交换链的 DLAA / NR, 或就地插入那份)? 那就只是放行。
    // 不这么分的话: 钩核心时我们自己的每帧调用会被当成「游戏在用 DLSS」,
    // 交换链于是把 feature 18 白让出去, 两边都不做。
    if (h != nullptr && g_is_ours != nullptr && g_is_ours(h))
        return orig ? orig(cmd, h, params, callback) : NVSDK_NGX_Result_Fail;

    // Both entry points belong to ONE evaluation, including non-image calls.
    // Never infer a feature from Output: shared parameter blocks retain it.
    nrdispatch::DepthScope depth(caller);
    const NVSDK_NGX_Result rr = orig ? orig(cmd, h, params, callback) : NVSDK_NGX_Result_Fail;
    if (nrdispatch::context.depth != 1) return rr;
    const int fid = feature_of(h);
    if (!worth_injecting(fid)) {
        nrdispatch::non_image_calls.fetch_add(1, std::memory_order_relaxed);
        if (fid == 11) {
            nrdispatch::framegen_calls.fetch_add(1, std::memory_order_relaxed);
            // Read the actual native FG contract. Pointer presence/size is not
            // evidence that the content is correctly aligned with NR output.
            // Never replace textures, invent MV scales, reset FG, or add waits.
            static std::atomic<ULONGLONG> next{0};auto due=next.load();const auto now=GetTickCount64();
            if(params && now>=due && next.compare_exchange_strong(due,now+5000)){
                void *back=nullptr,*hudless=nullptr,*depthMap=nullptr,*motionMap=nullptr;
                unsigned reset=~0u,mw=0,mh=0,hdr=~0u,jittered=~0u;
                params->Get("DLSSG.Backbuffer",&back);params->Get("DLSSG.HUDLess",&hudless);
                params->Get("DLSSG.Depth",&depthMap);params->Get("DLSSG.MVecs",&motionMap);
                params->Get("DLSSG.Reset",&reset);params->Get("DLSSG.ColorBuffersHDR",&hdr);
                params->Get("DLSSG.MVecsSubrectWidth",&mw);params->Get("DLSSG.MVecsSubrectHeight",&mh);
                params->Get("DLSSG.MvecJittered",&jittered);
                NLog("[033 FG contract] back=%p hudless=%p depth=%p motion=%p motion_rect=%ux%u HDR=%u jittered=%u reset=%u result=0x%08X; ~0=unavailable, content not verified",
                    back,hudless,depthMap,motionMap,mw,mh,hdr,jittered,reset,unsigned(rr));
            }
        }
        if (fid < 0) {
            const auto unknown = nrdispatch::unknown_calls.fetch_add(1) + 1;
            if (unknown <= 4) NLog("[feature-ledger] unknown handle=%p caller=%p; NR withheld until feature creation is known", h, reinterpret_cast<void*>(caller));
        }
        return rr;
    }
    if (rendercore::Integrated()) return rr; // The integrated SR seam owns NR, including FSR/XeSS inputs.
    const int evaluation = ++eval_total;
    ++game_eval; // Real registered SR/RR only; not each wrapper or generated frame.
    if (evaluation <= 2) NLog("[image-eval] feature=%d handle=%p caller=%p result=0x%08X", fid, h, reinterpret_cast<void*>(caller), unsigned(rr));
    // ★就地插入★ 让游戏的 DLSS 先跑完, 再在它刚写好的 Output 上做神经渲染。
    // Inner thin-wrapper/core calls belong to the same thread-local evaluation.
    if (nrdispatch::context.depth == 1 && rr == NVSDK_NGX_Result_Success &&
        g_after_eval != nullptr && params != nullptr)
    {
        nrdispatch::AfterScope writer;
        if (!writer.entered) return rr;
        call_after_guarded(cmd, params, h);
    }
    return rr;
}

static NVSDK_NGX_Result __cdecl My_EvaluateFeature(
    ID3D12GraphicsCommandList *cmd, const NVSDK_NGX_Handle *h,
    const NVSDK_NGX_Parameter *params, void *callback)
{ return eval_common(o_eval, cmd, h, params, callback, reinterpret_cast<uintptr_t>(_ReturnAddress())); }

static NVSDK_NGX_Result __cdecl Core_EvaluateFeature(
    ID3D12GraphicsCommandList *cmd, const NVSDK_NGX_Handle *h,
    const NVSDK_NGX_Parameter *params, void *callback)
{ return eval_common(o_eval_core, cmd, h, params, callback, reinterpret_cast<uintptr_t>(_ReturnAddress())); }

// 从模块自己的导出表读函数地址(不经 GetProcAddress)。转发导出返回空 —— 那不是这个模块里的函数。
static PVOID export_address(HMODULE m, const char *name)
{
    if (m == nullptr || name == nullptr) return nullptr;
    const auto *base = reinterpret_cast<const BYTE *>(m);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0) return nullptr;
    const auto *exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY *>(base + dir.VirtualAddress);
    const auto *names = reinterpret_cast<const DWORD *>(base + exp->AddressOfNames);
    const auto *ordinals = reinterpret_cast<const WORD *>(base + exp->AddressOfNameOrdinals);
    const auto *functions = reinterpret_cast<const DWORD *>(base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i)
    {
        if (std::strcmp(reinterpret_cast<const char *>(base + names[i]), name) != 0) continue;
        const WORD ordinal = ordinals[i];
        if (ordinal >= exp->NumberOfFunctions) return nullptr;
        const DWORD rva = functions[ordinal];
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) return nullptr;
        return reinterpret_cast<PVOID>(const_cast<BYTE *>(base) + rva);
    }
    return nullptr;
}

// ★核心那扇门补钩★ 装钩时核心还没进程/没载起来, 之后它出现了就补上。一局只试一次, 结果写日志。
// 按名字取【已经在进程里】的模块并钉住: 不是 033 LoadLibrary 的那份, 游戏将来卸载它时
// 我们装在里面的钩子不能悬空。没加载就返回空, 不会替游戏去加载。
static HMODULE pinned_module(const wchar_t *name)
{
    HMODULE m = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, name, &m) ? m : nullptr;
}

static void hook_core_late()
{
    if (s_core_late_tried || !installed || o_eval_core != nullptr) return;
    HMODULE core = pinned_module(L"_nvngx.dll");
    if (core == nullptr) return;   // 还没进进程, 下个 5 秒再看
    s_core_late_tried = true;
    PVOID eval = export_address(core, "NVSDK_NGX_D3D12_EvaluateFeature");
    PVOID create = export_address(core, "NVSDK_NGX_D3D12_CreateFeature");
    PVOID release = export_address(core, "NVSDK_NGX_D3D12_ReleaseFeature");
    if (eval == nullptr) { NLog("[核心] 补钩: _nvngx.dll=%p 导出表里没有 D3D12 求值入口, 不补", (void *)core); return; }
    if (eval == s_shell_eval_target) { NLog("[核心] 补钩: 核心求值和薄壳是同一个函数, 已经钩着了, 不用补"); return; }
    // 先填指针再 DetourAttach(&指针): 提交时其他线程是挂起的, 钩子生效那一刻指针已经是跳板。
    o_eval_core = reinterpret_cast<PFN_EvaluateFeature>(eval);
    const bool withCreate = o_create_core == nullptr && create != nullptr && create != s_shell_create_target;
    const bool withRelease = o_release_core == nullptr && release != nullptr && release != s_shell_release_target;
    if (withCreate) o_create_core = reinterpret_cast<PFN_CreateFeature>(create);
    if (withRelease) o_release_core = reinterpret_cast<PFN_ReleaseFeature18>(release);
    auto threads = mfgunlock::hook::internal::OpenOtherThreads();
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR)
    {
        result = DetourUpdateThread(GetCurrentThread());
        for (HANDLE thread : threads) if (result == NO_ERROR) result = DetourUpdateThread(thread);
        if (result == NO_ERROR) result = DetourAttach(&reinterpret_cast<PVOID &>(o_eval_core), reinterpret_cast<PVOID>(&Core_EvaluateFeature));
        if (result == NO_ERROR && withCreate) result = DetourAttach(&reinterpret_cast<PVOID &>(o_create_core), reinterpret_cast<PVOID>(&Core_CreateFeature));
        if (result == NO_ERROR && withRelease) result = DetourAttach(&reinterpret_cast<PVOID &>(o_release_core), reinterpret_cast<PVOID>(&Core_ReleaseFeature));
        if (result == NO_ERROR) result = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    for (HANDLE thread : threads) CloseHandle(thread);
    if (result != NO_ERROR)
    {
        o_eval_core = nullptr;
        if (withCreate) o_create_core = nullptr;
        if (withRelease) o_release_core = nullptr;
        NLog("[核心] 补钩核心失败: Detours=%ld, 仍只有薄壳一扇门", result);
        return;
    }
    NLog("[核心] 补钩核心成功: _nvngx.dll=%p 求值=%p 建=%s 放=%s", (void *)core, eval, withCreate ? "补上" : "原样", withRelease ? "补上" : "原样");
}

static int image_features_alive()
{
    int count = 0;
    AcquireSRWLockShared(&s_feature_lock);
    for (const auto &v : s_features) if (v.handle != nullptr && nrdispatch::image_candidate(v.id, false)) ++count;
    ReleaseSRWLockShared(&s_feature_lock);
    return count;
}

static void Report()
{
    static std::atomic<ULONGLONG> previous{0};
    const auto now = GetTickCount64();
    auto last = previous.load();
    if (now - last < 5000 || !previous.compare_exchange_strong(last, now)) return;
    hook_core_late();
    NLog("[nr-routing] image_evals=%d ignored_nonimage=%u ignored_fg=%u ignored_unknown=%u concurrent_NR_skips=%u",
         game_eval.load(), nrdispatch::non_image_calls.load(), nrdispatch::framegen_calls.load(), nrdispatch::unknown_calls.load(), nrdispatch::skipped.load());
    // ★超分建了、求值却一次没进来★ 一局只说一次, 体检报告能直接读懂是哪一种情况。
    static int silentReports = 0;
    static bool saidSilent = false;
    const int alive = image_features_alive();
    if (saidSilent || rendercore::Integrated() || game_eval.load() != 0 || alive == 0) silentReports = 0;
    else if (++silentReports >= 3)
    {
        saidSilent = true;
        NLog("[nr-routing] silent-image: 游戏建了超分/光线重构(%d 个), 但 15 秒里没有一次求值进钩子 | 薄壳钩=%d 核心钩=%d 核心加载=%s | 帧生成求值=%u",
             alive, o_eval != nullptr ? 1 : 0, o_eval_core != nullptr ? 1 : 0,
             g_ngx.mod != nullptr ? "033 自己加载" : (g_ngx.load_error.empty() ? "未尝试" : g_ngx.load_error.c_str()),
             nrdispatch::framegen_calls.load());
    }
}

// All resolved entry points are installed as one transaction. In particular,
// loading a thin wrapper must never leave the direct core CreateFeature unhooked.
static LONG attach_resolved_hooks()
{
    auto threads = mfgunlock::hook::internal::OpenOtherThreads();
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR) {
        result = DetourUpdateThread(GetCurrentThread());
        for (HANDLE thread : threads) if (result == NO_ERROR) result = DetourUpdateThread(thread);
        const auto attach = [&](PVOID *orig, PVOID replacement) {
            if (*orig != nullptr && result == NO_ERROR) result = DetourAttach(orig, replacement);
        };
        attach(&reinterpret_cast<PVOID &>(o_create), reinterpret_cast<PVOID>(&My_CreateFeature));
        attach(&reinterpret_cast<PVOID &>(o_create_core), reinterpret_cast<PVOID>(&Core_CreateFeature));
        attach(&reinterpret_cast<PVOID &>(o_eval), reinterpret_cast<PVOID>(&My_EvaluateFeature));
        attach(&reinterpret_cast<PVOID &>(o_eval_core), reinterpret_cast<PVOID>(&Core_EvaluateFeature));
        attach(&reinterpret_cast<PVOID &>(o_release), reinterpret_cast<PVOID>(&My_ReleaseFeature));
        attach(&reinterpret_cast<PVOID &>(o_release_core), reinterpret_cast<PVOID>(&Core_ReleaseFeature));
        if (result == NO_ERROR) result = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    for (HANDLE thread : threads) CloseHandle(thread);
    return result;
}

// ---------- 安装 ----------
// 钩的是【我们已经加载的那个 NGX 核心】里的函数本体。renodx 用的是同一个
// 驱动核心(注册表 NGXCore 指向的那份), 所以它的调用一定经过这里。
// 给就地插入用的入口: 只装钩子, 不碰任何参数(跑分辨率杠杆那套默认全关)
static bool install();
static bool install_hooks_for_inject() { return install(); }
static const char *status_text() { return status; }

static bool install()
{
    if (installed) return true;
    // 核心可能还没加载 —— 先确保它在场, 否则「只钩核心」那条路(Streamline 游戏)
    // 第一次进来会白等一轮。load_ngx_once 自己有幂等闸, 反复叫没成本。
    load_ngx_once();

    // ★★ 钩「游戏走的那扇门」, 不是我们自己走的那扇 ★★
    //   驱动那边是两层:
    //     nvngx.dll   薄壳 —— 【游戏】链的就是它 (导出偏移 ~0xA4F4)
    //     _nvngx.dll  核心 —— 我们自己的 NR 调用直接进它 (偏移 ~0x681E0)
    //   以前钩在核心上, 结果【只拦到自己的调用】。实测证据(古墓丽影暗影 +
    //   审判之眼): 拦到的求值参数永远只有 4 个、尺寸永远是输出分辨率
    //   5120x2160, 而且交换链一让位, 拦截次数立刻归零、帧号跟交换链 1:1 对齐
    //   —— 那都是我们自己。
    //   钩薄壳这一层: 既拦得到游戏的 DLSS, 又天然不会拦到我们自己的调用。
    HMODULE shell = pinned_module(L"nvngx.dll");   // 游戏自己载的也钉住, 钩子不会随它卸载而悬空
    const bool game_had_it = (shell != nullptr);
    DWORD load_err = 0;
    if (shell == nullptr && !g_ngx.core_dir.empty())
    {
        const std::string p = g_ngx.core_dir + "\\nvngx.dll";
        shell = LoadLibraryA(p.c_str());
        if (shell == nullptr) load_err = GetLastError();
        // 只说一次: 分清「游戏还没用 DLSS」和「我们自己也载不动」
        static bool said = false;
        if (!said)
        {
            said = true;
            if (shell != nullptr)
                NLog("[核心] 游戏还没加载薄壳, 我们先自己加载了一份: %s", p.c_str());
            else
                NLog("[核心] ! 薄壳自己也载不动 (错误 %lu): %s", load_err, p.c_str());
        }
    }

    // ★核心那扇门不靠 033 自己加载成功★ (2026-09-13 死亡搁浅2: 核心求值=0, 超分求值一次没进来)
    //   033 自己 LoadLibrary(_nvngx.dll) 可能失败(游戏收紧了 DLL 搜索路径), 但游戏/薄壳早把核心载进来了。
    //   那就用进程里现成的那份, 地址直接读导出表。
    HMODULE coreMod = g_ngx.mod;
    PFN_CreateFeature    coreCreate  = nullptr;
    PFN_EvaluateFeature  coreEval    = nullptr;
    PFN_ReleaseFeature18 coreRelease = nullptr;
    if (coreMod != nullptr)
    {
        coreCreate  = g_ngx.create;
        coreEval    = g_ngx.evaluate;
        coreRelease = reinterpret_cast<PFN_ReleaseFeature18>(GetProcAddress(coreMod, "NVSDK_NGX_D3D12_ReleaseFeature"));
    }
    else if ((coreMod = pinned_module(L"_nvngx.dll")) != nullptr)
    {
        coreCreate  = reinterpret_cast<PFN_CreateFeature>(export_address(coreMod, "NVSDK_NGX_D3D12_CreateFeature"));
        coreEval    = reinterpret_cast<PFN_EvaluateFeature>(export_address(coreMod, "NVSDK_NGX_D3D12_EvaluateFeature"));
        coreRelease = reinterpret_cast<PFN_ReleaseFeature18>(export_address(coreMod, "NVSDK_NGX_D3D12_ReleaseFeature"));
    }
    {
        static bool saidCore = false;
        if (!saidCore && g_ngx.mod == nullptr)
        {
            saidCore = true;
            NLog("[核心] 033 自己没载起 _nvngx.dll: %s | 进程里现成的核心=%p", g_ngx.load_error.c_str(), (void *)coreMod);
        }
    }

    PFN_CreateFeature   sc = nullptr;
    PFN_EvaluateFeature se = nullptr;
    if (shell != nullptr)
    {
        sc = reinterpret_cast<PFN_CreateFeature>(GetProcAddress(shell, "NVSDK_NGX_D3D12_CreateFeature"));
        se = reinterpret_cast<PFN_EvaluateFeature>(GetProcAddress(shell, "NVSDK_NGX_D3D12_EvaluateFeature"));
    }

    if (sc != nullptr && se != nullptr)
    {
        NLog("[核心] 钩驱动薄壳 nvngx.dll 模块=%p 来源=%s",
             (void *)shell, game_had_it ? "游戏已加载" : "我们自己加载");
    }
    else
    {
        // ★找不到薄壳就【什么都不装】, 接着等★
        //   退回钩核心是有害的: 那样只拦得到我们自己的调用, 却让上层以为
        //   「已挂上游戏的 DLSS」, 于是交换链把 feature 18 让出去 —— 两边都不做。
        //   薄壳是游戏用 DLSS 时才会被加载的, 所以:
        //     游戏用 DLSS → 迟早加载 → 我们装上 → 插到它的渲染分辨率上
        //     游戏不用 DLSS → 永远等不到 → 交换链自己做(满分辨率, 照旧能用)
        //   这正好是我们想要的两种行为, 不需要别的判断。
        // ★薄壳不在场不等于没救★ 用 Streamline 的游戏(燕云)根本不走薄壳,
        //   它的 DLSS 是 sl.dlss.dll 直接调核心 —— 那就只钩核心。
        //   我们自己的调用靠 g_is_ours 按句柄排除, 不会自己拦自己。
        if (coreEval != nullptr)
        {
            sc = coreCreate;        // create 只给分辨率杠杆/日志用, 没有也不影响插入
            se = nullptr;           // 求值走下面单独那套核心钩子
            NLog("[核心] 薄壳不在场 → 只钩核心 _nvngx.dll (Streamline 游戏走这条)");
        }
        else
        {
            strcpy_s(status, load_err ? "驱动的 nvngx.dll 载不动(见 nrscale 日志)"
                                      : "等游戏打开 DLSS…");
            return false;
        }
    }

    o_create = sc;
    o_create_core = coreCreate;
    if (o_create_core == o_create) o_create_core = nullptr;
    o_eval   = se;
    // ★ReleaseFeature 也要钩★ —— 账本的另一半。没有它就只知道谁建了,
    //   不知道谁放了, 换引擎永远等不到"对面撒手"。
    o_release = nullptr;
    if (shell != nullptr)
        o_release = reinterpret_cast<PFN_ReleaseFeature18>(
            GetProcAddress(shell, "NVSDK_NGX_D3D12_ReleaseFeature"));
    o_release_core = coreRelease;
    if (o_release_core != nullptr && o_release_core == o_release) o_release_core = nullptr;
    // 核心那扇门: 只要它在场就一起钩上(Streamline 游戏靠它, 普通游戏多钩一层也无害
    // —— 套着触发时 线程内的调用深度会让内层让位给外层)
    o_eval_core = coreEval;
    if (o_eval_core != nullptr && o_eval_core == o_eval) o_eval_core = nullptr;   // 别重复钩同一个

    s_shell_eval_target    = reinterpret_cast<PVOID>(o_eval);
    s_shell_create_target  = reinterpret_cast<PVOID>(o_create);
    s_shell_release_target = reinterpret_cast<PVOID>(o_release);
    const LONG r = attach_resolved_hooks();

    if (r != NO_ERROR)
    {
        _snprintf_s(status, sizeof(status), _TRUNCATE, "挂钩失败, Detours 返回 %ld", r);
        NLog("★DetourTransactionCommit 失败: %ld★", r);
        o_create = nullptr; o_create_core = nullptr; o_eval = nullptr; o_eval_core = nullptr;
        o_release = nullptr; o_release_core = nullptr;
        return false;
    }

    if (o_eval == nullptr && o_eval_core == nullptr)
    {
        strcpy_s(status, "两扇门都钩不上");
        return false;
    }
    installed = true;
    strcpy_s(status, "核心已挂钩, 等运行库出现");
    NLog("[核心] 挂钩成功: 薄壳求值=%p 核心求值=%p 薄壳建=%p 核心建=%p",
         reinterpret_cast<void *>(o_eval), reinterpret_cast<void *>(o_eval_core),
         reinterpret_cast<void *>(o_create), reinterpret_cast<void *>(o_create_core));
    return true;
}

// ---------- 挂运行库(snippet)的钩子 ----------
// renodx 会自己把 nvngx_dlssnr.dll 加载进来。它一出现就钩上 ——
// 所以这个要在 present 里反复试, 直到成功。
static bool install_snippet()
{
    if (snip_hooked) return true;

    HMODULE m = GetModuleHandleA("nvngx_dlssnr.dll");
    if (m == nullptr) return false;              // renodx 还没加载它

    auto c = reinterpret_cast<PFN_CreateFeature>(GetProcAddress(m, "NVSDK_NGX_D3D12_CreateFeature"));
    auto e = reinterpret_cast<PFN_EvaluateFeature>(GetProcAddress(m, "NVSDK_NGX_D3D12_EvaluateFeature"));
    if (c == nullptr || e == nullptr)
    {
        strcpy_s(status, "运行库在, 但没有 D3D12 导出");
        return false;
    }

    s_create = c; s_eval = e;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID &>(s_create), reinterpret_cast<PVOID>(&Snip_CreateFeature));
    DetourAttach(&reinterpret_cast<PVOID &>(s_eval),   reinterpret_cast<PVOID>(&Snip_EvaluateFeature));
    const LONG r = DetourTransactionCommit();

    if (r != NO_ERROR)
    {
        _snprintf_s(status, sizeof(status), _TRUNCATE, "运行库挂钩失败 %ld", r);
        NLog("★[运行库] DetourTransactionCommit 失败: %ld★", r);
        s_create = nullptr; s_eval = nullptr;
        return false;
    }

    snip_hooked = true;
    strcpy_s(status, "运行库已挂钩 —— renodx 的 NR 归我们管了");
    NLog("★[运行库] 挂钩成功★ 模块=%p CreateFeature=%p EvaluateFeature=%p",
         reinterpret_cast<void *>(m),
         reinterpret_cast<void *>(s_create), reinterpret_cast<void *>(s_eval));
    return true;
}

static void uninstall()
{
    if (!installed) return;
    auto threads = mfgunlock::hook::internal::OpenOtherThreads();
    if (DetourTransactionBegin() != NO_ERROR) { for (HANDLE t : threads) CloseHandle(t); return; }
    DetourUpdateThread(GetCurrentThread());
    for (HANDLE t : threads) DetourUpdateThread(t);
    if (o_create != nullptr)
        DetourDetach(&reinterpret_cast<PVOID &>(o_create), reinterpret_cast<PVOID>(&My_CreateFeature));
    if (o_create_core != nullptr)
        DetourDetach(&reinterpret_cast<PVOID &>(o_create_core), reinterpret_cast<PVOID>(&Core_CreateFeature));
    if (o_eval != nullptr)
        DetourDetach(&reinterpret_cast<PVOID &>(o_eval), reinterpret_cast<PVOID>(&My_EvaluateFeature));
    if (o_eval_core != nullptr)
        DetourDetach(&reinterpret_cast<PVOID &>(o_eval_core), reinterpret_cast<PVOID>(&Core_EvaluateFeature));
    if (o_release != nullptr)
        DetourDetach(&reinterpret_cast<PVOID &>(o_release), reinterpret_cast<PVOID>(&My_ReleaseFeature));
    if (o_release_core != nullptr)
        DetourDetach(&reinterpret_cast<PVOID &>(o_release_core), reinterpret_cast<PVOID>(&Core_ReleaseFeature));
    const LONG result = DetourTransactionCommit();
    for (HANDLE t : threads) CloseHandle(t);
    if (result != NO_ERROR) return;
    installed = false;
    strcpy_s(status, "已摘钩");
}

} // namespace nrscale
