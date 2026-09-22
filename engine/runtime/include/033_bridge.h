#pragma once
#include "033_input.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Bridge K033_Bridge;
typedef struct K033_BridgeAttach {
    uint32_t size,version;
    void* device11;void* context11; // Immediate context, serialized by caller.
    void* device12;void* queue12; // Same adapter; DIRECT queue used by runtime.
} K033_BridgeAttach;
typedef struct K033_BridgeStatus {
    uint32_t size,version,in_flight,borrowed,stopping;
    int32_t fault;
    uint64_t submitted,acquired,retired,cached_bytes;
} K033_BridgeStatus;
// Internal same-process transport, NOT game discovery, x86 IPC or a UI host.
// No window/device creation, worker launch, GPU waits or CPU completion waits.
// Owner excludes Destroy races and serializes external context/queue use.
K033_API int K033_CALL K033_BridgeCreate(const K033_BridgeAttach*,K033_Bridge**);
// Original ungraded DX11 color, optionally paired native depth/motion textures.
// All explicit states are zero for DX11; guide rectangles/scales are retained.
// The caller identifies semantic resources from the SAME producer frame and
// serializes their updates on context11; this API does not discover textures.
// The caller
// retains its producer lease until this call returns; after OK the copy is
// ordered on context11. Bridge owns COM refs until completion/retirement.
K033_API int K033_CALL K033_BridgeSubmit(K033_Bridge*,const K033_InputRequest*,uint64_t* token);
// Poll both completion fences. Only after the DX11 copy completes is the DX12
// snapshot submitted. Returns BUSY until ready, then borrows it exactly once.
// out is directly usable by SubmitInput on device12/queue12; no grade applied.
// Output is immutable in COMMON. Restore COMMON before BridgeRelease.
// Supplied guides travel in the same fence/lease as color, with original extent,
// active rectangles, reversal/scales and reset. Unguided input keeps reset=1;
// that placeholder route is not high-quality replacement or native input proof.
// Acquire in ascending token order.
K033_API int K033_CALL K033_BridgeAcquire(K033_Bridge*,uint64_t token,K033_InputRequest* out);
// After ALL consumers are queued on queue12. Cancellation before Acquire is
// supported. Failed signal retains resources; no later use after Release OK.
K033_API int K033_CALL K033_BridgeRelease(K033_Bridge*,uint64_t token);
// Snapshot only; does not advance commands. Submit/Acquire/Release poll progress.
K033_API int K033_CALL K033_BridgeReadStatus(K033_Bridge*,K033_BridgeStatus*);
// Bounded progress/retirement when the owner currently has no new source frame.
K033_API int K033_CALL K033_BridgePoll(K033_Bridge*);
// Stops admission and cancels unborrowed frames. Borrowed frames must first be
// released. BUSY retains everything; device loss waives only THAT device's work.
K033_API int K033_CALL K033_BridgeDrain(K033_Bridge*);
K033_API int K033_CALL K033_BridgeDestroy(K033_Bridge**);
#ifdef __cplusplus
}
#endif
// Internal resource-bundle transport. Session remains the owner of same-frame
// metadata; these functions cannot manufacture native exposure provenance.
#ifdef __cplusplus
extern "C" {
#endif
K033_API int K033_CALL K033_BridgeSubmitWithExposure(K033_Bridge*,const K033_InputRequest*,const K033_Exposure*,uint64_t*);
K033_API int K033_CALL K033_BridgeAcquireWithExposure(K033_Bridge*,uint64_t,K033_InputRequest*,K033_Exposure*);
#ifdef __cplusplus
}
#endif
