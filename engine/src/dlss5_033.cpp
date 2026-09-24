// 033 V6.1.3 runtime: one 033 control panel and a guarded NGX NR pipeline.
// This module initializes and evaluates models, records GPU work and changes
// game images when the selected route is admitted. It is not a handshake-only
// prototype. Native/API-bridge capabilities differ by the frozen role.
// ReShade, OptiScaler, NVIDIA and other upstream attribution remains in source
// and package notices. Offline build success does not establish game stability.
// Product owner: Bilibili @热心网友033. Version authority: release_version.h.
#include <Windows.h>
#include "engine_module.h"
#include "diagnostic_policy.h"
#include "render_core_client.h"
#include "nr_fault.h"
#include "nr_beta2_input.h"
#include <psapi.h>          // GetModuleInformation (snr.h 的 IAT 补丁)
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>   // ID3DBlob (scale.h 的着色器编译)
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>       // hostnr 的寄存式释放
#if defined(K033_MONOLITHIC_ENGINE)
#include "framegen_flow_service.h"
#endif

#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>

#include "ngx/nvsdk_ngx_params.h"

// ★构建指纹★ 编译期烧进二进制, 改不了(没源码就编不出新的)。
// 用途不是防盗 —— 免费包挡不住整包搬运 —— 而是【认版本】:
// 别人二次打包不会也改不掉这行, 观众发个面板截图, 就知道他手上那份
// 是哪次构建、是不是原版。日志里也打一份。
#if defined(K033_BETA2_RESHADE_HOST)
// 面板上给玩家看的版本号；内部谱系不往这里写。
#include "release_version.h"
#define K033_VER K033_ENGINE_VERSION
#elif defined(K033_MONOLITHIC_ENGINE)
#define K033_VER "v5.4.0 beta layered-NR 20260908"
#elif defined(K033_INTEGRATED_CANDIDATE)
#define K033_VER "v5.1 integrated candidate 20260906"
#elif defined(K033_OPTISCALER_PHASE1)
#define K033_VER "v5.0 OptiScaler phase1 candidate 20260906"
#elif defined(K033_FG_LATENCY_CANDIDATE)
#define K033_VER   "v5.0 FG routing+pacing candidate 20260906"
#elif defined(K033_STABILITY_CANDIDATE)
#define K033_VER   "v5.0 stability candidate 20260906"
#elif defined(K033_CAPABILITY_CANDIDATE)
#define K033_VER   "v5.0 capability candidate 20260906"
#else
#define K033_VER   "v5.0"
#endif
#define K033_BUILD (__DATE__ " " __TIME__)

extern "C" __declspec(dllexport) const char *NAME = "热心网友033";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "033 自研线控制台: 自己直连 NGX 的环境自检与握手。整理: B站 @热心网友033";

// ---------------------------------------------------------------- 配色
static const ImU32 COL_GOLD  = IM_COL32(255, 196,  84, 255);
static const ImU32 COL_GREEN = IM_COL32( 92, 205, 125, 255);
static const ImU32 COL_RED   = IM_COL32(233, 106, 106, 255);
static const ImU32 COL_GRAY  = IM_COL32(120, 124, 140, 255);
static const ImU32 COL_TEXT  = IM_COL32(238, 238, 245, 255);
static const ImU32 COL_SUB   = IM_COL32(168, 172, 185, 255);
static const ImU32 COL_CARD  = IM_COL32( 30,  32,  42, 255);
static const ImU32 COL_CARDB = IM_COL32( 58,  60,  72, 255);

// ------------------------------------------------------- NGX 入口点表
// ★第 5 个参数是 FeatureCommonInfo, 不是参数块★ 里面装着「去哪些文件夹找
//   feature dll」的清单(PathListInfo)。我们一直传 nullptr —— 于是核心只按默认
//   路径找, 而神经渲染那个 snippet(nvngx_dlssnr.dll)是我们自己带进游戏目录的,
//   核心根本不去那儿找 → 认识 feature 18 却没东西可以拿来建 → 0xBAD0000B。
//   RenoDX 的入口表里也正是 Init_Ext + AllocateParameters + CreateFeature 这三件套,
//   没有 GetScratchBufferSize / GetFeatureRequirements / UpdateFeature。
typedef NVSDK_NGX_Result (__cdecl *PFN_Init_Ext)(
    unsigned long long app_id, const wchar_t *data_path, ID3D12Device *dev,
    NVSDK_NGX_Version sdk_ver, const NVSDK_NGX_FeatureCommonInfo *info);
typedef NVSDK_NGX_Result (__cdecl *PFN_GetCapabilityParameters)(NVSDK_NGX_Parameter **out);
typedef NVSDK_NGX_Result (__cdecl *PFN_AllocateParameters)(NVSDK_NGX_Parameter **out);
typedef NVSDK_NGX_Result (__cdecl *PFN_DestroyParameters)(NVSDK_NGX_Parameter *p);
typedef NVSDK_NGX_Result (__cdecl *PFN_Shutdown1)(ID3D12Device *dev);
typedef NVSDK_NGX_Result (__cdecl *PFN_CreateFeature)(
    ID3D12GraphicsCommandList *cmd, NVSDK_NGX_Feature id,
    const NVSDK_NGX_Parameter *params, NVSDK_NGX_Handle **out);
typedef NVSDK_NGX_Result (__cdecl *PFN_ReleaseFeature)(NVSDK_NGX_Handle *h);
typedef NVSDK_NGX_Result (__cdecl *PFN_EvaluateFeature)(
    ID3D12GraphicsCommandList *cmd, const NVSDK_NGX_Handle *h,
    const NVSDK_NGX_Parameter *params, void *callback);

struct NgxEntries
{
    PFN_EvaluateFeature        evaluate = nullptr;
    HMODULE                    mod   = nullptr;
    PFN_Init_Ext               init  = nullptr;
    PFN_GetCapabilityParameters cap  = nullptr;
    PFN_AllocateParameters     alloc = nullptr;
    PFN_DestroyParameters      destroy = nullptr;
    PFN_Shutdown1              shutdown = nullptr;
    PFN_CreateFeature          create = nullptr;
    PFN_ReleaseFeature         release = nullptr;
    int  resolved = 0;                 // 解析成功的入口点个数
    int  missing_need = 0;             // 【真正用得上却缺了】的个数
    std::string core_dir;              // 注册表读到的核心目录
    std::string load_error;            // 加载失败原因
};

static NgxEntries g_ngx;

// ★核心路探针的结论★ 第 5 帧验一次: 核心到底能不能建 feature 18。
//   能建的话 nrfwd 就走核心路(跟 RenoDX 同一条), 不能就照旧走转发器。
static bool g_core_probe_ok = false;

// 握手结果
struct Handshake
{
    bool        ran        = false;
    bool        ok         = false;
    NVSDK_NGX_Result r_init = NVSDK_NGX_Result_Fail;
    NVSDK_NGX_Result r_cap  = NVSDK_NGX_Result_Fail;
    NVSDK_NGX_Result r_alloc = NVSDK_NGX_Result_Fail;
    NVSDK_NGX_Result r_shutdown = NVSDK_NGX_Result_Fail;
    std::string note;
};

static Handshake g_hs;

// -------------------------------------------------------- 日志 + 配置
// 日志写在游戏目录 dlss5-033.log, 每条立刻落盘 —— 游戏被杀也留得下证据。
static std::string game_dir()
{
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir(exe);
    const size_t slash = dir.find_last_of('\\');
    if (slash != std::string::npos)
        dir.resize(slash);
    return dir;
}

static void Log(const char *fmt, ...)
{
    static std::string path = game_dir() + "\\dlss5-033.log";
    FILE *f = nullptr;
    if (fopen_s(&f, path.c_str(), "ab") != 0 || f == nullptr)
        return;
    SYSTEMTIME t; GetLocalTime(&t);
    std::fprintf(f, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

// dlss5-033.cfg 里某个键是否等于 1 (autoprobe=1 / carrier=1 ...)
// 「这个键被显式写成 0 了吗」—— 用来做「默认开、出事能一键关」的总闸
static bool cfg_key_is_0(const char *key)
{
#ifdef K033_BETA2_RESHADE_HOST
    // Legacy per-game switches are not an authority for the managed package.
    // Active NR/FG settings arrive through the shared settings bridge.
    (void)key;return false;
#endif
    const std::string path = game_dir() + "\\dlss5-033.cfg";
    FILE *f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return false;
    const size_t klen = std::strlen(key);
    char line[256]; bool off = false;
    while (std::fgets(line, sizeof(line), f))
        if (std::strncmp(line, key, klen) == 0 && line[klen] == '=' && line[klen + 1] == '0') { off = true; break; }
    std::fclose(f);
    return off;
}

static bool cfg_key_is_1(const char *key)
{
#ifdef K033_BETA2_RESHADE_HOST
    // Never restore old autoprobe, snippet-hook or diagnostic requests from a
    // stale game-root file. Production settings use the shared typed bridge.
    (void)key;return false;
#endif
    const std::string path = game_dir() + "\\dlss5-033.cfg";
    FILE *f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr)
        return false;
    const size_t klen = std::strlen(key);
    char line[256]; bool on = false;
    while (std::fgets(line, sizeof(line), f))
        if (std::strncmp(line, key, klen) == 0 && line[klen] == '=' && line[klen + 1] == '1') { on = true; break; }
    std::fclose(f);
    return on;
}
static bool cfg_autoprobe() { return cfg_key_is_1("autoprobe"); }

// -------------------------------------------------- 注册表定位 NGX 核心
static std::string read_ngx_core_dir()
{
    HKEY  key   = nullptr;
    char  buf[MAX_PATH] = {};
    DWORD len   = sizeof(buf);
    DWORD type  = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
                      0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    const LSTATUS st = RegQueryValueExA(key, "FullPath", nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buf), &len);
    RegCloseKey(key);

    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return std::string(buf);
}

