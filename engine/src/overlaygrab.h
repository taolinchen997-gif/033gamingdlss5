// =====================================================================
//  overlaygrab.h —— 把别家插件的 ReShade 标签「截」下来, 画进我们这一页
//
//  为什么: 业主要求「一个面板」。ReShade 的标签是各插件自己
//  ReShadeRegisterOverlay(标题, 回调) 注册的, 想拿掉别人的没有公开接口。
//  但注册函数是 ReShade 模块的【导出函数】—— 用 Detours 钩住它,
//  别家来注册时我们把 (标题, 回调) 收进兜里、不往下转发, 它的标签就不会出现;
//  然后在我们自己的面板里【原样调用那个回调】, 它的界面就长在我们这一页上。
//
//  ★好处★ 不是抄它的设置, 是把它本人搬过来 —— 控件还是实时的,
//  不存在"改完要重进游戏"。
//
//  ★前提是我们先加载★ ReShade 按文件名顺序加载插件,
//  dlss5-033.addon64 排在 renodx-dlss5.addon64 前面(d < r), 所以钩得上。
//  钩不上也不会坏事: 那就是回到"两个标签"的老样子。
// =====================================================================
#pragma once

#include <detours/detours.h>
#include <vector>
#include <string>
#include <cstring>

namespace ovgrab
{

using PFN_Reg    = void(__cdecl *)(const char *, void(*)(reshade::api::effect_runtime *));
using PFN_RegFor = void(__cdecl *)(void *, const char *, void(*)(reshade::api::effect_runtime *));

struct Grabbed
{
    std::string title;
    void (*cb)(reshade::api::effect_runtime *);
};

static std::vector<Grabbed> g_list;
static PFN_Reg    o_reg    = nullptr;
static PFN_RegFor o_regfor = nullptr;
static bool       g_hooked = false;
static const char kOurs[]  = "热心网友033";

static bool is_ours(const char *t) { return t != nullptr && std::strcmp(t, kOurs) == 0; }

static void take(const char *title, void (*cb)(reshade::api::effect_runtime *))
{
    for (size_t i = 0; i < g_list.size(); ++i)
        if (g_list[i].title == title) return;          // 别收重了
    g_list.push_back(Grabbed{ title, cb });
    Log("[面板] 截下别家的标签「%s」, 并进我们这一页", title);
}

static void __cdecl hk_reg(const char *title, void (*cb)(reshade::api::effect_runtime *))
{
    if (title != nullptr && cb != nullptr && !is_ours(title)) { take(title, cb); return; }
    if (o_reg != nullptr) o_reg(title, cb);
}

static void __cdecl hk_regfor(void *mod, const char *title, void (*cb)(reshade::api::effect_runtime *))
{
    if (title != nullptr && cb != nullptr && !is_ours(title)) { take(title, cb); return; }
    if (o_regfor != nullptr) o_regfor(mod, title, cb);
}

static void install(HMODULE reshade_mod)
{
    if (g_hooked || reshade_mod == nullptr) return;
    o_reg    = reinterpret_cast<PFN_Reg>(GetProcAddress(reshade_mod, "ReShadeRegisterOverlay"));
    o_regfor = reinterpret_cast<PFN_RegFor>(GetProcAddress(reshade_mod, "ReShadeRegisterOverlayForAddon"));
    if (o_reg == nullptr && o_regfor == nullptr)
    { Log("[面板] 没找到 ReShade 的注册函数, 标签合并跳过"); return; }
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (o_reg    != nullptr) DetourAttach(&reinterpret_cast<PVOID &>(o_reg),    static_cast<PVOID>(hk_reg));
    if (o_regfor != nullptr) DetourAttach(&reinterpret_cast<PVOID &>(o_regfor), static_cast<PVOID>(hk_regfor));
    const LONG r = DetourTransactionCommit();
    g_hooked = (r == NO_ERROR);
    Log("[面板] 标签合并钩子: %s (Detours 0x%lX)", g_hooked ? "装上了" : "没装上", r);
}

static void uninstall()
{
    if (!g_hooked) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (o_reg    != nullptr) DetourDetach(&reinterpret_cast<PVOID &>(o_reg),    static_cast<PVOID>(hk_reg));
    if (o_regfor != nullptr) DetourDetach(&reinterpret_cast<PVOID &>(o_regfor), static_cast<PVOID>(hk_regfor));
    DetourTransactionCommit();
    g_hooked = false;
}

static bool any() { return !g_list.empty(); }

// 在我们面板里把收来的界面画出来
static void draw(reshade::api::effect_runtime *rt)
{
    for (size_t i = 0; i < g_list.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::CollapsingHeader(g_list[i].title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            if (g_list[i].cb != nullptr) g_list[i].cb(rt);
        ImGui::PopID();
    }
}

} // namespace ovgrab
