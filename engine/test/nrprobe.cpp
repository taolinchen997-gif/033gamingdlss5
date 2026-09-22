// =====================================================================
//  nrprobe —— 独立验证: 我们能不能自己把 feature 18 建起来并跑一次
//
//  不依赖游戏、不依赖 ReShade、不钩任何人。只回答一个问题:
//    走 nvngx.dll_033.dll 转发器, 我们自己能不能在【缩小的尺寸】上
//    创建并求值 DLSS 神经渲染?
//
//  成了 = "把 OptiScaler 那套搬进我们自己插件" 的最大风险清零。
// =====================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <ngx/nvsdk_ngx.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "advapi32.lib")

static void L(const char *f, ...)
{
    char b[1024];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof(b), f, a);
    va_end(a);
    printf("%s\n", b);
    fflush(stdout);
}

// ---- 转发器接口 (跟 dlssnr_forwarder.cpp 的导出一一对应) ----
using PFN_NrCreate = void *(__cdecl *)(const wchar_t *, const wchar_t *, ID3D12Device *,
                                       ID3D12GraphicsCommandList *, void *, unsigned, unsigned,
                                       int, float, int, float, float, float, int, int);
using PFN_NrEval = int(__cdecl *)(ID3D12GraphicsCommandList *, void *, void *, ID3D12Resource *,
                                  ID3D12Resource *, ID3D12Resource *, ID3D12Resource *, unsigned,
                                  unsigned, unsigned, unsigned, int, int, float, int, float, float,
                                  float, int, float, float);
using PFN_SetFloatSlot = void(__cdecl *)(int);
using PFN_ProbeFloat = void(__cdecl *)(void *, const char *, float, int);

// ---- NGX 核心 ----
typedef NVSDK_NGX_Result(__cdecl *PFN_Init_Ext)(unsigned long long, const wchar_t *, ID3D12Device *,
                                                NVSDK_NGX_Version,
                                                const NVSDK_NGX_FeatureCommonInfo *);
typedef NVSDK_NGX_Result(__cdecl *PFN_GetCaps)(NVSDK_NGX_Parameter **);

static std::wstring core_dir()
{
    HKEY k;
    wchar_t buf[1024];
    DWORD sz = sizeof(buf);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", 0,
                      KEY_READ, &k) != ERROR_SUCCESS)
        return L"";
    LSTATUS r = RegQueryValueExW(k, L"FullPath", nullptr, nullptr, (LPBYTE) buf, &sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS ? std::wstring(buf) : L"";
}

static ID3D12Resource *tex(ID3D12Device *d, DXGI_FORMAT f, unsigned w, unsigned h)
{
    D3D12_HEAP_PROPERTIES hp {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = f;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *r = nullptr;
    d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&r));
    return r;
}