static void load_ngx_once()
{
    if (g_ngx.mod != nullptr || !g_ngx.load_error.empty())
        return;

    g_ngx.core_dir = read_ngx_core_dir();
    if (g_ngx.core_dir.empty())
    {
        g_ngx.load_error = "注册表里没有 NGXCore\\FullPath —— 驱动没装好, 或者不是 N 卡";
        return;
    }

    const std::string dll = g_ngx.core_dir + "\\_nvngx.dll";
    g_ngx.mod = LoadLibraryA(dll.c_str());
    DWORD firstError = g_ngx.mod == nullptr ? GetLastError() : 0;
    if (g_ngx.mod == nullptr)
    {
        // 游戏可能收紧了 DLL 搜索路径(SetDefaultDllDirectories), 核心的依赖就在它自己目录里:
        // 带上「从它自己目录找依赖」再试一次。(2026-09-13 死亡搁浅2 核心门没钩上)
        g_ngx.mod = LoadLibraryExA(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    if (g_ngx.mod == nullptr)
    {
        char tmp[512];
        std::snprintf(tmp, sizeof(tmp), "LoadLibrary 失败 (错误码 %lu, 带依赖目录再试 %lu): %s",
                      firstError, GetLastError(), dll.c_str());
        g_ngx.load_error = tmp;
        return;
    }

    g_ngx.init     = reinterpret_cast<PFN_Init_Ext>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_Init_Ext"));
    g_ngx.cap      = reinterpret_cast<PFN_GetCapabilityParameters>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_GetCapabilityParameters"));
    g_ngx.alloc    = reinterpret_cast<PFN_AllocateParameters>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_AllocateParameters"));
    g_ngx.destroy  = reinterpret_cast<PFN_DestroyParameters>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_DestroyParameters"));
    g_ngx.shutdown = reinterpret_cast<PFN_Shutdown1>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_Shutdown1"));
    g_ngx.create   = reinterpret_cast<PFN_CreateFeature>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_CreateFeature"));
    g_ngx.release  = reinterpret_cast<PFN_ReleaseFeature>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_ReleaseFeature"));
    g_ngx.evaluate = reinterpret_cast<PFN_EvaluateFeature>(GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_EvaluateFeature"));

    // ★分清「必需」和「有更好」★
    //   以前是数个数, 8 个不齐就整个停掉 —— 评论区两个人卡在「NGX入口点没解析全」。
    //   其中 Shutdown1 我们【从来不调】(调它会把游戏自己的 DLSS 会话一起拆掉,
    //   那是写在 do_handshake 上方的血泪), 老驱动没导出它就把人挡在门外, 纯冤枉。
    //   现在只认真正用得上的那几个, 而且缺哪个直接写进日志。
    struct { const char *name; void *p; bool need; } need_list[] = {
        { "Init_Ext",                 (void *)g_ngx.init,     true  },
        { "GetCapabilityParameters",  (void *)g_ngx.cap,      true  },
        { "AllocateParameters",       (void *)g_ngx.alloc,    true  },
        { "DestroyParameters",        (void *)g_ngx.destroy,  true  },
        { "CreateFeature",            (void *)g_ngx.create,   true  },
        { "ReleaseFeature",           (void *)g_ngx.release,  true  },
        { "EvaluateFeature",          (void *)g_ngx.evaluate, true  },
        { "Shutdown1",                (void *)g_ngx.shutdown, false },   // 我们不调, 缺了也无所谓
    };
    int got = 0, miss_need = 0;
    std::string missing;
    for (const auto &e : need_list)
    {
        if (e.p != nullptr) { ++got; continue; }
        if (e.need) { ++miss_need; }
        if (!missing.empty()) missing += ", ";
        missing += e.name;
        if (!e.need) missing += "(用不上)";
    }
    if (!missing.empty())
        Log("[033] NGX 核心少了这些入口: %s  —— 核心: %s", missing.c_str(), dll.c_str());
    g_ngx.missing_need = miss_need;
    g_ngx.resolved = got;
}

// ----------------------------------------------------------- 握手一次
static void do_handshake(ID3D12Device *dev)
{
    g_hs = Handshake{};
    g_hs.ran = true;

    if (dev == nullptr)          { g_hs.note = "拿不到原生 D3D12 设备"; return; }
    if (g_ngx.missing_need > 0)  { g_hs.note = "驱动 NGX 少了必需入口(见日志)"; return; }

    // 应用 ID: NGX 用它做遥测分组。0x0 不合法, 这里用一个固定的自有 ID。
    const unsigned long long kAppId = 0x4F33334ull;   // "O33"

    wchar_t data_path[MAX_PATH] = L".";
    GetCurrentDirectoryW(MAX_PATH, data_path);

    // ★★把「去哪找 snippet」的清单交上去★★ (2026-09-04 夜)
    //   这些必须是 static: 核心会把指针留下来, 栈上的活不过这一帧。
    static std::wstring          s_path_game;      // 游戏目录(我们的 nvngx_dlssnr.dll 就在这)
    static const wchar_t        *s_path_list[2] = { nullptr, nullptr };
    static NVSDK_NGX_FeatureCommonInfo s_fci = {};
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe);
        const size_t sl = dir.find_last_of(L'\\');
        if (sl != std::wstring::npos) dir.resize(sl);
        s_path_game = dir;
        s_path_list[0] = s_path_game.c_str();
        s_fci.PathListInfo.Path   = s_path_list;
        s_fci.PathListInfo.Length = 1;
        s_fci.InternalData        = nullptr;
        s_fci.LoggingInfo.LoggingCallback          = nullptr;
        s_fci.LoggingInfo.MinimumLoggingLevel      = NVSDK_NGX_LOGGING_LEVEL_OFF;
        s_fci.LoggingInfo.DisableOtherLoggingSinks = false;
    }
    g_hs.r_init = g_ngx.init(kAppId, data_path, dev, NVSDK_NGX_Version_API, &s_fci);
    Log("[2] Init_Ext 带路径清单: %ls  -> 0x%08X", s_path_game.c_str(),
        static_cast<unsigned>(g_hs.r_init));
    if (g_hs.r_init != NVSDK_NGX_Result_Success)
    {
        g_hs.note = "Init_Ext 没过 —— 后面几步不做了";
        return;
    }

    NVSDK_NGX_Parameter *cap_params = nullptr;
    g_hs.r_cap = g_ngx.cap(&cap_params);

    NVSDK_NGX_Parameter *my_params = nullptr;
    g_hs.r_alloc = g_ngx.alloc(&my_params);
    if (g_hs.r_alloc == NVSDK_NGX_Result_Success && my_params != nullptr)
        g_ngx.destroy(my_params);

    // ★★ 血泪教训 (20260902, 古墓丽影暗影实测) ★★
    // 这里【绝对不能】调 NVSDK_NGX_D3D12_Shutdown1(dev)。
    // 自带 DLSS 的游戏进程里【本来就有一个 NGX 会话】(游戏自己开的),
    // NGX 的会话是按设备算的、不是按调用者算的 —— 我们一 Shutdown,
    // 等于把游戏自己的 DLSS 会话也拆了, 游戏当场无声无息被杀
    // (没有崩溃转储、没有 Windows 错误事件、ReShade 日志也来不及记)。
    // 这也正是 renodx 选择【钩游戏的 NGX 调用】而不是自己开会话的原因。
    // 我们只 Init 不 Shutdown, 让进程退出时由驱动回收。
    g_hs.r_shutdown = NVSDK_NGX_Result_Success;

    g_hs.ok = (g_hs.r_init == NVSDK_NGX_Result_Success) &&
              (g_hs.r_alloc == NVSDK_NGX_Result_Success);
    g_hs.note = g_hs.ok ? "我们自己的代码已经能跟 NGX 对话了"
                        : "握手部分失败, 看下面的返回码";
}

// ═══════════════════════════════════════════════════════════════════
//  自己创建 NR 特性 (feature 18)
//
//  为什么这一步是「降开销」的钥匙:
//  喂帧路线的「工作分辨率滑块」不是让 NR 自己降分辨率(那是超分契约,
//  运行库会拒收), 而是【把整份契约建在更小的尺寸上】—— 开销 ∝ 像素数,
//  75% 省一半, 50% 只剩四分之一。直挂路线一直没有这根滑块, 唯一原因是
//  那条路上契约在 renodx 手里。我们自己建, 就自己说了算。
//
//  本版只【创建】不【求值】: 不碰游戏画面, 建完立刻释放, 只把返回码摆出来。
//  contract 是逆向出来的(61 条 DLSSNR.* 词表), 失败的返回码就是下一步的线索。
// ═══════════════════════════════════════════════════════════════════
struct NrProbe
{
    bool             ran   = false;
    NVSDK_NGX_Result code  = NVSDK_NGX_Result_Fail;
    unsigned int     w = 0, h = 0;      // 实际请求的契约尺寸
    unsigned long    seh   = 0;         // 结构化异常码(0 = 没炸)
    std::string      note;
};

static NrProbe g_nr;
static int     g_work_pct = 100;        // 工作分辨率百分比 50..100

// NGX 的 feature 18: 公开头文件里它只是个没名字的保留槽
static const NVSDK_NGX_Feature kFeatureNR = static_cast<NVSDK_NGX_Feature>(18);

// 裸调用单独包一层: 里面不放任何需要析构的 C++ 对象, 好让 SEH 干净地兜住
static NVSDK_NGX_Result raw_create(ID3D12GraphicsCommandList *cmd,
                                   NVSDK_NGX_Parameter *p,
                                   NVSDK_NGX_Handle **out,
                                   unsigned long *seh)
{
    NVSDK_NGX_Result r = NVSDK_NGX_Result_Fail;
    *seh = 0;
    __try
    {
        r = g_ngx.create(cmd, kFeatureNR, p, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *seh = GetExceptionCode();
    }
    return r;
}

// ── 直接问 NGX: feature 18 到底要什么 ────────────────────────────
// CreateFeature 只会甩一个 0xBAD0000C(版本过旧)给我们, 说不清是驱动、
// 硬件、系统还是压根没实现。GetFeatureRequirements 会把原因拆成位域。
typedef NVSDK_NGX_Result (__cdecl *PFN_GetFeatureRequirements)(
    IDXGIAdapter *adapter, const NVSDK_NGX_FeatureDiscoveryInfo *info,
    NVSDK_NGX_FeatureRequirement *out);

struct NrReq
{
    bool             ran = false;
    NVSDK_NGX_Result code = NVSDK_NGX_Result_Fail;
    unsigned int     支持位 = 0xFFFFFFFFu;
    unsigned int     min_arch = 0;
    std::string      min_os;
    std::string      note;
};

static NrReq g_req;

static void probe_requirements()
{
    g_req = NrReq{};
    g_req.ran = true;

    PFN_GetFeatureRequirements fn = nullptr;
    if (g_ngx.mod != nullptr)
        fn = reinterpret_cast<PFN_GetFeatureRequirements>(
            GetProcAddress(g_ngx.mod, "NVSDK_NGX_D3D12_GetFeatureRequirements"));
    if (fn == nullptr) { g_req.note = "这版驱动没导出 GetFeatureRequirements"; return; }

    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))
    { g_req.note = "建不了 DXGI 工厂"; return; }

    IDXGIAdapter1 *adapter = nullptr;
    if (FAILED(factory->EnumAdapters1(0, &adapter)))
    { factory->Release(); g_req.note = "枚举不到显卡"; return; }

    wchar_t data_path[MAX_PATH] = L".";
    GetCurrentDirectoryW(MAX_PATH, data_path);

    NVSDK_NGX_FeatureCommonInfo common = {};
    NVSDK_NGX_FeatureDiscoveryInfo info = {};
    info.SDKVersion   = NVSDK_NGX_Version_API;
    info.FeatureID    = kFeatureNR;
    info.Identifier.IdentifierType   = NVSDK_NGX_Application_Identifier_Type_Application_Id;
    info.Identifier.v.ApplicationId  = 0x4F33334ull;
    info.ApplicationDataPath = data_path;
    info.FeatureInfo  = &common;

    NVSDK_NGX_FeatureRequirement out = {};
    g_req.code = fn(adapter, &info, &out);
    if (g_req.code == NVSDK_NGX_Result_Success)
    {
        g_req.支持位  = static_cast<unsigned int>(out.FeatureSupported);
        g_req.min_arch = out.MinHWArchitecture;
        g_req.min_os   = out.MinOSVersion;
        g_req.note = (g_req.支持位 == 0) ? "★NGX 说 feature 18 在这台机器上是支持的★"
                                          : "NGX 给出了不支持的原因(见下)";
    }
    else
    {
        g_req.note = "查询本身就失败了";
    }

    adapter->Release();
    factory->Release();
}

