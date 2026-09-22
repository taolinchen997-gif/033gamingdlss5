#pragma once
#include "033_worker.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Remote K033_Remote;
typedef struct K033_RemoteAttach {
    uint32_t size,version;K033_Attach graphics;K033_WorkerLaunch worker;
    uint32_t width,height,format,color;uint64_t epoch;
    uint32_t frame_timeout_ms,reserved; // 100..30000; no GPU timer queries
} K033_RemoteAttach;
typedef struct K033_RemoteStatus {
    uint32_t size,version,phase,pending;int32_t error;uint32_t stopping;
    uint64_t submitted,copied_back; // copied_back records queued copies, not displayed frames
} K033_RemoteStatus;
// The trusted in-game adapter supplies the current device/context or DIRECT
// queue. Both x86 and x64 use the same API and hash-checked windowless x64 worker.
// Source validation and final Present ownership remain with that adapter.
K033_API int K033_CALL K033_RemoteCreate(const K033_RemoteAttach*,K033_Remote**);
// Called on the original renderer's serialized presentation boundary. Copies
// this frame's original color BEFORE any completed older output is copied back.
// No GPU Wait. BUSY/BYPASS leave game Present available. Settings are the same
// panel snapshot; changed settings discard old output. One fixed-size epoch.
K033_API int K033_CALL K033_RemoteProcess(K033_Remote*,const K033_Frame*,const K033_Settings*);
K033_API int K033_CALL K033_RemoteReadStatus(K033_Remote*,K033_RemoteStatus*);
// Retain owner until OK. Both local GPU completion and worker GPU retirement
// are required. A killed worker with unproven writes stays quarantined until
// device loss/process exit; never recycle those textures as a successful stop.
K033_API int K033_CALL K033_RemoteDrain(K033_Remote*);
K033_API int K033_CALL K033_RemoteDestroy(K033_Remote**);
#ifdef __cplusplus
}
#endif