int wmain(int argc, wchar_t **argv)
{
    SetConsoleOutputCP(65001);
    const unsigned FULL_W = argc > 1 ? _wtoi(argv[1]) : 5120;
    const unsigned FULL_H = argc > 2 ? _wtoi(argv[2]) : 2160;
    const float SCALE = argc > 3 ? (float) _wtof(argv[3]) : 0.75f;
    const unsigned W = (unsigned) (FULL_W * SCALE + 0.5f);
    const unsigned H = (unsigned) (FULL_H * SCALE + 0.5f);

    L("=== nrprobe ===");
    L("画面 %ux%u   模型分辨率 %.0f%%  ->  %ux%u", FULL_W, FULL_H, SCALE * 100, W, H);

    // ---------- 1. D3D12 设备 ----------
    IDXGIFactory6 *fac = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac))))
    {
        L("x DXGI 工厂建不出来");
        return 1;
    }
    IDXGIAdapter1 *ad = nullptr;
    fac->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&ad));
    ID3D12Device *dev = nullptr;
    if (FAILED(D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev))))
    {
        L("x D3D12 设备建不出来");
        return 1;
    }
    DXGI_ADAPTER_DESC1 adesc {};
    if (ad)
        ad->GetDesc1(&adesc);
    L("[1] 设备: %ls", adesc.Description);

    // ---------- 2. NGX 核心: 初始化 + 拿能力参数块 ----------
    const std::wstring cdir = core_dir();
    if (cdir.empty())
    {
        L("x 注册表里没有 NGXCore 路径");
        return 1;
    }
    const std::wstring cpath = cdir + L"\\_nvngx.dll";
    HMODULE core = LoadLibraryW(cpath.c_str());
    if (!core)
    {
        L("x 载不动 %ls (错误 %lu)", cpath.c_str(), GetLastError());
        return 1;
    }
    L("[2] NGX 核心: %ls", cpath.c_str());

    auto initx = (PFN_Init_Ext) GetProcAddress(core, "NVSDK_NGX_D3D12_Init_Ext");
    auto getcp = (PFN_GetCaps) GetProcAddress(core, "NVSDK_NGX_D3D12_GetCapabilityParameters");
    if (!initx || !getcp)
    {
        L("x 核心缺导出 (Init_Ext=%p GetCaps=%p)", (void *) initx, (void *) getcp);
        return 1;
    }

    wchar_t datap[MAX_PATH];
    GetTempPathW(MAX_PATH, datap);
    NVSDK_NGX_Result r = initx(0x24480451ull, datap, dev, NVSDK_NGX_Version_API, nullptr);
    L("    Init_Ext -> 0x%08X %s", r, r == NVSDK_NGX_Result_Success ? "成功" : "失败");
    if (r != NVSDK_NGX_Result_Success)
        return 1;

    NVSDK_NGX_Parameter *params = nullptr;
    r = getcp(&params);
    L("    能力参数块 -> 0x%08X  %p", r, (void *) params);
    if (r != NVSDK_NGX_Result_Success || !params)
        return 1;

    // ---------- 3. 转发器 ----------
    HMODULE fw = LoadLibraryW(
        argc > 4 ? argv[4]
                 : L"E:\\033插件\\build\\nvngx.dll_033.dll");
    if (!fw)
    {
        L("x 转发器载不动 (错误 %lu)", GetLastError());
        return 1;
    }
    // 两套导出名都试: nr033_* 是我们自己写的转发器, dlssnr_call_* 是 OptiScaler 那份。
    // 同机同运行库做 A/B, 才知道差在哪。
    const bool theirs = GetProcAddress(fw, "nr033_create") == nullptr;
    #define PICK(a, b) (theirs ? (b) : (a))
    L("[3b] 转发器种类: %s", theirs ? "OptiScaler (dlssnr_call_*)" : "自研 (nr033_*)");
    auto nr_create = (PFN_NrCreate) GetProcAddress(fw, PICK("nr033_create", "dlssnr_call_create"));
    auto nr_eval = (PFN_NrEval) GetProcAddress(fw, PICK("nr033_evaluate", "dlssnr_call_evaluate"));
    auto set_slot = (PFN_SetFloatSlot) GetProcAddress(fw, PICK("nr033_set_float_slot", "dlssnr_call_set_float_slot"));
    auto probe_f = (PFN_ProbeFloat) GetProcAddress(fw, PICK("nr033_probe_float", "dlssnr_call_probe_float"));
    auto p_init = (int *) GetProcAddress(fw, PICK("nr033_last_init", "dlssnr_call_last_init"));
    auto p_create = (int *) GetProcAddress(fw, PICK("nr033_last_create", "dlssnr_call_last_create"));
    if (!nr_create || !nr_eval)
    {
        L("x 转发器缺导出");
        return 1;
    }
    L("[3] 转发器就位");

    // ---------- 4. 找 float 参数在 vtable 的哪个槽 ----------
    if (probe_f && set_slot)
    {
        const char *key = "DLSSNR.033FloatProbe";
        const float want = 0.375f; // 二进制精确, 往返必须一模一样
        const int cand[] = { 1, 2, 5, 6, 7, 4, 3, 0 };
        int found = -1;
        for (int s : cand)
        {
            float back = 0.0f;
            probe_f(params, key, want, s);
            if (params->Get(key, &back) == NVSDK_NGX_Result_Success && back == want)
            {
                found = s;
                break;
            }
        }
        if (found >= 0)
        {
            set_slot(found);
            L("[4] float 参数走 vtable 槽位 %d", found);
        }
        else
        {
            L("[4] ! 没找到 float 槽位(强度类参数会失效, 不影响本次验证)");
        }
    }

    // ---------- 5. 命令队列/列表 ----------
    D3D12_COMMAND_QUEUE_DESC qd {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *q = nullptr;
    dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q));
    ID3D12CommandAllocator *al = nullptr;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&al));
    ID3D12GraphicsCommandList *cl = nullptr;
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, IID_PPV_ARGS(&cl));

    // ---------- 6. 建 feature 18, 尺寸是【缩小后的】 ----------
    void *feat = nr_create(argc > 5 ? argv[5]
                                    : L"E:\\Steam\\steamapps\\common\\Shadow of the Tomb "
                                      L"Raider\\nvngx_dlssnr.dll",
                           datap, dev, cl, params, W, H,
                           /*preset*/ 3, /*intensity*/ 1.0f, /*style*/ 0,
                           /*localStructure*/ 1.0f, /*localTone*/ 1.0f, /*skinStructure*/ 1.0f,
                           /*useAutoMask*/ 0, /*uiCorrection*/ 0);
    L("[5] 建 feature 18 @ %ux%u", W, H);
    L("    snippet Init -> 0x%08X", p_init ? *p_init : -1);
    L("    CreateFeature -> 0x%08X   handle=%p", p_create ? *p_create : -1, feat);
    if (!feat)
    {
        L("");
        L("*** 结论: 建不出来 —— 这条路走不通 ***");
        return 2;
    }

    // ---------- 7. 造假贴图, 求值一次 ----------
    ID3D12Resource *color = tex(dev, DXGI_FORMAT_R11G11B10_FLOAT, W, H);
    ID3D12Resource *out = tex(dev, DXGI_FORMAT_R11G11B10_FLOAT, W, H);
    ID3D12Resource *depth = tex(dev, DXGI_FORMAT_R32_FLOAT, W, H);
    ID3D12Resource *motion = tex(dev, DXGI_FORMAT_R16G16_FLOAT, W, H);
    L("[6] 贴图: color=%p out=%p depth=%p motion=%p", (void *) color, (void *) out, (void *) depth,
      (void *) motion);

    int er = nr_eval(cl, feat, params, color, depth, motion, out, W, H, W, H, /*depthInverted*/ 1,
                     /*reset*/ 1, 1.0f, 0, 1.0f, 1.0f, 1.0f, 0, 1.0f, 1.0f);
    L("[7] EvaluateFeature -> 0x%08X %s", er, er == 1 ? "成功" : "失败");

    cl->Close();
    ID3D12CommandList *lists[] = { cl };
    q->ExecuteCommandLists(1, lists);
    ID3D12Fence *fence = nullptr;
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    q->Signal(fence, 1);
    fence->SetEventOnCompletion(1, ev);
    WaitForSingleObject(ev, 5000);
    L("[8] GPU 执行完毕");

    L("");
    if (er == 1)
        L("*** 成了: 我们自己在 %ux%u 上跑通了神经渲染 ***", W, H);
    else
        L("*** 建得出来但求值失败 (0x%08X) ***", er);
    return er == 1 ? 0 : 3;
}