static void probe_nr(ID3D12Device *dev, ID3D12GraphicsCommandList *cmd,
                     unsigned int out_w, unsigned int out_h)
{
    g_nr = NrProbe{};
    g_nr.ran = true;

    if (dev == nullptr || cmd == nullptr) { g_nr.note = "没拿到设备或命令列表"; return; }
    if (g_ngx.missing_need > 0)           { g_nr.note = "驱动 NGX 少了必需入口(见日志)"; return; }
    if (g_hs.r_init != NVSDK_NGX_Result_Success) { g_nr.note = "先点上面的「开始握手」"; return; }

    // 工作分辨率: 契约建在这个尺寸上, 开销按面积走
    g_nr.w = static_cast<unsigned int>(out_w * g_work_pct / 100);
    g_nr.h = static_cast<unsigned int>(out_h * g_work_pct / 100);
    if (g_nr.w < 64 || g_nr.h < 64) { g_nr.note = "尺寸算出来太小"; return; }

    NVSDK_NGX_Parameter *p = nullptr;
    if (g_ngx.alloc(&p) != NVSDK_NGX_Result_Success || p == nullptr)
    {
        g_nr.note = "参数块分配失败";
        return;
    }

    // 通用 NGX 建特性参数
    p->Set("CreationNodeMask",   1u);
    p->Set("VisibilityNodeMask", 1u);
    // NR 自己的契约 (键名取自运行库内的 61 条词表)
    p->Set("DLSSNR.Width",   g_nr.w);
    p->Set("DLSSNR.Height",  g_nr.h);
    p->Set("DLSSNR.Enabled", 1u);
    p->Set("DLSSNR.Style",     0u);
    p->Set("DLSSNR.Intensity", 2.0f);   // 词表里 Intensity 是浮点, 探针也照浮点写

    NVSDK_NGX_Handle *h = nullptr;
    g_nr.code = raw_create(cmd, p, &h, &g_nr.seh);

    if (g_nr.seh != 0)
    {
        g_nr.note = "创建时抛了结构化异常 —— 契约缺东西, 看异常码";
    }
    else if (g_nr.code == NVSDK_NGX_Result_Success && h != nullptr)
    {
        g_nr.note = "★创建成功★ 工作分辨率归我们说了算了";
        g_ngx.release(h);          // 只是验证, 立刻还回去
    }
    else
    {
        g_nr.note = "创建被拒 —— 返回码就是下一步的线索";
    }

    g_ngx.destroy(p);
}

// ── 游戏自带帧生成检测 ────────────────────────────────────────────
// 策略(20260902 定案): 有自带 FG 就让路, 没有才补我们的, 永远不叠加。
// 判据: 进程里加载了 DLSS-FG 运行库(nvngx_dlssg.dll) 或 Streamline 的 FG 插件(sl.dlss_g.dll)。
struct GameFG
{
    bool checked = false;
    bool dlssg   = false;   // nvngx_dlssg.dll 在进程里
    bool sl_g    = false;   // sl.dlss_g.dll 在进程里
    bool sl_core = false;   // sl.interposer.dll 在(说明游戏走 Streamline)
};
static GameFG g_fg;

static void check_game_fg()
{
    g_fg.checked = true;
    g_fg.dlssg   = GetModuleHandleW(L"nvngx_dlssg.dll")   != nullptr;
    g_fg.sl_g    = GetModuleHandleW(L"sl.dlss_g.dll")     != nullptr;
    g_fg.sl_core = GetModuleHandleW(L"sl.interposer.dll") != nullptr;
}

// ── 对照实验: 用同一套管线建一个 DLAA (feature 1) ──────────────────
// 喂帧模块干的就是这件事(Feeder 源码 1526-1545 行的契约照抄)。
// 如果 DLAA 能建成而 18 建不成, 就证明管线没问题, 是 18 本身不能独立创建。
struct DlaaProbe
{
    bool             ran  = false;
    NVSDK_NGX_Result code = NVSDK_NGX_Result_Fail;
    unsigned long    seh  = 0;
    unsigned int     w = 0, h = 0;
};

static DlaaProbe g_dlaa;

static NVSDK_NGX_Result raw_create_id(ID3D12GraphicsCommandList *cmd, NVSDK_NGX_Feature id,
                                      NVSDK_NGX_Parameter *p, NVSDK_NGX_Handle **out,
                                      unsigned long *seh)
{
    NVSDK_NGX_Result r = NVSDK_NGX_Result_Fail;
    *seh = 0;
    __try { r = g_ngx.create(cmd, id, p, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh = GetExceptionCode(); }
    return r;
}

static void probe_dlaa(ID3D12GraphicsCommandList *cmd, unsigned int w, unsigned int h)
{
    g_dlaa = DlaaProbe{};
    g_dlaa.ran = true;
    g_dlaa.w = w; g_dlaa.h = h;
    if (cmd == nullptr || g_hs.r_init != NVSDK_NGX_Result_Success) return;

    NVSDK_NGX_Parameter *p = nullptr;
    if (g_ngx.alloc(&p) != NVSDK_NGX_Result_Success || p == nullptr) return;

    const unsigned int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                               NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    p->Set(NVSDK_NGX_Parameter_CreationNodeMask,   1u);
    p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    p->Set(NVSDK_NGX_Parameter_Width,     w);
    p->Set(NVSDK_NGX_Parameter_Height,    h);
    p->Set(NVSDK_NGX_Parameter_OutWidth,  w);
    p->Set(NVSDK_NGX_Parameter_OutHeight, h);
    p->Set(NVSDK_NGX_Parameter_PerfQualityValue,
           static_cast<unsigned int>(NVSDK_NGX_PerfQuality_Value_DLAA));
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
    p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0u);

    NVSDK_NGX_Handle *hnd = nullptr;
    g_dlaa.code = raw_create_id(cmd, NVSDK_NGX_Feature_SuperSampling, p, &hnd, &g_dlaa.seh);
    if (g_dlaa.seh == 0 && g_dlaa.code == NVSDK_NGX_Result_Success && hnd != nullptr)
        g_ngx.release(hnd);
    g_ngx.destroy(p);
}

// ── 自动诊断: 进游戏后自己跑一遍, 全部写日志 ────────────────────────
// 前向声明, 实现在下面
static void probe_requirements();
static int  g_frames = 0;
static bool g_autoprobe_done = false;

#include "safemode.h"
#include "snr.h"       // 签名运行库加载器(IAT 补丁, 自研 NR 内核实验) —— 用到 g_ngx/game_dir/Log, 都在上面
#include "nrscale.h"   // 模型分辨率杠杆 · 探针 —— 用到 snr::patch_iat 和上面的 PFN_/game_dir
static bool g_snr_tried = false;

// handover 状态机在下面才包进来(它要用 carrier/hostnr), 这里隔一层函数指针,
// 跟 g_after_eval / nrscale::g_is_ours 一个套路。DllMain 里填上。
static void (*g_handover_pump)() = nullptr;

