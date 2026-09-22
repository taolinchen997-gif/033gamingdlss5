// =====================================================================
//  fg.h ——  帧生成
//
//  三条路, 各自的适用范围和障碍完全不同, 所以分成三个独立选项:
//
//   A  多倍帧生成 (40系)
//      给【本身就带 DLSS 帧生成】的游戏把倍数从 2x 提到 3x/4x。
//      做法: 钩 Streamline 的 slDLSSGSetOptions, 把 numFramesToGenerate 改大;
//      再钩 slDLSSGGetState 把 numFramesToGenerateMax 报高, 否则游戏自己会夹回去。
//      前提: 游戏目录里有 sl.dlss_g.dll / nvngx_dlssg.dll。
//
//   B  注入式帧生成
//      给【完全没有帧生成】的游戏加。必须包住交换链, ReShade 插件层做不到,
//      要由我们的 dxgi 代理自己上。工作量最大, 留待后续。
//
//   C  驱动平滑运动 (NVIDIA Smooth Motion)
//      驱动级, 零代码, 但只有 64 位进程 + 特定驱动/显卡能用
//      (驱动根本没有 32 位的 NvPresent, 32 位游戏永远没戏)。
//      我们只做探测和引导, 开关在 NVIDIA App 里。
//
//  本文件当前只做【探测】: 老老实实报告这台机器、这个游戏能走哪条,
//  不做任何改动。
// =====================================================================
#pragma once
#include "nr_distribution_policy.h"

namespace fg
{

struct Info
{
    bool  probed = false;

    // A
    bool        sl_present = false;      // 游戏带 Streamline / DLSS-FG
    std::string sl_what;                 // 找到的是哪个文件
    bool        ada = false;             // 40 系
    // ★显卡代数★ 20/30/40/50, 认不出是 0。多帧生成这件事三代三个待遇:
    //   50 系: 原厂就有, 什么都不用做
    //   40 系: 能解锁(我们已经并进来了) —— 驱动的 nvngx_dlssg 里有 sm_89 机器码
    //   20/30 系: ★真 MFG 永远不可能★ —— 那个运行库里【只有 sm_89】,
    //            没有 sm_75/sm_86, 而 PTX 只向前兼容, 模块根本加载不起来。
    //            (2026-09-05 我们自己解 fatbin 数过, 不是听说。)
    //            这两代要多帧只有一条路: 把游戏的 DLSS-FG 调用转接到 FSR3 上。
    int         nvgen = 0;
    std::string gpu;

    // C
    bool        proc64 = false;
    bool        nvpresent = false;       // 驱动带 NvPresent64(Smooth Motion 的实现)
};
static Info in;

static bool file_in_game(const char *name)
{
    const std::string p = game_dir() + "\\" + name;
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void probe()
{
    if (in.probed) return;
    in.probed = true;

    in.proc64 = (sizeof(void *) == 8);

    // ---- A: 游戏有没有带 DLSS 帧生成 ----
    static const char *kSl[] = { "sl.dlss_g.dll", "nvngx_dlssg.dll", "sl.interposer.dll" };
    for (const char *n : kSl)
    {
        if (file_in_game(n) || GetModuleHandleA(n) != nullptr)
        { in.sl_present = true; in.sl_what = n; break; }
    }

    // ---- 显卡型号: 用 DXGI 问, 不引 nvapi ----
    {
        IDXGIFactory1 *fac = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&fac))))
        {
            IDXGIAdapter1 *ad = nullptr;
            for (UINT i = 0; fac->EnumAdapters1(i, &ad) == S_OK; ++i)
            {
                DXGI_ADAPTER_DESC1 d = {};
                ad->GetDesc1(&d);
                if (d.VendorId == 0x10DE)      // NVIDIA
                {
                    char nm[256] = {};
                    WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, nm, sizeof(nm) - 1, nullptr, nullptr);
                    in.gpu = nm;
                    in.nvgen = nrdistribution033::NvidiaGeneration(in.gpu);
                    in.ada = in.nvgen == 40;
                    ad->Release();
                    break;
                }
                ad->Release();
            }
            fac->Release();
        }
    }

    // ---- C: 驱动里有没有 Smooth Motion 的实现 ----
    {
        char sys[MAX_PATH] = {};
        GetSystemDirectoryA(sys, MAX_PATH);
        const std::string p = std::string(sys) + "\\NvPresent64.dll";
        in.nvpresent = GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    Log("[fg] 探测: 显卡=%s %d系 40系=%d | DLSS-FG=%s | 64位=%d | NvPresent64=%d",
        in.gpu.c_str(), in.nvgen, in.ada ? 1 : 0,
        in.sl_present ? in.sl_what.c_str() : "无", in.proc64 ? 1 : 0, in.nvpresent ? 1 : 0);
}

// 三条路各自能不能用, 给面板显示
static const char *status_a()
{
    if (!in.sl_present) return "本游戏没有 DLSS 帧生成";
    if (!in.ada)        return "显卡不是 40 系";
    return "可用";
}
static const char *status_b() { return "尚未实现"; }
static const char *status_c()
{
    if (!in.proc64)    return "32 位游戏不支持";
    if (!in.nvpresent) return "驱动未提供";
    return "在 NVIDIA App 里开启";
}

} // namespace fg
