// =====================================================================
//  smoothmotion.h  ——  给【本身没有帧生成】的游戏强制开驱动级插帧
//
//  ★这是第四条路, 跟前三条本质不同★
//    前三条(50系原厂 / 40系解锁 / 20-30 转接 FSR3)都是「接管游戏自己的
//    DLSS 帧生成调用」—— 游戏不调, 就没得接。
//    Smooth Motion 是【驱动自己】在两张已渲染的帧之间插一张, 游戏什么都
//    不用支持。老游戏、没有 DLSS 的游戏都能开。
//    代价: 只有 40/50 系有(20/30 系驱动里根本没这个功能)。
//
//  ★它就是驱动的一个逐游戏配置项★ NVIDIA App 在背后写的也是它:
//      Smooth Motion - Allowed APIs  0xB0CC0875   0=关  3=DX11/12  7=全部(含VK)
//    (「允许哪些接口」就是开关本身; 社区表里那个 Enable=0xB0D384C0 驱动不认)
//    (ID 从 nvidiaProfileInspector 的 CustomSettingNames.xml 扒的)
//
//  ★NVAPI 的调法跟 NGX 一样★ 只导出 nvapi_QueryInterface(id), 别的函数
//    都得拿数字 ID 去问它要地址。下面那串是公开 ID, 已实测全部拿得到
//    (Initialize=0 / CreateSession=0 / LoadSettings=0)。
//
//  ★结构体的 version = sizeof(结构) | (版本<<16)★
//    错一个字节整个调用就被拒。所以照 nvapi.h 的布局原样写,
//    version 一律 sizeof 现算, 绝不手填数字。
//
//  ★写完要重进游戏★ 驱动是在进程启动时读这份配置的。
// =====================================================================
#pragma once