// Mark the last callback before ReShade flushes its immediate command list.
// Bounded diagnostics only; no waits, resets, extra submissions or fallback.
static void on_present(reshade::api::effect_runtime *runtime)
{
    static unsigned seen=0;const bool trace=diagnostic033::StartupTraces && seen++<8;
    struct End {bool active;~End(){if(active)Log("[033 startup] legacy present callback returned tid=%lu",GetCurrentThreadId());}} end{trace};
    if(trace)Log("[033 startup] legacy present callback begin runtime=%p tid=%lu",runtime,GetCurrentThreadId());
    // ★这两行必须在最前面★
    //   下面有好几处提前 return(autoprobe 做完就 return 等等), 放在它们后面
    //   等于永远不执行 —— 实测: 游戏好好跑了 120 秒, 账本却还停在 1,
    //   于是下一局被误判成「又崩了一次」, 三局就把自己关掉了。
    safemode::tick();              // 活够一分钟就把「异常退出」的账清掉
    if (safemode::off()) return;   // 一级: 这一局我们什么都不做

    nrscale::tick();                                  // feature 18 账本的帧计数
    if (g_handover_pump != nullptr) g_handover_pump(); // 换引擎的分帧交接


    // 模型分辨率杠杆: Detours 钩住 NGX 核心里 CreateFeature/EvaluateFeature 的
    // 函数本体。钩的是机器码不是导入表, 所以 renodx 什么时候解析的都无所谓。
    // ★必须先 load_ngx_once★ —— 拿到核心模块和函数地址才有东西可钩;
    //   以前它只在打开面板时才调, 用户不开面板就永远装不上钩子。
    // ★★★ 默认全关 ★★★
    //
    // 血泪教训(古墓丽影实测, 时间线铁证):
    //   06:57:57.947  renodx 建 feature 18   -> 成功
    //   06:57:57.963  第一次求值             -> 成功
    //   06:57:58.034  ★我们钩住 nvngx_dlssnr.dll★
    //   06:57:59.061  下一次求值             -> 0xBAD00002, NR 从此暂停
    //
    // Detours 会改写函数入口的机器码, 而这个运行库是社区改版、由 renodx 用
    // "签名 snippet" 机制加载的 —— 它自己校验代码完整性, 一改就挂。
    // 也就是说: 这个运行库钩不得。
    //
    // 所以两个钩子都改成【必须在 cfg 里显式打开】, 默认一个都不装。
    // 玩家的神经渲染能正常工作, 比我们这个实验功能重要得多。
    // ★薄壳 nvngx.dll / 核心 _nvngx.dll 这两扇门: 默认就装★
    //   它们本来就是就地插入天天在用的那两个钩子(inject.h 里无条件 install()),
    //   安全性早被几十个游戏验过。现在换引擎的 feature 18 账本也靠它们 ——
    //   没有账本, 交接就是闭着眼睛抢, 那正是两次掉显卡的原因。
    if (!nrscale::installed) { load_ngx_once(); nrscale::install(); }
    // ★运行库 nvngx_dlssnr.dll 那扇门: 永远不钩★
    //   它是社区签名版, 自校验代码完整性, Detours 改一个字节就挂。
    //   代价是 renodx 建/放 feature 18 我们看不见 —— 所以「主插件 → 033」
    //   那个方向只能靠 handover 的静置期兜, 不能靠账本。明知有洞, 但不能补。
    if (!nrscale::snip_hooked && cfg_key_is_1("nrscale_snip"))
        nrscale::install_snippet();

    // 签名运行库补丁: 越早越好(必须赶在 NGX 首次触碰 feature 18 之前), 只在 snr_patch=1 时。
    if (!g_snr_tried && cfg_key_is_1("snr_patch"))
    {
        g_snr_tried = true;
        load_ngx_once();                        // 让 snr::pick_signed_target 能读到 g_ngx.core_dir
        const bool ok = snr::apply();
        Log("[snr] 补丁: %s | 已加载=%d 改写thunk=%d 重定向目标=%s | %s",
            ok ? "就位" : "未成", snr::g_patch.loaded, snr::g_patch.hooked,
            snr::g_patch.target.c_str(), snr::g_patch.note.c_str());
    }

    // ══════════════════════════════════════════════════════════════
    //  ★核心路一次性探针★ (2026-09-04 夜)
    //  问题只有一个: 给 Init_Ext 交上「去哪找 snippet」的清单之后,
    //  核心的 CreateFeature(18) 还回不回 0xBAD0000B。
    //  ★必须趁早跑★ —— 全进程只许有一个 feature 18, 等我们自己的引擎
    //  建起来就没法验了(旧的 autoprobe 正是因此被 carrier=1 挡掉的)。
    //  第 5 帧, 游戏还没调过 DLSS, 场上一个 feature 都没有, 建完立刻还。
    // ══════════════════════════════════════════════════════════════
    {
        static bool core_probe_done = false;
        // ★只在我们自己的引擎真的在驾驶时才跑★ (2026-09-05, 鬼武者 DEVICE_HUNG 之后)
        //   探针会在游戏进程里对 NGX 核心做 Init_Ext + CreateFeature(18)。
        //   那是我们自己记过的禁忌动作(20260903「补核心 Init 会把显卡整掉线」)。
        //   033 引擎目前是灰的、谁都选不了, carrier 一律为 0 —— 这种时候
        //   在主插件的渲染路上做这种动作, 全是风险没有收益。
        if (!core_probe_done && g_frames == 5 && cfg_key_is_1("carrier"))
        {
            core_probe_done = true;
            load_ngx_once();
            reshade::api::device *d0 = runtime->get_device();
            if (d0 != nullptr && d0->get_api() == reshade::api::device_api::d3d12)
            {
                ID3D12Device *nat = reinterpret_cast<ID3D12Device *>(d0->get_native());
                ID3D12GraphicsCommandList *cl0 = nullptr;
                if (reshade::api::command_queue *q0 = runtime->get_command_queue())
                    if (reshade::api::command_list *c0 = q0->get_immediate_command_list())
                        cl0 = reinterpret_cast<ID3D12GraphicsCommandList *>(c0->get_native());
                uint32_t pw = 0, ph = 0; runtime->get_screenshot_width_and_height(&pw, &ph);
                Log("==== 核心路探针 ==== 设备 %p 命令列表 %p 画面 %ux%u", nat, cl0, pw, ph);
                Log("     握手状态: Init_Ext 0x%08X | 分配参数 0x%08X",
                    static_cast<unsigned>(g_hs.r_init), static_cast<unsigned>(g_hs.r_alloc));
                for (int pct : { 100, 75 })
                {
                    g_work_pct = pct;
                    probe_nr(nat, cl0, pw, ph);
                    Log("     ★核心 CreateFeature(18) @%d%% %ux%u -> 0x%08X | SEH 0x%08lX | %s",
                        pct, g_nr.w, g_nr.h, static_cast<unsigned>(g_nr.code), g_nr.seh,
                        g_nr.note.c_str());
                    if (g_nr.code == NVSDK_NGX_Result_Success && g_nr.seh == 0)
                        g_core_probe_ok = true;
                }
                Log("==== 核心路探针结束: %s ====",
                    g_core_probe_ok ? "★能建 —— 这一局走核心路(RenoDX 那条)★"
                                    : "建不起, 照旧走转发器");
            }
            else Log("==== 核心路探针: 不是 D3D12, 跳过 ====");
        }
    }

    if (g_autoprobe_done) return;
    if (++g_frames < 240) return;              // 等画面稳定几秒再动手
    g_autoprobe_done = true;
    if (!cfg_autoprobe()) return;
    // 排雷(设计复核指出): autoprobe 会在 ReShade 的 immediate 列表上 Create 一个 DLAA 再立刻 Release,
    // 与载体管线同场是 GPU 危险动作。载体开着就一律不跑 autoprobe。
    if (cfg_key_is_1("carrier")) { Log("autoprobe 跳过: 载体管线已启用, 两者不能同场"); return; }

    Log("==== autoprobe 开始 (033 v0.2) ====");
    load_ngx_once();
    Log("NGX 核心: %s | 入口点 %d/7 | %s", g_ngx.core_dir.c_str(), g_ngx.resolved,
        g_ngx.load_error.empty() ? "ok" : g_ngx.load_error.c_str());

    reshade::api::device *dev = runtime->get_device();
    const bool is12 = dev->get_api() == reshade::api::device_api::d3d12;
    ID3D12Device *native = is12 ? reinterpret_cast<ID3D12Device *>(dev->get_native()) : nullptr;
    Log("渲染 API: %s | 原生设备 %p", is12 ? "D3D12" : "非 D3D12", native);

    uint32_t bw = 0, bh = 0;
    runtime->get_screenshot_width_and_height(&bw, &bh);
    Log("输出尺寸: %ux%u", bw, bh);

    check_game_fg();
    Log("游戏自带帧生成: nvngx_dlssg=%d sl.dlss_g=%d sl.interposer=%d → %s",
        g_fg.dlssg, g_fg.sl_g, g_fg.sl_core,
        (g_fg.dlssg || g_fg.sl_g) ? "让路" : "可补我们的");

    // 1) 只读查询, 最安全, 先做
    probe_requirements();
    Log("[1] GetFeatureRequirements(18): 返回 0x%08X | 支持位域 0x%X | 最低架构 0x%X | 最低系统 '%s' | %s",
        static_cast<unsigned int>(g_req.code), g_req.支持位, g_req.min_arch,
        g_req.min_os.c_str(), g_req.note.c_str());

    // 2) 握手
    do_handshake(native);
    Log("[2] 握手: Init_Ext 0x%08X | 能力参数 0x%08X | 分配参数 0x%08X | %s",
        static_cast<unsigned int>(g_hs.r_init), static_cast<unsigned int>(g_hs.r_cap),
        static_cast<unsigned int>(g_hs.r_alloc), g_hs.note.c_str());

    ID3D12GraphicsCommandList *cmd = nullptr;
    if (reshade::api::command_queue *q = runtime->get_command_queue())
        if (reshade::api::command_list *cl = q->get_immediate_command_list())
            cmd = reinterpret_cast<ID3D12GraphicsCommandList *>(cl->get_native());
    Log("命令列表: %p", cmd);

    // 3) feature 18 @100% 和 @75%
    for (int pct : {100, 75})
    {
        g_work_pct = pct;
        probe_nr(native, cmd, bw, bh);
        Log("[3] CreateFeature(18) @%d%% %ux%u: 返回 0x%08X | SEH 0x%08lX | %s",
            pct, g_nr.w, g_nr.h, static_cast<unsigned int>(g_nr.code), g_nr.seh, g_nr.note.c_str());
    }

    // 4) 对照: DLAA (feature 1) —— 最后做, 风险最高
    probe_dlaa(cmd, bw, bh);
    Log("[4] CreateFeature(1=DLAA) %ux%u: 返回 0x%08X | SEH 0x%08lX",
        g_dlaa.w, g_dlaa.h, static_cast<unsigned int>(g_dlaa.code), g_dlaa.seh);

    // 5) ★关键实验★ 绕开核心, 直接调 dlssnr 自己的 Init+CreateFeature 建 feature 18
    //    (核心在碰 dlssnr 前就回 OutOfDate; renodx 就是走这条直连路)
    if (snr::g_patch.hooked >= 1)
    {
        NVSDK_NGX_Parameter *dparams = nullptr;
        if (g_ngx.alloc(&dparams) == NVSDK_NGX_Result_Success && dparams != nullptr)
        {
            snr::probe_direct(native, cmd, dparams, bw, bh);
            Log("[5] dlssnr 直连 feature 18: Init_Ext 0x%08X | CreateFeature 0x%08X | SEH 0x%08lX | %s",
                static_cast<unsigned int>(snr::g_direct.r_init),
                static_cast<unsigned int>(snr::g_direct.r_create),
                snr::g_direct.seh, snr::g_direct.note.c_str());
            g_ngx.destroy(dparams);
        }
        else Log("[5] dlssnr 直连: 参数块分配失败");
    }
    else Log("[5] dlssnr 直连: 跳过(snr 补丁没就位)");

    Log("==== autoprobe 结束 ====");
}

