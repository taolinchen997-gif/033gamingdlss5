#pragma once
#include <stdint.h>
#include <wchar.h>
#ifdef _WIN32
#define K033_CALL __cdecl
#ifdef K033_RUNTIME_BUILD
#define K033_API __declspec(dllexport)
#else
#define K033_API
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Runtime K033_Runtime;
enum K033_Result { K033_OK=0, K033_BYPASS=1, K033_BUSY=2, K033_INVALID=-1,
    K033_UNSUPPORTED=-2, K033_DEVICE_LOST=-3, K033_BACKEND_ERROR=-4, K033_IO_ERROR=-5 };
enum K033_Backend { K033_D3D11=11, K033_D3D12=12 };
enum K033_Color { K033_LINEAR=0, K033_SRGB=1, K033_HDR10=2, K033_SCRGB=3 };
enum K033_Source { K033_SOURCE_NATIVE=0, K033_SOURCE_WINDOW_CAPTURE=1 };
// ABI v1: no legacy NR/FG controls are silently applied or claimed supported.
typedef struct K033_Settings {
    uint32_t size, version, enabled, style;
    float exposure, contrast, saturation, warmth, tint, highlights, style_strength;
} K033_Settings;
// Shared settings persistence receipt, not GPU/model adoption.
typedef struct __declspec(align(8)) K033_PreferencesStatus {
    uint32_t size,version;uint64_t queued,saved;
    int32_t load_result,save_result;uint32_t active,win32_error,pending,reserved;
} K033_PreferencesStatus;
typedef struct K033_Attach {
    uint32_t size, version, backend;
    void* device; // ID3D11Device or ID3D12Device; backend holds strong references.
    void* queue; // D3D11 immediate context or D3D12 DIRECT command queue.
} K033_Attach;
typedef struct K033_Frame {
    uint32_t size, version, color, source;
    void* texture; // ID3D11Texture2D or ID3D12Resource; same device as Attach.
    uint64_t sequence, epoch; // monotonic sequence inside a nonzero source epoch
    float diffuse_white;
    uint32_t before_state, after_state; // exact D3D12 state; no pending split barrier
} K033_Frame;
typedef struct K033_Status {
    uint32_t size, version, backend, attached, stopping, faulted;
    uint32_t in_flight, enabled;
    uint64_t submitted, completed, bypassed, rejected;
    // submitted means queued commands, NOT a displayed frame.
} K033_Status;
K033_API int K033_CALL K033_Create(K033_Runtime** out);
K033_API int K033_CALL K033_AttachDevice(K033_Runtime*, const K033_Attach*);
K033_API int K033_CALL K033_Process(K033_Runtime*, const K033_Frame*);
K033_API int K033_CALL K033_GetSettings(K033_Runtime*, K033_Settings*);
K033_API int K033_CALL K033_SetSettings(K033_Runtime*, const K033_Settings*);
K033_API int K033_CALL K033_ReadStatus(K033_Runtime*, K033_Status*);
// Feed real host key events (F11=0x7A toggles NR only); common processing and
// native FG intent are unchanged. Never polls or controls the desktop.
K033_API int K033_CALL K033_Key(K033_Runtime*, uint32_t key, uint32_t down, uint32_t repeat, uint32_t focused);
// Dedicated independent config only. Paths use the current Windows ANSI codepage.
// Preserve unknown keys; invalid owned values reject the whole load transaction.
K033_API int K033_CALL K033_LoadConfig(K033_Runtime*, const char* path);
K033_API int K033_CALL K033_SaveConfig(K033_Runtime*, const char* path);
// Same settings/schema, Unicode filename; no per-game defaults or profiles.
K033_API int K033_CALL K033_LoadConfigW(K033_Runtime*, const wchar_t* path);
K033_API int K033_CALL K033_SaveConfigW(K033_Runtime*, const wchar_t* path);
// Nonblocking: BUSY keeps all references; call again after normal queue progress.
// Stop presenting before draining. Device removal permits terminal cleanup.
K033_API int K033_CALL K033_Drain(K033_Runtime*);
// Destroy only after Drain succeeds; failed destruction leaves *runtime intact.
K033_API int K033_CALL K033_Destroy(K033_Runtime** runtime);
#ifdef __cplusplus
}
#endif
