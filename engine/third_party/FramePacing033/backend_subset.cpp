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


#include "FrameInterpolationSwapchainDX12.h"
FfxDevice ffxGetDeviceDX12(ID3D12Device* dx12Device)
{
    FFX_ASSERT(NULL != dx12Device);
    return reinterpret_cast<FfxDevice>(dx12Device);
}

FfxCommandList ffxGetCommandListDX12(ID3D12CommandList* cmdList)
{
    FFX_ASSERT(NULL != cmdList);
    return reinterpret_cast<FfxCommandList>(cmdList);
}

FfxApiResource ffxGetResourceDX12(const ID3D12Resource* dx12Resource,
    FfxApiResourceDescription ffxResDescription,
    const wchar_t* ffxResName,
    uint32_t state /*=FFX_API_RESOURCE_STATE_COMPUTE_READ*/)
{
    FfxApiResource resource = {};
    resource.resource    = reinterpret_cast<void*>(const_cast<ID3D12Resource*>(dx12Resource));
    resource.state = state;
    resource.description = ffxResDescription;

    FFX_UNUSED(ffxResName);

    return resource;
}

D3D12_RESOURCE_STATES ffxGetDX12StateFromResourceState(FfxApiResourceState state)
{
    switch (state) {

        case FFX_API_RESOURCE_STATE_GENERIC_READ:
            return D3D12_RESOURCE_STATE_GENERIC_READ;
        case FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case FFX_API_RESOURCE_STATE_COMPUTE_READ:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case FFX_API_RESOURCE_STATE_PIXEL_READ:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case FFX_API_RESOURCE_STATE_COPY_SRC:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case FFX_API_RESOURCE_STATE_COPY_DEST:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT:
            return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case FFX_API_RESOURCE_STATE_PRESENT:
            return D3D12_RESOURCE_STATE_PRESENT;
        case FFX_API_RESOURCE_STATE_COMMON:
            return D3D12_RESOURCE_STATE_COMMON;
        case FFX_API_RESOURCE_STATE_RENDER_TARGET:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case FFX_API_RESOURCE_STATE_DEPTH_ATTACHMENT:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        default:
            FFX_ASSERT_MESSAGE(false, "Resource state not yet supported");
            return D3D12_RESOURCE_STATE_COMMON;
    }
}

