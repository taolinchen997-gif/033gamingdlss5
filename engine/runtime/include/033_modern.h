#pragma once
#include "033_ngx_owner.h"
#include "033_nr.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_ModernStatus {
    uint32_t size,version,phase,import_slots,owners,retiring,gateway_bound;
    int32_t result;
    uint64_t lookups,creates,evaluates;
} K033_ModernStatus;
typedef struct K033_ModernControlsStatus {
    uint32_t size,version,phase;int32_t result;
} K033_ModernControlsStatus;
// Control service is independent of NGX observation. A ready control service
// proves neither native input availability nor NR/frame processing.
K033_API int K033_CALL K033_ModernControlsStart(void);
K033_API int K033_CALL K033_ModernReadControlsStatus(K033_ModernControlsStatus*);
K033_API int K033_CALL K033_ModernReadPreferences(K033_PreferencesStatus*);
typedef struct K033_ModernFrame {
    uint32_t size,version;uint64_t owner_token;
    void* context11; // Borrowed identity only; not a background command context.
    K033_Ngx11OwnedFrame input;
} K033_ModernFrame;
typedef struct K033_ModernFrame3 {
    uint32_t size,version;uint64_t owner_token;void* context11;K033_Ngx11OwnedFrame3 input;
} K033_ModernFrame3;
K033_API int K033_CALL K033_ModernAcquire3(K033_ModernFrame3*);
typedef struct K033_ModernSrFrame {
    uint32_t size,version;uint64_t owner_token;void* context11;K033_Ngx11SrFrame frame;
} K033_ModernSrFrame;
K033_API int K033_CALL K033_ModernAcquireSr(K033_ModernSrFrame*);
K033_API int K033_CALL K033_ModernReleaseSr(uint64_t owner_token,uint64_t sr_lease);
// Internal production entry bootstrap, never DllMain. Native implementation
// observes GetProcAddress imports in the main executable; cached lookups and
// other modules/static NGX imports require additional adapters. No helper UI.
K033_API int K033_CALL K033_ModernStart(void);
K033_API int K033_CALL K033_ModernReadStatus(K033_ModernStatus*);
K033_API int K033_CALL K033_ModernGetSettings(K033_Settings*);
K033_API int K033_CALL K033_ModernSetSettings(const K033_Settings*);
K033_API int K033_CALL K033_ModernKey(uint32_t key,uint32_t down,uint32_t repeat,uint32_t focused);
// Internal model consumer. Exact source owner stays alive across release or
// shutdown until this frame is surrendered; queue consumers as OwnedFrame says.
K033_API int K033_CALL K033_ModernAcquire(K033_ModernFrame*);
K033_API int K033_CALL K033_ModernRelease(uint64_t owner_token,uint64_t lease);
K033_API int K033_CALL K033_ModernGetNrSettings(K033_NrSettings*);
K033_API int K033_CALL K033_ModernSetNrSettings(const K033_NrSettings*);
// UI edit adapter: BUSY only for a confirmed stale epoch; true invalid values
// retain INVALID. The original SetNrSettings ABI keeps its old result semantics.
K033_API int K033_CALL K033_ModernEditNrSettings(const K033_NrSettings*);
K033_API int K033_CALL K033_ModernReadNrPreferences(K033_NrPreferencesStatus*);
#ifdef __cplusplus
}
#endif
