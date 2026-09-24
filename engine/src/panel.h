// =====================================================================
//  panel.h  ——  033 面板
//
//  Integrated engine: 033 Render Studio, with a responsive page layout.
//  Existing compatibility/diagnostic actions remain in the legacy sections.
// =====================================================================
#pragma once
#include "panel_studio.h"
#include "yanyun_appearance_store.h"
#include "panel_yanyun_recipe.h"
#include "panel_yanyun_columns.h"
#include "yanyun_tuning.h"
#include "yanyun_hotkey_store.h"
#include "yanyun_fg_check.h"

namespace panel
{

static float LabelW() { return ImGui::GetFontSize() * 5.5f; }

// 面板上显示的档位名 = 内部工作比例 + 50。
// 内部还是 50..100(真实的分辨率占比, 日志/cfg/平方律算账都按它来),
// ★界面上就显示真实倍率★ (业主 2026-09-04 夜: 「150 这种读取的也改回正常的」)
//   以前为了"默认档显示成 100 才像个基准"给它加了 50 的偏移, 结果 100% 显示成 150,
//   跟参考实现的 0.25x-2.0x 对不上, 自己看日志也要换算。现在一比一显示:
//   100 = 模型跟画面同尺寸(1:1), 50 = 一半, 200 = 超采样两倍。
static int  DispWork(int w)   { return w; }
static int  UndispWork(int d) { return d; }

// 只作用于我们这一页的视觉基调 —— 析构时自动还原, 任何 return 路径都不会漏。
struct StyleScope
{
    int vars = 0, cols = 0;
    void var(ImGuiStyleVar v, float x)        { ImGui::PushStyleVar(v, x); ++vars; }
    void var(ImGuiStyleVar v, const ImVec2 x) { ImGui::PushStyleVar(v, x); ++vars; }
    void col(ImGuiCol c, ImU32 x)             { ImGui::PushStyleColor(c, ImColor(x).Value); ++cols; }
    ~StyleScope()
    {
        if (vars) ImGui::PopStyleVar(vars);
        if (cols) ImGui::PopStyleColor(cols);
    }
};

static void apply_skin(StyleScope &st, float fs)
{
    st.var(ImGuiStyleVar_FrameRounding,  fs * 0.34f);
    st.var(ImGuiStyleVar_GrabRounding,   fs * 0.34f);
    st.var(ImGuiStyleVar_PopupRounding,  fs * 0.34f);
    st.var(ImGuiStyleVar_ChildRounding,  fs * 0.40f);
    st.var(ImGuiStyleVar_FramePadding,   ImVec2(fs * 0.52f, fs * 0.30f));
    st.var(ImGuiStyleVar_ItemSpacing,    ImVec2(fs * 0.55f, fs * 0.36f));
    st.var(ImGuiStyleVar_ItemInnerSpacing, ImVec2(fs * 0.45f, fs * 0.30f));
    st.var(ImGuiStyleVar_GrabMinSize,    fs * 0.9f);
    st.col(ImGuiCol_FrameBg,          IM_COL32(38, 41, 53, 255));
    st.col(ImGuiCol_FrameBgHovered,   IM_COL32(50, 54, 70, 255));
    st.col(ImGuiCol_FrameBgActive,    IM_COL32(58, 63, 82, 255));
    st.col(ImGuiCol_SliderGrab,       IM_COL32(255, 196, 84, 225));
    st.col(ImGuiCol_SliderGrabActive, IM_COL32(255, 216, 132, 255));
    st.col(ImGuiCol_CheckMark,        IM_COL32(255, 196, 84, 255));
    st.col(ImGuiCol_Button,           IM_COL32(46, 50, 65, 255));
    st.col(ImGuiCol_ButtonHovered,    IM_COL32(64, 70, 90, 255));
    st.col(ImGuiCol_ButtonActive,     IM_COL32(255, 196, 84, 235));
    st.col(ImGuiCol_Header,           IM_COL32(255, 196, 84, 38));
    st.col(ImGuiCol_HeaderHovered,    IM_COL32(255, 196, 84, 68));
    st.col(ImGuiCol_HeaderActive,     IM_COL32(255, 196, 84, 95));
    st.col(ImGuiCol_Separator,        IM_COL32(72, 77, 96, 190));
}

// 顶部卡片: 名字 + 版本 + 一枚状态药丸
static void header_card(const char *title, const char *ver, const char *state, ImU32 col)
{
    const float  fs = ImGui::GetFontSize();
    ImDrawList  *dl = ImGui::GetWindowDrawList();
    const float  w  = ImGui::GetContentRegionAvail().x;
    const float  h  = fs * 2.9f;
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h),
                                IM_COL32(37, 40, 54, 255), IM_COL32(30, 32, 43, 255),
                                IM_COL32(26, 28, 37, 255), IM_COL32(33, 36, 48, 255));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 196, 84, 55), fs * 0.42f, 0, 1.2f);
    dl->AddRectFilled(ImVec2(p.x, p.y + fs * 0.5f), ImVec2(p.x + fs * 0.26f, p.y + h - fs * 0.5f),
                      IM_COL32(255, 196, 84, 255), fs * 0.13f);
    dl->AddText(ImVec2(p.x + fs * 0.85f, p.y + fs * 0.55f), IM_COL32(238, 240, 248, 255), title);
    dl->AddText(ImVec2(p.x + fs * 0.85f, p.y + fs * 1.62f), IM_COL32(140, 146, 166, 255), ver);
    // 右边的状态药丸
    const ImVec2 ts = ImGui::CalcTextSize(state);
    const float  pw = ts.x + fs * 1.1f, ph = fs * 1.5f;
    const ImVec2 pp = ImVec2(p.x + w - pw - fs * 0.6f, p.y + (h - ph) * 0.5f);
    dl->AddRectFilled(pp, ImVec2(pp.x + pw, pp.y + ph), (col & 0x00FFFFFF) | 0x33000000, ph * 0.5f);
    dl->AddCircleFilled(ImVec2(pp.x + fs * 0.55f, pp.y + ph * 0.5f), fs * 0.24f, col);
    dl->AddText(ImVec2(pp.x + fs * 0.95f, pp.y + (ph - ts.y) * 0.5f), col, state);
    ImGui::Dummy(ImVec2(w, h + fs * 0.25f));
}

static void section(const char *title)
{
    const float fs = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0, fs * 0.55f));
    ImDrawList  *dl = ImGui::GetWindowDrawList();
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    // 左边一小根强调色竖条 —— 比纯文字标题好认
    dl->AddRectFilled(ImVec2(p.x, p.y + fs * 0.18f), ImVec2(p.x + fs * 0.2f, p.y + fs * 0.95f),
                      ImColor(COL_GOLD), fs * 0.1f);
    ImGui::Dummy(ImVec2(fs * 0.55f, 0)); ImGui::SameLine();
    ImGui::TextColored(ImColor(COL_GOLD), "%s", title);
    // 标题右边拉一条淡线到头
    ImGui::SameLine();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float  w = ImGui::GetContentRegionAvail().x;
    if (w > fs)
        dl->AddLine(ImVec2(a.x + fs * 0.35f, a.y + fs * 0.58f), ImVec2(a.x + w, a.y + fs * 0.58f),
                    ImColor(78, 82, 100, 150), 1.0f);
    ImGui::NewLine();
    ImGui::Dummy(ImVec2(0, fs * 0.1f));
}

static void label(const char *s)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImColor(COL_SUB), "%s", s);
    ImGui::SameLine(LabelW());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// 两列表格里的一格: 左边小标签, 右边控件占满这一格剩下的宽度。
// 一行放两个旋钮 —— 滑杆不再横跨整屏, 视觉上就不散了。
static void cell2(const char *s)
{
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImColor(COL_SUB), "%s", s);
    ImGui::SameLine(ImGui::GetFontSize() * 3.6f);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

static void info(const char *k, const char *v, ImU32 col = COL_TEXT)
{
    ImGui::TextColored(ImColor(COL_SUB), "%s", k);
    ImGui::SameLine(LabelW());
    ImGui::TextColored(ImColor(col), "%s", v);
}

static void tip(const char *s)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", s);
}

static const char *key_name(int vk)
{
    static char b[16];
    if (vk >= 0x70 && vk <= 0x87) { std::snprintf(b, sizeof(b), "F%d", vk - 0x6F); return b; }
    if (vk >= '0' && vk <= 'Z')   { std::snprintf(b, sizeof(b), "%c", vk); return b; }
    std::snprintf(b, sizeof(b), "%d", vk);
    return b;
}

// ---------- 主界面 ----------
static int g_engine_pending = -1;   // 面板上选了但还没重进游戏的引擎

