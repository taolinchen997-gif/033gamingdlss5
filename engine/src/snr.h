// =====================================================================
//  033 · 签名运行库加载器 (自研 NR 内核 · 实验件)
//
//  目的: 复刻 renodx 让【未签名的】nvngx_dlssnr.dll 被 NGX 接受的机制,
//  从而我们自己(不经 renodx)能创建/求值 NGX feature 18 (神经渲染)。
//
//  机制(全部从 renodx-dlss5.addon64 二进制 + 本机实证逆向得出):
//    · 我们的 nvngx_dlssnr.dll 是泄露的 SF-v2, Authenticode = NotSigned;
//      NGX 因此拒收 → CreateFeature(18) 回 0xBAD0000C OutOfDate。
//    · 该运行库自检 = 调 GetModuleFileNameW(自己模块) 拿到自身路径,
//      再对该文件做签名验证(未签 → 拒绝初始化)。renodx 把该运行库
//      IAT 里的 GetModuleFileNameW 改写, 让自检拿到【旁边那个真签名的
//      nvngx_dlss.dll 的路径】→ 验签读到 NVIDIA 签名文件 → 通过。
//
//  ★★法律边界★★: 这一步绕过 NVIDIA 对泄露二进制的签名校验, 是全链
//    法律最灰的地方。业主已知情并拍板。仅在 cfg 里 snr_patch=1 时启用。
//
//  本头依赖 dlss5_033.cpp 已定义的 g_ngx / g_hs / Log / game_dir。
// =====================================================================
#pragma once

namespace snr
{
// 补丁状态(面板/日志用)
struct Patch
{
    bool        ran        = false;
    bool        loaded     = false;   // dlssnr 已 LoadLibrary
    bool        writable   = false;   // IAT 改可写成功
    int         hooked     = 0;       // 改写了几个 thunk (期望 2: W + A)
    bool        signed_target = false;// 重定向目标文件确实是签名的
    std::string target;               // 重定向到的签名文件路径
    std::string note;
};
static Patch g_patch;

static HMODULE     g_dlssnr = nullptr;          // 我们 LoadLibrary 的实例
static wchar_t     g_signed_pathW[MAX_PATH]{};  // 自检被重定向到的签名文件(宽)
static char        g_signed_pathA[MAX_PATH]{};  // 同, 窄

// 真函数指针(hook 里给非 dlssnr 的调用透传用)
typedef DWORD (WINAPI *PFN_GMFNW)(HMODULE, LPWSTR, DWORD);
typedef DWORD (WINAPI *PFN_GMFNA)(HMODULE, LPSTR,  DWORD);
static PFN_GMFNW g_real_gmfnw = nullptr;
static PFN_GMFNA g_real_gmfna = nullptr;

// dlssnr 模块的地址区间(判断"是不是在查它自己")
static uintptr_t g_dlssnr_lo = 0, g_dlssnr_hi = 0;

static bool addr_in_dlssnr(HMODULE h)
{
    const uintptr_t a = reinterpret_cast<uintptr_t>(h);
    return a >= g_dlssnr_lo && a < g_dlssnr_hi;
}

// ── 我们的 hook: 被问到 dlssnr 自己时, 返回签名文件路径; 其余透传 ──
static DWORD WINAPI Hook_GMFNW(HMODULE h, LPWSTR buf, DWORD n)
{
    if (addr_in_dlssnr(h) && g_signed_pathW[0])
    {
        const size_t len = wcslen(g_signed_pathW);
        if (n > len) { wcscpy_s(buf, n, g_signed_pathW); return static_cast<DWORD>(len); }
    }
    return g_real_gmfnw ? g_real_gmfnw(h, buf, n) : 0;
}
static DWORD WINAPI Hook_GMFNA(HMODULE h, LPSTR buf, DWORD n)
{
    if (addr_in_dlssnr(h) && g_signed_pathA[0])
    {
        const size_t len = strlen(g_signed_pathA);
        if (n > len) { strcpy_s(buf, n, g_signed_pathA); return static_cast<DWORD>(len); }
    }
    return g_real_gmfna ? g_real_gmfna(h, buf, n) : 0;
}

// ── 在已加载模块 base 的 IAT 里, 把 kernel32!name 的 thunk 改写成 repl ──
// 返回被改写的原函数指针(供透传), 找不到返回 nullptr。
static void *patch_iat(BYTE *base, const char *want_dll, const char *want_fn, void *repl)
{
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) return nullptr;
    auto *imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);

    for (; imp->Name != 0; ++imp)
    {
        const char *dll = reinterpret_cast<const char *>(base + imp->Name);
        if (_stricmp(dll, want_dll) != 0) continue;

        // INT(名字表, OriginalFirstThunk) 与 IAT(FirstThunk) 平行遍历
        auto *orig = reinterpret_cast<IMAGE_THUNK_DATA *>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto *iat  = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imp->FirstThunk);
        for (; orig->u1.AddressOfData != 0; ++orig, ++iat)
        {
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;   // 按序号导入的跳过
            auto *bn = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + orig->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char *>(bn->Name), want_fn) != 0) continue;

            void **slot = reinterpret_cast<void **>(&iat->u1.Function);
            void  *old  = *slot;
            DWORD  prot = 0;
            if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &prot))
                return nullptr;                                     // 改不可写 = 被杀毒锁了
            *slot = repl;
            VirtualProtect(slot, sizeof(void *), prot, &prot);
            return old;
        }
    }
    return nullptr;
}

