// 033 beta 2 integration. ReShade is used under its retained BSD-3-Clause license.
#include "033_product.hpp"
#include "dll_log.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>

extern HMODULE g_module_handle;
extern std::filesystem::path get_module_path(HMODULE module);

namespace
{
    INIT_ONCE core_once = INIT_ONCE_STATIC_INIT;
    INIT_ONCE register_once = INIT_ONCE_STATIC_INIT;
    thread_local bool in_core_bootstrap = false;
    thread_local bool in_core_registration = false;
    std::atomic<HMODULE> core_module { nullptr };
    using renderer_entry = BOOL (WINAPI *)(HMODULE, DWORD, LPVOID);
    std::atomic<renderer_entry> core_renderer { nullptr };
    std::atomic<bool> renderer_registered { false };
    using allocator_entry = int (__cdecl *)(uint32_t, ID3D12GraphicsCommandList *, ID3D12CommandAllocator *);
    std::atomic<allocator_entry> core_allocator { nullptr };
    std::atomic<bool> allocator_failure_logged { false };

    BOOL CALLBACK load_core(PINIT_ONCE, PVOID, PVOID *)
    {
        // Load only the component owned by this installation. The absolute
        // path and DLL-load-dir search keep the caller's CWD out of lookup.
        const auto path = get_module_path(g_module_handle).parent_path() /
            L"033-runtime" / L"033-engine.dll";
        const HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        const DWORD load_error = module ? ERROR_SUCCESS : GetLastError();
        core_module.store(module, std::memory_order_release);
        if (!module)
        {
            reshade::log::message(reshade::log::level::error,
                "[033 startup] stage=load_core path=\"%ls\" win32=%lu.", path.c_str(), load_error);
            return TRUE; // One attempt per process; no repeated hook setup.
        }

        const auto renderer = reinterpret_cast<renderer_entry>(
            GetProcAddress(module, "K033_ReShadeEntry"));
        const auto prepare = reinterpret_cast<int (__cdecl *)()>(
            GetProcAddress(module, "K033_Beta2PrepareSettings"));
        const auto early = reinterpret_cast<int (__cdecl *)()>(
            GetProcAddress(module, "K033_Beta2EarlyInitialize"));
        if (!renderer || !prepare || !early)
        {
            reshade::log::message(reshade::log::level::error,
                "[033 startup] stage=resolve_exports K033_ReShadeEntry=%d K033_Beta2PrepareSettings=%d K033_Beta2EarlyInitialize=%d.",
                renderer != nullptr, prepare != nullptr, early != nullptr);
            // DllMain may already have installed process-wide hooks. Keep
            // that code mapped rather than unloading callbacks still in use.
            return TRUE;
        }
        // Both bootstrap ABI calls return K033_OK == 0 on success.
        const int settings_status = prepare();
        if (settings_status != 0)
        {
            reshade::log::message(reshade::log::level::error,
                "[033 startup] stage=prepare_settings status=%d.", settings_status);
            return TRUE;
        }
        const int early_status = early();
        if (early_status != 0)
        {
            reshade::log::message(reshade::log::level::error,
                "[033 startup] stage=early_initialize status=%d.", early_status);
            return TRUE;
        }
        // Resolve only the module loaded above. Publish after bootstrap; the
        // observation path must never load another module or rerun hooks.
        const auto allocator = reinterpret_cast<allocator_entry>(
            GetProcAddress(module, "K033_Beta2BindCommandAllocator"));
        if (!allocator)
            reshade::log::message(reshade::log::level::warning,
                "[033 startup] stage=resolve_allocator_export K033_Beta2BindCommandAllocator=0; allocator ownership remains unknown.");
        core_allocator.store(allocator, std::memory_order_release);
        core_renderer.store(renderer, std::memory_order_release);
        return TRUE;
    }

    BOOL CALLBACK register_core(PINIT_ONCE, PVOID, PVOID *)
    {
        const auto renderer = core_renderer.load(std::memory_order_acquire);
        const auto module = core_module.load(std::memory_order_acquire);
        const bool registered = renderer(module, DLL_PROCESS_ATTACH, nullptr) != FALSE;
        renderer_registered.store(registered, std::memory_order_release);
        if (!registered)
            reshade::log::message(reshade::log::level::error,
                "[033 startup] stage=register_renderer module=%p status=0; core entry rejected registration.", module);
        return TRUE;
    }
}

bool k033_ensure_core_loaded()
{
    // Core initialization can call a graphics entry recursively. Its nested
    // call must forward without waiting on its own InitOnce invocation.
    if (in_core_bootstrap)
        return false;
    in_core_bootstrap = true;
    BOOL initialized = FALSE;
    // This primitive-only wrapper has no C++ unwind objects. Restore the TLS
    // flag even if the loader/callback raises an exception; do not swallow it.
    __try { initialized = InitOnceExecuteOnce(&core_once, load_core, nullptr, nullptr); }
    __finally { in_core_bootstrap = false; }
    return initialized && core_renderer.load(std::memory_order_acquire) != nullptr;
}

bool k033_register_renderer()
{
    // A recursive device creation while loading the core must not consume the
    // registration once before bootstrap publishes the complete interfaces.
    if (in_core_bootstrap || in_core_registration || !k033_ensure_core_loaded())
        return false;
    in_core_registration = true;
    BOOL initialized = FALSE;
    __try { initialized = InitOnceExecuteOnce(&register_once, register_core, nullptr, nullptr); }
    __finally { in_core_registration = false; }
    return initialized && renderer_registered.load(std::memory_order_acquire);
}

void k033_unregister_renderer()
{
    // Add-on manager references describe devices, not the process lifetime.
    // Keep registrations and native hooks together. Real destroy_device /
    // destroy_effect_runtime events already release their device resources.
    // Do not synthesize DLL_PROCESS_DETACH or call ATTACH again on recreation.
}

bool k033_is_core_module(void *module)
{
    return module != nullptr && module == core_module.load(std::memory_order_acquire);
}

void k033_bind_command_allocator(ID3D12GraphicsCommandList *list, ID3D12CommandAllocator *allocator)
{
    if (!list || !allocator)
        return;
    const auto callback = core_allocator.load(std::memory_order_acquire);
    if (!callback)
        return;
    const int status = callback(1, list, allocator);
    if (status != 0 && !allocator_failure_logged.exchange(true, std::memory_order_relaxed))
        reshade::log::message(reshade::log::level::warning,
            "[033 allocator] exact native association rejected status=%d; ownership remains unknown.", status);
}