static void draw_legacy(reshade::api::effect_runtime *runtime,int selected=-1)
{
    const float fs = ImGui::GetFontSize();
    reshade::api::device *dev = runtime->get_device();
    const bool is_d3d12 = dev->get_api() == reshade::api::device_api::d3d12;
    const bool in_host   = hostnr::attached();      // 老游戏那条路: 我们在 Feeder 帮手进程里
    // ★就地插入这条路的活儿也记在 hostnr 里★
    //   交换链一让位, carrier::g.frames_done 就永远是 0 —— 面板要是只看它,
    //   明明在游戏的 DLSS 里跑得好好的, 顶上却写「启动中」、已处理「0 帧」,
    //   用户必然以为没生效(实测古墓丽影暗影就是这样)。
    const bool in_inject = inject::active();
    const bool use_host  = in_host || in_inject || nrbackbuffer::Claimed();   // 这两条路的数字都读 hostnr
    const bool running   = use_host ? hostnr::active()
                                   : (carrier::g.frames_done > 0 && !carrier::g.disabled);
    const bool own_nr    = carrier::cfg.mode >= 3;
    const bool on        = carrier::cfg.enabled != 0 && !carrier::g.disabled;
    // ★谁在做神经渲染★ v4.5 起自带 DLSS 的 D3D12 游戏交给 RenoDX,
    //   那时我们自己引擎的旋钮一个都不起作用 —— 一律置灰, 免得白调。
    // ★★这一局的引擎是不是我们★★ (2026-09-05 业主实测:「面板没有 033 开关」)
    //   老判据 = (enabled != 0) || (inject != 0), 两个都会失效:
     //     · enabled 是 F12 那个总开关 —— 一按关, 开关按钮自己就消失了,
    //       面板上再也开不回来, 只剩 F12。业主就是这么卡住的。
    //     · inject 在 RE 引擎(bindless)上被安装器写成 0 —— 那类游戏走交换链,
    //       所以「inject 代表我们是引擎」这个约定在它们身上正好不成立。
    //   新判据只问一件事: 【这一局 033 是不是引擎】, 跟开关状态无关。
    //     cfg.engine==1 : 面板上选过 033
    //     cfg.inject!=0 : 就地插入路(非 bindless 的老约定, 保留)
     //     主插件不在场 : 安装器给 033 装的游戏根本不铺 renodx, 那只能是我们
    const bool reno_here_boot = (carrier::g_renodx_boot_uplift >= 0);
    //   ★不再看 cfg.engine★ (2026-09-05 回归): 安装器一旦往 cfg 写 engine=,
     //   就会激活 carrier.h 里那段「按引擎选择对齐 NeuralUplift / EnableHooks」——
    //   而自动写 EnableHooks 会让某些 Streamline 游戏启动期挂死。engine 这个键
    //   只许面板在用户真点了引擎单选时写, 所以这里不能拿它当判据。
#ifdef K033_MONOLITHIC_ENGINE
    const bool we_are_engine=true;
#else
    const bool we_are_engine  = (carrier::cfg.inject != 0)
                             || (reno_here_boot && !carrier::g_renodx_present);
#endif
    const bool we_render      = we_are_engine || (carrier::cfg.enabled != 0);

    StyleScope st;            // 只影响我们这一页, 出函数自动还原
    if(selected<0)apply_skin(st, fs);
    if(selected<0) {

    // ── 崩溃自愈: 停在一级时, 这条要摆在最上面 ──────────────
    if (safemode::off())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(COL_GOLD).Value);
#ifdef K033_MONOLITHIC_ENGINE
        ImGui::TextUnformatted("本次启动已暂停 033 神经渲染");
        ImGui::PopStyleColor();
        ImGui::TextColored(ImColor(COL_SUB), "有 %d 次异常退出记录；超分与帧生成设置仍可检查。", safemode::count());
        if (ImGui::Button("下次启动恢复神经渲染")) safemode::retry();
        tip("清除异常退出计数，下次启动游戏时恢复。记录不能确定死机原因；不会自动修改挂载文件。");
#else
        ImGui::TextUnformatted("这一局已自动停用");
        ImGui::PopStyleColor();
        ImGui::TextColored(ImColor(COL_SUB), "连着 %d 次没正常退出, 这一局没做任何渲染。",
            safemode::count());
        if (ImGui::Button("再试一次"))
            safemode::retry();
        tip("确认跟本包无关(游戏本来就会崩)就点它, 下次进游戏恢复。\n也可以删掉游戏目录里的 dlss5-033.state。");
#endif
        ImGui::Separator();
    }

    // ── 顶部卡片 ────────────────────────────────────────────
    {
        char  s[192];
        ImU32 c = IM_COL32(150, 155, 170, 255);
        if (carrier::g.disabled)          { std::snprintf(s, sizeof(s), "已停用"); c = IM_COL32(235, 105, 105, 255); }
        // ★主插件不在场就别说「主插件在做」★ 033 接管的游戏根本不铺 renodx,
        //   这时候两边都没做, 写成「主插件在做」是骗人(业主实测撞上过)。
        else if (!we_render && reno_here_boot && !carrier::g_renodx_present)
                                          { std::snprintf(s, sizeof(s), "都没在做 · 主插件没装"); c = IM_COL32(235, 105, 105, 255); }
        else if (!we_render)              { std::snprintf(s, sizeof(s), "主插件在做"); c = IM_COL32(255, 196, 84, 255); }
        else if (!carrier::cfg.enabled)   { std::snprintf(s, sizeof(s), "已关闭"); }
        else if (!running)                { std::snprintf(s, sizeof(s), "启动中"); c = IM_COL32(255, 196, 84, 255); }
        else
        {
            std::snprintf(s, sizeof(s), "运行中 · %d%%", DispWork(carrier::cfg.work));
            c = IM_COL32(110, 216, 150, 255);
        }
        char ver[96];
        std::snprintf(ver, sizeof(ver), "DLSS5 神经渲染 · %s", K033_VER);
        header_card("热心网友033", ver, s, c);
    }

    // ══════════════════════════════════════════════════════════════
    //  控制条: 总开关 + 引擎 + 状态提示, 一行放完
    //
    //  ★自查 2026-09-05 改掉的四件事★
    //   ① 【bug】总开关以前只在 we_render 时才画 —— 主插件在做的那一半时间里
    //      面板上【根本没有总开关】, 而且留下一句孤零零的「快捷键 F12」。
    //      现在两种情况都有东西: 我们在做就是真开关, 主插件在做就说清楚开关在哪。
    //   ② 【错误文案】引擎那条提示还在讲「几秒钟就换过来, 不用重进」——
    //      那是分帧交接时代的说法, 那套已经删了(见 handover.h)。用户会照着做然后发现没换。
    //   ③ 总开关、引擎、提示原来是三行各自为政, 现在收成一行。
    //   ④ 「设置自动保存」以前一直挂着占位置, 改成只在真存过之后闪一下。
    // ══════════════════════════════════════════════════════════════
    {
        if (we_render)
        {
            if (ImGui::Button(on ? "关闭神经渲染" : "开启神经渲染", ImVec2(fs * 8.6f, fs * 1.7f)))
                carrier::Toggle();
            tip("开关整套神经渲染，默认快捷键 F11。\n设置会自动保存。");
        }
        else
        {
            // 主插件在做: 我们没有安全的开关 —— 它的 NR 归它自己管,
            // 我们去动只会变成抢 feature 18。所以这里只说清楚开关在哪。
            ImGui::BeginDisabled();
            ImGui::Button("主插件在做", ImVec2(fs * 8.6f, fs * 1.7f));
            ImGui::EndDisabled();
            tip("这一局神经渲染由主插件 RenoDX 做, 它的开关在下面「主插件原界面」里那个\n「开启 DLSS5 神经渲染」。我们不去动它 —— 动了就是两个引擎抢同一个资源。");
        }

#ifndef K033_MONOLITHIC_ENGINE
        // 引擎二选一: 只写配置, 重进游戏生效。理由见 handover.h 顶上那段。
        ImGui::SameLine(0.0f, fs * 1.2f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImColor(COL_SUB), "引擎");
        ImGui::SameLine(0.0f, fs * 0.5f);
        {
            // ★★单选永远停在「主插件」★★ (业主 2026-09-05:「千万不能点 033, 默认就是主插件」)
            //   以前这里写的是 eng = we_render ? 1 : 0 —— 也就是「谁在渲染就点亮谁」。
            //   于是万一这一局是 033 在做(主插件没接上、或者旧配置留下的),
            //   面板一进去就把 033 点亮了, 看着像「默认选它」。033 现在是停用状态,
            //   点亮它只会让人以为该用它。
            //   现在: 除非用户这一局自己点过, 否则永远显示主插件。
            //   实际在跑的是谁, 用下面那行提示说清楚, 不靠单选按钮表达。
            int eng = (handover::target() >= 0) ? handover::target() : (we_render ? 1 : 0);
            const int was = eng;
            // ★主插件不在场就不许选它★
            //   2026-09-05 生化危机 4 实测: 它走的是 Feeder 路线, 目录里
            //   压根没有 renodx-dlss5.addon64 —— 而面板照样把「主插件」摆出来。
            //   业主选了它, 结果 engine=0 但主插件不存在 = 两边都不做, 画面没有
            //   任何神经渲染, 还看不出为什么。
            const bool reno_here = carrier::g_renodx_present;
            const bool snap_done = (carrier::g_renodx_boot_uplift >= 0);
            if (snap_done && !reno_here) ImGui::BeginDisabled();
            if (ImGui::RadioButton("主插件##e0", eng == 0)) eng = 0;
            if (snap_done && !reno_here)
            {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("这个游戏没装主插件 —— 它走的是我们自己伪造 DLSS 契约那条路, \n主插件在这种游戏上接不上, 所以只有 033 自研能做。");
            }
            ImGui::SameLine(0.0f, fs * 0.7f);
            // ★033 自研暂时停用★ (业主 2026-09-05 拍板:「033引擎暂时灰色, 所有游戏用不了」)
            //   画质上主插件在自带 DLSS 的游戏里明显更好, 而 Feeder 路线上主插件
            //   一样能干活(它钩的是 NGX 求值, 分不清那次调用是游戏发的还是我们伪造的)。
            //   所以这一版全部交给主插件, 自研引擎留着但不让选 —— 代码一行没删,
            //   想放开只要把下面这两行注释掉。
            ImGui::BeginDisabled();
            if (ImGui::RadioButton("033 自研##e1", eng == 1)) eng = 1;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("这一版暂时不开放 —— 神经渲染统一由主插件做, 画质更好。");
            // ★★只接受「切到主插件」这一个动作★★ (业主 2026-09-05 再次确认:
            //   「033 灰色不能点才对, 就是无法被选中」)
            //   上面 BeginDisabled 已经让它点不动了, 这里再焊一道:
            //   哪怕将来谁把 BeginDisabled 注释掉、或者 ImGui 换版本行为变了,
            //   handover::start(true) 也永远不会被这里调用。
            //   放开 033 的时候, 把下面这行的 && eng == 0 去掉。
            if (eng != was && eng == 0 && !(snap_done && !reno_here)) handover::start(false);
        }
        tip("谁来做神经渲染。\n主插件 RenoDX: 自带 DLSS 的游戏上画质最好, 永远满分辨率跑。\n033 自研: 多一根「精度」杠杆能降开销; 老游戏、32 位、没有 DLSS 的游戏只有它做得了。\n\n★换引擎要退出游戏重进一次★ 神经渲染一个进程只准活一个, 而主插件是在\n游戏第一次调 DLSS 的那一刻抢走它的 —— 那个时刻由游戏定, 它的钩子游戏里\n也拆不掉。只能开局就定好谁做。(试过三轮, 三次掉显卡, 见 handover.h)");

#endif
        // 存过之后闪一下就收, 不常驻
        if (carrier::config_save.has_saved && GetTickCount64() - carrier::config_save.saved < 1600)
        {
            ImGui::SameLine(0.0f, fs * 1.2f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImColor(120, 210, 150, 255), "设置已保存");
        }

        // ★实际在跑的是 033 —— 单选不点亮它, 但得说清楚★
        if (we_render)
        {
            ImGui::TextColored(ImColor(255, 196, 84, 255), "%s",
                "这一局由 033 自研引擎处理");
            tip("033 统一处理渲染与扩展能力，设置均在本页操作。");
        }
        // 上一局没干净退出, 配置刚在启动时补齐 —— 这一局可能两边都没做
        if (carrier::g_hooks_mismatch)
        {
            ImGui::TextColored(ImColor(255, 196, 84, 255), "%s",
                "上一局没正常退出, 引擎配置刚补齐 —— 再重进一次游戏就正常了");
        }
        // 选了但还没重进游戏
        else if (handover::target() >= 0 && (handover::target() == 1) != we_render)
        {
            ImGui::TextColored(ImColor(255, 196, 84, 255), "%s",
                "已记下 —— 退出游戏重进一次就生效");
            tip("神经渲染一个进程只准活一个, 谁先抢到是开局那一刻定的, 中途换不了。");
        }
    }

    } // Legacy header only
    // ══════════════════════════════════════════════════════════════
    //  画质 —— ★直接用主插件本人的界面, 不再自己复刻一套★
    //
    //  业主 2026-09-05 截图打脸(说得对):
    //    我们复刻那一栏显示 强度 2.00 / 风格 电影 / 预设 三,
    //    而主插件自己那套同一时刻是 整体强度 1.00 / 风格 默认 / 预设 默认。
    //    ★两边数值根本对不上★ —— 因为我们那栏读写的是 ReShade 配置键,
    //    那是【下次启动才生效】的东西; 主插件运行时的实际状态在它自己内存里。
    //    也就是说, 我们那一栏是个假面板: 拖了半天, 画面一点没变。
    //
    //  所以整栏删掉, 直接把它本人的界面搬到最上面。控件是实时的, 拖一下画面就变。
    //  (ovgrab 早就把它的 overlay 截下来了, 我们爱画哪儿画哪儿。)
    // ══════════════════════════════════════════════════════════════
    //  画质 —— ★谁在驾驶就画谁的旋钮★
    //
    //  历史: 我们本来复刻了一栏主插件的画质控件, 业主截图打脸 —— 数值跟它
    //  对不上, 因为我们读写的是 ReShade 配置键(下次启动才生效), 而它的实际
    //  状态在自己内存里。那一栏是个假面板, 拖了没用, 所以删了。
    //
    //  ★但删过头了★ (2026-09-05 业主实测:「面板目前都没有 033 引擎的东西」)
    //  033 真的接管的时候(RE 引擎那类), 面板上一个旋钮都没有 —— 连最要紧的
    //  「处理精度」都没处调, 而那正是 033 相对主插件唯一的杀手锏。
    //  现在按驾驶者分流: 033 在开就画我们自己的【真】控件(直接改内存, 实时生效);
    //  主插件在开就把它本人的界面搬过来。两边都不再有假值。
    // ══════════════════════════════════════════════════════════════
    ImGui::Dummy(ImVec2(0, fs * 0.35f));
    if(selected<0) {
    if (we_are_engine)
    {
        section("画质 (033)");

        // ★处理精度★ 033 唯一比主插件多的杠杆 —— 主插件永远满分辨率跑
        static int observedWork=-1,desiredWork=100,observedPasses=-1,desiredPasses=1,observedPassWork=-1,desiredPassWork=100;
        if(observedPassWork!=carrier::cfg.passwork){observedPassWork=carrier::cfg.passwork;desiredPassWork=observedPassWork;}
        if(observedWork!=carrier::cfg.work){observedWork=carrier::cfg.work;desiredWork=observedWork;}
        if(observedPasses!=carrier::cfg.passes){observedPasses=carrier::cfg.passes;desiredPasses=observedPasses;}
        label("处理精度");
        ImGui::SliderInt("##qwork",&desiredWork,25,200,"%d%%");
        tip("调整模型宽高比例，点击应用后生效。默认 70%（省性能）；100% 是与基准同尺寸；200% 是四倍像素。\n拖动时不反复创建模型，不会偷偷降低你选择的分辨率。");
        label("模型遍数");
        const bool supportsPasses=rendercore::Integrated() || inject::s_on || hostnr::attached();
        ImGui::BeginDisabled(!supportsPasses);
        ImGui::SliderInt("##nrpasses",&desiredPasses,1,nrfeatures::MaxPasses,"%d 遍");
        ImGui::EndDisabled();
        tip("每一遍都保留神经模型的明暗、颜色和细节改动，不再仅保留微小纹理。\n已准备过的层数直接切换；首次创建新模型时继续显示当前效果。额外模型有上限地保留在显存中。增加遍数仍增加计算开销。");
        label("后续层精度");
        ImGui::SliderInt("##nrpasswork",&desiredPassWork,50,100,"%d%%");
        tip("只调整第 2 层及以后。默认 70%：后续层按第 1 层的七成尺寸跑，省下的计算量最明显；100% 是满精度，画质上限更高但更吃性能。每层处理当前帧，不随运动速度自动淡出神经效果。\n修改后点击应用。");
        if(ImGui::Button("多层低延迟")) desiredPassWork=75;
        ImGui::SameLine();if(ImGui::Button("后续层全精度")) desiredPassWork=100;
        ImGui::TextDisabled("待应用：%d%% · %d 遍 · 约 %.2f 倍模型像素处理量",desiredWork,desiredPasses,
            float(desiredWork)*desiredWork*(1.f+(desiredPasses-1)*float(desiredPassWork)*desiredPassWork/10000.f)/10000.f);
        ImGui::BeginDisabled(desiredWork==carrier::cfg.work && desiredPasses==carrier::cfg.passes && desiredPassWork==carrier::cfg.passwork);
        if(ImGui::Button("应用模型设置")){carrier::cfg.work=desiredWork;if(supportsPasses){carrier::cfg.passes=desiredPasses;carrier::cfg.passwork=desiredPassWork;}}
        ImGui::EndDisabled();
        {
            const unsigned mw = hostnr::model_w(), mh = hostnr::model_h();
            if (mw > 0)
            {
                ImGui::Dummy(ImVec2(LabelW(), 0)); ImGui::SameLine();
                ImGui::TextColored(ImColor(COL_SUB), "模型 %ux%u", mw, mh);
            }
        }

        const bool hostControls=!rendercore::Detect() || rendercore::Status().owner!=k033core::NativeVulkan;
        ImGui::BeginDisabled(!hostControls);
        if(ImGui::CollapsingHeader("模型原生参数（高级）")) {
        static const char *kPreset[] = { "默认", "一号", "二号", "三号" };
        static const char *kStyle[]  = { "0（默认）", "1", "2（会整片偏色）" };
        int pr = carrier::cfg.preset; if (pr < 0 || pr > 3) pr = 0;
        label("模型预设");
        if (ImGui::Combo("##qpre", &pr, kPreset, 4)) carrier::cfg.preset = pr;
        tip("已核对传参和重建路径；当前合成测试中，1/2/3 的差异未可靠超过模型自身波动。\n不能把参数回读当作生效证明。修改需要准备模型；想调明确的风格，请用下面的前置风格。");
        // 3 号就是 2 号的别名, 列表里不放它(业主: 一样的东西没必要占个位置)。
        // 已经存过 3 的照常能用: 这里显示成 2, 但不主动改写玩家存下的值 ——
        // 只有他自己动了这个控件才会写。
        int st2 = carrier::cfg.style; if (st2 < 0 || st2 > 2) st2 = (st2 == 3) ? 2 : 0;
        label("模型风格");
        if (ImGui::Combo("##qsty", &st2, kStyle, 3)) carrier::cfg.style = st2;
        tip("0/1/2 是三个【不同的模型】(社区逐字节校验过输出不同), 3 是 2 的别名。\n2 号是调色模型: 差异视图上满屏大面积变色, 人脸首当其冲 —— 评论区「人物发黑」多半是它。\n出厂用 0 号(参考实现 OptiScaler 的默认也是 0)。想要风格化请用下面的前置调色。");

        ImGui::Dummy(ImVec2(0, fs * 0.2f));
        label("整体强度");
        ImGui::SliderFloat("##qint", &carrier::cfg.intensity, 0.0f, 2.0f, "%.2f");
        tip("模型原生参数；修改后需准备新模型，不是即时的前置调色。");
        label("局部结构");
        ImGui::SliderFloat("##qls", &carrier::cfg.local_structure, 0.0f, 2.0f, "%.2f");
        label("整体色调");
        ImGui::SliderFloat("##qgt", &carrier::cfg.global_tone, 0.0f, 2.0f, "%.2f");
        tip("模型的原生整体色调。这一项是否真的在模型里生效，我们还没验证过。");
        label("局部色调");
        ImGui::SliderFloat("##qlt", &carrier::cfg.local_tone, 0.0f, 2.0f, "%.2f");
        label("皮肤质感（实验）");
        ImGui::SliderFloat("##qsk", &carrier::cfg.skin_structure, -1.0f, 2.0f,
                           carrier::cfg.skin_structure < 0.0f ? "跟随结构" : "%.2f");
        tip("拉到最左 = 跟随「局部结构」, 不单独控制。往左拉可以压住法令纹这类皮肤褶皱。\n这一项目前还不会被记住, 退出游戏回默认。");
        ImGui::TextDisabled("皮肤保护（实验）：已开启，叠层越多越强，不用手动调");
        tip("【本版新增，实验中】按肤色逐像素判定（不是人脸识别，所以手、胳膊一样管，\n转视角再快也不会失效）。它压住皮肤上的锐化，并且让模型对皮肤只改明暗、不改颜色 ——\n就是评论区说的「叠几层之后人物发黑、法令纹变重」那两件事。");

        bool autoMask = carrier::cfg.auto_mask != 0;
        bool uiCorrection = carrier::cfg.ui_correct != 0;
        label("模型保护");
        if (ImGui::Checkbox("自动遮罩##qmask", &autoMask)) carrier::cfg.auto_mask = autoMask ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("界面校正##qui", &uiCorrection)) carrier::cfg.ui_correct = uiCorrection ? 1 : 0;

        ImGui::Dummy(ImVec2(0, fs * 0.2f));
        label("清理");
        if (ImGui::Button("清一次历史帧##qresethist")) hostnr::RequestHistoryReset();
        tip("把模型攒下来的【时间历史】丢掉, 下一帧从头起步。\n糊影、残影、拖尾、切过画质之后画面不对, 按一下就清干净, 不用重进游戏。\n走的是游戏切换颜色格式时本来就在走的那条路, 不重建模型、不释放显存, 所以按了不会卡。");

        }
        ImGui::Dummy(ImVec2(0, fs * 0.2f));
        ImGui::EndDisabled();
        ImGui::TextDisabled("033 前置调教 → 神经渲染 → 颜色回桥输出");
        if(rendercore::Detect()) {
            const auto core=rendercore::Status();
            ImGui::TextWrapped("033 整合渲染核心：已接入；NR 处理 %llu 帧，等待或跳过 %llu 帧",(unsigned long long)core.processed,(unsigned long long)core.withheld);
            ImGui::TextDisabled("全部扩展功能位于本页下方的完整兼容设置。");
        }

        if(!hostControls)ImGui::TextDisabled("原生 Vulkan 的 NR 参数在核心高级设置；033 前置调色尚未接入该路径。");
        ImGui::BeginDisabled(!hostControls);
        bool grade=carrier::cfg.pre.enabled!=0;
        if(ImGui::Checkbox("前置画面调教##pregrade",&grade))carrier::cfg.pre.enabled=grade?1:0;
        tip("先调整模型输入，再做神经渲染。输出不再叠加肤色、锐化或运动淡出处理。\n关闭即回到原来的颜色流程，六倍帧生成保持原值。");
        if(grade) {
            static const char* inputStyles[]={"中性（手动调色）","自然写实调色","柔和电影调色","动漫色阶"};
            int inputStyle=int(carrier::cfg.pre.style);
            label("前置风格");if(ImGui::Combo("##pgstyle",&inputStyle,inputStyles,4))carrier::cfg.pre.style=uint32_t(inputStyle);
            tip("在送入神经模型之前改变颜色和明暗。动漫色阶是轻量着色器，不是 AnimeGAN，不改人物五官。\n切换不重建神经模型；模型最终会如何解释该风格，需要游戏里比较。");
            ImGui::BeginDisabled(inputStyle==0);
            label("风格强度");ImGui::SliderFloat("##pgstylestrength",&carrier::cfg.pre.styleStrength,0.f,1.f,"%.2f");
            ImGui::EndDisabled();
            if(ImGui::Button("中性保真##pgneutral")){carrier::cfg.pre={};carrier::cfg.pre.enabled=1;}
            ImGui::SameLine();
            if(ImGui::Button("轻度自然##pgnatural")){carrier::cfg.pre={};carrier::cfg.pre.enabled=1;carrier::cfg.pre.contrast=1.03f;carrier::cfg.pre.saturation=1.02f;carrier::cfg.pre.highlights=0.04f;}
            tip("温和的起点；最终风格仍需同镜头比较。");
            label("曝光");ImGui::SliderFloat("##pgexp",&carrier::cfg.pre.exposure,-2.f,2.f,"%.2f EV");
            label("明暗层次");ImGui::SliderFloat("##pgcontrast",&carrier::cfg.pre.contrast,0.75f,1.25f,"%.2f");
            label("饱和度");ImGui::SliderFloat("##pgsat",&carrier::cfg.pre.saturation,0.f,1.5f,"%.2f");
            label("冷暖");ImGui::SliderFloat("##pgwarm",&carrier::cfg.pre.warmth,-0.25f,0.25f,"%.2f");
            label("绿紫偏色");ImGui::SliderFloat("##pgtint",&carrier::cfg.pre.tint,-0.25f,0.25f,"%.2f");
            label("高光收敛");ImGui::SliderFloat("##pghigh",&carrier::cfg.pre.highlights,0.f,0.25f,"%.2f");
        }
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, fs * 0.2f));
        ImGui::TextColored(ImColor(COL_SUB), "%s",
            "颜色回桥和分屏对照位于下面的排障信息；常规画面调教在模型之前完成。");
    }
    else if (ovgrab::any())
    {
        section("画质");
        ovgrab::draw(runtime);
    }
    else
    {
        section("画质");
        ImGui::TextColored(ImColor(255, 196, 84, 255), "%s", "没找到主插件的界面");
        tip("这一局多半没装它, 或者它还没加载完。\n重进一次游戏; 还没有就双击游戏目录里的「检查有没有生效.cmd」。");
    }
    } // Legacy quality layout only
    // ── 多帧生成 (折叠) ──────────────────────────────────────
    //   并进来的 MFG Unlock(MIT · ImDreamt/mavismmg · 技术路线源自 dashdogy)。
    //   ★为什么单独一栏而不是并排放第二个 addon★: 作者自己的兼容表写着,
    //   MFG Unlock 和 RenoDX DLSS5 两个 addon 同时加载时, STALKER2/2077 菜单
    //   会卡成个位数帧。我们本身就是神经渲染 addon, 撞的是同一类。
    //
    //   ★40 系专属, 这是硬的★ 50 系原厂就有; 20/30 系解了闸也没用 ——
    //   驱动自带的 nvngx_dlssg.dll 里只有 sm_89(Ada) 的机器码, 没有 sm_75/86,
    //   PTX 又只能向前兼容, 模块根本加载不起来。(我们自己解 fatbin 数过。)
    if(selected<0 || selected==2)
    {
        ImGui::Dummy(ImVec2(0, fs * 0.4f));
        if (ImGui::CollapsingHeader("多帧生成 (三代三条路)"))
        {
            // ★先把三代的待遇摆出来★ (业主 2026-09-05:「我没看到你 20-30 多帧生成的页面」)
            //   以前只画「当前这张卡」那一支, 别的代看不见, 就以为没做。
            //   现在三行都列出来, 自己这代加个▶, 一眼知道整包覆盖到哪儿。
            {
                struct { int gen; const char *how; } kWay[] = {
                    { 20, "20/30 系  转接到 FSR3(外挂件, 最高 6x)" },
                    { 40, "40 系     插件内置解锁(3x/4x/6x)" },
                    { 50, "50 系     原厂就有 + 这里还能强制倍率/修时间点" },
                };
                for (const auto &w : kWay)
                {
                    const bool mine = (w.gen == 20 && (fg::in.nvgen == 20 || fg::in.nvgen == 30)) ||
                                      (w.gen == 40 && fg::in.nvgen == 40) ||
                                      (w.gen == 50 && fg::in.nvgen == 50);
                    ImGui::TextColored(ImColor(mine ? COL_GREEN : COL_GRAY), "%s %s",
                                       mine ? "▶" : "  ", w.how);
                }
                ImGui::TextColored(ImColor(COL_SUB), "%s",
                    "只对【本来就有 DLSS 帧生成】的游戏有效。");
                ImGui::Separator();
            }
            // ★40 和 50 系都给控件★ (业主 2026-09-05:「50 系也应该可以强制开启」)
            //   以前只认 fg::in.ada(40系), 于是 50 系掉进「只有说明没有控件」那一支。
            //   ★解闸和强制倍率是两回事★: 50 系的架构闸本来就是开的(不用改),
             //   但「强制倍率」和「时间点修正」对它一样有用 ——
            //   很多游戏自己的菜单只给到 2x/3x, 强制倍率能顶到它运行库真正的上限。
            const bool mfg_usable = (fg::in.nvgen == 40 || fg::in.nvgen == 50);
            if (!mfg_usable)
            {
                if (fg::in.nvgen == 20 || fg::in.nvgen == 30)
                {
                    // ★20/30 系: 真 MFG 永远不可能, 走「转接到 FSR3/XeSS」那条★
                    //   驱动的 nvngx_dlssg.dll 里只有 sm_89(Ada) 的机器码, 没有 sm_75/sm_86;
                    //   PTX 只向前兼容, 模块根本加载不起来。(我们自己解 fatbin 数过。)
                    //   所以这两代用的是 nvidia_mfg_bridge —— 它把游戏的 DLSS 帧生成调用
                    //   转接到 FSR3.1(或可选的 Intel XeSS)上, 最高 6x。安装器会按
                    //   「显卡是 20/30 系 + 游戏本来就有 DLSS 帧生成」两个条件自动装。
                    const std::string bri = game_dir() + "\\nvidia_mfg_bridge.dll";
                    const bool has_bridge = GetFileAttributesA(bri.c_str()) != INVALID_FILE_ATTRIBUTES;
                    if (!has_bridge)
                    {
                        ImGui::TextColored(ImColor(COL_SUB), "%s",
                            "这个游戏没有 DLSS 帧生成, 没东西可转接。");
                    }
                    else
                    {
                        ImGui::TextColored(ImColor(COL_GREEN), "%s",
                            "已装上 —— 转接到 FSR3, 最高 6x");
                        static const char *kMul[] = { "跟游戏走", "3x", "4x", "5x", "6x" };
                        static int  mul  = -1;
                        static bool dirty = false;
                        const std::string ini = game_dir() + "\\dlssg_to_fsr3.ini";
                        if (mul < 0)
                        {
                            mul = 0;
                            FILE *f = nullptr;
                            if (fopen_s(&f, ini.c_str(), "rb") == 0 && f != nullptr)
                            {
                                char line[256];
                                while (std::fgets(line, sizeof(line), f))
                                    if (std::strncmp(line, "ForceFrameGenOverride=", 22) == 0)
                                    { mul = std::atoi(line + 22); break; }
                                std::fclose(f);
                            }
                            if (mul < 0 || mul > 4) mul = 0;
                        }
                        label("倍率");
                        if (ImGui::Combo("##mul2030", &mul, kMul, 5))
                        {
                            // 写回它的 ini —— 只换 ForceFrameGenOverride 那一行, 别的原样留着
                            std::string keep;
                            FILE *f = nullptr;
                            if (fopen_s(&f, ini.c_str(), "rb") == 0 && f != nullptr)
                            {
                                char line[512];
                                while (std::fgets(line, sizeof(line), f))
                                    if (std::strncmp(line, "ForceFrameGenOverride=", 22) != 0) keep += line;
                                std::fclose(f);
                            }
                            if (fopen_s(&f, ini.c_str(), "wb") == 0 && f != nullptr)
                            {
                                std::fputs(keep.c_str(), f);
                                std::fprintf(f, "ForceFrameGenOverride=%d\n", mul);
                                std::fclose(f);
                                dirty = true;
                                Log("[mfg20-30] 倍率写入 ForceFrameGenOverride=%d (重进游戏生效)", mul);
                            }
                        }
                        tip("强制插帧倍率。「跟游戏走」= 用游戏自己设置里那一档。\n★改完要重进游戏★ 它是启动时读配置的。");
                        if (dirty)
                        {
                            ImGui::Dummy(ImVec2(LabelW(), 0)); ImGui::SameLine();
                            ImGui::TextColored(ImColor(255, 196, 84, 255), "%s",
                                "已记下 —— 退出游戏重进一次生效");
                        }
                        ImGui::Dummy(ImVec2(0, fs * 0.2f));
                        ImGui::TextColored(ImColor(COL_SUB), "%s",
                            "只用在单机游戏上; 带反作弊的网游别开。");
                    }
                }
                else
                {
                    ImGui::TextColored(ImColor(COL_SUB), "%s",
                        "认不出显卡代数 —— 多帧生成只对 40 系解锁, 50 系原厂就有。");
                }
            }
            else
            {
                ImGui::TextColored(ImColor(COL_SUB), "%s", fg::in.nvgen == 50
                    ? "50 系: 闸本来就开着, 这里管【强制倍率】和【时间点修正】"
                    : "把 NVIDIA 只给 50 系的 3x/4x/6x 放开给 40 系");
                tip("游戏菜单常常只给到 2x/3x, 强制倍率能顶到运行库真正的上限。\n改动全在内存里, 退出游戏即还原, 不碰任何文件。");
                mfg::draw(runtime);
                // ══════════════════════════════════════════════════
                //  ★出没出插帧 —— 用运行库自己报的数, 别自己发明指标★
                //
                //  ★上一版这里是错的(2026-09-05 当场翻车)★
                //    我拿「神经渲染帧/秒 ÷ 画面帧/秒」当倍率, 可这两个数都是
                //    【ReShade 每次 present 加一】—— 同一个东西除自己, 恒等于 1。
                //    于是面板报「倍率 1.01x 没看到插帧」, 而实际上帧生成好好地在出帧。
                //    自己发明的指标, 比没有还坏。
                //
                //  正确的数早就在手里: slDLSSGGetState 的 numFramesActuallyPresented,
                //  那是帧生成运行库自己报的「这一个真帧我总共递出去几帧」, 我们
                //  钩着它, 游戏每帧都在调(鬼武者实测采样 3479 次)。
                {
                    const unsigned gen = mfg::frames_presented();
                    const unsigned long long smp = mfg::state_samples();
                    static double s_t0 = 0.0;
                    static unsigned long long s_nr0 = 0;
                    static double s_fps_disp = 0.0, s_fps_nr = 0.0;
                    const double now = ImGui::GetTime();
                    const unsigned long long nr = static_cast<unsigned long long>(carrier::g.frames_done);
                    if (s_t0 <= 0.0) { s_t0 = now; s_nr0 = nr; }
                    else if (now - s_t0 >= 1.0)
                    {
                        const double dt = now - s_t0;
                        s_fps_nr   = static_cast<double>(nr - s_nr0) / dt;
                        s_fps_disp = ImGui::GetIO().Framerate;
                        s_t0 = now; s_nr0 = nr;
                    }
                    ImGui::Dummy(ImVec2(0, fs * 0.25f));
                    ImGui::Separator();
                    ImGui::Dummy(ImVec2(0, fs * 0.2f));
                    if (smp > 0 && gen >= 1)
                    {
                        ImGui::TextColored(ImColor(gen > 1 ? COL_GREEN : ImU32(COL_SUB)),
                                           "两次状态查询之间呈现 %u 帧   (采样 %llu 次，不代表倍率)",
                                           gen, smp);
                    }
                    else
                    {
                        ImGui::TextColored(ImColor(COL_SUB), "%s",
                                           "帧生成: 运行库还没报过数 —— 游戏里没开, 或者它还没开始查");
                    }
                    if (s_fps_disp > 0.0 && s_fps_nr > 0.5)
                    {
                        ImGui::TextColored(ImColor(COL_SUB), "界面回调 %.0f 次/秒   交换链 NR %.0f 次/秒",
                                           s_fps_disp, s_fps_nr);
                        ImGui::TextDisabled("回调频率不是屏幕帧率，也不能判断生成帧是否被重复处理。");
                    }
                }
            }

            // ── 第四条路: 游戏本身没有帧生成 ────────────────────────
            //   上面三条都是「接管游戏自己的 DLSS 帧生成调用」—— 游戏不调就没得接。
            //   Smooth Motion 是【驱动自己】在两张已渲染帧之间插一张, 游戏什么都
            //   不用支持, 老游戏也能开。只有 40/50 系有。
            ImGui::Dummy(ImVec2(0, fs * 0.3f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, fs * 0.2f));
            ImGui::TextColored(ImColor(COL_TEXT), "%s", "游戏本身就没有帧生成?");
            if (fg::in.nvgen == 40 || fg::in.nvgen == 50)
            {
                if (smooth::available())
                {
                    if (ImGui::Button("给这个游戏打开 Smooth Motion", ImVec2(fs * 16.0f, fs * 1.7f)))
                        smooth::apply(true);
                    ImGui::SameLine();
                    if (ImGui::Button("关掉", ImVec2(fs * 4.5f, fs * 1.7f)))
                        smooth::apply(false);
                    tip("Smooth Motion: 驱动在两张已渲染的帧之间插一张, 游戏什么都不用支持。\n写的是驱动里这个 exe 的配置, 跟 NVIDIA App 里那个开关是同一个。\n★写完要退出游戏重进一次★ 驱动是在进程启动时读它的。");
                    if (smooth::last() >= -1)
                    {
                        const bool okk = (smooth::last() == 0);
                        ImGui::TextColored(ImColor(okk ? COL_GREEN : (smooth::last() == -1 ? COL_SUB : ImU32(IM_COL32(235,105,105,255)))),
                                           "%s", smooth::note());
                    }
                }
                else
                {
                    ImGui::TextColored(ImColor(COL_SUB), "%s", smooth::note());
                    ImGui::TextColored(ImColor(COL_SUB), "%s",
                        "也可以手动开: NVIDIA App -> 图形 -> 选这个游戏 -> Smooth Motion。");
                }
            }
            else
            {
                ImGui::TextColored(ImColor(COL_SUB), "%s",
                    "驱动级插帧只有 40/50 系有 —— 这张卡用不了");
                tip("20/30 系想给没有帧生成的游戏加帧, 只能用 Lossless Scaling 那类外部软件。");
            }
        }
    }

    // ── 排障信息: 默认收起 ────────────────────────────────────
    //   业主原话「这些不需要」。确实, 正常玩的时候一行都不用看。
    //   但不能删 —— 评论区问问题时全靠它, 所以收起来, 出事再点开。
    ImGui::Dummy(ImVec2(0, fs * 0.4f));
    if ((selected<0 || selected==4) && ImGui::CollapsingHeader("运行状态与诊断"))
    {
    section("状态");
    // ── 先把所有状态收进一张表, 再两列铺开 ────────────────────
    //   十几行事实一行一条要滚屏; 两列一压就是六七行, 一眼扫完。
    //   只有「GPU 侧」那条太长, 单独整行放在表下面。
    struct SRow { std::string k, v; ImU32 c; };
    std::vector<SRow> srows;
    auto srow = [&srows](const char *k, const char *v, ImU32 c)
    { srows.push_back(SRow{ std::string(k), std::string(v ? v : ""), c }); };

    srow("图形接口", is_d3d12 ? "Direct3D 12" : "非 D3D12", is_d3d12 ? COL_GREEN : COL_RED);
    srow("调用方式", own_nr ? "自有转发" : "外部插件", own_nr ? COL_GREEN : COL_GOLD);
    srow("数据接入",nrgame033::DiscoveryNote(),COL_SUB);
    // 就地插入的状态: 让人一眼看出神经渲染跑在哪一层
    srow("插入位置", (inject::active() || carrier::cfg.inject) ? inject::note() : "交换链(已关插入)",
         inject::active() ? COL_GREEN : COL_SUB);
    if (in_host)        srow("老游戏路线", hostnr::note(), hostnr::active() ? COL_GREEN : COL_GOLD);
    else if (in_inject) srow("干活的那条路", hostnr::note(), hostnr::active() ? COL_GREEN : COL_GOLD);
    // 状态信封(bindless 引擎保护)只在就地插入/帮手路上工作, 让人看得见它有没有在还
    if (in_host || in_inject)
        srow("状态还回", staterestore::note(), staterestore::active() ? COL_GREEN : COL_SUB);
    if (in_inject && !carrier::PassthroughFor(hostnr::s_fmt))
        srow("HDR 白点", hostnr::exposure_note(), hostnr::s_exposure.valid ? COL_GREEN : COL_SUB);
    // 帧生成开着时, 交换链那条路会主动让开 —— 必须说清楚, 否则用户只看到「没效果」
    const bool fg_yield = (carrier::cfg.fgsafe && carrier::FrameGenRunning() && !in_inject);
    if (fg_yield) srow("帧生成", "交换链已让开", COL_GOLD);
    srow("转发组件", nrfwd::note(), nrfwd::loaded() ? COL_GREEN : COL_RED);
    {
        char b[128];
        if (use_host)
        {
            srow("深度与运动", hostnr::guides(), hostnr::guides_ok() ? COL_GREEN : COL_GOLD);
            srow("相机时序",nrgame033::JitterNote(),COL_SUB);
            if(nrinput033::ownership.Get()==nrinput033::Route::Presentation)
                srow("曝光输入","当前为成品画面 · 不重复应用游戏曝光",COL_SUB);
            else
                srow("曝光输入",nrgame033::ngxExposure.load()?"接口已提供 · 使用情况见 HDR 白点":"接口未提供纹理 · 使用情况见 HDR 白点",COL_SUB);
        }
        else
        {
            if (carrier::gd.live) std::snprintf(b, sizeof(b), "实时 %u × %u", carrier::gd.w, carrier::gd.h);
            else                  std::snprintf(b, sizeof(b), "占位 · %s", carrier::gd.note.c_str());
            srow("深度与运动", b, carrier::gd.live ? COL_GREEN : COL_GOLD);
        }

        if (use_host) std::snprintf(b, sizeof(b), "%llu 帧", hostnr::frames());
        else          std::snprintf(b, sizeof(b), "%llu 帧 · %.2f ms",
                                    static_cast<unsigned long long>(carrier::g.frames_done), carrier::g.last_ms);
        srow("已处理", b, (use_host ? hostnr::frames() > 0 : carrier::g.frames_done > 0) ? COL_GREEN : COL_SUB);

        srow("求值", carrier::g.last_eval == NVSDK_NGX_Result_Success ? "正常" : "异常",
             carrier::g.last_eval == NVSDK_NGX_Result_Success ? COL_GREEN : COL_RED);
    }
    srow("模型文件", g_rt.present ? "已加载" : "缺失", g_rt.present ? COL_GREEN : COL_RED);

    if (ImGui::BeginTable("##st2", 2, ImGuiTableFlags_SizingStretchSame, ImVec2(0, 0)))
    {
        for (size_t i = 0; i < srows.size(); ++i)
        {
            ImGui::TableNextColumn();
            ImGui::TextColored(ImColor(COL_SUB), "%s", srows[i].k.c_str());
            ImGui::SameLine(fs * 5.0f);
            ImGui::TextColored(ImColor(srows[i].c), "%s", srows[i].v.c_str());
        }
        ImGui::EndTable();
    }

    // ★GPU 侧分段★ 上面那个 ms 是 CPU 侧的, 这里才是显卡真花的时间。
    //   五段各多少, 一眼看出贵在哪 —— 这是回答「为什么别人帧数高」的唯一硬数据。
    //   这一条太长, 塞不进半列, 单独整行。
    if (gputime::ready() && gputime::fresh() && gputime::total() > 0.0)
    {
        char gb[192];
        if (use_host)   // 就地插入 / 帮手: 五段
            std::snprintf(gb, sizeof(gb), "拷入 %.2f · 编码 %.2f · 模型 %.2f · 合成 %.2f · 拷回 %.2f  = %.2f ms",
                          gputime::seg(0), gputime::seg(1), gputime::seg(2), gputime::seg(3), gputime::seg(4),
                          gputime::total());
        else            // 交换链: 三段(拷入拷回在 ReShade 的列表上, 量不到)
            std::snprintf(gb, sizeof(gb), "编码 %.2f · 模型 %.2f · 合成 %.2f  = %.2f ms  (不含拷入/拷回)",
                          gputime::seg(0), gputime::seg(1), gputime::seg(2), gputime::total());
        info("GPU 已完成", gb, COL_SUB);
    }
    else if (gputime::ready()) info("GPU 计时", "等待提交队列的完成信号；不显示估算值", COL_SUB);

    if (fg_yield)
    {
        ImGui::Dummy(ImVec2(LabelW(), 0)); ImGui::SameLine();
        ImGui::TextColored(ImColor(COL_SUB),
            "帧生成会接管交换链, 我们再往后缓冲写就是抢同一块表面(实测闪退)。想两个都要: 游戏里关掉 DLSS 帧生成。");
    }

    // ── 自检: 一句话说清楚现在到底是什么情况 ──────────────
    //   上面那八行是【事实】, 但玩家看不懂事实, 他只想知道「到底好没好、
    //   下一步该干嘛」。评论区一天几十条「按 Home 没反应」「没效果」,
    //   背后至少三种完全不同的情况, 在他眼里长得一模一样。这里给结论。
    {
        const char NL = 10;
        // 这一局到底出没出过帧 —— 判断「在不在工作」的唯一硬事实
        const bool frames_done_any =
            (use_host ? (hostnr::frames() > 0) : (carrier::g.frames_done > 0)) ||
            inject::active();
        const char *verdict = nullptr;
        const char *advice  = nullptr;
        ImU32 vcol = COL_GOLD;

        if (safemode::off())
        {
            verdict = "本包这一局已自动停用(这个游戏崩过好几次)";
            advice  = "确认跟本包无关的话, 点上面那个「再试一次」。";
        }
        // ★★「不是 D3D12」必须排在最前面★★ (2026-09-05 评论区实测)
        //   这是个【死条件】: 神经渲染的运行库只有 D3D12 接口, 游戏不跑 D3D12 就
        //   一点办法都没有 —— 勾开关、查杀毒、重装, 全是白费。
        //   ★之前它排在「总开关是关的」后面★, 于是 D3D11 游戏 + 开关没勾的人
        //   看到的建议是「勾上第一行开启」—— 他勾了, 还是不动, 然后来评论区问。
        //   一句错的建议比不给建议更浪费人时间。
        //   (原来那条「必须排在转发组件前」的理由照旧成立, 见下面转发组件那一支。)
        else if (!is_d3d12 && !use_host)
        {
            verdict = "这个游戏跑在 D3D11 上, 这条路只支持 D3D12";
                        advice  = "游戏画面设置里有 DX12 选项就切过去重启; 没有的话双击安装包的「DLSS5一键包.cmd」, 选【强制完整模式】重装(走 DX11 桥)。";
        }
        else if (carrier::cfg.enabled == 0)
        {
            verdict = "总开关是关的";
            advice  = "勾上本页第一行「开启 DLSS5 神经渲染」，或使用效果快捷键。";
        }
        else if (!g_rt.present)
        {
            verdict = "神经渲染模型文件缺失";  vcol = COL_RED;
            advice  = "多半被杀毒删了。把游戏目录加进杀毒白名单, 重装一次本包。";
        }
        // ★「已挂上、在等游戏调 DLSS」必须排在「转发组件」前面★ —— 转发组件是等游戏
        //   真的调 DLSS 那一刻才初始化的; 游戏没调, 它当然显示「未初始化」。先查它就会
        //   把人带去查杀毒、重装 —— 生化 4 实测就报错了这一条, 而真相只是游戏里没开 DLSS。
        // ★干过活就绝不许说「一次都没调用」★
        //   2026-09-05 评论区实测(鸣潮): 报告一边写「一次都没调用」, 一边写
        //   「已处理 1452 帧」「深度与运动: 实时(来自游戏)」—— 自相矛盾, 而且
        //   把一个【正在正常工作】的安装说成了坏的。根因见 nrscale.h 里那段。
        //   这里加一道: 只要出过帧, 就按正常算, 不管那行状态文字写的是什么。
        else if ((in_inject || in_host) && !running && frames_done_any)
        {
            verdict = "在工作 —— 只是刚才那一次求值不是 DLSS(多半是帧生成), 不影响";
            vcol = COL_GREEN;
            advice = "这是正常的: 开着帧生成的游戏每帧有两次 NGX 求值, 只有其中一次是 DLSS。";
        }
        else if ((in_inject || in_host) && !running)
        {
            verdict = "已经挂到游戏的 DLSS 上了, 但游戏一次都没调用它";
            advice  = "游戏画面设置里把 DLSS 打开(DLAA 或任意超分档都行, 别用 FSR/XeSS),"
                      "\n     还要进到实际游戏画面 —— 停在标题画面/菜单时很多游戏根本不跑 DLSS。";
        }
        else if (!nrfwd::loaded() && !use_host)
        {
            verdict = "转发组件没加载起来";  vcol = COL_RED;
            advice  = "同上: 先看杀毒有没有删文件, 再重装一次。";
        }
        else if (carrier::g.disabled)
        {
            verdict = "引擎自己停了";  vcol = COL_RED;
            advice  = "看下面的原因; 点「重新启用」可以再试一次。";
        }
        else if (running)
        {
            verdict = "一切正常 —— 神经渲染真的在跑";  vcol = COL_GREEN;
            advice  = "看不出区别的话: 把上面的【混合】拉到 0 再拉回 100, 对比一下。";
        }
        else if (carrier::cfg.fgsafe && carrier::FrameGenRunning())
        {
            verdict = "交换链让开了(游戏开着 DLSS 帧生成)";
            advice  = "想两个都要: 游戏里关掉 DLSS 帧生成。";
        }
        else
        {
            verdict = "还没开始工作";
            advice  = "先确认游戏里 DLSS 是开着的, 然后动一下画面等几秒再看这里。";
        }

        ImGui::Dummy(ImVec2(0, fs * 0.4f));
        section("自检");
        // ★DX11 桥装错地方了★ (2026-09-05)
        //   桥是给【D3D11 自带 DLSS】的游戏用的 —— 它自己开一个私有 D3D12 会话做
        //   神经渲染。要是这游戏本来就是 D3D12, 主插件已经在做了, 桥再来一份就是
         //   两个抢同一个 feature 18 = 掉显卡。安装器的判据已经很严, 但玩家会手动拷,
        //   所以这里再兜一道: 认出来就点名, 让他删掉那一个文件。
        if (is_d3d12)
        {
            const std::string bp = game_dir() + "\\dlss5-bridge.addon64";
            if (GetFileAttributesA(bp.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                ImGui::TextColored(ImColor(COL_RED), "%s",
                    "目录里有 dlss5-bridge.addon64, 但这游戏是 D3D12 —— 请删掉它");
                tip("桥只给 D3D11 游戏用。D3D12 游戏主插件自己就能做,\n再多一个桥就是两份神经渲染抢同一个资源, 会掉显卡。\n删掉游戏目录里的 dlss5-bridge.addon64 就行。");
            }
        }
        info("结论", verdict, vcol);
        ImGui::Dummy(ImVec2(LabelW(), 0)); ImGui::SameLine();
        ImGui::TextColored(ImColor(COL_SUB), "%s", advice);
        // 「挂上了但没出帧」这一条要多说一句 —— 光线重建是最常见的原因
        if ((in_inject || in_host) && !running)
        {
            ImGui::Dummy(ImVec2(LabelW(), 0)); ImGui::SameLine();
            ImGui::TextColored(ImColor(COL_SUB),
                "【光线重建 / Ray Reconstruction】关掉 —— 它跟神经渲染抢同一条管线。");
        }

        ImGui::Dummy(ImVec2(LabelW(), 0)); ImGui::SameLine();
        if (ImGui::Button("复制诊断信息"))
        {
            // 复制成一段纯文字, 让他直接贴到评论区 —— 比只说一句「没反应」有用得多
            char nbuf[256];
            std::string t;
            t += "DLSS5 一键包 "; t += K033_VER; t += NL;
            t += "结论: "; t += verdict; t += NL;
            t += "图形接口: "; t += (is_d3d12 ? "D3D12" : "非D3D12"); t += NL;
            t += "插入位置: "; t += inject::note(); t += NL;
            t += "转发组件: "; t += nrfwd::note(); t += NL;
            std::snprintf(nbuf, sizeof(nbuf), "已处理: %llu 帧 · %.2f ms",
                          static_cast<unsigned long long>(use_host ? hostnr::frames() : carrier::g.frames_done),
                          carrier::g.last_ms);
            t += nbuf; t += NL;
            std::snprintf(nbuf, sizeof(nbuf), "深度与运动: %s",
                          use_host ? hostnr::guides() : (carrier::gd.live ? "实时" : "占位"));
            t += nbuf; t += NL;
            if (in_inject) { t += "HDR白点: "; t += hostnr::exposure_note(); t += NL; }
            if (carrier::g.disabled) { t += "停用原因: "; t += carrier::g.disabled_why; t += NL; }
            std::snprintf(nbuf, sizeof(nbuf), "异常退出计数: %d", safemode::count());
            t += nbuf; t += NL;
            ImGui::SetClipboardText(t.c_str());
        }
        ImGui::SameLine();
        ImGui::TextColored(ImColor(COL_SUB), "(粘到评论区问人用)");
    }
    if (carrier::g.disabled)
    {
        ImGui::Dummy(ImVec2(0, fs * 0.3f));
        if (ImGui::Button("重新启用"))
        {
            carrier::g.disabled = false;
            carrier::g.consecutive_fails = 0;
            carrier::g.disabled_why.clear();
            Log("[carrier] 从面板重新启用");
        }
    }
    }   // 排障信息 折叠块

    // ★033 独有的东西一律沉到最下面★ (业主 2026-09-05:「033 的都移到最下面」)
    //   默认全走主插件, 033 只是留一条后路 —— 它的东西不该挤在画质和多帧生成中间。
    // ── 高级: 只有 033 引擎用得上 ────────────────────────────
    if (we_render && (selected<0 || selected==4))
    {
        ImGui::Dummy(ImVec2(0, fs * 0.4f));
        if (ImGui::CollapsingHeader("颜色回桥与画面对照"))
        {
            bool hold = carrier::cfg.holdframe != 0;
            bool skip = carrier::cfg.applymodel == 0;
            bool rp   = carrier::cfg.replica   != 0;
            // 三个开关先摆成一行。「完全复刻」必须在下面两个下拉【之前】读到,
            // 才能拿它去置灰那两项 —— 复刻模式下曲线和合成都是钉死的。
            ImGui::Dummy(ImVec2(fs * 3.6f, 0)); ImGui::SameLine();
            if (ImGui::Checkbox("稳定合成##arep", &rp)) carrier::cfg.replica = rp ? 1 : 0;
            tip("固定白点 3.16、Neutwo 曲线，限制高光编辑放大，不增加历史帧混合。\n高光护栏在此路径生效；旧混合和上色旋钮在整合路径不使用。游戏画质仍需实测。");
            ImGui::SameLine();
            ImGui::BeginDisabled(hostnr::feature()==nullptr);
            if (ImGui::Checkbox("冻结输入对照##ahold", &hold)) carrier::cfg.holdframe = hold ? 1 : 0;
            ImGui::EndDisabled();
            tip("就地渲染同时保留颜色、深度、运动矢量、有效白点及输入契约。\n以固定输入重新评估调教效果；测试运动画质和延迟时请关闭。");
            ImGui::SameLine();
            if (ImGui::Checkbox("跳过模型##askip", &skip)) carrier::cfg.applymodel = skip ? 0 : 1;
            tip("勾上 = 一切照跑但不把模型结果放回去。这是对照基准: 跟它比才知道模型做了什么。");
            ImGui::BeginDisabled(hostnr::feature()==nullptr);
            if(ImGui::Button("保存同帧原画与结果"))matchedcapture::Request();
            ImGui::EndDisabled();
            ImGui::SameLine();ImGui::TextUnformatted(matchedcapture::Note());
            tip("成功的 NR 帧生成一组成对原始像素与参数记录，写入游戏目录 033-captures。\n等待实际 GPU 完成后后台保存；默认关闭，不持续录制。");
            static const char *bridges[] = {"自动", "SDR sRGB", "场景线性 BT.709", "HDR10 PQ / BT.2020", "scRGB (80 尼特/单位)"};
            int bridge = carrier::cfg.colorbridge + 1;
            label("输入颜色");
            if (ImGui::Combo("##colorbridge", &bridge, bridges, 5)) carrier::cfg.colorbridge = bridge - 1;
            tip("自动模式读取交换链颜色空间；游戏内部浮点纹理按场景线性处理。\n交换链开了 HDR，不代表游戏内部的 DLSS 输出也是 PQ。");
            label("HDR 漫反射白");
            ImGui::SliderFloat("##diffusewhite", &carrier::cfg.diffuse_white, 80.f, 1000.f, "%.0f 尼特");
            ImGui::Dummy(ImVec2(0, fs * 0.25f));
            if (ImGui::BeginTable("##a2", 2, ImGuiTableFlags_SizingStretchSame, ImVec2(0, 0)))
            {
                if (rp) ImGui::BeginDisabled();
                static const char *kCurve[] = { "柔和滚降 (旧)", "Neutwo 可逆桥", "混合曲线" };
                int cv = carrier::cfg.curve; if (cv < 0 || cv > 2) cv = 1;
                cell2("编码曲线");
                if (ImGui::Combo("##acv", &cv, kCurve, 3)) carrier::cfg.curve = cv;
                tip("喂给模型之前画面按哪条曲线压进 0-1。\n混合曲线保持中间调，只在高光滚降；稳定合成约束逆曲线对编辑的放大。");
                if(!use_host) {
                static const char *kComp[] = { "比值合成", "稳定合成" };
                int cm = carrier::cfg.compose; if (cm < 0 || cm > 1) cm = 0;
                cell2("合成方式");
                if (ImGui::Combo("##acm", &cm, kComp, 2)) carrier::cfg.compose = cm;
                tip("稳定合成保留原图，再加入受限的模型编辑，避免逆曲线把小变化放大成闪光。\n不等于 RenoDX 的原样替换。");
                }
                if (rp) ImGui::EndDisabled();
                static const char *kBase[] = { "整帧", "游戏渲染分辨率" };
                int mf = carrier::cfg.modelfull ? 0 : 1;
                cell2("模型基准");
                if (ImGui::Combo("##amf", &mf, kBase, 2)) carrier::cfg.modelfull = (mf == 0) ? 1 : 0;
                tip("上面那个「精度」按什么尺寸算。选「游戏渲染分辨率」开销能少一半左右, 细节略软;\n只对就地插入路有意义。");
                int cmp = carrier::cfg.comparepct;
                cell2("分屏对比");
                if (ImGui::SliderInt("##acmp", &cmp, 0, 99, cmp == 0 ? "关" : "%d%%"))
                    carrier::cfg.comparepct = cmp;
                tip("0 = 关。拉开后左半边原画、右半边处理后, 中间一条白线。同一帧同一位置比。");
                if(!use_host) {
                int bl = carrier::cfg.blend;
                cell2("混合");
                if (ImGui::SliderInt("##abl", &bl, 0, 200, "%d%%")) carrier::cfg.blend = bl;
                tip("模型那张图被够到多少。0 = 完全不参与。");
                cell2("上色");
                ImGui::SliderFloat("##aco", &carrier::cfg.colour, 0.0f, 1.0f, "%.2f");
                tip("模型的颜色跟不跟过来。0 = 只让光影带上模型的判断, 色相保持游戏自己的。");
                }
                cell2("高光护栏");
                ImGui::SliderFloat("##agd", &carrier::cfg.guard, 1.0f, 8.0f, "%.1f 倍");
                tip("稳定合成：限制模型编辑的放大和相对原图的高光峰值。数值越小约束越强。\n比值合成：限制明暗倍率。模型无改动时保留原有高光。");
                ImGui::BeginDisabled(rp);
                cell2("HDR 白点");
                ImGui::SliderFloat("##awp", &carrier::cfg.whitepoint, 0.1f, 8.0f, "%.2f");
                tip("稳定回桥开启时固定为 3.16，这个旧配置不参与；关闭稳定回桥后才使用。");
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            ImGui::Dummy(ImVec2(0, fs * 0.25f));
            ImGui::Dummy(ImVec2(fs * 3.6f, 0)); ImGui::SameLine();
            if (ImGui::Button("压力自测##abench")) carrier::bench::start();
            tip("连着换几档处理精度, 看会不会掉设备 —— 排障用。");
        }
    }

    carrier::PollConfig();
}


#ifdef K033_MONOLITHIC_ENGINE
// Includes are outside namespace panel: the presentation header declares studio033.
#endif
static void draw_main(reshade::api::effect_runtime* runtime){
#ifndef K033_MONOLITHIC_ENGINE
    draw_legacy(runtime);
#else
    studio033::State state;
    static nrcontrolsabi::Snapshot lastControls;static bool haveControls=false;
    nrcontrolsabi::Snapshot currentControls;
    state.controlsFresh=nrcontrols::Read(&currentControls)!=0;
    if(state.controlsFresh){lastControls=currentControls;haveControls=true;}
    state.controlPending=lastControls.pending;
    for(unsigned i=0;i<nrcontrolsabi::Count;++i){state.values[i]=lastControls.values[i];state.applied[i]=lastControls.modelValues[i];}
    state.modelActive=lastControls.modelActive!=0;
    state.activeLayers=lastControls.activeLayers;state.cachedLayers=lastControls.cachedLayers;
    state.whiteStatus=lastControls.whiteStatus;state.effectiveWhite=lastControls.effectiveWhite;
    state.whiteEncoding=lastControls.whiteEncoding;
    state.hotkey=lastControls.hotkey;
    state.exposureRecorded=lastControls.exposureRecorded;state.exposureHeld=lastControls.exposureHeld;state.exposureBypass=lastControls.exposureBypass;
    state.portraitAvailable=lastControls.portraitAvailable!=0;state.portraitFaces=lastControls.portraitFaces;
    state.portraitUnavailable=lastControls.portraitUnavailable!=0;
    state.portraitStatus=lastControls.portraitStatus;state.portraitCaptureW=lastControls.portraitCaptureW;
    state.portraitCaptureH=lastControls.portraitCaptureH;state.portraitAgeMs=lastControls.portraitAgeMs;state.portraitCpuMs=lastControls.portraitCpuMs;
    state.running=(hostnr::attached()||inject::active()||nrbackbuffer::Claimed())?hostnr::active():(carrier::g.frames_done>0&&!carrier::g.disabled);
    state.recoveryReady=safemode::recovery_ready();state.recoveryWriteFailed=safemode::recovery_write_failed();
    state.paused=safemode::off();state.failed=carrier::g.disabled||(nrbackbuffer::Claimed()&&(nrbackbuffer::stopped||hostnr::s_failed));state.available=haveControls;
    state.gradeAvailable=!rendercore::Detect()||rendercore::Status().owner!=k033core::NativeVulkan;
    state.width=hostnr::model_w();state.height=hostnr::model_h();state.frames=hostnr::frames();
    for(unsigned i=0;i<3;++i){state.layerModelW[i]=lastControls.layerModelW[i];state.layerModelH[i]=lastControls.layerModelH[i];}
    state.modelPending=lastControls.tuningPending!=0;
    std::snprintf(state.modelWait,sizeof(state.modelWait),"%s",lastControls.modelWait);
    state.presetBuilt=hostnr::s_feat?unsigned(hostnr::s_model_cfg.preset):0;
    state.saved=carrier::config_save.has_saved&&!carrier::config_save.dirty;
#if defined(K033_BETA2_RESHADE_HOST)
    k033beta2::SettingsStatus sharedStatus;
    const int sharedResult=k033beta2::Inspect(sharedStatus);
    state.settingsReady=sharedResult==K033_OK&&sharedStatus.active==2;
    state.settingsPending=sharedStatus.queued!=sharedStatus.saved||sharedStatus.nr_queued!=sharedStatus.nr_saved||sharedStatus.fg_queued!=sharedStatus.fg_saved;
    state.settingsFailed=sharedResult<0||sharedStatus.save_result<0||sharedStatus.nr_save_result<0||sharedStatus.fg_save_result<0;
    state.available=state.available&&carrier::shared_ready;
    state.gradeAvailable=state.gradeAvailable&&carrier::shared_ready;
#endif
    state.mfgAvailable=(fg::in.nvgen==40||fg::in.nvgen==50)&&!g_stood_down &&
        (lastControls.mfgAccepted>0||fg::in.sl_present);
    state.mfgRequested=lastControls.mfgRequested;state.mfgAccepted=lastControls.mfgAccepted;
    state.mfgPacingReady=lastControls.mfgPacingReady!=0;state.mfgBlocked=lastControls.mfgBlocked!=0;state.mfgOff=lastControls.mfgOff!=0;
    // 2026-09-12 移植：随包转接件的文件事实与 INI 值。只看游戏目录里的文件，不查显卡、不碰驱动。
    static int bridgeSave=0; // 0 本次没写过, 1 写成功, 2 写失败
    state.gpuGen=unsigned(fg::in.nvgen);state.gameFrameGen=fg::in.sl_present;
    {
        const auto bridge=bridge2030::Current();const auto ini=bridge2030::CurrentSettings();
        state.bridgePresent=bridge.layout!=bridge2030::Layout::None;state.bridgeLegacy=bridge.layout==bridge2030::Layout::Legacy;
        state.bridgeProvider=state.gpuGen==20?bridge.sm75:bridge.sm86;
        std::snprintf(state.bridgeLoader,sizeof(state.bridgeLoader),"%s",bridge.loader);
        state.bridgeMode=ini.mode;state.bridgeForce=(std::max)(0,(std::min)(4,ini.force));
        state.bridgeSaved=bridgeSave==1;state.bridgeSaveFailed=bridgeSave==2;
    }
    // S38: Windows GPU scheduling and build, read-only, every five seconds (the FG page names what is missing on RTX 20/30).
    {static ULONGLONG checked=0;static unsigned hags=0,build=0;
        if(!checked||GetTickCount64()-checked>=5000){checked=GetTickCount64();hags=unsigned(fgcheck033::Scheduling());build=fgcheck033::WindowsBuild();}
        state.hags=hags;state.osBuild=build;}
    const auto timing=gputime::snapshot();if(timing.fresh())state.nrMs=float(timing.total);
    if(mfgunlock::reflex::report_available.load()==1 && GetTickCount64()-mfgunlock::reflex::state_time_ms.load()<2000 && mfgunlock::reflex::gpu_frame_us.load()>0)
        state.gpuFrameMs=float(mfgunlock::reflex::gpu_frame_us.load())/1000.f;
    yyappearance::Refresh();
    yyappearance::changed=[]{yyappearance::Save();auto module=reshade::internal::get_reshade_module_handle();if(module){auto setter=reinterpret_cast<void(__cdecl*)(unsigned)>(GetProcAddress(module,"K033_SetYanYunAppearance"));if(setter)setter(unsigned(yyappearance::light)|(unsigned(yyappearance::english)<<1));}};
    static studio033::View view;studio033::Edits edits;studio033::Theme skin;
    static studio033::RecipeView recipeView;studio033::RecipeEdits recipeEdits{};
    static yanyunrecipe::Store<> recipeStore;
    static yanyunrecipe::LibraryStore<> libraryStore;
    // S23 (user): saved records. On first use the single preset earlier builds
    // kept (recipes.v2.txt) becomes the first record instead of being lost. A
    // record file that exists but cannot be read is never written over: the list
    // stays unloaded, saving is refused, and it is read again two seconds later.
    static ULONGLONG libraryRetryMs=0;
    if(!recipeView.libraryLoaded&&GetTickCount64()>=libraryRetryMs){
        const auto read=libraryStore.Load(recipeView.library);
        if(read==yanyunrecipe::LibraryRead::Failed)libraryRetryMs=GetTickCount64()+2000;
        else{recipeView.libraryLoaded=true;yanyunrecipe::Recipe earlier;
            if(read==yanyunrecipe::LibraryRead::Missing&&recipeStore.Load(earlier)){yanyunrecipe::LibraryEntry entry;
                yanyunrecipe::SetName(entry,yyappearance::Text("之前保存的方案","Earlier saved preset"));entry.recipe=earlier;
                recipeView.library.push_back(entry);libraryStore.Save(recipeView.library);}}} // kept in the list even if this write fails; the next save writes it
    // S35 033特调: the badge shows whether the before-file exists. While the settings folder
    // cannot be reached the switch stays unknown (not clickable) and is asked again in two seconds.
    static yanyuntuning::Store<> tuningStore;static int tuningOn=-1;static ULONGLONG tuningRetryMs=0;
    if(tuningOn<0&&GetTickCount64()>=tuningRetryMs){const auto presence=tuningStore.Exists();
        if(presence==yanyuntuning::Presence::Unknown)tuningRetryMs=GetTickCount64()+2000;else tuningOn=presence==yanyuntuning::Presence::Yes?1:0;}
    state.tuningOn=tuningOn==1;
    recipeView.requestRevision=nrcontrols::RecipeRevision();
    // S32 超分模型: what the core holds for the game's own DLSS and what its last creation asked for.
    {const auto sr=srmodel033::Status();recipeView.srModel=sr.requested;recipeView.srApplied=sr.applied;recipeView.srPending=sr.pending!=0;
        recipeView.srMajor=sr.major;recipeView.srMinor=sr.minor;recipeView.srPatch=sr.patch;recipeView.srExternal=sr.external!=0;
        recipeView.srQueries=sr.queries;recipeView.srRenderW=sr.renderW;recipeView.srRenderH=sr.renderH;recipeView.srForcedMilli=sr.forcedMilli;recipeView.srSizePending=sr.sizePending!=0;}
    recipeView.runtimeNote=yanyundual::note.load();
    recipeView.recognitionFailed=yanyundual::recognitionStatus.load()<0;
    {nrdispatch::WriterAccess access;if(access.entered){recipeView.regionalApplied=yanyundual::enabled;recipeView.outputW=hostnr::s_w;recipeView.outputH=hostnr::s_h;recipeView.renderW=hostnr::s_built_gw;recipeView.renderH=hostnr::s_built_gh;}}
    if(!recipeView.initialized||!recipeView.dirty){nrdispatch::WriterAccess access;
        if(access.entered&&yanyundual::haveAppliedRecipe&&recipeView.appliedRevision!=yanyundual::revision){recipeView.draft=yanyundual::appliedRecipe;recipeView.initialized=true;recipeView.appliedRevision=yanyundual::revision;}}
    const bool visible=studio033::BeginBody();
    if(visible){
        studio033::Header(state,view,edits);
#if defined(K033_BETA2_RESHADE_HOST)
        if(view.page==4){
        nrbeta2::Snapshot currentInput;const bool haveCurrentInput=nrbeta2::Read(&currentInput)!=0;
        if(haveCurrentInput)ImGui::TextWrapped("%s",nrbeta2::Text(nrbeta2::State(currentInput.state)));
        if(nrbackbuffer::Claimed())ImGui::TextWrapped("%s",nrbackbuffer::reason);
        }
#else
        if(view.page==4&&nrbackbuffer::Claimed())ImGui::TextDisabled("%s",nrbackbuffer::reason);
#endif
        switch(view.page){
        case 0:studio033::RecipePre(state,recipeView,recipeEdits);break;
        case 1:studio033::RecipeColumns(state,recipeView,recipeEdits);break;
        case 2:{
            const bool universalRoute=embeddedui::SupplementalFramegen()==2;
            // 2026-09-12：按【谁真的在出帧】分。燕云自带 DLSS-G（310.6 + Streamline 2.11.1），转接件接管得上；
            // 转接件在场就显示它那张卡，没有就用 033 自己的解锁。
            // S38: RTX 20/30 always get this card, so a missing bridge or GPU scheduling is named there.
            if(state.bridgePresent||state.gpuGen==20||state.gpuGen==30)studio033::Bridge2030(state,edits);
            else if(!universalRoute)studio033::FrameGeneration(state,edits);
            break;}
        case 5:studio033::RecipeSr(state,recipeView,recipeEdits);break;
        case 4:{
            studio033::Heading(studio033::L("运行状态"));studio033::ModelDiagnostics(state);
            ImGui::TextWrapped("%s",studio033::L(yanyundual::note.load()));
            const auto recognitions=yanyundual::recognitions.load();
            ImGui::Text(yyappearance::Text("分区记录 %llu · 识别完成 %llu","Regional records %llu · Recognitions %llu"),(unsigned long long)yanyundual::recorded.load(),(unsigned long long)recognitions);
            if(recognitions){const auto result=yanyundual::Worker().Snapshot();const auto now=GetTickCount64();if(result&&now>=result->source.capturedMs)ImGui::Text(yyappearance::Text("识别 %.1f ms · 遮罩距今 %llu ms","Recognition %.1f ms · Mask age %llu ms"),result->inferenceMs,(unsigned long long)(now-result->source.capturedMs));}
#if defined(K033_BETA2_RESHADE_HOST)
            nrbeta2::Snapshot input;
            if(nrbeta2::Read(&input)){
                ImGui::TextWrapped("%s",nrbeta2::Text(nrbeta2::State(input.state)));
                ImGui::Text("NR 命令记录 %llu",(unsigned long long)input.records);
                ImGui::TextWrapped("原生引导已用于记录：%s；同次求值：%s。",input.native_guides_recorded?"是":"尚无记录",input.same_evaluate?"是":"未证明");
                ImGui::TextWrapped("命令已记录不代表屏幕实际显示或画质验收通过。");
            }
            ImGui::Text("共用设置：加载 %d · 调色保存 %d · NR 保存 %d · FG 保存 %d",sharedStatus.load_result,sharedStatus.save_result,sharedStatus.nr_save_result,sharedStatus.fg_save_result);
            ImGui::TextWrapped("%s",beta2gradepresent::Reason());
#endif
            studio033::Space();ImGui::TextDisabled("日志：dlss5-033.log");ImGui::TextWrapped("%s",K033_VER);break;}

        }
        studio033::RecipeFooter(state,edits);
        if(ImGui::Button(studio033::L("运行状态")))view.page=4;
    }
    studio033::EndBody();studio033::ApplyWindowEdits(view,edits);
    static_assert(nrcontrolsabi::Count<=sizeof(edits.changed)*8,"panel edit mask");
    studio033::SubmitEdits(state,view,edits,[](uint32_t id,float value){return nrcontrols::Set(id,value)!=0;});
    if(edits.toggle)nrcontrols::Set(nrcontrolsabi::Enabled,state.values[nrcontrolsabi::Enabled]!=0?0.f:1.f);
    if(edits.recover)safemode::retry();
    if(edits.neutral)nrcontrols::Action(nrcontrolsabi::NeutralGrade);
    if(edits.portrait)nrcontrols::Action(nrcontrolsabi::PortraitNatural);
    if(edits.resetHistory)hostnr::RequestHistoryReset();
    if(edits.mfg)nrcontrols::SetMfg(edits.multiplier);
    // S37 (owner: 「另外快捷键能改」): the NR key chosen on the panel, live at once and kept per user.
    if(edits.hotkey>=0&&hotkey033::Allowed(edits.hotkey)){
        const int before=carrier::cfg.hotkey;carrier::SetHotkey(edits.hotkey);const bool saved=hotkey033::Save(edits.hotkey);
        static char keyNote[320];
        std::snprintf(keyNote,sizeof(keyNote),saved?yyappearance::Text("NR 快捷键已换成 %s。","The NR key is now %s."):
            yyappearance::Text("NR 快捷键已换成 %s，但没能保存，下次进游戏还是原来的键。","The NR key is now %s but was not saved; the next start uses the previous key."),
            studio033::L(hotkey033::Name(edits.hotkey)));
        // S39: what to know about the new key (typing keys, the numpad, F9 with Shift).
        if(const char* extra=hotkey033::Note(edits.hotkey)){const size_t used=std::strlen(keyNote);std::snprintf(keyNote+used,sizeof(keyNote)-used,"%s",studio033::L(extra));}
        recipeView.note=keyNote;
        Log("[033 hotkey] NR key changed on the panel: vk %d -> %d (%s), saved %s",before,edits.hotkey,hotkey033::Name(edits.hotkey),saved?"yes":"NO");
    }
    // S39: while the panel waits for a new key or one of its text fields takes typing, that press
    // must not also switch NR (letters and digits can be the NR key now).
    carrier::HotkeyPanelFrame(ImGui::GetIO().WantTextInput||studio033::keyCapture);
    if(edits.bridge){
        const bool written=bridge2030::WriteSettings(edits.bridgeMode,edits.bridgeForce);bridgeSave=written?1:2;
        Log("[mfg2030] dlssg_to_fsr3.ini FGMode=%d ForceFrameGenOverride=%d → %s（重进游戏生效）",edits.bridgeMode,edits.bridgeForce,written?"已写入":"写入失败");
    }
    if(edits.save)nrcontrols::Action(nrcontrolsabi::Save);
    if(recipeEdits.load){yanyunrecipe::Recipe loaded;
        if(recipeStore.Load(loaded))studio033::RecipeLoaded(recipeView,loaded);
        else recipeView.note="未读取到兼容方案；当前草稿已保留。";}
    yanyundual::previewMask.store(recipeView.preview);
    if(recipeEdits.srModel>=0){const auto result=srmodel033::Request(uint32_t(recipeEdits.srModel));
        if(result==srmodel033::RequestResult::Refused)recipeView.note="超分模型没能切换，原来的模型保留。";
        else if(result==srmodel033::RequestResult::NotSaved)recipeView.note="超分模型已切换，但没能保存，下次进游戏会用回原来的。";}
    if(recipeEdits.save){
        if(!recipeView.libraryLoaded)recipeView.note="保存记录暂时读不到，请稍后再试。";
        else if(recipeView.library.size()>=yanyunrecipe::LibraryMax)recipeView.note="保存记录已满（40 条），请先删除不用的。";
        else{SYSTEMTIME now{};GetLocalTime(&now);yanyunrecipe::LibraryEntry entry;entry.recipe=recipeView.draft;
            yanyunrecipe::SetName(entry,yanyunrecipe::UniqueName(recipeView.library,yanyunrecipe::TimeName(now.wMonth,now.wDay,now.wHour,now.wMinute)));
            recipeView.library.push_back(entry);const bool saved=libraryStore.Save(recipeView.library);if(!saved)recipeView.library.pop_back();
            recipeView.note=saved?"已存为一条新记录，名字可以直接改。":"保存失败，原方案已保留。";}}
    if(recipeEdits.remove>=0&&size_t(recipeEdits.remove)<recipeView.library.size()){
        const auto removed=recipeView.library[size_t(recipeEdits.remove)];recipeView.library.erase(recipeView.library.begin()+recipeEdits.remove);
        if(libraryStore.Save(recipeView.library))recipeView.note="已删除这条记录。";
        else{recipeView.library.insert(recipeView.library.begin()+recipeEdits.remove,removed);recipeView.note="删除失败，这条记录还在。";}}
    if(recipeEdits.libraryChanged&&!libraryStore.Save(recipeView.library))recipeView.note="改名未能保存，请重试。";
    const char* switchedTo=nullptr;
    if(recipeEdits.switchTo>=0&&size_t(recipeEdits.switchTo)<recipeView.library.size()){
        const auto& entry=recipeView.library[size_t(recipeEdits.switchTo)];
        recipeView.draft=entry.recipe;recipeView.initialized=true;recipeView.dirty=true;switchedTo=entry.name;
        // Same gate as the Apply button: NR running and precision within this resolution.
        const char* bad="";int allowed=0;
        if(state.available&&!state.paused&&!studio033::RecipePrecisionInvalid(recipeView,bad,allowed))recipeEdits.applyWhole=true;
        else{static char loadedNote[160];
            std::snprintf(loadedNote,sizeof(loadedNote),yyappearance::Text("已载入「%s」，尚未应用。","Loaded \"%s\"; not applied yet."),entry.name);recipeView.note=loadedNote;}}
    if(recipeEdits.applyWhole){const auto result=nrcontrols::SetRecipe(recipeView.draft,recipeView.draft.regional,recipeView.requestRevision);
        if(result==yanyunrecipe::SubmitResult::Accepted)recipeView.dirty=false;
        recipeView.note=result==yanyunrecipe::SubmitResult::Accepted?"方案已提交，正在准备模型；生效状态见上方。":"提交失败，草稿已保留，请重试。";
        if(switchedTo&&result==yanyunrecipe::SubmitResult::Accepted){static char switchNote[160];
            std::snprintf(switchNote,sizeof(switchNote),yyappearance::Text("已切换到「%s」，正在准备模型。","Switched to \"%s\"; preparing the model."),switchedTo);recipeView.note=switchNote;}}
    // S35 (owner 2026-09-24): 「启用033特调」. On: keep the player's applied recipe, SR model and
    // multiplier, then apply the preset. Off: put them back. A refused step leaves the switch as it was.
    if(edits.tuning&&tuningOn>=0){
        static char tuningNote[256];
        auto precisionInvalid=[&](const yanyunrecipe::Recipe& r,const char*& bad,int& allowed){
            studio033::RecipeView probe;probe.draft=r;probe.outputW=recipeView.outputW;probe.outputH=recipeView.outputH;
            probe.renderW=recipeView.renderW;probe.renderH=recipeView.renderH;return studio033::RecipePrecisionInvalid(probe,bad,allowed);};
        const char* bad="";int allowed=0;
        if(tuningOn==0){
            yanyunrecipe::Recipe preset;
            if(!yanyuntuning::Preset(preset))recipeView.note="033特调的参数读不出来，没有开启。";
            else if(!state.available||state.paused)recipeView.note="NR 现在没在运行，033特调没有开启。";
            else if(precisionInvalid(preset,bad,allowed)){
                std::snprintf(tuningNote,sizeof(tuningNote),yyappearance::Text("033特调的%s精度超过这个分辨率的上限（最多 %d%%），没有开启。",
                    "033 tuning: its %s precision exceeds this resolution's limit (at most %d%%); not enabled."),bad,allowed);recipeView.note=tuningNote;}
            else{
                yanyuntuning::Before before;
                {nrdispatch::WriterAccess access;before.recipe=access.entered&&yanyundual::haveAppliedRecipe?yanyundual::appliedRecipe:recipeView.draft;}
                before.srModel=recipeView.srModel;before.frameGeneration=state.mfgAvailable;before.multiplier=state.mfgRequested;
                if(!tuningStore.Save(before))recipeView.note="没能记下你现在的设置，033特调没有开启。";
                else if(nrcontrols::SetRecipe(preset,preset.regional,recipeView.requestRevision)!=yanyunrecipe::SubmitResult::Accepted){
                    tuningStore.Remove();recipeView.note="提交失败，033特调没有开启，原来的设置不变。";}
                else{
                    recipeView.draft=preset;recipeView.initialized=true;recipeView.dirty=false;tuningOn=1;
                    const uint32_t sr=yanyuntuning::SrModelFor(state.gpuGen);srmodel033::Request(sr);
                    if(state.mfgAvailable)nrcontrols::SetMfg(yanyuntuning::Multiplier);
                    recipeView.note="已启用 033特调，正在准备模型。";
                    Log("[033 tuning] on: owner preset submitted (mode %u), SR model %u, frame generation %s; before-state kept",
                        preset.regional,sr,state.mfgAvailable?"6x":"unchanged");
                }
            }
        }else{
            yanyuntuning::Before before;const auto read=tuningStore.Load(before);
            if(read!=yanyuntuning::Read::Ok){
                if(tuningStore.Remove()){tuningOn=0;recipeView.note="已关闭 033特调；开启前的设置没找到，当前参数保留。";}
                else recipeView.note="033特调没能关闭，请稍后再试。";
            }else if(precisionInvalid(before.recipe,bad,allowed)){
                // Not applied: it would not build at this resolution. The player adjusts and applies it.
                recipeView.draft=before.recipe;recipeView.initialized=true;recipeView.dirty=true;srmodel033::Request(before.srModel);
                if(before.frameGeneration&&state.mfgAvailable)nrcontrols::SetMfg(before.multiplier);
                if(tuningStore.Remove())tuningOn=0;
                std::snprintf(tuningNote,sizeof(tuningNote),yyappearance::Text("已关闭 033特调；开启前的设置里%s精度超过这个分辨率的上限（最多 %d%%），已放进草稿，调低后点应用。",
                    "033 tuning off. Your earlier settings exceed this resolution's %s precision limit (at most %d%%); they are in the draft: lower it and apply."),bad,allowed);recipeView.note=tuningNote;
            }else if(!state.available||state.paused)recipeView.note="NR 现在没在运行，033特调没有关闭。";
            else if(nrcontrols::SetRecipe(before.recipe,before.recipe.regional,recipeView.requestRevision)!=yanyunrecipe::SubmitResult::Accepted)
                recipeView.note="提交失败，033特调仍然开着。";
            else{
                recipeView.draft=before.recipe;recipeView.initialized=true;recipeView.dirty=false;srmodel033::Request(before.srModel);
                if(before.frameGeneration&&state.mfgAvailable)nrcontrols::SetMfg(before.multiplier);
                if(tuningStore.Remove()){tuningOn=0;recipeView.note="已关闭 033特调，回到开启前的设置，正在准备模型。";}
                else recipeView.note="设置已换回，但开关记录没能清掉，请再点一次关闭。";
                Log("[033 tuning] off: before-state submitted (mode %u), SR model %u, frame generation %s",
                    before.recipe.regional,before.srModel,before.frameGeneration&&state.mfgAvailable?"restored":"unchanged");
            }
        }
    }
    if(edits.close)runtime->open_overlay(false,reshade::api::input_source::mouse);
    carrier::PollConfig();
#endif
}

} // namespace panel