// 找一个本机真 NVIDIA 签名的 NGX dll 当重定向目标: 优先游戏目录 nvngx_dlss.dll,
// 退而求其次驱动 DriverStore 的 _nvngx.dll (由主文件已读到的 g_ngx.core_dir 提供)。
static bool pick_signed_target()
{
    const std::string dir = game_dir();
    const char *cands[] = { "\\nvngx_dlss.dll", "\\nvngx_dlssg.dll" };
    for (const char *c : cands)
    {
        std::string p = dir + c;
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            // 只用它当"存在的签名文件"路径; 是否真签名交给上层 Authenticode 判断/日志
            strcpy_s(g_signed_pathA, p.c_str());
            MultiByteToWideChar(CP_ACP, 0, p.c_str(), -1, g_signed_pathW, MAX_PATH);
            g_patch.target = p;
            return true;
        }
    }
    // 驱动核心目录里的 _nvngx.dll (WHQL 签名)
    if (!g_ngx.core_dir.empty())
    {
        std::string p = g_ngx.core_dir + "\\_nvngx.dll";
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            strcpy_s(g_signed_pathA, p.c_str());
            MultiByteToWideChar(CP_ACP, 0, p.c_str(), -1, g_signed_pathW, MAX_PATH);
            g_patch.target = p;
            return true;
        }
    }
    return false;
}