// ═══════════════════════════════════════════════════════════════════
//  v0.3 载体管线: 缩小 → 自建 DLAA 契约(renodx 插 NR) → 放大回写
// ═══════════════════════════════════════════════════════════════════
static bool file_contains(const std::string &path, const char *needle);   // 实现在下面的运行库自检段
#include "fg.h"        // 帧生成: 三条路的探测(A 40系多倍 / B 注入式 / C 驱动平滑运动)
#include "smoothmotion.h"  // 驱动级插帧(Smooth Motion): 给本身没有帧生成的游戏兜底
#include "overlaygrab.h"  // 把别家插件的标签截下来, 画进我们这一页(业主要求"一个面板")
#include "nrfwd.h"     // ★神经渲染转发器★ —— 过运行库的调用方门禁, 让我们能自己调 feature 18
#include "gputime_fenced.h" // Only publish samples confirmed by their submitting queue's fence.
// ReShade 官方 examples/utils 的状态跟踪(BSD-3/MIT, 见 reshade_utils/LICENSE-reshade-examples.md)
//   单文件编译: 把 .cpp 也包进来; 套一层 namespace 是因为它文件顶上 using namespace reshade::api
// ★标准头必须在 namespace 外先包一遍★ —— 官方文件里自己 #include <vector>/<unordered_map>,
//   要是第一次被包是在 namespace rsu 里, 整个 std 就被塞进 rsu::, <cmath> 当场炸(实测 C2061 conditional_t)。
//   先在外面包过, 里面的 #include 就被 include guard 变成空操作。
#include <vector>
#include <unordered_map>
namespace rsu {
#include "reshade_utils/state_tracking.hpp"
#include "reshade_utils/state_tracking.cpp"
}
#define STATERESTORE_LOG Log
#include "staterestore.h"   // 状态信封: 就地插入路借了游戏的计算态要还 —— 必须在 carrier(load_cfg 读它的档位) 和 hostnr 之前
#include "carrier.h"
#include "exposure.h"  // 游戏 DLSS 曝光纹理 -> 自有 1x1 同帧白点；依赖 carrier 的状态配置
#include "facebox.h"   // 人脸包围盒探测 —— 用到 scale:: 的编译器和 Barrier, 必须在 carrier(它包了 scale.h) 之后
#include "resolve_leases.h"
#ifdef K033_BETA2_RESHADE_HOST
// The fork calls this after actual Create/Reset success with its native pair.
// No ReShade SDK call, model work or duplicate lease-reset notification here.
extern "C" __declspec(dllexport) int __cdecl K033_Beta2BindCommandAllocator(uint32_t version,
    ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator)
{
    try{return resolveleases::BindCommandAllocator(version,list,allocator);}
    catch(...){return -3;}
}
#include "beta2_grade_host.h"
#endif
#include "exposure_frames.h"
#include "yanyun_dual_renderer.h"
#include "matched_capture.h"
#include "nr_input_policy.h"
#include "hostnr.h"    // 老游戏(32位)路线: 在 Feeder 帮手进程里做神经渲染
#include "nr_backbuffer.h"
extern "C" __declspec(dllexport) int __cdecl K033_WantsNativeGuides(){return nrgame033::wanted.load()?1:0;}
#include "inject.h"    // 就地插入: 钩住游戏自己的 DLSS, 在那里做神经渲染
extern "C" __declspec(dllexport) void __cdecl K033_RequestCapture(){matchedcapture::Request();}
extern "C" __declspec(dllexport) int __cdecl K033_AfterUpscale(const k033core::Frame* frame)
{
    if(nrfault033::Blocked())return 0;
#ifdef K033_MONOLITHIC_ENGINE
    if(!engine033::host_ready.load(std::memory_order_acquire))return 0;
#endif
    if (!k033core::Valid(frame) || safemode::off()) return 0;
#ifdef K033_BETA2_RESHADE_HOST
    // An API bridge is not evidence that the frame's semantic inputs came
    // from the game. Keep unqualified bridges out of the native NR path.
    if(frame->source!=k033core::Dx12){nrbeta2::Note(nrbeta2::State::UnverifiedBridge);return 0;}
#endif
    if (!rendercore::Detect() || !rendercore::Api()->claim(k033core::Host033)) return 0;
    nrdispatch::AfterScope writer;
    if (!writer.entered) return 0;
#ifdef K033_BETA2_RESHADE_HOST
    // S54: independent grade consumes this successful SR O before NR gates.
    // A partial copy poisons O and must prevent subsequent NR commands too.
    const auto graded=yanyundual::enabled?beta2grade::Result{}:beta2gradehost::Process(*frame);
    if(graded.output_uncertain)return 0;
    if(!carrier::cfg.enabled)return 0;
    hostnr::InputScope gradeInput(frame,graded.recorded);
#else
    if(!carrier::cfg.enabled)return 0;
#endif
    if(!nrinput033::ownership.Claim(nrinput033::Route::Upscale))return 0;
    struct Scope { bool saved; Scope():saved(rendercore::in_callback){rendercore::in_callback=true;}
        ~Scope(){rendercore::in_callback=saved;} } scope;
    inject::s_on=true;
    hostnr::s_defer_build=true;
    nrscale::FeatureInfo info;info.id=frame->feature;info.flags=frame->flags;info.haveFlags=frame->haveFlags!=0;
    info.outputW=frame->outputW;info.outputH=frame->outputH;
    return inject::AfterGameDlss(static_cast<ID3D12GraphicsCommandList*>(frame->command),
        static_cast<NVSDK_NGX_Parameter*>(frame->parameters),reinterpret_cast<const NVSDK_NGX_Handle*>(frame->stream),&info);
}

#include "handover.h" // 游戏内换引擎: 分帧交接状态机 —— 用到 carrier/hostnr, 必须在它们之后
// ★多帧生成解锁★ 并进来的 MFG Unlock(MIT, ImDreamt/mavismmg, 源自 dashdogy 的研究)。
//   不当独立 addon 跑 —— 作者自己的兼容表就写着「两个 addon 同时加载」会出事。
//   它自带匿名 namespace, 尾部的 attach/detach/draw 是我们加的接线层。
// 2026-09-12 移植：这条血统本来不认识随包转接件，而面板要按「转接件在不在」决定显示哪张帧生成卡片。
#include "mfg2030_bridge.h"
#include "mfg/mfg_body.inl"
#include "yanyun_sr_model.h" // S32: 超分模型 (the game's own DLSS preset), stored per user
#ifdef K033_BETA2_RESHADE_HOST
extern "C" int __cdecl K033_Beta2InitializeVendor();
// The managed ReShade factory bootstrap calls this after shared settings are
// prepared and before forwarding the game's first DXGI factory request.
// early_attach installs the preserved native FG hooks exactly once; event
// registration remains in K033_ReShadeEntry after register_addon succeeds.
extern "C" __declspec(dllexport) int __cdecl K033_Beta2EarlyInitialize(){
    const auto self=engine033::Module();if(!self)return -1;
    // ReShade SDK caches the first owner argument forever. Early MFG logging
    // queries it without an argument, so prime our actual owner BEFORE vendor
    // initialization or any MFG/SDK call. Never replace a foreign cached owner.
    if(reshade::internal::get_current_module_handle(self)!=self)return -2;
    const int result=K033_Beta2InitializeVendor();if(result!=0)return result;
    srmodel033::ApplyStored(); // S32: before the game creates DLSS; the vendor config exists now
    mfg::early_attach(self);return 0;
}
#endif
#include "nr_controls.h"
#include "yanyun_recipe_store.h"
#include "fgen.h"      // 注入式帧生成(B) —— 用到 carrier 的命令列表/纹理助手, 必须在它后面

// -------------------------------------------------------- 「这是我们自己的 feature 吗」
// ★为什么必须按句柄认★
//   钩核心(_nvngx.dll)时, 我们自己每帧的调用也会撞进钩子里。要是把它当成
//   「游戏在用 DLSS」, 交换链就会把 feature 18 让给就地插入, 而插入那边其实
//   没有真正的游戏 DLSS 可插 —— 两边都不做, 用户一点效果都没有。
//   这三个句柄就是我们自己的全部: 交换链的 DLAA、交换链的 NR、就地插入那份。
static bool is_our_feature(const void *h)
{
    if (h == nullptr) return false;
    return h == static_cast<const void *>(carrier::g.feature)
        || h == static_cast<const void *>(carrier::g.nr_feat)
        || h == hostnr::feature();
}

