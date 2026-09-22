// This file is part of the FidelityFX SDK.
//
// Copyright (C) 2026 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "../OptiScaler033/external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/fsr3/include/ffx_frameinterpolation.h"
#include "../OptiScaler033/external/FidelityFX-SDK-v2/Kits/FidelityFX/backend/dx12/ffx_dx12.h"

#include "FrameInterpolationSwapchainDX12_Helpers.h"
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

#include <string> //needed for std::to_wstring

IDXGIFactory* getDXGIFactoryFromSwapChain(IDXGISwapChain* swapChain)
{
    IDXGIFactory* factory = nullptr;
    if (FAILED(swapChain->GetParent(IID_PPV_ARGS(&factory)))) {

    }

    return factory;
}

void waitForPerformanceCount(const int64_t targetCount, const int64_t frequency, const UINT timerResolution, const UINT spinTime)
{
                                
    int64_t currentCount;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentCount));
    if (currentCount >= targetCount)
        return;
        
    double millis = static_cast<double>(((targetCount - currentCount) * 1000000) / frequency) / 1000.;

    //Sleep if safe, to free up cores.
    while (timerResolution != UNKNOWN_TIMER_RESOlUTION && millis > spinTime * timerResolution)
    {
        MMRESULT result = timeBeginPeriod(timerResolution);           //Request 1ms timer resolution from OS. Necessary to prevent overshooting sleep.
        if (result != TIMERR_NOERROR)
            break; //Can't guarantee sleep precision.              
        Sleep(static_cast<DWORD>((millis - timerResolution*spinTime)));  //End sleep a few timer resolution units early to prevent overshooting.
        timeEndPeriod(timerResolution);

        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentCount));

        millis = static_cast<double>(((targetCount - currentCount) * 1000000) / frequency) / 1000.;
    }

    do
    {
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentCount));
    } while (currentCount < targetCount);
}

bool waitForFenceValue(ID3D12Fence* fence, UINT64 value, DWORD dwMilliseconds, FfxWaitCallbackFunc waitCallback, const bool)
{
    if(pacing033::Completed(fence,value))return true;
    if(!fence)throw pacing033::Failure{E_POINTER};
    const auto start=GetTickCount64();const DWORD limit=dwMilliseconds==INFINITE?5000:std::min(dwMilliseconds,DWORD(5000));
    struct Event{HANDLE h=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(h)CloseHandle(h);}} event;
    if(!event.h)throw pacing033::Failure{HRESULT_FROM_WIN32(GetLastError())};
    pacing033::Check(fence->SetEventOnCompletion(value,event.h));
    while(!pacing033::Completed(fence,value)){
        pacing033::CheckFault();if(GetTickCount64()-start>=limit)throw pacing033::Failure{DXGI_ERROR_WAIT_TIMEOUT};
        const auto result=WaitForSingleObject(event.h,1);if(result==WAIT_FAILED)throw pacing033::Failure{HRESULT_FROM_WIN32(GetLastError())};
        if(waitCallback){wchar_t name[]=L"033 scheduler fence";waitCallback(name,value);}
    }
    return true;
}

bool isTearingSupported(IDXGIFactory* dxgiFactory)
{
    BOOL bTearingSupported = FALSE;

    IDXGIFactory5* pFactory5 = nullptr;
    if (dxgiFactory && SUCCEEDED(dxgiFactory->QueryInterface(IID_PPV_ARGS(&pFactory5))))
    {
        
        if (SUCCEEDED(pFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bTearingSupported, sizeof(bTearingSupported)))) {

        }

        SafeRelease(pFactory5);
    }

    return bTearingSupported == TRUE;
}

inline bool isValidHandle(HANDLE handle)
{
    return handle != NULL;
}

bool isExclusiveFullscreen(IDXGISwapChain* swapChain)
{
    bool bIsExclusiveFullscreen = false;

    BOOL isFullscreen = FALSE;
    if (SUCCEEDED(swapChain->GetFullscreenState(&isFullscreen, nullptr)))
    {
        bIsExclusiveFullscreen = (isFullscreen == TRUE);
    }

    return bIsExclusiveFullscreen;
}

IDXGIOutput6* getMostRelevantOutputFromSwapChain(IDXGISwapChain* swapChain)
{
    IDXGIOutput6* pOutput6 = nullptr;

    IDXGIFactory* pFactory = getDXGIFactoryFromSwapChain(swapChain);
    if (pFactory)
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)))
        {
            UINT largestArea = 0;
            RECT windowRect{};
            if (GetWindowRect(desc.OutputWindow, &windowRect) == TRUE)
            {
                UINT          adapterIdx = 0;
                IDXGIAdapter* pAdapter   = nullptr;
                while (pFactory->EnumAdapters(adapterIdx++, &pAdapter) != DXGI_ERROR_NOT_FOUND)
                {
                    UINT         outputIdx = 0;
                    IDXGIOutput* pOutput   = nullptr;
                    while (pAdapter->EnumOutputs(outputIdx++, &pOutput) != DXGI_ERROR_NOT_FOUND)
                    {
                        DXGI_OUTPUT_DESC outputDesc{};
                        if (SUCCEEDED(pOutput->GetDesc(&outputDesc)))
                        {
                            RECT intersection{};
                            if (IntersectRect(&intersection, &windowRect, &outputDesc.DesktopCoordinates) == TRUE)
                            {
                                UINT area = (intersection.right - intersection.left) * (intersection.bottom - intersection.top);

                                if (area > largestArea)
                                {
                                    IDXGIOutput6* newOutput = nullptr;
                                    if (SUCCEEDED(pOutput->QueryInterface(IID_PPV_ARGS(&newOutput))))
                                    {
                                        largestArea = area;
                                        // release previous output before assigning new one
                                        SafeRelease(pOutput6);
                                        pOutput6 = newOutput;

                                    } else {
                                        // release result of failed query
                                        SafeRelease(newOutput);
                                    }
                                }
                            }
                        }

                        SafeRelease(pOutput);
                    }

                    SafeRelease(pAdapter);
                }
            }
        }
        SafeRelease(pFactory);
    }

    return pOutput6;
}

bool getMonitorLuminanceRange(IDXGISwapChain* swapChain, float *outMinLuminance, float *outMaxLuminance)
{
    bool bResult = false;

    IDXGIOutput6* dxgiOutput = getMostRelevantOutputFromSwapChain(swapChain);

    if (dxgiOutput)
    {
        DXGI_OUTPUT_DESC1 outputDesc1;
        if (SUCCEEDED(dxgiOutput->GetDesc1(&outputDesc1)))
        {
            *outMinLuminance = outputDesc1.MinLuminance;
            *outMaxLuminance = outputDesc1.MaxLuminance;
            bResult = true;
        }
        SafeRelease(dxgiOutput);
    }

    return bResult;
}

uint64_t GetResourceGpuMemorySize(ID3D12Resource* resource)
{
    uint64_t      size = 0;
    D3D12_RESOURCE_ALLOCATION_INFO allocInfo = {};
    if (resource)
    {
        D3D12_RESOURCE_DESC desc = resource->GetDesc();
        ID3D12Device4* pDevice4 = nullptr;
        if (SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&pDevice4))))
        {
            allocInfo = pDevice4->GetResourceAllocationInfo(0, 1, &desc);
            size = allocInfo.SizeInBytes;
            SafeRelease(pDevice4);
        }
    }
    return size;
}