// ── 主入口: 加载 dlssnr 并给它的自检导入打补丁 ──────────────────────
// 必须在 NGX 首次触碰 feature 18 之前调(越早越好)。返回是否补丁全部就位。
static bool apply()
{
    if (g_patch.ran) return g_patch.hooked >= 1;
    g_patch.ran = true;

    if (!pick_signed_target()) { g_patch.note = "找不到签名文件当重定向目标"; return false; }

    const std::string p = game_dir() + "\\nvngx_dlssnr.dll";
    if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) { g_patch.note = "游戏目录没有 nvngx_dlssnr.dll"; return false; }

    // 提前 LoadLibrary, 让后续 NGX 核心用的是我们这份已打补丁的映射
    g_dlssnr = LoadLibraryExW(std::wstring(p.begin(), p.end()).c_str(), nullptr, 0);
    if (g_dlssnr == nullptr) { char b[64]; std::snprintf(b, sizeof(b), "LoadLibrary 失败 %lu", GetLastError()); g_patch.note = b; return false; }
    g_patch.loaded = true;

    // 模块地址区间(判断自检是否在查它自己)
    MODULEINFO mi{};
    if (GetModuleInformation(GetCurrentProcess(), g_dlssnr, &mi, sizeof(mi)))
    {
        g_dlssnr_lo = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        g_dlssnr_hi = g_dlssnr_lo + mi.SizeOfImage;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    g_real_gmfnw = reinterpret_cast<PFN_GMFNW>(GetProcAddress(k32, "GetModuleFileNameW"));
    g_real_gmfna = reinterpret_cast<PFN_GMFNA>(GetProcAddress(k32, "GetModuleFileNameA"));

    void *ow = patch_iat(reinterpret_cast<BYTE *>(g_dlssnr), "KERNEL32.dll", "GetModuleFileNameW", reinterpret_cast<void *>(&Hook_GMFNW));
    void *oa = patch_iat(reinterpret_cast<BYTE *>(g_dlssnr), "KERNEL32.dll", "GetModuleFileNameA", reinterpret_cast<void *>(&Hook_GMFNA));
    // dlssnr 的导入名可能写作 "KERNEL32.dll" 或 "api-ms-win-core-..." 转发; 两种都试
    if (ow == nullptr) ow = patch_iat(reinterpret_cast<BYTE *>(g_dlssnr), "api-ms-win-core-libraryloader-l1-2-0.dll", "GetModuleFileNameW", reinterpret_cast<void *>(&Hook_GMFNW));
    if (oa == nullptr) oa = patch_iat(reinterpret_cast<BYTE *>(g_dlssnr), "api-ms-win-core-libraryloader-l1-2-0.dll", "GetModuleFileNameA", reinterpret_cast<void *>(&Hook_GMFNA));

    if (ow) { g_real_gmfnw = reinterpret_cast<PFN_GMFNW>(ow); g_patch.hooked++; }
    if (oa) { g_real_gmfna = reinterpret_cast<PFN_GMFNA>(oa); g_patch.hooked++; }
    g_patch.writable = g_patch.hooked > 0;

    if (g_patch.hooked == 0) g_patch.note = "IAT 里找不到 GetModuleFileName*(可能被杀毒锁或名字转发)";
    else g_patch.note = "补丁就位, 重定向自检到签名文件";
    return g_patch.hooked >= 1;
}

