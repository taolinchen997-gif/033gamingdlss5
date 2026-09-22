#define CINTERFACE
#include <d3d12.h>
#include <cstddef>
#include <cstdio>
#include "nr_native_input_events.h"
static_assert(offsetof(ID3D12GraphicsCommandListVtbl,ResourceBarrier)/sizeof(void*)==nrnative033::ResourceBarrierSlot);
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Barrier)/sizeof(void*)==nrnative033::EnhancedBarrierSlot);
int main(){puts("Native input COM slots: 2 compile-time ABI checks; no COM object or GPU created");}