// -------------------------------------------------------- 运行库自检
struct RuntimeInfo
{
    bool        checked = false;
    bool        present = false;
    long long   size    = 0;
    bool        has_global_tone = false;   // 4.70 新旋钮, 老运行库没有
    std::string path;
};

static RuntimeInfo g_rt;

// 在一段字节里找子串 (二进制安全, 不能用 strstr)
static const unsigned char *find_bytes(const unsigned char *hay, size_t hn,
                                       const char *nee, size_t nn)
{
    if (nn == 0 || hn < nn)
        return nullptr;
    for (size_t i = 0; i + nn <= hn; ++i)
        if (hay[i] == static_cast<unsigned char>(nee[0]) &&
            memcmp(hay + i, nee, nn) == 0)
            return hay + i;
    return nullptr;
}

static bool file_contains(const std::string &path, const char *needle)
{
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;

    const size_t nlen = strlen(needle);
    const DWORD  kChunk = 1 << 20;
    std::string  buf;
    buf.resize(kChunk + nlen);

    bool  found = false;
    DWORD got   = 0;
    size_t carry = 0;
    while (ReadFile(f, &buf[carry], kChunk, &got, nullptr) && got > 0)
    {
        const size_t total = carry + got;
        if (find_bytes(reinterpret_cast<const unsigned char *>(buf.data()),
                       total, needle, nlen)) { found = true; break; }
        carry = (total >= nlen - 1) ? nlen - 1 : total;
        memmove(&buf[0], buf.data() + total - carry, carry);
    }
    CloseHandle(f);
    return found;
}

static void check_runtime()
{
    if (g_rt.checked)
        return;
    g_rt.checked = true;

    char exe[MAX_PATH] = {};
    GetModuleFileNameA(
#ifdef K033_BETA2_RESHADE_HOST
        engine033::Module(),
#else
        nullptr,
#endif
        exe, MAX_PATH);
    std::string dir(exe);
    const size_t slash = dir.find_last_of('\\');
    if (slash != std::string::npos)
        dir.resize(slash);

    g_rt.path = dir + "\\nvngx_dlssnr.dll";

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExA(g_rt.path.c_str(), GetFileExInfoStandard, &fad))
        return;

    g_rt.present = true;
    g_rt.size = (static_cast<long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    g_rt.has_global_tone = file_contains(g_rt.path, "DLSSNR.GlobalToneStrength");
}

// ------------------------------------------------------------ 小部件
static ImVec2 card_begin(float h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), COL_CARD, 8.0f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), COL_CARDB, 8.0f);
    return p;
}

static void kv(const char *k, const std::string &v, ImU32 col)
{
    const float fs = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
    ImGui::TextColored(ImColor(COL_SUB), "%s", k);
    ImGui::SameLine(fs * 12.0f);
    ImGui::TextColored(ImColor(col), "%s", v.c_str());
}

static std::string hex(unsigned int v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

static std::string result_text(NVSDK_NGX_Result r)
{
    if (r == NVSDK_NGX_Result_Success)
        return "成功 (0x1)";
    return "失败 " + hex(static_cast<unsigned int>(r));
}

// -------------------------------------------------------------- 面板
[[maybe_unused]] static void draw_diagnostics(reshade::api::effect_runtime *runtime)
{
    load_ngx_once();
    check_runtime();

    reshade::api::device *dev = runtime->get_device();
    const bool is_d3d12 = dev->get_api() == reshade::api::device_api::d3d12;
    ID3D12Device *native = is_d3d12
        ? reinterpret_cast<ID3D12Device *>(dev->get_native())
        : nullptr;

    const float fs = ImGui::GetFontSize();

    // ── 状态灯 ────────────────────────────────────────────────
    {
        const float h = fs * 3.0f;
        const ImVec2 p = card_begin(h);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        const bool ready = (g_ngx.resolved == 7) && is_d3d12;
        dl->AddCircleFilled(ImVec2(p.x + fs * 1.6f, p.y + h * 0.5f), fs * 0.8f,
                            ready ? COL_GREEN : COL_RED);
        dl->AddText(ImVec2(p.x + fs * 3.0f, p.y + h * 0.5f - fs - fs * 0.1f), COL_TEXT,
                    ready ? "自研通道: 就绪" : "自研通道: 未就绪");
        dl->AddText(ImVec2(p.x + fs * 3.0f, p.y + h * 0.5f + fs * 0.15f), COL_SUB,
                    ready ? "驱动 NGX 入口点已握在我们自己手里"
                          : "看下面哪一项是红的");
        ImGui::Dummy(ImVec2(0, h + fs * 0.5f));
    }

    // ── 1. 图形环境 ───────────────────────────────────────────
    ImGui::TextColored(ImColor(COL_GOLD), "1. 图形环境");
    kv("渲染 API", is_d3d12 ? "Direct3D 12" : "不是 D3D12 (本版只做 D3D12)",
       is_d3d12 ? COL_GREEN : COL_RED);
    kv("原生设备指针", native ? hex(static_cast<unsigned int>(
           reinterpret_cast<uintptr_t>(native) & 0xFFFFFFFFu)) : "拿不到",
       native ? COL_TEXT : COL_RED);
    ImGui::Dummy(ImVec2(0, fs * 0.4f));

    // ── 2. NGX 核心 ───────────────────────────────────────────
    ImGui::TextColored(ImColor(COL_GOLD), "2. 驱动 NGX 核心");
    if (!g_ngx.load_error.empty())
    {
        kv("加载", g_ngx.load_error, COL_RED);
    }
    else
    {
        kv("核心目录", g_ngx.core_dir.empty() ? "(空)" : g_ngx.core_dir, COL_SUB);
        char n[64];
        std::snprintf(n, sizeof(n), "%d / 7", g_ngx.resolved);
        kv("入口点解析", n, g_ngx.resolved == 7 ? COL_GREEN : COL_RED);
        kv("来源", "注册表 NGXCore\\FullPath, 不写死路径", COL_SUB);
    }
    ImGui::Dummy(ImVec2(0, fs * 0.4f));

    // ── 3. 运行库 ─────────────────────────────────────────────
    ImGui::TextColored(ImColor(COL_GOLD), "3. 神经渲染运行库");
    if (!g_rt.present)
    {
        kv("nvngx_dlssnr.dll", "游戏目录里没有", COL_RED);
    }
    else
    {
        char sz[64];
        std::snprintf(sz, sizeof(sz), "%lld 字节", g_rt.size);
        kv("nvngx_dlssnr.dll", sz, COL_GREEN);
        kv("全局色调支持", g_rt.has_global_tone
               ? "有 (认识 GlobalToneStrength)"
               : "没有 —— 面板里那个「全局色调强度」拉了也没用",
           g_rt.has_global_tone ? COL_GREEN : COL_GOLD);
    }
    if (!g_fg.checked) check_game_fg();
    {
        const bool has = g_fg.dlssg || g_fg.sl_g;
        std::string s = has ? "有 (" : "没有";
        if (g_fg.dlssg) s += "DLSS-FG 运行库已加载";
        if (g_fg.dlssg && g_fg.sl_g) s += ", ";
        if (g_fg.sl_g)  s += "Streamline FG 插件已加载";
        if (has) s += ") —— 让路, 不叠我们的";
        else     s += g_fg.sl_core ? " (走 Streamline 但没开 FG) —— 可以补我们的" : " —— 可以补我们的";
        kv("游戏自带帧生成", s, has ? COL_GOLD : COL_GREEN);
    }
    ImGui::Dummy(ImVec2(0, fs * 0.6f));

    // ── 4. 握手 ───────────────────────────────────────────────
    ImGui::TextColored(ImColor(COL_GOLD), "4. 跟 NGX 握手");
    ImGui::TextColored(ImColor(COL_SUB),
        "  只做初始化和参数分配, 不创建特性、不碰画面, 做完立刻关掉。");

    ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(COL_GOLD).Value);
    ImGui::PushStyleColor(ImGuiCol_Text,   ImColor(IM_COL32(20, 21, 27, 255)).Value);
    const bool clicked = ImGui::Button("开始握手", ImVec2(fs * 8.0f, fs * 2.0f));
    ImGui::PopStyleColor(2);
    if (clicked)
        do_handshake(native);

    if (g_hs.ran)
    {
        ImGui::Dummy(ImVec2(0, fs * 0.3f));
        kv("Init_Ext",            result_text(g_hs.r_init),
           g_hs.r_init == NVSDK_NGX_Result_Success ? COL_GREEN : COL_RED);
        kv("能力参数",            result_text(g_hs.r_cap),
           g_hs.r_cap == NVSDK_NGX_Result_Success ? COL_GREEN : COL_GRAY);
        kv("分配参数",            result_text(g_hs.r_alloc),
           g_hs.r_alloc == NVSDK_NGX_Result_Success ? COL_GREEN : COL_RED);
        kv("Shutdown",            "故意不调 (会拆掉游戏自己的 DLSS 会话)", COL_GRAY);
        ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
        ImGui::TextColored(ImColor(g_hs.ok ? COL_GREEN : COL_RED), "%s", g_hs.note.c_str());
    }

    // ── 5. 自己建 NR 特性 ────────────────────────────────────
    ImGui::Dummy(ImVec2(0, fs * 0.5f));
    ImGui::TextColored(ImColor(COL_GOLD), "5. 自己建 NR 特性（feature 18）");
    ImGui::TextColored(ImColor(COL_SUB),
        "  只创建、不求值、不碰画面，建完立刻释放。这一步验证的是：");
    ImGui::TextColored(ImColor(COL_SUB),
        "  工作分辨率这根杠杆能不能握在我们自己手里 —— 开销按面积走，75%% 省一半。");

    uint32_t bw = 0, bh = 0;
    runtime->get_screenshot_width_and_height(&bw, &bh);

    ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 14.0f);
    ImGui::SliderInt("工作分辨率", &g_work_pct, 50, 100, "%d%%");
    ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
    ImGui::TextColored(ImColor(COL_SUB), "输出 %ux%u  →  契约 %ux%u  (开销约为 %d%%)",
                       bw, bh, bw * g_work_pct / 100, bh * g_work_pct / 100,
                       g_work_pct * g_work_pct / 100);

    ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(COL_GOLD).Value);
    ImGui::PushStyleColor(ImGuiCol_Text,   ImColor(IM_COL32(20, 21, 27, 255)).Value);
    const bool probe = ImGui::Button("尝试创建", ImVec2(fs * 8.0f, fs * 2.0f));
    ImGui::PopStyleColor(2);
    if (probe)
    {
        ID3D12GraphicsCommandList *cmd = nullptr;
        reshade::api::command_queue *q = runtime->get_command_queue();
        if (q != nullptr)
        {
            reshade::api::command_list *cl = q->get_immediate_command_list();
            if (cl != nullptr)
                cmd = reinterpret_cast<ID3D12GraphicsCommandList *>(cl->get_native());
        }
        probe_nr(native, cmd, bw, bh);
    }

    if (g_nr.ran)
    {
        ImGui::Dummy(ImVec2(0, fs * 0.3f));
        char dim[64];
        std::snprintf(dim, sizeof(dim), "%u x %u", g_nr.w, g_nr.h);
        kv("请求尺寸", dim, COL_TEXT);
        kv("CreateFeature", result_text(g_nr.code),
           g_nr.code == NVSDK_NGX_Result_Success ? COL_GREEN : COL_RED);
        if (g_nr.seh != 0)
            kv("结构化异常", hex(g_nr.seh), COL_RED);
        ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
        ImGui::TextColored(ImColor(g_nr.code == NVSDK_NGX_Result_Success ? COL_GREEN : COL_GOLD),
                           "%s", g_nr.note.c_str());
    }

    // ── 6. 问 NGX 要支持情况 ─────────────────────────────────
    ImGui::Dummy(ImVec2(0, fs * 0.5f));
    ImGui::TextColored(ImColor(COL_GOLD), "6. 直接问 NGX：feature 18 支持吗");
    ImGui::TextColored(ImColor(COL_SUB),
        "  CreateFeature 只甩一个笼统的失败码，这里把原因拆成位域。");
    ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
    if (ImGui::Button("查询支持情况", ImVec2(fs * 9.0f, fs * 1.8f)))
        probe_requirements();

    if (g_req.ran)
    {
        ImGui::Dummy(ImVec2(0, fs * 0.3f));
        kv("查询返回", result_text(g_req.code),
           g_req.code == NVSDK_NGX_Result_Success ? COL_GREEN : COL_RED);
        if (g_req.code == NVSDK_NGX_Result_Success)
        {
            const unsigned int b = g_req.支持位;
            kv("支持位域", hex(b), b == 0 ? COL_GREEN : COL_RED);
            if (b & 1)  kv("  ", "检查项不存在", COL_GOLD);
            if (b & 2)  kv("  ", "驱动版本不支持", COL_RED);
            if (b & 4)  kv("  ", "这张显卡不支持", COL_RED);
            if (b & 8)  kv("  ", "系统版本低于最低要求", COL_RED);
            if (b & 16) kv("  ", "这个特性根本没实现", COL_RED);
            char a[64]; std::snprintf(a, sizeof(a), "0x%X", g_req.min_arch);
            kv("最低硬件架构", a, COL_SUB);
            kv("最低系统版本", g_req.min_os.empty() ? "(空)" : g_req.min_os, COL_SUB);
        }
        ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
        ImGui::TextColored(ImColor(COL_GOLD), "%s", g_req.note.c_str());
    }

    // ── 7. 载体管线 (v0.3) ────────────────────────────────────
    ImGui::Dummy(ImVec2(0, fs * 0.5f));
    ImGui::TextColored(ImColor(COL_GOLD), "7. 载体管线 —— 工作分辨率杠杆（所有路线）");
    ImGui::TextColored(ImColor(COL_SUB),
        "  每帧: 画面缩小 → 我们自己的 DLAA 契约(renodx 在里面插 NR) → 放大回写。开销按面积走。");
    {
        char st[160];
        if (carrier::g.disabled)
            std::snprintf(st, sizeof(st), "已停止: %s", carrier::g.disabled_why.c_str());
        else if (!carrier::cfg.enabled)
            std::snprintf(st, sizeof(st), "未启用 (cfg 里 carrier=1 打开)");
        else if (carrier::g.frames_done == 0)
            std::snprintf(st, sizeof(st), "启用, 等待首帧 (宽限 %d/%d)", carrier::g.create_grace, carrier::cfg.create_delay);
        else
            std::snprintf(st, sizeof(st), "运行中: 已交付 %llu 帧, %ux%u → %ux%u, 上次 CPU 侧 %.2f ms",
                          carrier::g.frames_done, carrier::g.bb_w, carrier::g.bb_h, carrier::g.w, carrier::g.h, carrier::g.last_ms);
        kv("状态", st, carrier::g.disabled ? COL_RED : (carrier::g.frames_done ? COL_GREEN : COL_SUB));
        char wk[64]; std::snprintf(wk, sizeof(wk), "%d%% (开销约 %d%%)", carrier::cfg.work, carrier::cfg.work * carrier::cfg.work / 100);
        kv("工作分辨率", wk, COL_TEXT);
        kv("上次求值", result_text(carrier::g.last_eval),
           carrier::g.last_eval == NVSDK_NGX_Result_Success ? COL_GREEN : COL_RED);
        if (carrier::g.disabled)
        {
            ImGui::Dummy(ImVec2(fs * 0.6f, 0)); ImGui::SameLine();
            if (ImGui::Button("重新启用载体", ImVec2(fs * 9.0f, fs * 1.8f)))
            {
                // Feeder 的 Re-enable 语义: 只清标志, 不拆资源, 从下一帧继续
                carrier::g.disabled = false;
                carrier::g.consecutive_fails = 0;
                carrier::g.disabled_why.clear();
                Log("[carrier] 业主手动重新启用");
            }
        }
    }

    ImGui::Dummy(ImVec2(0, fs * 0.8f));
    ImGui::TextColored(ImColor(COL_GRAY),
        "—— 033 神经渲染控制台 v0.3 · B站 @热心网友033 ——");
}

