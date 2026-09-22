// =====================================================================
//  DLSS5 一键包 · 图形使用向导 (ReShade addon)
//  状态灯 + 三步卡片 + 状态速查 + 署名横幅, 全 ImGui 绘制
//  整理: B站 @热心网友033
// =====================================================================
#include <Windows.h>
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>

extern "C" __declspec(dllexport) const char *NAME = "DLSS5 使用向导";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "图形化引导: 状态灯 + 三步用法 + 状态速查。整理: B站 @热心网友033";

static const ImU32 COL_GOLD   = IM_COL32(255, 196,  84, 255);
static const ImU32 COL_GOLD_D = IM_COL32(255, 196,  84,  40);
static const ImU32 COL_GREEN  = IM_COL32( 92, 205, 125, 255);
static const ImU32 COL_GRAY   = IM_COL32(120, 124, 140, 255);
static const ImU32 COL_CARD   = IM_COL32( 30,  32,  42, 255);
static const ImU32 COL_CARD_B = IM_COL32( 58,  60,  72, 255);
static const ImU32 COL_TEXT   = IM_COL32(238, 238, 245, 255);
static const ImU32 COL_SUB    = IM_COL32(168, 172, 185, 255);

// 圆角卡片, 返回内容起点
static ImVec2 card_begin(float height)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + height), COL_CARD, 8.0f);
    dl->AddRect      (p, ImVec2(p.x + w, p.y + height), COL_CARD_B, 8.0f);
    return p;
}

static void step_card(int idx, const char *line1, const char *line2)
{
    const float fs  = ImGui::GetFontSize();
    const float h   = fs * 3.6f;
    ImDrawList *dl  = ImGui::GetWindowDrawList();
    const ImVec2 p  = card_begin(h);
    // 序号圆
    const float r   = fs * 0.95f;
    const ImVec2 c  = ImVec2(p.x + r + fs * 0.7f, p.y + h * 0.5f);
    dl->AddCircleFilled(c, r, COL_GOLD);
    char num[8]; wsprintfA(num, "%d", idx);
    const ImVec2 ns = ImGui::CalcTextSize(num);
    dl->AddText(ImVec2(c.x - ns.x * 0.5f, c.y - ns.y * 0.5f), IM_COL32(20, 21, 27, 255), num);
    // 两行文字
    const float tx = c.x + r + fs * 0.8f;
    dl->AddText(ImVec2(tx, p.y + h * 0.5f - fs - fs * 0.15f), COL_TEXT, line1);
    dl->AddText(ImVec2(tx, p.y + h * 0.5f + fs * 0.15f),      COL_SUB,  line2);
    ImGui::Dummy(ImVec2(0, h + fs * 0.45f));
}

static void kv_row(const char *k, const char *v, ImU32 vcol)
{
    const float fs = ImGui::GetFontSize();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(p.x + fs * 0.35f, p.y + fs * 0.55f), fs * 0.18f, COL_GOLD);
    dl->AddText(ImVec2(p.x + fs * 0.95f, p.y), COL_TEXT, k);
    const float kw = ImGui::CalcTextSize(k).x;
    dl->AddText(ImVec2(p.x + fs * 0.95f + kw + fs * 0.6f, p.y), vcol, v);
    ImGui::Dummy(ImVec2(0, fs * 1.55f));
}

