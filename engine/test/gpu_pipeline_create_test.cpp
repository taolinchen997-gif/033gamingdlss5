// 在 WARP D3D12 设备上实际创建本次改动涉及的根签名、PSO、纹理和描述符堆。
// 这比只调用 D3DCompile 更接近游戏初始化，但不替代真实游戏的曝光极性/资源状态验证。
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdarg>
#include <cstdio>
#include <string>

static void Log(const char *, ...) {}

#include "../src/scale.h"
#include "../src/exposure.h"

template <typename T> static void ReleaseTest(T *&p)
{
    if (p != nullptr) { p->Release(); p = nullptr; }
}

int main()
{
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *warp = nullptr;
    ID3D12Device *device = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory));
    if (SUCCEEDED(hr)) hr = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void **>(&warp));
    if (SUCCEEDED(hr)) hr = D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0,
                                              __uuidof(ID3D12Device), reinterpret_cast<void **>(&device));
    if (FAILED(hr))
    {
        std::printf("FAIL device 0x%08X\n", static_cast<unsigned>(hr));
        ReleaseTest(warp); ReleaseTest(factory);
        return 1;
    }

    scale::Blitter blitter;
    exposure::Meter meter;
    const bool scale_ok = scale::Create(blitter, device);
    const bool exposure_ok = exposure::Create(meter, device);
    std::printf("scale=%s%s exposure=%s%s\n",
                scale_ok ? "OK" : "FAIL ", scale_ok ? "" : blitter.error.c_str(),
                exposure_ok ? "OK" : "FAIL ", exposure_ok ? "" : meter.error.c_str());

    exposure::Destroy(meter);
    scale::Destroy(blitter);
    ReleaseTest(device); ReleaseTest(warp); ReleaseTest(factory);
    return (scale_ok && exposure_ok) ? 0 : 2;
}
