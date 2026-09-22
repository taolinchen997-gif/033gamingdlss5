// 033 beta 2 integration. Upstream ReShade retains its original notices.
#pragma once
#include <Windows.h>

// Called at exported graphics entry points, before forwarding device/factory
// creation, never from the ReShade DllMain. No window or background launcher.
bool k033_ensure_core_loaded();
bool k033_register_renderer();
void k033_unregister_renderer();
bool k033_is_core_module(void *module);

// Exact successful native Create/Reset observation; never triggers core loading.
struct ID3D12GraphicsCommandList;
struct ID3D12CommandAllocator;
void k033_bind_command_allocator(ID3D12GraphicsCommandList *list, ID3D12CommandAllocator *allocator);