static void draw_guide(reshade::api::effect_runtime *runtime)
{
    const float fs = ImGui::GetFontSize();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    // ---- 状态灯卡片 ----
    int nr_on = 0;
    reshade::get_config_value(runtime, "RenoDX.DLSS5", "NeuralUplift", nr_on);
    {
        const float h  = fs * 3.2f;
        const ImVec2 p = card_begin(h);
        const float r  = fs * 0.75f;
        const ImVec2 c = ImVec2(p.x + r + fs * 0.8f, p.y + h * 0.5f);
        dl->AddCircleFilled(c, r, nr_on ? COL_GREEN : COL_GRAY);
        if (nr_on) // 亮圈
            dl->AddCircle(c, r + fs * 0.28f, IM_COL32(92, 205, 125, 110), 0, 2.0f);
        const char *t1 = nr_on ? "神经渲染: 开" : "神经渲染: 关";
        const char *t2 = nr_on ? "游戏里按 F6 可随时开关" : "去「神经渲染」页勾上第一行, 或按 F6";
        const float tx = c.x + r + fs * 0.9f;
        dl->AddText(ImVec2(tx, p.y + h * 0.5f - fs - fs * 0.1f), nr_on ? COL_GREEN : COL_TEXT, t1);
        dl->AddText(ImVec2(tx, p.y + h * 0.5f + fs * 0.1f), COL_SUB, t2);
        ImGui::Dummy(ImVec2(0, h + fs * 0.6f));
    }

    // ---- 治闪烁卡片 ----
    static bool s_fix_written = false;
    {
        dl->AddText(ImGui::GetCursorScreenPos(), COL_GOLD, "画面闪烁? (记分牌/界面闪, 2K26 这类)");
        ImGui::Dummy(ImVec2(0, fs * 1.55f));
        int ui = 0, mask = 0, inten = 2;
        reshade::get_config_value(runtime, "RenoDX.DLSS5", "NRUICorrection", ui);
        reshade::get_config_value(runtime, "RenoDX.DLSS5", "NRAutoMask",     mask);
        reshade::get_config_value(runtime, "RenoDX.DLSS5", "NRIntensity",    inten);
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(255, 196,  84, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 214, 130, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(230, 170,  60, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32( 20,  21,  27, 255));
        if (ImGui::Button("一键治闪烁", ImVec2(fs * 8.0f, fs * 2.0f)))
        {
            reshade::set_config_value(runtime, "RenoDX.DLSS5", "NRUICorrection", 1);
            reshade::set_config_value(runtime, "RenoDX.DLSS5", "NRAutoMask",     1);
            reshade::set_config_value(runtime, "RenoDX.DLSS5", "NRIntensity",    1);
            s_fix_written = true;
        }
        ImGui::PopStyleColor(4);
        ImGui::SameLine(0, fs * 0.8f);
        if (ImGui::Button("还原默认", ImVec2(fs * 6.5f, fs * 2.0f)))
        {
            reshade::set_config_value(runtime, "RenoDX.DLSS5", "NRUICorrection", 0);
            reshade::set_config_value(runtime, "RenoDX.DLSS5", "NRAutoMask",     0);
            reshade::set_config_value(runtime, "RenoDX.DLSS5", "NRIntensity",    2);
            s_fix_written = true;
        }
        char st[160];
        wsprintfA(st, "现在: 界面校正 %s · 自动遮罩 %s · 整体强度 %d",
                  ui ? "开" : "关", mask ? "开" : "关", inten);
        ImGui::TextColored(ImVec4(0.66f, 0.67f, 0.73f, 1.0f), "%s", st);
        if (s_fix_written)
            ImGui::TextColored(ImVec4(0.36f, 0.80f, 0.49f, 1.0f),
                "已写入 ✓ 重启游戏生效。等不及就去「神经渲染」页手动勾, 立即生效。");
        ImGui::Dummy(ImVec2(0, fs * 0.6f));
    }

    // ---- 三步卡片 ----
    dl->AddText(ImGui::GetCursorScreenPos(), COL_GOLD, "三步用起来");
    ImGui::Dummy(ImVec2(0, fs * 1.6f));
    step_card(1, "游戏画质设置里把 DLSS 打开", "别用 FSR / TSR / XeSS, 神经渲染挂在 DLSS 上");
    step_card(2, "顶上「神经渲染」标签页, 勾第一行", "预设强度随便拉, 拉坏了点「重置」就回来");
    step_card(3, "嫌卡按 F6 关, 闪烁勾「界面校正」", "过场闪退? 过场前 F6 关, 看完再开");

    // ---- 状态速查 ----
    ImGui::Dummy(ImVec2(0, fs * 0.3f));
    dl->AddText(ImGui::GetCursorScreenPos(), COL_GOLD, "调节页最底下那行字 = 现在的状态");
    ImGui::Dummy(ImVec2(0, fs * 1.6f));
    kv_row("已注入, 工作中",            "一切正常, 关面板玩就行",        COL_GREEN);
    kv_row("已挂好钩, 但游戏没开DLSS",  "回到第 1 步",                   COL_TEXT);
    kv_row("没挂上画面 (待机/失败)",    "把游戏目录 ReShade.log 发UP主", COL_TEXT);

    // ---- 署名横幅 ----
    ImGui::Dummy(ImVec2(0, fs * 0.5f));
    {
        const float h  = fs * 2.2f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w  = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), COL_GOLD_D, 8.0f);
        dl->AddRect      (p, ImVec2(p.x + w, p.y + h), COL_GOLD,   8.0f);
        const char *sig = "整理: B站 @热心网友033 · 转载请保留出处";
        const ImVec2 ts = ImGui::CalcTextSize(sig);
        dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), COL_GOLD, sig);
        ImGui::Dummy(ImVec2(0, h + fs * 0.3f));
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule))
            return FALSE;
        reshade::register_overlay("使用向导", draw_guide);
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_overlay("使用向导", draw_guide);
        reshade::unregister_addon(hModule);
        break;
    }
    return TRUE;
}