FfxApiSurfaceFormat ffxGetSurfaceFormatDX12(DXGI_FORMAT format)
{
    switch (format) {

        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R32G32B32A32_TYPELESS;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32_FLOAT:
            return FFX_API_SURFACE_FORMAT_R32G32B32_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_UINT:
            return FFX_API_SURFACE_FORMAT_R32G32B32A32_UINT;

        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;

        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
            return FFX_API_SURFACE_FORMAT_R32G32_FLOAT;

        case DXGI_FORMAT_R32G32_UINT:
            return FFX_API_SURFACE_FORMAT_R32G32_UINT;

        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R32_FLOAT;

        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R32_UINT;

        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
            return FFX_API_SURFACE_FORMAT_R8_UINT;

        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
        
        case DXGI_FORMAT_R11G11B10_FLOAT:
            return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;

        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB;
        case DXGI_FORMAT_R8G8B8A8_SNORM:
            return FFX_API_SURFACE_FORMAT_R8G8B8A8_SNORM;

        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return FFX_API_SURFACE_FORMAT_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB;

        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16_FLOAT:
            return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R16G16_UINT:
            return FFX_API_SURFACE_FORMAT_R16G16_UINT;
        case DXGI_FORMAT_R16G16_SINT:
            return FFX_API_SURFACE_FORMAT_R16G16_SINT;
        case DXGI_FORMAT_R32_UINT:
            return FFX_API_SURFACE_FORMAT_R32_UINT;
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
            return FFX_API_SURFACE_FORMAT_R32_FLOAT;

        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8_UINT:
            return FFX_API_SURFACE_FORMAT_R8G8_UINT;
        case DXGI_FORMAT_R8G8_UNORM:
            return FFX_API_SURFACE_FORMAT_R8G8_UNORM;

        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_FLOAT:
            return FFX_API_SURFACE_FORMAT_R16_FLOAT;
        case DXGI_FORMAT_R16_UINT:
            return FFX_API_SURFACE_FORMAT_R16_UINT;
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
            return FFX_API_SURFACE_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R16_SNORM:
            return FFX_API_SURFACE_FORMAT_R16_SNORM;

        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_A8_UNORM:
            return FFX_API_SURFACE_FORMAT_R8_UNORM;
        case DXGI_FORMAT_R8_SNORM:
            return FFX_API_SURFACE_FORMAT_R8_SNORM;
        case DXGI_FORMAT_R8_UINT:
            return FFX_API_SURFACE_FORMAT_R8_UINT;

        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
            return FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP;

        case DXGI_FORMAT_UNKNOWN:
            return FFX_API_SURFACE_FORMAT_UNKNOWN;
        default:
            FFX_ASSERT_MESSAGE(false, "Format not yet supported");
            return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

bool IsDepthDX12(DXGI_FORMAT format)
{
    return (format == DXGI_FORMAT_D16_UNORM) || 
           (format == DXGI_FORMAT_D32_FLOAT) || 
           (format == DXGI_FORMAT_D24_UNORM_S8_UINT) ||
           (format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
}

bool IsStencilDX12(DXGI_FORMAT format)
{
    return (format == DXGI_FORMAT_D24_UNORM_S8_UINT) || (format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
}

FfxApiResourceDescription ffxGetResourceDescriptionDX12(const ID3D12Resource* pResource, FfxApiResourceUsage additionalUsages /*=FFX_API_RESOURCE_USAGE_READ_ONLY*/)
{
    FfxApiResourceDescription resourceDescription = {};

    // This is valid
    if (!pResource)
        return resourceDescription;

    D3D12_RESOURCE_DESC desc = const_cast<ID3D12Resource*>(pResource)->GetDesc();
        
    if( desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        resourceDescription.flags  = FFX_API_RESOURCE_FLAGS_NONE;
        resourceDescription.usage  = FFX_API_RESOURCE_USAGE_UAV;
        resourceDescription.size  = (uint32_t)desc.Width;
        resourceDescription.stride = (uint32_t)desc.Height;
        resourceDescription.format = ffxGetSurfaceFormatDX12(desc.Format);

        // What should we initialize this to?? No case for this yet
        resourceDescription.depth    = 0;
        resourceDescription.mipCount = 0;

        // Set the type
        resourceDescription.type = FFX_API_RESOURCE_TYPE_BUFFER;
    }
    else
    {
        // Set flags properly for resource registration
        resourceDescription.flags     = FFX_API_RESOURCE_FLAGS_NONE;
           
        // Check for depth use
        resourceDescription.usage     = IsDepthDX12(desc.Format) ? FFX_API_RESOURCE_USAGE_DEPTHTARGET : FFX_API_RESOURCE_USAGE_READ_ONLY;
            
        if (IsStencilDX12(desc.Format))
            resourceDescription.usage = (FfxApiResourceUsage)(resourceDescription.usage | FFX_API_RESOURCE_USAGE_STENCILTARGET);

        // Unordered access use
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
            resourceDescription.usage = (FfxApiResourceUsage)(resourceDescription.usage | FFX_API_RESOURCE_USAGE_UAV);

        // Resource-specific supplemental use flags
        resourceDescription.usage    = (FfxApiResourceUsage)(resourceDescription.usage | additionalUsages);

        resourceDescription.width    = (uint32_t)desc.Width;
        resourceDescription.height   = (uint32_t)desc.Height;
        resourceDescription.depth    = desc.DepthOrArraySize;
        resourceDescription.mipCount = desc.MipLevels;
        resourceDescription.format   = ffxGetSurfaceFormatDX12(desc.Format);

        switch (desc.Dimension)
        {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE1D;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (FFX_CONTAINS_FLAG(additionalUsages, FFX_API_RESOURCE_USAGE_ARRAYVIEW))
                resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            else if (desc.DepthOrArraySize == 1)
                resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            else if (desc.DepthOrArraySize == 6)
                resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE_CUBE;
            else
                resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE3D;
            break;
        default:
            FFX_ASSERT_MESSAGE(false, "FFXInterface: DX12: Unsupported texture dimension requested. Please implement.");
            break;
        }
    }

    return resourceDescription;
}

FfxCommandQueue ffxGetCommandQueueDX12(ID3D12CommandQueue* pCommandQueue)
{
    FFX_ASSERT(nullptr != pCommandQueue);
    return reinterpret_cast<FfxCommandQueue>(pCommandQueue);
}

FfxSwapchain ffxGetSwapchainDX12(IDXGISwapChain4* pSwapchain)
{
    FFX_ASSERT(nullptr != pSwapchain);
    return reinterpret_cast<FfxSwapchain>(pSwapchain);
}

IDXGISwapChain4* ffxGetDX12SwapchainPtr(FfxSwapchain ffxSwapchain)
{
    return reinterpret_cast<IDXGISwapChain4*>(ffxSwapchain);
}


DXGI_FORMAT ffxGetDX12FormatFromSurfaceFormat(FfxApiSurfaceFormat surfaceFormat)
{
    switch (surfaceFormat) 
    {
        case (FFX_API_SURFACE_FORMAT_R32G32B32A32_TYPELESS):
            return DXGI_FORMAT_R32G32B32A32_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R32G32B32A32_UINT):
            return DXGI_FORMAT_R32G32B32A32_UINT;
        case (FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT):
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R16G16B16A16_TYPELESS):
            return DXGI_FORMAT_R16G16B16A16_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT):
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R32G32B32_FLOAT):
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R32G32_TYPELESS):
            return DXGI_FORMAT_R32G32_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R32G32_FLOAT):
            return DXGI_FORMAT_R32G32_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R32G32_UINT):
            return DXGI_FORMAT_R32G32_UINT;
        case (FFX_API_SURFACE_FORMAT_R32_TYPELESS):
            return DXGI_FORMAT_R32_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R32_UINT):
            return DXGI_FORMAT_R32_UINT;
        case(FFX_API_SURFACE_FORMAT_R10G10B10A2_TYPELESS):
            return DXGI_FORMAT_R10G10B10A2_TYPELESS;
        case(FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM):
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case (FFX_API_SURFACE_FORMAT_R8G8B8A8_TYPELESS):
            return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM):
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case (FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB):
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case (FFX_API_SURFACE_FORMAT_R8G8B8A8_SNORM):
            return DXGI_FORMAT_R8G8B8A8_SNORM;
        case (FFX_API_SURFACE_FORMAT_B8G8R8A8_TYPELESS):
            return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM):
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case (FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB):
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case (FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT):
            return DXGI_FORMAT_R11G11B10_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R16G16_TYPELESS):
            return DXGI_FORMAT_R16G16_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R16G16_FLOAT):
            return DXGI_FORMAT_R16G16_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R16G16_UINT):
            return DXGI_FORMAT_R16G16_UINT;
        case (FFX_API_SURFACE_FORMAT_R16G16_SINT):
            return DXGI_FORMAT_R16G16_SINT;
        case (FFX_API_SURFACE_FORMAT_R16_TYPELESS):
            return DXGI_FORMAT_R16_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R16_FLOAT):
            return DXGI_FORMAT_R16_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R16_UINT):
            return DXGI_FORMAT_R16_UINT;
        case (FFX_API_SURFACE_FORMAT_R16_UNORM):
            return DXGI_FORMAT_R16_UNORM;
        case (FFX_API_SURFACE_FORMAT_R16_SNORM):
            return DXGI_FORMAT_R16_SNORM;
        case (FFX_API_SURFACE_FORMAT_R8_TYPELESS):
            return DXGI_FORMAT_R8_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R8_UNORM):
            return DXGI_FORMAT_R8_UNORM;
        case (FFX_API_SURFACE_FORMAT_R8_SNORM):
            return DXGI_FORMAT_R8_SNORM;
        case (FFX_API_SURFACE_FORMAT_R8_UINT):
            return DXGI_FORMAT_R8_UINT;
        case (FFX_API_SURFACE_FORMAT_R8G8_UINT):
            return DXGI_FORMAT_R8G8_UINT;
        case (FFX_API_SURFACE_FORMAT_R8G8_TYPELESS):
            return DXGI_FORMAT_R8G8_TYPELESS;
        case (FFX_API_SURFACE_FORMAT_R8G8_UNORM):
            return DXGI_FORMAT_R8G8_UNORM;
        case (FFX_API_SURFACE_FORMAT_R32_FLOAT):
            return DXGI_FORMAT_R32_FLOAT;
        case (FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP):
            return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        case (FFX_API_SURFACE_FORMAT_UNKNOWN):
            return DXGI_FORMAT_UNKNOWN;

        default:
            FFX_ASSERT_MESSAGE(false, "Format not yet supported");
            return DXGI_FORMAT_UNKNOWN;
    }
}