#include "embedded_ui.h"
#include "panel.h"
#include "monitor.h"   // 图形界面 —— 用到上面的 COL_*/card 助手和 nrscale 状态
#include "hang_watchdog.h"   // S25: 游戏卡死时自动留一份现场转储

// present 事件的签名比 fgen 需要的多几个参数, 这里转一下
static void fg_on_present(reshade::api::command_queue *queue, reshade::api::swapchain *sc,
                          const reshade::api::rect *, const reshade::api::rect *,
                          uint32_t, const reshade::api::rect *)
{
    // S25: 记下这一帧出画面的时间和游戏窗口; 卡死守卫只看这两样, 不碰游戏。
    hangwatch033::OnPresent(sc != nullptr ? static_cast<HWND>(sc->get_hwnd()) : nullptr);
    // ★停车场每帧泵一次★ —— 以前只在 hostnr::Stage 里泵, 而让位/停用之后
    //   Stage 不再被调用, 停在场里的纹理就永远不放(显存白占)。
    //   放这儿是因为 on_present 定义在 hostnr.h 的 include 之前, 看不见它。
    ++nrbackbuffer::realFrame;
#ifdef K033_BETA2_RESHADE_HOST
    beta2gradepresent::Capture(queue,sc,nrbackbuffer::realFrame.load());
#endif
    const bool startupTrace=diagnostic033::StartupTraces && nrbackbuffer::realFrame.load()<=8;
    if(startupTrace){gputime::submit::traceCalls=0;resolveleases::resetTraceCalls=0;}
    // Only the real-frame thread opts in. Existing submission observers on
    // background threads do not perform diagnostic file I/O.
    gputime::submit::trace=startupTrace?+[](const char* phase,unsigned call,ID3D12CommandQueue* queue,UINT count){
        Log("[033 startup submit] tid=%lu call=%u queue=%p lists=%u phase=%s",GetCurrentThreadId(),call,queue,count,phase);
    }:nullptr;
    if(startupTrace)Log("[033 startup] real=%llu controls begin",nrbackbuffer::realFrame.load());
    nrcontrols::Pump();
#ifdef K033_BETA2_RESHADE_HOST
    if(!carrier::cfg.enabled)nrbeta2::Note(nrbeta2::State::Disabled);
    else if(nrbeta2::state.load()==uint32_t(nrbeta2::State::Disabled))nrbeta2::Note(nrbeta2::State::Waiting);
#endif
    carrier::PollConfig();
    fgscene033::Enabled(carrier::cfg.enabled!=0);
    resolveleases::NrEnabled(carrier::cfg.enabled!=0);
    matchedcapture::Pump();
    if(startupTrace)Log("[033 startup] lease pump begin");
    resolveleases::Pump(queue && queue->get_device()->get_api()==reshade::api::device_api::d3d12
        ? reinterpret_cast<ID3D12CommandQueue*>(queue->get_native()) : nullptr,carrier::cfg.enabled!=0 || carrier::cfg.pre.enabled!=0);
#ifdef K033_BETA2_RESHADE_HOST
    beta2grade::Pump(); // retire/prepare even when NR is off or O is poisoned
#endif
    nrnative033::Pump();
    if(startupTrace)Log("[033 startup] lease pump returned; model retirement begin");
    hostnr::ParkTick();
    nrscale::Report();
    if(startupTrace)Log("[033 startup] model retirement returned");
    if (sc != nullptr) carrier::source_space = sc->get_color_space();
    // Ownership must also be known when the carrier never creates a session.
    if (carrier::g_renodx_boot_uplift < 0)
        carrier::SnapshotRenodxBoot(GetFileAttributesA((game_dir() + "\\renodx-dlss5.addon64").c_str()) != INVALID_FILE_ATTRIBUTES);

    // ★每帧重试接入 Feeder★
    // 插件加载顺序不归我们管: 只在 DllMain 里试一次的话, 我们先加载
    // 就抓空、然后永久放弃 —— D3D11 游戏就卡在「启动中/非 D3D12」。
    // 里面自己有开关, 接上了就不再查。
    if (rendercore::Detect()) rendercore::Api()->setEnabled(carrier::cfg.enabled ? 1 : 0);
    else hostnr::try_attach();
    if (!rendercore::Integrated() && carrier::cfg.inject && !inject::s_on && !hostnr::attached()) inject::enable();
    if (inject::s_on || nrbackbuffer::Claimed() || hostnr::s_want_build || hostnr::s_building.load()) hostnr::PumpBuild();   // feature 在这里建(present 不在 NGX 调用栈里)

    yanyundual::PumpBuild();

    // ★让位之后它一直不出帧 → 把 feature 18 收回来★
    //   否则一旦让错(游戏其实没在用 DLSS, 或插入这条路出了别的问题),
    //   两边都不做, 用户一点效果都没有。600 帧(约 5-10 秒)够它建完并出帧。
    if (carrier::cfg.autoroute && carrier::g_inject_grace > 0 && !carrier::g_inject_dead &&
        !inject::active() && !inject::owns_feature())
    {
        if (carrier::g_inject_grace > 600)
        {
            carrier::g_inject_dead = true;
            Log("[carrier] 就地插入让了 600 帧还没出帧 → 位子收回来, 交换链自己做");
        }
    }

    // GetState is not guaranteed once per real frame. Never use its accumulated
    // present count as a multiplier or use it to skip NR work.
    carrier::g_fg_presented = 1;
    mfgunlock::reflex::Report(mfgunlock::framecount::g_accepted_mode.load(),
                             mfgunlock::framecount::g_accepted_generated.load());
    fgen::OnPresent(queue, sc);
    if(startupTrace)Log("[033 startup] control pump returned");
}

