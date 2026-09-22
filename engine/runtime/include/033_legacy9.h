#pragma once
#include "033_remote.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Legacy9 K033_Legacy9;
enum {K033_LEGACY9_GAME_OWNED_DEVICE=1};
typedef struct K033_Legacy9Attach {
    uint32_t size,version;void* device9;K033_WorkerLaunch worker;uint64_t generation;
    uint32_t frame_timeout_ms;
    union {uint32_t reserved;uint32_t flags;};
} K033_Legacy9Attach;
typedef struct K033_Legacy9Status {
    uint32_t size,version,can_present,stopping;int32_t error;uint32_t checked_out;
    uint64_t admitted,returned;
} K033_Legacy9Status;
typedef struct K033_Legacy9RetirementStatus {
    uint32_t size,version,capacity,free_slots,reserved,pending,polling,quarantined;
    uint64_t submitted,completed;
} K033_Legacy9RetirementStatus;
// Requires a real IDirect3DDevice9On12. The compatibility/creation adapter
// selects that system route; a native D3D9 device is explicitly UNSUPPORTED.
K033_API int K033_CALL K033_Legacy9Create(const K033_Legacy9Attach*,K033_Legacy9**);
// Called before the original D3D9 Present, with its actual backbuffer surface.
// Caller serializes all translation-layer access across this boundary.
// On error inspect can_present: never invoke D3D9 on a checked-out resource.
K033_API int K033_CALL K033_Legacy9Process(K033_Legacy9*,void* surface9,uint64_t sequence,const K033_Settings*);
K033_API int K033_CALL K033_Legacy9ReadStatus(K033_Legacy9*,K033_Legacy9Status*);
// Attach v1 keeps the device owned (reserved=0), as before. Attach v2 may use
// GAME_OWNED_DEVICE: Create/Process run on the game device/window owner thread;
// the adapter keeps the device valid through each call. Successful Detach stops
// new frames and releases the borrowed identity. Only after that success may
// Drain/Destroy run on a background reaper. BUSY still belongs to the adapter.
// No final D3D9 device Release may be moved to the background worker/reaper.
K033_API int K033_CALL K033_Legacy9Detach(K033_Legacy9*);
// For a final game-side release, transfer a v2 game-owned instance into its
// pre-reserved 033 retirement slot. *p=NULL proves ownership transfer (or an
// immediate clean destruction), NOT GPU completion. OK=detached/clean;
// BYPASS=checked-out D3D9 references quarantined until process exit, while the
// remote worker is still stopped. Other results leave *p owned by the caller.
// A wrong-thread final Retire also uses quarantine; it never bypasses the
// D3D9 thread-affinity gate or calls final D3D9 Release on that thread.
// Creation of v2 game-owned instances starts the internal reaper automatically;
// Create/Retire must be outside DllMain. No window, console or manual host.
K033_API int K033_CALL K033_Legacy9Retire(K033_Legacy9**);
K033_API int K033_CALL K033_Legacy9ReadRetirementStatus(K033_Legacy9RetirementStatus*);
K033_API int K033_CALL K033_Legacy9Drain(K033_Legacy9*);
K033_API int K033_CALL K033_Legacy9Destroy(K033_Legacy9**);
#ifdef __cplusplus
}
#endif