D3D12_RESOURCE_FLAGS ffxGetDX12ResourceFlags(FfxApiResourceUsage flags)
{
    D3D12_RESOURCE_FLAGS dx12ResourceFlags = D3D12_RESOURCE_FLAG_NONE;
    if (flags & FFX_API_RESOURCE_USAGE_RENDERTARGET) dx12ResourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (flags & FFX_API_RESOURCE_USAGE_UAV) dx12ResourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return dx12ResourceFlags;
}

FfxErrorCode ffxGetResourceSizeFromDescriptionDX12(FfxDevice device, const FfxCreateResourceDescription* createResourceDescription, uint64_t* sizeInBytes, uint64_t* alignment)
{
    FFX_ASSERT(NULL != device);
    FFX_ASSERT(NULL != createResourceDescription);
    FFX_ASSERT(NULL != sizeInBytes);

    if (createResourceDescription->heapInfo.heapType == FFX_HEAP_TYPE_UPLOAD) 
    {
        if (alignment) 
        {
            *alignment = 1u;
        }
        *sizeInBytes = 0u;
        return FFX_OK;
    }

    D3D12_RESOURCE_DESC dx12ResourceDescription = {};
    dx12ResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
    dx12ResourceDescription.Width = 1;
    dx12ResourceDescription.Height = 1;
    dx12ResourceDescription.MipLevels = 1;
    dx12ResourceDescription.DepthOrArraySize = 1;
    dx12ResourceDescription.SampleDesc.Count = 1;
    dx12ResourceDescription.Flags = ffxGetDX12ResourceFlags((FfxApiResourceUsage)createResourceDescription->resourceDescription.usage);

    switch (createResourceDescription->resourceDescription.type) {

    case FFX_API_RESOURCE_TYPE_BUFFER:
        dx12ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        dx12ResourceDescription.Width = createResourceDescription->resourceDescription.width;
        dx12ResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        break;

    case FFX_API_RESOURCE_TYPE_TEXTURE1D:
        dx12ResourceDescription.Format = ffxGetDX12FormatFromSurfaceFormat((FfxApiSurfaceFormat)createResourceDescription->resourceDescription.format);
        dx12ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        dx12ResourceDescription.Width = createResourceDescription->resourceDescription.width;
        dx12ResourceDescription.DepthOrArraySize = UINT16(createResourceDescription->resourceDescription.depth);
        dx12ResourceDescription.MipLevels = UINT16(createResourceDescription->resourceDescription.mipCount);
        break;

    case FFX_API_RESOURCE_TYPE_TEXTURE2D:
        dx12ResourceDescription.Format = ffxGetDX12FormatFromSurfaceFormat((FfxApiSurfaceFormat)createResourceDescription->resourceDescription.format);
        dx12ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dx12ResourceDescription.Width = createResourceDescription->resourceDescription.width;
        dx12ResourceDescription.Height = createResourceDescription->resourceDescription.height;
        dx12ResourceDescription.DepthOrArraySize = UINT16(createResourceDescription->resourceDescription.depth);
        dx12ResourceDescription.MipLevels = UINT16(createResourceDescription->resourceDescription.mipCount);
        break;

    case FFX_API_RESOURCE_TYPE_TEXTURE_CUBE:
    case FFX_API_RESOURCE_TYPE_TEXTURE3D:
        dx12ResourceDescription.Format = ffxGetDX12FormatFromSurfaceFormat((FfxApiSurfaceFormat)createResourceDescription->resourceDescription.format);
        dx12ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        dx12ResourceDescription.Width = createResourceDescription->resourceDescription.width;
        dx12ResourceDescription.Height = createResourceDescription->resourceDescription.height;
        dx12ResourceDescription.DepthOrArraySize = UINT16(createResourceDescription->resourceDescription.depth);
        dx12ResourceDescription.MipLevels = UINT16(createResourceDescription->resourceDescription.mipCount);
        break;

    default:
        break;
    }

    ID3D12Device* pDevice = nullptr;
    if (SUCCEEDED(reinterpret_cast< IUnknown* >(device)->QueryInterface(IID_PPV_ARGS(&pDevice))))
    {
        const D3D12_RESOURCE_ALLOCATION_INFO allocInfo = pDevice->GetResourceAllocationInfo(0, 1, &dx12ResourceDescription);
        pDevice->Release();
        *sizeInBytes = allocInfo.SizeInBytes;
        if (alignment)
        {
            *alignment = allocInfo.Alignment > 0 ? allocInfo.Alignment : 1u;
        }

        if (allocInfo.SizeInBytes != UINT64_MAX)
        {
            return FFX_OK;
        }
    }

    return FFX_ERROR_INVALID_ARGUMENT;
}
FfxErrorCode GetResourceSizeFromDescription(FfxDevice device,const FfxCreateResourceDescription* desc,uint64_t* size,uint64_t* alignment){return ffxGetResourceSizeFromDescriptionDX12(device,desc,size,alignment);}
