// =====================================================================
//  dynwork.h  ·  「看不见的时候才不跑模型」控制器 (历史实验，禁止接回产品)
//
//  2026-09-04 结论：时域模型会跨帧收敛，逐帧跑/不跑会留下旧答案并产生恢复跳变；
//  社区实现也没有可复现的正向证据。此文件和测试只留作反例档案，产品入口没有 include，
//  后续不要再以“省帧”为由接回或开放配置。
//
//  为什么要有这个
//    实测(4090): 一帧里模型占 97-99% 的 GPU 开销 (3840x1620 要 8.1-8.9ms,
//    2560x1080 要 4.2-4.8ms), 拷入/编码/合成/拷回加起来才 0.3ms。
//    想省开销只有一条路: 少跑模型。
//
//  ★为什么不能改分辨率★
//    换 feature 的分辨率 = 重建 feature, 一次 200ms 级, 而且参考实现的文档
//    明写了: 频繁重建会让驱动停止响应。所以模型分辨率一旦建好【绝不动态改】。
//    这里唯一允许的动态动作是: 这一帧「跑模型」还是「不跑模型」。
//
//  ★业主的硬约束: 画质不许有可感知的下降★
//    所以只在【细节物理上看不见】的帧上不跑:
//      (a) 镜头/画面快速运动 —— 整帧运动矢量的【中位数】位移超过阈值
//          (像素/帧, 渲染分辨率下)。快速运动时显示器自身的运动模糊 +
//          眼睛的追踪极限已经把细节抹掉了, 模型加的细节根本落不到视网膜上。
//      (b) 暗场 —— 大部分像素低于合成着色器的暗膝 (白点归一后的线性亮度
//          < 0.05; UNORM/SDR 代理图是 sRGB 编码的, 先 pow 2.2 线性化)。
//          那一段合成本来就几乎不叠差值, 跑了也白跑。
//
//  ★为什么用中位数不用平均数★
//    平均数会被两头骗: HUD/准星/静止的地面是零位移, 会把平均数往下拉,
//    让「整个画面在飞」看起来像「没怎么动」; 反过来一个人物在静止背景前
//    奔跑, 少数像素位移巨大会把平均数抬上去 —— 可背景纹理清清楚楚, 这时
//    跳过就是肉眼可见的画质下降。中位数只回答一件事: 「一半以上的像素
//    在动多少」, 这才是「细节看不看得见」的正确问法。
//
//  ★跳过的那一帧到底发生了什么★
//    就地插入路: Stage 在拷入之前就 return 0, 游戏自己的 DLSS Output 原样
//    往下走; 交换链路: 后缓冲一个字节都不碰。也就是说, 跳过的帧 = 「神经
//    渲染关闭」那一帧 —— 永远不会比 NR 关掉更差, 只是暂时少了模型那份加成,
//    而这份加成恰好在那一帧看不见。
//
//  ★为什么恢复时要 reset=1★
//    模型是时域的: 它拿上一帧的输出做历史来稳定这一帧。跳过了 N 帧之后,
//    它手里的历史是 N 帧前的画面, 按现在的运动矢量去 warp 一张陈旧的图 =
//    拖影/红斑(实测栽过)。所以跳过段结束后的第一次求值必须 reset=1, 让它
//    当单帧重新开始。
//
//  ★为什么要斜坡★
//    恢复的瞬间是「原画」跳到「原画+模型差值」, 一帧之内加成从 0 到满 =
//    肉眼可见的「啪」一下, 尤其模型刚 reset 的头几帧没有历史, 输出偏差
//    最大。所以 blend 从 0 用 ~10 帧爬到用户设的值 (前段压得更低: smoothstep),
//    眼睛看到的是渐进变清楚, 不是跳变。
//
//  ★为什么要滞回 + 上限★
//    滞回: 连续 N 帧超阈值才跳, 连续 M 帧回落才恢复 —— 阈值附近抖动的画面
//    不会被反复 reset (每次 reset 都是一次「历史清空」, 抖着切比不切更糟)。
//    上限: 最近 120 帧里最多跳多少 —— 多疑的用户可以用它把影响封死
//    (dynwork_maxskip=10 就是「顶多 10% 的帧」), 顶到上限就强制恢复。
//
//  ★没有统计就绝不跳★
//    统计(位移中位/暗占比)来自 GPU 侧一趟很便宜的统计着色器 + 回读环
//    (跟 gputime.h 一样隔几帧读, 不阻塞)。回读没到 / 统计着色器建不起 /
//    force_off 打开 —— 任何一种「看不清情况」都只允许一个动作: 跑模型。
//    ★统计那一趟在跳过的帧上也必须继续跑★ (读的是运动矢量引导图和暗占比,
//    不依赖模型输出), 否则跳过期间没有统计 → 立刻恢复 → 又跳 → 来回抽。
//
//  线程: 全部静态变量, 只在渲染线程(调 Stage 的那条线程)上 tick;
//        面板从另一条线程读 note()/计数器只是看数字, 错一帧无所谓。
//
//  依赖: 什么都不依赖 (纯 C++). 日志走 DYNWORK_LOG(可选):
//    #define DYNWORK_LOG Log   放在 #include "dynwork.h" 之前即可。
//  配置: dlss5-033.cfg 里的 key=int 行由 carrier::load_cfg 转发到 set() ——
//    见文件末尾「接线」。
// =====================================================================
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifndef DYNWORK_LOG
#define DYNWORK_LOG(...) ((void)0)
#endif

