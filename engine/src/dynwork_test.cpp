// =====================================================================
//  dynwork_test.cpp —— 证明 dynwork.h 在 /std:c++17 /utf-8 /W3 + ReShade 6.8 头文件
//  下能编过, 并用一段合成序列 (静止 → 横摇 → 静止) 验证状态机:
//    · 只在连续 on_frames 帧超阈值后才跳
//    · 回落 off_frames 帧后恢复, needs_reset 恰好为 true 一次, blend 从 0 爬到 1
//    · 上限/暗场/无统计/强制关/配置转发
//  不进成品。用法 (与 build.bat 同一套环境变量):
//    cl /nologo /std:c++17 /O2 /MT /EHa /utf-8 /W3 /I sdk\reshade-6.8.0\include /I sdk /I src
//       /Fe<临时目录>\dynwork_test.exe /Fo<临时目录>\ src\dynwork_test.cpp
// =====================================================================
#include <Windows.h>
#include <d3d12.h>
#include <reshade.hpp>
#include <cstdarg>
#include <cstdio>

static int g_logs = 0;
static void TestLog(const char *fmt, ...)
{
    ++g_logs;
    va_list ap; va_start(ap, fmt);
    std::printf("    log> ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
}
#define DYNWORK_LOG TestLog
#include "dynwork.h"

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  [失败] %s (行 %d)\n", msg, __LINE__); } } while (0)

static const unsigned kRenderH = 1253;     // 就地插入实测的引导图高 (5120x2160 → 2970x1253)
static unsigned g_idx = 0;

static dynwork::FrameStats mk(float mv, float dark, bool valid = true, float model_ms = 8.5f)
{
    dynwork::FrameStats s;
    s.mv_median_px = mv; s.dark_frac = dark; s.stats_valid = valid;
    s.model_ms = model_ms; s.frame_index = ++g_idx;
    return s;
}

static void fresh(int enabled = 1)
{
    dynwork::reset_state();
    dynwork::set("dynwork", enabled);
    dynwork::set("dynwork_mv", 0);
    dynwork::set("dynwork_mvpct", 10);
    dynwork::set("dynwork_dark", 85);
    dynwork::set("dynwork_on_frames", 6);
    dynwork::set("dynwork_off_frames", 3);
    dynwork::set("dynwork_ramp", 10);
    dynwork::set("dynwork_maxskip", 50);
    dynwork::set("dynwork_log", 1);
}

// ---- 场景 A: 静止 → 横摇 → 静止 (主线) ----
static void scenario_still_pan_still()
{
    std::printf("场景 A: 静止 → 横摇 → 静止\n");
    fresh();
    const float thr = dynwork::threshold_px(kRenderH);
    std::printf("  阈值 = %.2f px (10‰ × %u)\n", thr, kRenderH);
    CHECK(thr > 12.0f && thr < 13.0f, "10‰@1253 ≈ 12.5px");

    // 30 帧静止: 绝不跳, 不 reset, blend 1.0
    bool any_skip = false, any_reset = false, blend_ok = true;
    for (int i = 0; i < 30; ++i)
    {
        dynwork::tick(mk(0.4f, 0.10f), kRenderH);
        any_skip  |= dynwork::skip_this_frame();
        any_reset |= dynwork::needs_reset();
        blend_ok  &= (dynwork::blend_scale() == 1.0f);
    }
    CHECK(!any_skip, "静止 30 帧不跳");
    CHECK(!any_reset, "静止期间不 reset");
    CHECK(blend_ok, "静止期间 blend 1.0");
    CHECK(dynwork::window_skip_pct() == 0, "窗口跳过 0%");

    // 横摇 30px/帧: 前 5 帧不跳, 第 6 帧开始跳
    int first_skip = -1;
    for (int i = 1; i <= 40; ++i)
    {
        dynwork::tick(mk(30.0f, 0.10f), kRenderH);
        if (dynwork::skip_this_frame() && first_skip < 0) first_skip = i;
        if (i < 6) CHECK(!dynwork::skip_this_frame(), "on_frames 之前不跳");
        else       CHECK(dynwork::skip_this_frame(), "on_frames 之后每帧都跳");
        CHECK(!dynwork::needs_reset(), "跳过期间不产生 reset");
    }
    CHECK(first_skip == 6, "第 6 个超阈值帧才开始跳");
    CHECK(dynwork::skip_runs() == 1, "一段跳过");
    CHECK(dynwork::frames_skipped() == 35, "横摇 40 帧里跳了 35");
    std::printf("  横摇中: %s\n", dynwork::note());

    // 回到静止: 前 2 帧还在跳, 第 3 帧恢复
    dynwork::tick(mk(0.3f, 0.10f), kRenderH);
    CHECK(dynwork::skip_this_frame(), "回落第 1 帧仍跳");
    dynwork::tick(mk(0.3f, 0.10f), kRenderH);
    CHECK(dynwork::skip_this_frame(), "回落第 2 帧仍跳");
    dynwork::tick(mk(0.3f, 0.10f), kRenderH);
    CHECK(!dynwork::skip_this_frame(), "回落第 3 帧恢复");
    CHECK(dynwork::reset_pending(), "恢复帧 reset 待发");
    CHECK(dynwork::needs_reset(), "恢复后第一次求值 reset=1");
    CHECK(!dynwork::needs_reset(), "reset 读一次即清");
    float prev = dynwork::blend_scale();
    std::printf("  恢复第 1 帧 blend_scale = %.3f\n", prev);
    CHECK(prev > 0.0f && prev < 0.2f, "恢复第 1 帧 blend 很低但非零");
    bool mono = true, reached = false;
    for (int i = 2; i <= 10; ++i)
    {
        dynwork::tick(mk(0.3f, 0.10f), kRenderH);
        CHECK(!dynwork::needs_reset(), "斜坡期间不再 reset");
        const float b = dynwork::blend_scale();
        if (b < prev) mono = false;
        prev = b;
        if (i == 10) reached = (b == 1.0f);
        std::printf("  恢复第 %2d 帧 blend_scale = %.3f\n", i, b);
    }
    CHECK(mono, "blend 单调上升");
    CHECK(reached, "第 10 帧 blend 到 1.0");
    dynwork::tick(mk(0.3f, 0.10f), kRenderH);
    CHECK(dynwork::blend_scale() == 1.0f, "斜坡之后一直 1.0");
    std::printf("  恢复后: %s\n", dynwork::note());

    // 斜坡没爬完不许再跳: 恢复后立刻又横摇
    fresh();
    for (int i = 0; i < 10; ++i) dynwork::tick(mk(30.0f, 0.1f), kRenderH);   // 进入跳过
    CHECK(dynwork::skip_this_frame(), "已在跳");
    for (int i = 0; i < 3; ++i) dynwork::tick(mk(0.3f, 0.1f), kRenderH);     // 恢复
    CHECK(!dynwork::skip_this_frame(), "已恢复");
    (void)dynwork::needs_reset();
    int resk = -1;
    for (int i = 2; i <= 20; ++i)   // 恢复帧算第 1 帧, 从第 2 帧起又横摇
    {
        dynwork::tick(mk(30.0f, 0.1f), kRenderH);
        if (dynwork::skip_this_frame() && resk < 0) resk = i;
    }
    std::printf("  恢复后马上再横摇: 第 %d 帧才再次跳 (斜坡 10 帧 + on_frames 6)\n", resk);
    CHECK(resk == 11, "斜坡 10 帧爬完 (第 1..10 帧跑) 之后第 11 帧才准再跳");
}

// ---- 场景 B: 上限 —— 连续横摇 480 帧, 任意 120 帧窗口内跳过 ≤ 60 ----
static void scenario_cap()
{
    std::printf("场景 B: 上限 50%% (连续横摇 480 帧)\n");
    fresh();
    static unsigned char hist[480];
    int resets = 0, worst = 0, total = 0, pct_bad = 0;
    const unsigned long long skipped_before = dynwork::frames_skipped();   // 终身计数不随 reset_state 清零
    for (int i = 0; i < 480; ++i)
    {
        dynwork::tick(mk(40.0f, 0.1f), kRenderH);
        hist[i] = dynwork::skip_this_frame() ? 1 : 0;
        total += hist[i];
        if (!hist[i] && dynwork::needs_reset()) ++resets;
        if (dynwork::window_skip_pct() > 50) ++pct_bad;
    }
    CHECK(pct_bad == 0, "面板上的窗口跳过% 从不超过上限");
    for (int s = 0; s + 120 <= 480; ++s)
    {
        int c = 0;
        for (int k = 0; k < 120; ++k) c += hist[s + k];
        if (c > worst) worst = c;
    }
    std::printf("  任意 120 帧窗口最多跳 %d 帧 · 本场景跳 %d/480 · 恢复次数 %d · 估省 %.0f ms\n",
                worst, total, resets, dynwork::saved_ms());
    CHECK(worst <= 60, "任意 120 帧窗口内跳过 ≤ 60");
    CHECK(total * 2 <= 480, "本场景总跳过 ≤ 50%");
    CHECK(dynwork::frames_skipped() == skipped_before + static_cast<unsigned long long>(total), "终身计数累加正确");
    CHECK(resets >= 1, "顶到上限时恢复过 (带 reset)");
    CHECK(dynwork::skip_runs() >= 2, "上限之后还能再进入跳过段");
    std::printf("  %s\n", dynwork::note());

    // 上限 0 = 永不跳
    fresh();
    dynwork::set("dynwork_maxskip", 0);
    bool any = false;
    for (int i = 0; i < 60; ++i) { dynwork::tick(mk(40.0f, 0.1f), kRenderH); any |= dynwork::skip_this_frame(); }
    CHECK(!any, "maxskip=0 永不跳");
}

// ---- 场景 C: 暗场 ----
static void scenario_dark()
{
    std::printf("场景 C: 暗场\n");
    fresh();
    int first = -1;
    for (int i = 1; i <= 12; ++i)
    {
        dynwork::tick(mk(0.0f, 0.95f), kRenderH);
        if (dynwork::skip_this_frame() && first < 0) first = i;
    }
    CHECK(first == 6, "暗场 6 帧后跳");
    CHECK(std::strstr(dynwork::reason(), "暗场") != nullptr, "原因写的是暗场");
    // 暗占比 84% 不够 (阈 85)
    for (int i = 0; i < 3; ++i) dynwork::tick(mk(0.0f, 0.84f), kRenderH);
    CHECK(!dynwork::skip_this_frame(), "暗占比降到 84% 三帧后恢复");
    CHECK(dynwork::needs_reset(), "暗场恢复也要 reset");
    std::printf("  %s\n", dynwork::note());
}

// ---- 场景 D: 统计失效 / 强制关 ----
static void scenario_invalid()
{
    std::printf("场景 D: 统计失效 / 强制关\n");
    fresh();
    for (int i = 0; i < 10; ++i) dynwork::tick(mk(30.0f, 0.1f), kRenderH);
    CHECK(dynwork::skip_this_frame(), "已在跳");
    dynwork::tick(mk(30.0f, 0.1f, false), kRenderH);      // 统计没到
    CHECK(!dynwork::skip_this_frame(), "统计失效立刻恢复");
    CHECK(dynwork::needs_reset(), "统计失效恢复也 reset");
    bool any = false;
    for (int i = 0; i < 30; ++i) { dynwork::tick(mk(99.0f, 0.99f, false), kRenderH); any |= dynwork::skip_this_frame(); }
    CHECK(!any, "没有统计绝不跳");
    CHECK(std::strstr(dynwork::note(), "无统计") != nullptr, "note 标出无统计");

    fresh();
    dynwork::force_off(true);
    any = false;
    for (int i = 0; i < 30; ++i) { dynwork::tick(mk(99.0f, 0.99f), kRenderH); any |= dynwork::skip_this_frame(); }
    CHECK(!any, "force_off 绝不跳");
    dynwork::force_off(false);

    // 渲染高度 0 且没有绝对阈值 → 不跳; 给了绝对阈值 → 能跳
    fresh();
    any = false;
    for (int i = 0; i < 30; ++i) { dynwork::tick(mk(99.0f, 0.1f), 0); any |= dynwork::skip_this_frame(); }
    CHECK(!any, "没有渲染高度不跳");
    dynwork::set("dynwork_mv", 12);
    for (int i = 0; i < 6; ++i) dynwork::tick(mk(99.0f, 0.1f), 0);
    CHECK(dynwork::skip_this_frame(), "绝对阈值 dynwork_mv 生效");
}

// ---- 场景 E: 配置转发 / 关 / 抖动 ----
static void scenario_cfg()
{
    std::printf("场景 E: 配置转发 / 关 / 阈值附近抖动\n");
    CHECK(dynwork::set("dynwork", 1), "认识 dynwork");
    CHECK(dynwork::set("dynwork_mvpct", 10), "认识 dynwork_mvpct");
    CHECK(dynwork::set("dynwork_log", 1), "认识 dynwork_log");
    CHECK(!dynwork::set("blend", 100), "不吃别人的键");
    CHECK(!dynwork::set("dynworkx", 1), "不吃相近的键");
    CHECK(!dynwork::set(nullptr, 1), "空键返回 false");
    dynwork::set("dynwork_dark", 5);
    CHECK(dynwork::cfg_dark_pct == 50, "暗场阈值钳在 50% 以上");
    dynwork::set("dynwork_on_frames", 0);
    CHECK(dynwork::cfg_on_frames == 1, "on_frames 至少 1");

    fresh(0);
    bool any = false;
    for (int i = 0; i < 60; ++i) { dynwork::tick(mk(40.0f, 0.99f), kRenderH); any |= dynwork::skip_this_frame(); }
    CHECK(!any, "dynwork=0 永不跳");
    CHECK(std::strstr(dynwork::note(), "dynwork=1") != nullptr, "关着时 note 提示怎么打开");

    fresh();
    any = false;
    for (int i = 0; i < 120; ++i)   // 13 / 11 交替: 永远凑不齐 6 帧连续
    {
        dynwork::tick(mk((i & 1) ? 13.0f : 11.0f, 0.1f), kRenderH);
        any |= dynwork::skip_this_frame();
    }
    CHECK(!any, "阈值附近抖动不跳");
}

int main()
{
    std::printf("dynwork_test —— 合成序列验证\n");
    scenario_still_pan_still();
    scenario_cap();
    scenario_dark();
    scenario_invalid();
    scenario_cfg();
    std::printf("\n通过 %d · 失败 %d · 日志 %d 条\n", g_pass, g_fail, g_logs);
    return g_fail == 0 ? 0 : 1;
}