#ifdef K033_MONOLITHIC_ENGINE
// The explicit ReShade runtime renders its own effects/overlay but does not
// emit the automatic DXGI proxy's addon_event::present. Keep the unified
// renderer's control/lifetime pump on real frames on this path as well.
extern "C" __declspec(dllexport) void __cdecl K033_UniversalPresent(void* opaque, uint32_t colourSpace)
{
    auto* runtime=static_cast<reshade::api::effect_runtime*>(opaque);if(!runtime)return;
    fg_on_present(runtime->get_command_queue(),nullptr,nullptr,nullptr,0,nullptr);
    carrier::source_space=static_cast<reshade::api::color_space>(colourSpace);
#ifdef K033_BETA2_RESHADE_HOST
    beta2gradepresent::Capture(runtime->get_command_queue(),runtime->get_current_back_buffer(),
        carrier::source_space,nrbackbuffer::realFrame.load());
#endif
}
#endif

// -------------------------------------------------------------- 面板入口
// 早期那套握手诊断(手动握手/尝试直建/重复的工作分辨率滑块)已经完成使命,
// 现在是我们自己直连 NR, 那些按钮全是死的, 留着只是把面板搞乱 —— 删掉。
// 真要排障看游戏目录的 dlss5-033.log, 里面比面板全。
static void draw(reshade::api::effect_runtime *runtime)
{
    static bool reported=false;
    if(!reported){Log("[ui] 033 panel drawn by ReShade");reported=true;}
    load_ngx_once();
    check_runtime();
    fg::probe();
    panel::draw_main(runtime);
#ifndef K033_MONOLITHIC_ENGINE
    if(rendercore::Detect() && ImGui::CollapsingHeader("超分、帧生成、低延迟与完整兼容设置")) embeddedui::Draw();

    const float fs = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0, fs * 0.6f));
    ImGui::TextColored(ImColor(COL_GRAY), "日志: dlss5-033.log        B站 @热心网友033");
    ImGui::TextColored(ImColor(COL_GRAY), "%s  构建 %s", K033_VER, K033_BUILD);
#endif
}

// -------------------------------------------------------------- 入口
#if defined(K033_MONOLITHIC_ENGINE)
extern "C" __declspec(dllexport) BOOL APIENTRY K033_ReShadeEntry(HMODULE mod, DWORD reason, LPVOID)
#else
BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
#endif
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
#ifdef K033_MONOLITHIC_ENGINE
        // Code callbacks belong to the engine; their ReShade lifetime belongs
        // to the registration adapter. Refuse SDKs without explicit ownership.
        {
            auto host=reshade::internal::get_reshade_module_handle();
            const char* required[]={"ReShadeRegisterEventForAddon","ReShadeUnregisterEventForAddon",
                "ReShadeRegisterOverlayForAddon","ReShadeUnregisterOverlayForAddon"};
            for(auto name:required)if(!host || !GetProcAddress(host,name))return FALSE;
        }
#endif
        // ★崩溃自愈: 连续崩过就别再往里掺和★
        //   二级 = 干脆不加载, 这是我们能给出的最彻底的退场。
        if (!safemode::on_attach())
            return FALSE;
        if (!reshade::register_addon(mod))
            return FALSE;
        engine033::host_ready.store(true,std::memory_order_release);
        // ★先装标签合并钩子, 再注册我们自己的★
        //   ReShade 按文件名顺序加载插件: dlss5-033 排在 renodx-dlss5 前面(d < r),
        //   所以我们先跑, 钩得住它的注册。钩不上也不会坏事, 顶多回到两个标签。
#ifndef K033_MONOLITHIC_ENGINE
        ovgrab::install(reshade::internal::get_reshade_module_handle());
#endif
        reshade::register_overlay("热心网友033", draw);
        // 日志第一行也打指纹: 观众发日志截图时同样能认出是哪次构建、是不是原版
        Log("[033] %s  构建 %s   B站 @热心网友033", K033_VER, K033_BUILD);
        hangwatch033::sink = [](const char *line) { Log("%s", line); };
        // ★★★ 默认绝不挂钩 ★★★
        // 血泪: 这句原先是无条件的, 于是就算 cfg 写了 nrscale=0, 核心层的钩子
        // 照样每次启动都装上 —— 结果 renodx 的每一次求值都回 0xBAD00002,
        // 玩家看到的就是"DLSS5 完全失效"。
        // 运行库会顺着返回地址认调用方, 我们的跳板一插进去它就翻脸;
        // 正确做法是走 nvngx.dll_* 转发器另起一路, 而不是钩别人的。
        // 这里只留给排障用, 必须在 dlss5-033.cfg 里显式写 nrscale=1 才装。
        if (cfg_key_is_1("nrscale"))
            nrscale::install();
        nrscale::g_is_ours = &is_our_feature;   // 让钩子分得清谁的调用
        g_handover_pump = &handover::pump;      // 换引擎交接: 每帧走一步
        // ★多帧生成解锁★ 默认开, 出事在 dlss5-033.cfg 里写 mfg=0 一键关。
        //   必须在这儿挂(DllMain), 不能挪到 present —— 它靠 loadhook 在
        //   nvngx_dlssg.dll 被映射【之前】就截住, 晚了就没得截。
        if (!cfg_key_is_0("mfg")) mfg::attach(mod);
        else Log("[mfg] cfg 里 mfg=0 —— 多帧生成解锁这一局不装");
        reshade::register_event<reshade::addon_event::reshade_overlay>(monitor033::Draw);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(monitor033::Destroy);
        reshade::register_event<reshade::addon_event::reshade_present>(on_present);
        carrier::load_cfg();
        carrier::config_save.Init(carrier::CfgSig());
        // 一级: present 还是要挂的 —— 它负责「活够久就把账清了」;
        //       但渲染那几个事件一个都不挂, 我们对这一局的画面零参与。
        if (safemode::off())
            break;
        fgen::load_cfg();
        hostnr::try_attach();   // 在帮手进程里才会真的挂上
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(carrier::OnFinishEffects);
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(nrbackbuffer::OnFinish);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(nrbackbuffer::OnDestroyRuntime);
        nrgame033::Register();
        reshade::register_event<reshade::addon_event::present>(fg_on_present);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(carrier::OnDestroyRuntime);
        reshade::register_event<reshade::addon_event::destroy_device>(carrier::OnDestroyDevice);
        staterestore::register_events();   // 状态信封: 抓游戏的计算根签名/堆/PSO(restorestate=0 时不挂)
        gputime::RegisterEvents();
        resolveleases::RegisterEvents();
        break;
    case DLL_PROCESS_DETACH:
        engine033::host_ready.store(false,std::memory_order_release);
        mfg::detach();
        carrier::FlushUpliftOnExit();   // 换引擎留下的待办, 退出时写才安全
        carrier::FlushHooksOnExit();
        safemode::on_detach();   // 好好退出了 = 这一局不算崩
        carrier::g.dying = true;   // 进程在退: renodx 可能已拆钩, 从此不再碰 NGX (Feeder g_ngx_dying 语义)
        staterestore::unregister_events();
        resolveleases::UnregisterEvents();
        gputime::UnregisterEvents();
        reshade::unregister_event<reshade::addon_event::destroy_device>(carrier::OnDestroyDevice);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(carrier::OnDestroyRuntime);
        reshade::unregister_event<reshade::addon_event::present>(fg_on_present);
        reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(carrier::OnFinishEffects);
        reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(nrbackbuffer::OnFinish);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(nrbackbuffer::OnDestroyRuntime);
        nrgame033::Unregister();
        reshade::unregister_event<reshade::addon_event::reshade_overlay>(monitor033::Draw);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(monitor033::Destroy);
        reshade::unregister_event<reshade::addon_event::reshade_present>(on_present);
#ifndef K033_MONOLITHIC_ENGINE
        ovgrab::uninstall();
#endif
        reshade::unregister_overlay("热心网友033", draw);
        reshade::unregister_addon(mod);
        if (g_ngx.mod != nullptr)
            FreeLibrary(g_ngx.mod);
        break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) int __cdecl K033_HasEmbeddedUi(){return engine033::host_ready.load(std::memory_order_acquire)?1:0;}
extern "C" __declspec(dllexport) int __cdecl K033_GetNrInputStatus(nrbeta2::Snapshot* out){return nrbeta2::Read(out);}