namespace dynwork
{

// ------------------------------------------------------------ 每帧输入
struct FrameStats
{
    float    mv_median_px;   // 整帧 |MV| 的中位数, 单位: 像素/帧, 【渲染分辨率】下
    float    dark_frac;      // 0..1: 代理图里低于暗膝(线性亮度 < 0.05)的像素占比
    bool     stats_valid;    // false = 这一帧没有可信统计 (回读没到/着色器没建) → 绝不跳
    float    model_ms;       // 可选: 最近一次模型 GPU 耗时 (只用来估算省了多少; 0 = 不知道)
    unsigned frame_index;    // 只用来写日志
};

// ------------------------------------------------------------ 配置 (cfg 里全是整数)
static int cfg_enabled    = 0;    // dynwork=0/1            ★默认关★ 业主没拍板前不默认开
static int cfg_mv_px      = 0;    // dynwork_mv=像素        绝对阈值; 0 = 用下面的千分比推
static int cfg_mvpct      = 10;   // dynwork_mvpct=千分比   阈值 = 渲染高 × 千分比 / 1000
                                  //   默认 10‰: 1253 高 → 12.5px (≈ 1440p 级的 12px)
static int cfg_dark_pct   = 85;   // dynwork_dark=百分比    暗像素占比 ≥ 85% 才算暗场
static int cfg_on_frames  = 6;    // dynwork_on_frames      连续超阈值多少帧才开始跳
static int cfg_off_frames = 3;    // dynwork_off_frames     连续回落多少帧才恢复
static int cfg_ramp       = 10;   // dynwork_ramp           恢复后 blend 从 0 爬到满用几帧
static int cfg_maxskip    = 50;   // dynwork_maxskip=百分比 最近 120 帧里最多跳这个比例
static int cfg_log        = 1;    // dynwork_log            状态转换写日志

static const int kWindow = 120;   // 滚动窗口长度 (帧)

// ------------------------------------------------------------ 内部状态
enum State { kRun = 0, kSkip = 1 };

static State          s_state        = kRun;
static bool           s_force_off    = false;
static int            s_over         = 0;      // 连续「看不见」的帧数
static int            s_under        = 0;      // 连续「看得见」的帧数
static bool           s_pending_reset = false; // 恢复后第一次求值要 reset=1 (读一次即清)
static int            s_ramp_pos     = 1 << 20; // 斜坡进度 (帧); 大数 = 已经到顶
static unsigned       s_since_resume = 1u << 20; // 上次恢复以来跑了多少帧 (斜坡没爬完不许再跳)
static unsigned char  s_ring[kWindow] = {};   // 滚动窗口: 1 = 那帧跳过了
static int            s_ring_idx     = 0;
static int            s_ring_filled  = 0;
static int            s_ring_skips   = 0;      // 窗口内跳过的帧数
static FrameStats     s_last         = {};
static float          s_last_thr_px  = 0.0f;
static unsigned long long s_frames   = 0;      // tick 过的总帧数
static unsigned long long s_skipped  = 0;      // 跳过的总帧数
static unsigned       s_skip_runs    = 0;      // 跳过段的段数
static int            s_run_len      = 0;      // 当前/最近一段跳了几帧
static double         s_saved_ms     = 0.0;    // 估算省下的模型时间 (Σ model_ms)
static char           s_reason[64]   = "还没动作";  // 最近一次转换 / 当前为何不跳
static int            s_logged       = 0;      // 日志条数 (防刷)

// ------------------------------------------------------------ 小工具
static int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   cap_frames() { return kWindow * cfg_maxskip / 100; }     // 窗口内允许的最多跳过帧数

// 位移阈值 (像素/帧). 0 = 无法判定 (没有渲染高度也没有绝对阈值) → 不跳
static float threshold_px(unsigned render_h)
{
    if (cfg_mv_px > 0) return static_cast<float>(cfg_mv_px);
    if (render_h == 0) return 0.0f;
    return static_cast<float>(render_h) * static_cast<float>(cfg_mvpct) / 1000.0f;
}
static float dark_threshold() { return static_cast<float>(cfg_dark_pct) / 100.0f; }

static void ring_push(bool skip)
{
    if (s_ring_filled == kWindow) s_ring_skips -= s_ring[s_ring_idx];
    else                          ++s_ring_filled;
    s_ring[s_ring_idx] = skip ? 1 : 0;
    if (skip) ++s_ring_skips;
    s_ring_idx = (s_ring_idx + 1) % kWindow;
}

static void log_transition(const char *what, const FrameStats &st)
{
    if (!cfg_log) return;
    if (s_logged == 400) { ++s_logged; DYNWORK_LOG("[dynwork] 转换太频繁, 日志静默 (调大 on/off_frames)"); return; }
    if (s_logged > 400) return;
    ++s_logged;
    DYNWORK_LOG("[dynwork] 帧 %u: %s · 位移中位 %.1f px (阈 %.1f) · 暗占比 %.0f%% (阈 %d%%) · 近%d帧已跳 %d/%d · 本段 %d 帧",
                st.frame_index, what, st.mv_median_px, s_last_thr_px, st.dark_frac * 100.0f,
                cfg_dark_pct, kWindow, s_ring_skips, cap_frames(), s_run_len);
}

static void enter_skip(bool motion, bool dark, const FrameStats &st)
{
    s_state   = kSkip;
    s_run_len = 0;
    ++s_skip_runs;
    std::snprintf(s_reason, sizeof(s_reason), "跳过: %s",
                  (motion && dark) ? "快速运动+暗场" : (motion ? "快速运动" : "暗场"));
    log_transition(s_reason, st);
}

static void resume(const char *why, const FrameStats &st)
{
    s_state         = kRun;
    s_pending_reset = true;   // 历史陈旧 → 下一次求值 reset=1
    s_ramp_pos      = 0;      // blend 从 0 爬
    s_since_resume  = 0;
    s_over = 0; s_under = 0;
    std::snprintf(s_reason, sizeof(s_reason), "恢复: %s", why);
    log_transition(s_reason, st);
}

// ------------------------------------------------------------ 配置读入
// carrier::load_cfg 把它不认识的 key=int 转发到这里; 吃掉了就返回 true。
// 安全钳位: 暗场阈值不许低于 50% (再低就是在「不暗」的帧上跳 = 可见掉画质);
//           位移千分比不许低于 2‰ (2.5px@1253 已经是走路级的位移)。
static bool set(const char *key, int v)
{
    if (key == nullptr) return false;
    if      (!std::strcmp(key, "dynwork"))            cfg_enabled    = v ? 1 : 0;
    else if (!std::strcmp(key, "dynwork_mv"))         cfg_mv_px      = clampi(v, 0, 10000);
    else if (!std::strcmp(key, "dynwork_mvpct"))      cfg_mvpct      = clampi(v, 2, 1000);
    else if (!std::strcmp(key, "dynwork_dark"))       cfg_dark_pct   = clampi(v, 50, 100);
    else if (!std::strcmp(key, "dynwork_on_frames"))  cfg_on_frames  = clampi(v, 1, 600);
    else if (!std::strcmp(key, "dynwork_off_frames")) cfg_off_frames = clampi(v, 1, 600);
    else if (!std::strcmp(key, "dynwork_ramp"))       cfg_ramp       = clampi(v, 0, 120);
    else if (!std::strcmp(key, "dynwork_maxskip"))    cfg_maxskip    = clampi(v, 0, 100);
    else if (!std::strcmp(key, "dynwork_log"))        cfg_log        = v ? 1 : 0;
    else return false;
    return true;
}

// 统计不可信/排障时一刀切: 打开后绝不跳, 正在跳的立刻恢复
static void force_off(bool on) { s_force_off = on; }
static bool forced_off()       { return s_force_off; }

// feature 重建 / 设备重建后调: 历史全清 (新 feature 本来就没有历史, 不需要 reset/斜坡)
static void reset_state()
{
    s_state = kRun; s_over = 0; s_under = 0; s_pending_reset = false;
    s_ramp_pos = 1 << 20; s_since_resume = 1u << 20; s_run_len = 0;
    std::memset(s_ring, 0, sizeof(s_ring));
    s_ring_idx = 0; s_ring_filled = 0; s_ring_skips = 0;
    std::snprintf(s_reason, sizeof(s_reason), "重建后重新计数");
}

// ------------------------------------------------------------ 每帧一次
// 在 Stage 里、拷入之前调一次 (每帧恰好一次); render_h = 引导图高度 (渲染分辨率)。
static void tick(const FrameStats &st, unsigned render_h)
{
    ++s_frames;
    s_last        = st;
    s_last_thr_px = threshold_px(render_h);

    // 允许判定的前提: 开着 · 没被强制关 · 统计可信 · 阈值算得出来
    const char *block = nullptr;
    if      (!cfg_enabled)         block = "已关";
    else if (s_force_off)          block = "强制不跳";
    else if (!st.stats_valid)      block = "没有统计";
    else if (s_last_thr_px <= 0.f) block = "没有渲染高度";
    const bool allowed = (block == nullptr);

    const bool motion    = allowed && st.mv_median_px >= s_last_thr_px;
    const bool dark      = allowed && st.dark_frac    >= dark_threshold();
    const bool invisible = motion || dark;

    if (allowed) { if (invisible) { ++s_over; s_under = 0; } else { ++s_under; s_over = 0; } }
    else         { s_over = 0; s_under = 0; }

    const bool cap_ok = s_ring_skips < cap_frames();   // 窗口里还有跳一帧的余量

    if (s_state == kSkip)
    {
        const char *why = nullptr;
        if      (!allowed)                    why = block;
        else if (s_under >= cfg_off_frames)   why = "画面又看得清了";
        else if (!cap_ok)                     why = "到了跳过上限";
        if (why != nullptr) resume(why, st);
    }
    else
    {
        if (allowed && s_over >= cfg_on_frames && cap_ok &&
            s_since_resume >= static_cast<unsigned>(cfg_ramp))
            enter_skip(motion, dark, st);
        else if (!allowed)
            std::snprintf(s_reason, sizeof(s_reason), "不跳: %s", block);
        else if (invisible && !cap_ok)
            std::snprintf(s_reason, sizeof(s_reason), "不跳: 到了上限");
    }

    const bool skip = (s_state == kSkip);
    ring_push(skip);
    if (skip)
    {
        ++s_skipped; ++s_run_len;
        if (st.model_ms > 0.0f) s_saved_ms += st.model_ms;
    }
    else
    {
        if (s_since_resume < (1u << 30)) ++s_since_resume;
        if (s_ramp_pos < cfg_ramp)       ++s_ramp_pos;
    }
}

// ------------------------------------------------------------ 每帧读
static bool skip_this_frame() { return s_state == kSkip; }

// 恢复后第一次求值 = true, 读一次即清。★只在 nrfwd::evaluate 的调用点读★ (面板用 reset_pending)
static bool needs_reset()
{
    const bool r = s_pending_reset;
    s_pending_reset = false;
    return r;
}
static bool reset_pending() { return s_pending_reset; }

// 0..1: 恢复斜坡期间乘在 cfg.blend 上; 平时 1.0
//   smoothstep: 头几帧压得更低 —— 刚 reset 的模型没历史, 输出偏差最大
static float blend_scale()
{
    if (cfg_ramp <= 0 || s_ramp_pos >= cfg_ramp) return 1.0f;
    float t = static_cast<float>(s_ramp_pos) / static_cast<float>(cfg_ramp);
    if (t < 0.0f) t = 0.0f;
    return t * t * (3.0f - 2.0f * t);
}

// ------------------------------------------------------------ 面板用
static const char *state_name()  { return s_state == kSkip ? "跳过中" : "运行中"; }
// 分母固定 120 (没填满的格子按「没跳」算): 上限是按【任意 120 帧里的绝对帧数】封的,
//   这样面板上的数永远 ≤ dynwork_maxskip, 不会在开局前几十帧显示成 90% 吓人。
static int  window_skip_pct()    { return (100 * s_ring_skips) / kWindow; }
static unsigned long long frames_total()   { return s_frames; }
static unsigned long long frames_skipped() { return s_skipped; }
static unsigned skip_runs()      { return s_skip_runs; }
static double   saved_ms()       { return s_saved_ms; }
static float    last_mv_px()     { return s_last.mv_median_px; }
static float    last_dark_frac() { return s_last.dark_frac; }
static float    last_thr_px()    { return s_last_thr_px; }
static const char *reason()      { return s_reason; }
static bool     enabled()        { return cfg_enabled != 0; }

// 一行状态: 状态 · 窗口内跳过% · 位移中位 · 暗占比 · 原因
static const char *note()
{
    static char b[256];
    if (!cfg_enabled)
    {
        std::snprintf(b, sizeof(b), "已否决（历史实验，不接回产品）");
        return b;
    }
    std::snprintf(b, sizeof(b),
                  "%s · 近%d帧跳过 %d%% (上限 %d%%) · 位移中位 %.1f px (阈 %.1f) · 暗占比 %.0f%% (阈 %d%%) · %s%s",
                  state_name(), kWindow, window_skip_pct(), cfg_maxskip,
                  s_last.mv_median_px, s_last_thr_px, s_last.dark_frac * 100.0f, cfg_dark_pct,
                  s_reason, s_last.stats_valid ? "" : " · ★无统计★");
    return b;
}

} // namespace dynwork

// =====================================================================
//  接线 (给整合者, 本文件不改任何现有文件):
//
//  dlss5_033.cpp  —— 在 #include "carrier.h" 之前 (load_cfg 要转发 key):
//      #define DYNWORK_LOG Log
//      #include "dynwork.h"
//
//  carrier.h load_cfg() 的 else-if 链末尾加一行:
//      else if (dynwork::set(line, v)) {}
//
//  hostnr.h Stage():  在 `if (s_grace > 0) { --s_grace; return 0; }` 之后、
//      `staterestore::Envelope state_env(cl);` 之前 (还没 Begin 计时、还没绑任何状态):
//      dynwork::tick(stats_from_readback /* FrameStats */, gh_now /* 引导图高 */);
//      if (dynwork::skip_this_frame()) return 0;     // 游戏自己的 Output 原样往下走
//
//      nrfwd::evaluate(..., depthInverted, reset | (dynwork::needs_reset() ? 1 : 0), ...)
//
//      scale::DispatchResolve(..., carrier::cfg.blend / 100.0f * dynwork::blend_scale(), ...)
//
//  panel.h:  panel::info("省帧", dynwork::note());
// =====================================================================