// ── dlssnr 自己导出的 NGX 入口(renodx 就是直接调这些, 绕开核心) ──────
// 核心 _nvngx.dll 会在碰 dlssnr 前就以驱动理由回 OutOfDate; 而 dlssnr 本身
// 导出了整套 NVSDK_NGX_D3D12_*, 它自己的 Init 才是签名自检发生的地方
// (我们的 IAT 补丁在这里生效)。参数块仍用核心的 AllocateParameters。
typedef NVSDK_NGX_Result (__cdecl *PFN_Init_Ext)(unsigned long long, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_Parameter *);
typedef NVSDK_NGX_Result (__cdecl *PFN_CreateFeature)(ID3D12GraphicsCommandList *, NVSDK_NGX_Feature, const NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
typedef NVSDK_NGX_Result (__cdecl *PFN_ReleaseFeature)(NVSDK_NGX_Handle *);
typedef NVSDK_NGX_Result (__cdecl *PFN_Shutdown1)(ID3D12Device *);

struct DirectProbe
{
    bool             ran = false;
    NVSDK_NGX_Result r_init   = NVSDK_NGX_Result_Fail;
    NVSDK_NGX_Result r_create = NVSDK_NGX_Result_Fail;
    unsigned long    seh = 0;
    std::string      note;
};
static DirectProbe g_direct;

// NGX 的日志回调: 把 NR 运行库/核心的内部报错原样喊到我们的日志里
static void NVSDK_CONV ngx_log_cb(const char *msg, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature)
{
    if (msg) Log("[ngx] %s", msg);
}

// 5 参数 FeatureCommonInfo 版 Init(公开头是 4 参, 运行库实际收 FeatureCommonInfo; Feeder 也这么调)
typedef NVSDK_NGX_Result (__cdecl *PFN_Init_Common)(unsigned long long, const wchar_t *, ID3D12Device *,
                                                    const NVSDK_NGX_FeatureCommonInfo *, NVSDK_NGX_Version);

// 裸执行(无 C++ 对象, 好让 __try 兜住): dlssnr 自己的 Init + CreateFeature(18)
static void raw_direct(ID3D12Device *dev, ID3D12GraphicsCommandList *cmd, NVSDK_NGX_Parameter *params,
                       unsigned int w, unsigned int h,
                       NVSDK_NGX_Result *r_init, NVSDK_NGX_Result *r_create, unsigned long *seh)
{
    *r_init = NVSDK_NGX_Result_Fail; *r_create = NVSDK_NGX_Result_Fail; *seh = 0;
    auto init   = reinterpret_cast<PFN_Init_Common>(GetProcAddress(g_dlssnr, "NVSDK_NGX_D3D12_Init"));
    auto create = reinterpret_cast<PFN_CreateFeature>(GetProcAddress(g_dlssnr, "NVSDK_NGX_D3D12_CreateFeature"));
    auto rel    = reinterpret_cast<PFN_ReleaseFeature>(GetProcAddress(g_dlssnr, "NVSDK_NGX_D3D12_ReleaseFeature"));
    if (init == nullptr || create == nullptr) { *seh = 0xE0000001; return; }
    wchar_t data_path[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, data_path);

    // FeatureCommonInfo 带日志回调 —— 让 NGX 把失败原因说出来
    NVSDK_NGX_FeatureCommonInfo common; ZeroMemory(&common, sizeof(common));
    common.LoggingInfo.LoggingCallback = &ngx_log_cb;
    common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_VERBOSE;

    __try
    {
        *r_init = init(0x4F33334ull, data_path, dev, &common, NVSDK_NGX_Version_API);
        if (*r_init != NVSDK_NGX_Result_Success) return;
        params->Set("CreationNodeMask", 1u);   params->Set("VisibilityNodeMask", 1u);
        params->Set("DLSSNR.Width",  w);        params->Set("DLSSNR.Height", h);
        params->Set("DLSSNR.InputWidth",  w);   params->Set("DLSSNR.InputHeight", h);
        params->Set("DLSSNR.OutputWidth", w);   params->Set("DLSSNR.OutputHeight", h);
        params->Set("DLSSNR.Enabled", 1u);
        NVSDK_NGX_Handle *hnd = nullptr;
        *r_create = create(cmd, static_cast<NVSDK_NGX_Feature>(18), params, &hnd);
        if (*r_create == NVSDK_NGX_Result_Success && hnd != nullptr && rel) rel(hnd);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh = GetExceptionCode(); }
}

// 用 dlssnr 自己的 Init + CreateFeature 直接建 feature 18 (绕开核心)。
static void probe_direct(ID3D12Device *dev, ID3D12GraphicsCommandList *cmd,
                         NVSDK_NGX_Parameter *params, unsigned int w, unsigned int h)
{
    g_direct = DirectProbe{};
    g_direct.ran = true;
    if (g_dlssnr == nullptr) { g_direct.note = "dlssnr 未加载(补丁没跑?)"; return; }
    if (dev == nullptr || cmd == nullptr || params == nullptr) { g_direct.note = "缺 设备/命令列表/参数块"; return; }

    raw_direct(dev, cmd, params, w, h, &g_direct.r_init, &g_direct.r_create, &g_direct.seh);

    if (g_direct.seh != 0) g_direct.note = "dlssnr 直连抛异常";
    else if (g_direct.r_init != NVSDK_NGX_Result_Success) g_direct.note = "dlssnr 自己的 Init_Ext 没过(自检可能不止查文件名)";
    else if (g_direct.r_create == NVSDK_NGX_Result_Success) g_direct.note = "★dlssnr 直连: feature 18 建成★";
    else g_direct.note = "dlssnr Init 过了, 但 CreateFeature 被拒(看返回码)";
}
} // namespace snr
