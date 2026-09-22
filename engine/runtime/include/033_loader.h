#pragma once
#include "033_worker.h"
#ifdef K033_LOADER_BUILD
#define K033_LOADER_API __declspec(dllexport)
#else
#define K033_LOADER_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Trusted internal package owner supplies its catalog, not game/user input.
// root is absolute. Fixed relative payload names are not caller-controlled:
// 033-worker-client32.dll, compat32/033-compat32.dll, 033-worker64.exe.
typedef struct K033_LoaderPackage {
    uint32_t size,version,route,reserved;const wchar_t* root;
    uint8_t client_sha256[32],compat_sha256[32],worker_sha256[32];
    uint32_t startup_timeout_ms,stop_timeout_ms;K033_Settings settings;
} K033_LoaderPackage;
typedef struct K033_LoaderStatus {
    uint32_t size,version,phase,route,stage,process_lifetime;
    int32_t result;uint32_t win32_error;
} K033_LoaderStatus;
// Process-wide route/package identity is immutable after the first attempt.
// 0=cold,1=starting,2=initialized,3=terminal failure. Initialized is NOT GPU,
// adapter, NR/FG, game or displayed-frame acceptance. Call outside DllMain.
K033_LOADER_API int K033_CALL K033_LoaderStart(const K033_LoaderPackage*);
K033_LOADER_API int K033_CALL K033_LoaderReadStatus(K033_LoaderStatus*);
K033_LOADER_API int K033_CALL K033_LoaderGetSettings(K033_Settings*);
K033_LOADER_API int K033_CALL K033_LoaderSetSettings(const K033_Settings*);
// Feed the existing 033 game input events; no independent polling or UI.
K033_LOADER_API int K033_CALL K033_LoaderKey(uint32_t key,uint32_t down,uint32_t repeat,uint32_t focused);
#ifdef __cplusplus
}
#endif
