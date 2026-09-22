#pragma once
#include <cstdint>
#include <dxgi1_6.h>
#include <d3d12.h>
// Private image-only scheduler. This is NOT the AMD provider ABI and must not
// be loaded as a replacement for the game's native frame-generation library.
namespace ufgprovider033 {
constexpr uint32_t Version=1,MaxGenerated=2;
struct Stats {uint32_t size=sizeof(Stats),version=Version;uint64_t real=0,generated=0,failed=0;int32_t error=0;uint64_t workingBytes=0;};
struct Api {uint32_t size=sizeof(Api),version=Version,maxGenerated=MaxGenerated;
 HRESULT(__cdecl* create)(HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,ID3D12CommandQueue*,IDXGIFactory*,IDXGISwapChain4**);
 int(__cdecl* read)(IDXGISwapChain4*,Stats*);
};
using GetApi=const Api*(__cdecl*)(uint32_t);
}
