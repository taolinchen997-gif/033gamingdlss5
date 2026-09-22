// =====================================================================
//  frmstat_test.cpp —— 只为证明 frmstat.h 在 /std:c++17 /utf-8 + ReShade 6.8
//  头文件下能编过(cl /c 出 obj 就算数)。不链接、不进成品。
//  用法(与 build.bat 同一套环境变量):
//    cl /nologo /c /std:c++17 /O2 /MT /EHa /utf-8 /W3 /I sdk\reshade-6.8.0\include /I sdk /I src
//       /Fo<临时目录>\frmstat_test.obj src\frmstat_test.cpp
// =====================================================================
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <reshade.hpp>

// 用一个假的日志函数走一遍 FRMSTAT_LOG 那条宏路
static void TestLog(const char *, ...) {}
#define FRMSTAT_LOG TestLog
#include "frmstat.h"

// 模拟 hostnr::Stage 的用法: 建 → 每帧派发 → 读 → 设备销毁时拆
int StageLike(ID3D12GraphicsCommandList *cl, ID3D12Device *dev,
              ID3D12Resource *mv, ID3D12Resource *proxy, ID3D12Resource *out,
              unsigned gw, unsigned gh, unsigned sw, unsigned sh, float sx, float sy, int ran_model)
{
    // (参数不能叫 small: Windows 的 rpcndr.h 把 small 定义成了 char)
    if (frmstat::cfg_enabled && !frmstat::ready() && !frmstat::failed()) frmstat::Create(dev);
    frmstat::Dispatch(cl, mv, DXGI_FORMAT_UNKNOWN, gw, gh,
                      proxy, DXGI_FORMAT_R16G16B16A16_FLOAT, sw, sh,
                      ran_model ? out : nullptr, 1, sx, sy);
    const frmstat::Stats &st = frmstat::Read();
    if (!st.valid) return 0;
    return (st.mv_median_px > 8.0f || st.dark_frac > 0.9f) ? 1 : 0;
}

void DestroyLike()
{
    frmstat::cfg_stride = 2;
    (void)frmstat::note();
    (void)frmstat::stale();
    (void)frmstat::reads();
    (void)frmstat::dispatches();
    frmstat::Destroy();
}

// 纯 CPU 部分也走一遍: 空设备建不起来、空列表不派发、格式映射
int CpuLike()
{
    int bad = 0;
    if (frmstat::Create(nullptr)) ++bad;
    frmstat::Dispatch(nullptr, nullptr, DXGI_FORMAT_UNKNOWN, 0, 0, nullptr, DXGI_FORMAT_UNKNOWN, 0, 0, nullptr, 0);
    if (frmstat::ViewFormat(DXGI_FORMAT_R16G16_TYPELESS) != DXGI_FORMAT_R16G16_FLOAT) ++bad;
    if (frmstat::ViewFormat(DXGI_FORMAT_R32G32_TYPELESS) != DXGI_FORMAT_R32G32_FLOAT) ++bad;
    if (frmstat::PickFormat(nullptr, DXGI_FORMAT_R32G32_FLOAT) != DXGI_FORMAT_UNKNOWN) ++bad;
    if (!frmstat::IsTypeless(DXGI_FORMAT_R16G16_TYPELESS) || frmstat::IsTypeless(DXGI_FORMAT_R16G16_FLOAT)) ++bad;
    // 直方图分位数: 100 个样本全在第 3 桶 → 中位数 3.5
    uint32_t hist[frmstat::kBins] = {};
    hist[3] = 100;
    const float med = frmstat::Quantile(hist, 100, 0.5);
    if (med < 3.49f || med > 3.51f) ++bad;
    if (frmstat::Quantile(hist, 0, 0.5) != 0.0f) ++bad;
    return bad;
}