namespace smooth
{

static const unsigned kUniMax = 2048;    // NVAPI_UNICODE_STRING_MAX
static const unsigned kBinMax = 4096;    // NVAPI_BINARY_DATA_MAX

typedef wchar_t NvUniStr[kUniMax];
struct NvBinary { unsigned valueLength; unsigned char valueData[kBinMax]; };

// NVDRS_SETTING v1 —— 照 nvapi.h 的字段顺序原样排
struct NvSetting
{
    unsigned  version;
    NvUniStr  settingName;
    unsigned  settingId;
    unsigned  settingType;        // 0 = DWORD
    unsigned  settingLocation;    // 0 = 当前配置
    unsigned  isCurrentPredefined;
    unsigned  isPredefinedValid;
    union { unsigned u32PredefinedValue; NvBinary binPre; NvUniStr wszPre; };
    union { unsigned u32CurrentValue;    NvBinary binCur; NvUniStr wszCur; };
};

struct NvProfile
{
    unsigned  version;
    NvUniStr  profileName;
    unsigned  gpuSupport;
    unsigned  isPredefined;
    unsigned  numOfApps;
    unsigned  numOfSettings;
};

struct NvApplication
{
    unsigned  version;
    unsigned  isPredefined;
    NvUniStr  appName;
    NvUniStr  userFriendlyName;
    NvUniStr  launcher;
};

#define SM_VER(T, v)  (static_cast<unsigned>(sizeof(T)) | ((v) << 16))

typedef void *(__cdecl *PFN_QI)(unsigned);
typedef int (__cdecl *PFN_0)();
typedef int (__cdecl *PFN_Sess)(void **);
typedef int (__cdecl *PFN_S)(void *);
typedef int (__cdecl *PFN_FindApp)(void *, const wchar_t *, void **, NvApplication *);
typedef int (__cdecl *PFN_NewProf)(void *, NvProfile *, void **);
typedef int (__cdecl *PFN_NewApp)(void *, void *, NvApplication *);
typedef int (__cdecl *PFN_SetSet)(void *, void *, NvSetting *);

static HMODULE     s_mod   = nullptr;
static PFN_QI      s_qi    = nullptr;
static bool        s_ready = false;
static bool        s_tried = false;
static std::string s_note  = "还没查";
static int         s_last  = -1;

static void probe()
{
    if (s_tried) return;
    s_tried = true;
    s_mod = LoadLibraryA("nvapi64.dll");
    if (s_mod == nullptr) { s_note = "系统里没有 nvapi64.dll"; return; }
    s_qi = reinterpret_cast<PFN_QI>(GetProcAddress(s_mod, "nvapi_QueryInterface"));
    if (s_qi == nullptr) { s_note = "nvapi64.dll 里没有 QueryInterface"; return; }
    auto init = reinterpret_cast<PFN_0>(s_qi(0x0150E828));
    if (init == nullptr || init() != 0) { s_note = "NVAPI 初始化失败"; return; }
    s_ready = true;
    s_note  = "就绪";
}

static bool        available() { probe(); return s_ready; }
static const char *note()      { return s_note.c_str(); }
static int         last()      { return s_last; }

// 给【当前这个 exe】写上 Smooth Motion。返回 0 = 成功(重进游戏才生效)。
static int apply(bool on)
{
    probe();
    if (!s_ready) { s_last = -100; return s_last; }

    auto f_create  = reinterpret_cast<PFN_Sess>(s_qi(0x0694D52E));
    auto f_load    = reinterpret_cast<PFN_S>(s_qi(0x375DBD6B));
    auto f_save    = reinterpret_cast<PFN_S>(s_qi(0xFCBC7E14));
    auto f_destroy = reinterpret_cast<PFN_S>(s_qi(0xDAD9CFF8));
    auto f_find    = reinterpret_cast<PFN_FindApp>(s_qi(0xEEE566B2));
    auto f_newprof = reinterpret_cast<PFN_NewProf>(s_qi(0xCC176068));
    auto f_newapp  = reinterpret_cast<PFN_NewApp>(s_qi(0x4347A9DE));
    auto f_set     = reinterpret_cast<PFN_SetSet>(s_qi(0x577DD202));
    if (!f_create || !f_load || !f_save || !f_destroy || !f_find || !f_set)
    { s_note = "NVAPI 少了 DRS 里的函数"; s_last = -101; return s_last; }

    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t *exe = wcsrchr(path, 92);        // 反斜杠
    exe = exe ? exe + 1 : path;

    void *sess = nullptr;
    if (f_create(&sess) != 0) { s_note = "开不了 DRS 会话"; s_last = -102; return s_last; }
    if (f_load(sess) != 0) { f_destroy(sess); s_note = "读不了驱动配置"; s_last = -103; return s_last; }

    void *prof = nullptr;
    NvApplication app = {};
    app.version = SM_VER(NvApplication, 1);
    const int rf = f_find(sess, exe, &prof, &app);
    if (rf != 0 || prof == nullptr)
    {
        if (!f_newprof || !f_newapp)
        { f_destroy(sess); s_note = "驱动里没有这个游戏的配置, 也建不了新的"; s_last = -104; return s_last; }
        NvProfile p = {};
        p.version = SM_VER(NvProfile, 1);
        _snwprintf_s(p.profileName, kUniMax, _TRUNCATE, L"DLSS5一键包 - %s", exe);
        if (f_newprof(sess, &p, &prof) != 0 || prof == nullptr)
        { f_destroy(sess); s_note = "建不了驱动配置"; s_last = -105; return s_last; }
        NvApplication na = {};
        na.version = SM_VER(NvApplication, 1);
        wcscpy_s(na.appName, kUniMax, exe);
        wcscpy_s(na.userFriendlyName, kUniMax, exe);
        if (f_newapp(sess, prof, &na) != 0)
        { f_destroy(sess); s_note = "配置建了但加不进这个 exe"; s_last = -106; return s_last; }
    }

    // ★只写「允许哪些图形接口」这一个设置 —— 它本身就是开关★
    //   2026-09-05 实测: 社区那份 CustomSettingNames.xml 里写的
    //   「Smooth Motion - Enable」= 0xB0D384C0 【驱动不认】, 写它返回
    //   NVAPI_ID_OUT_OF_RANGE(-160)。而驱动自己的 Reference.xml 里
    //   压根没有这一条, 只有 0xB0CC0875「Smooth Motion - Allowed APIs」
    //   (要求驱动 >= 571.86, 本机 616.56)。
    //   也就是说: 允许的接口集合就是开关 —— 7 = DX11/DX12/Vulkan 全允许,
    //   0 = 一个都不允许 = 关掉。同一次测试里 0xB0CC0875 写入返回 0(成功)。
    //   ★别再把那个 Enable ID 加回来★ 它只会让整次写入失败。
    struct { unsigned id; unsigned val; const wchar_t *nm; } kSet[] = {
        { 0xB0CC0875, on ? 7u : 0u, L"Smooth Motion - Allowed APIs" },
    };
    for (const auto &k : kSet)
    {
        NvSetting st = {};
        st.version         = SM_VER(NvSetting, 1);
        st.settingId       = k.id;
        st.settingType     = 0;
        st.settingLocation = 0;
        st.u32CurrentValue = k.val;
        wcscpy_s(st.settingName, kUniMax, k.nm);
        const int rs = f_set(sess, prof, &st);
        if (rs != 0)
        {
            f_destroy(sess);
            char b[96]; std::snprintf(b, sizeof(b), "写设置 0x%08X 失败(NVAPI %d)", k.id, rs);
            s_note = b; s_last = rs; return rs;
        }
    }

    const int rsave = f_save(sess);
    f_destroy(sess);
    if (rsave != 0)
    { char b[64]; std::snprintf(b, sizeof(b), "存不进驱动(NVAPI %d)", rsave); s_note = b; s_last = rsave; return rsave; }
    s_note = on ? "已给这个游戏打开 —— 重进游戏生效" : "已关闭 —— 重进游戏生效";
    s_last = 0;
    Log("[SmoothMotion] %s: %ls", on ? "开" : "关", exe);
    return 0;
}

} // namespace smooth
