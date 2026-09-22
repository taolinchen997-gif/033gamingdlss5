#pragma once
#include "033_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
enum K033_PresentationEntry {
    K033_ENTRY_DXGI_FACTORY=1,K033_ENTRY_DXGI_FACTORY1=2,
    K033_ENTRY_DXGI_FACTORY2=3,K033_ENTRY_D3D11_SWAPCHAIN=4,
    K033_ENTRY_D3D11_DEVICE=5
};
// Internal exact-prototype entry binding; NULL means use the original system
// address. No existing object is adopted and no GPU/UI capability is implied.
K033_API void* K033_CALL K033_ModernBindPresentationEntry(uint32_t kind,void* original);
#ifdef __cplusplus
}
#endif
