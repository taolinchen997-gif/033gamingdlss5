#pragma once
#include "033_sr.h"
#include "033_session.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Output11 K033_Output11;
// INTERNAL trusted render-adapter contract, not another user-facing host.
// The adapter owns the exact scene output interval BEFORE later HUD/postprocess
// reads. It must keep that interval open until Commit or Cancel succeeds.
// Identical dimensions/pointers alone do not prove identical game frames.
typedef struct K033_Output11Target {
    uint32_t size,version;
    uint64_t producer_id,epoch,sequence;
    void* texture11;K033_Rect rect;
    uint32_t color,before_hud,reserved[2]; // LINEAR, before_hud=1 only
} K033_Output11Target;
typedef struct K033_Output11Status {
    uint32_t size,version,phase,faulted;int32_t result;uint32_t reserved;
    uint64_t token,submitted,committed,cancelled;
} K033_Output11Status;
// Attach on the adapter's serialized D3D11 immediate-context thread. D3D12 uses
// the SR owner's same DIRECT queue/device. Same physical adapter is required.
K033_API int K033_CALL K033_Output11Open(const K033_SessionAttach*,K033_Output11**);
// Snapshot the original target alpha, then compose exact-frame SR RGB into a
// private shared output. No target write until Commit. Keep the SR output lease
// borrowed until Commit/Cancel returns OK; resource refs alone cannot stop reuse.
K033_API int K033_CALL K033_Output11Begin(K033_Output11*,const K033_SrOutput*,const K033_Output11Target*,const K033_Settings*,uint64_t*);
// Nonblocking progress; never uses the game immediate context or waits on GPU.
K033_API int K033_CALL K033_Output11Poll(K033_Output11*,K033_Output11Status*);
// Original adapter thread/context only. Same target/frame declaration required.
// First ready call queues the region copy; BUSY thereafter retains target/SR
// leases until the real D3D11 consumer fence completes. No swapchain Present.
K033_API int K033_CALL K033_Output11Commit(K033_Output11*,uint64_t,const K033_Output11Target*);
// Before the target copy, cancel keeps the game image intact. After submission
// it only drains the copy; it cannot undo an already queued target write.
K033_API int K033_CALL K033_Output11Cancel(K033_Output11*,uint64_t);
K033_API int K033_CALL K033_Output11Close(K033_Output11**);
#ifdef __cplusplus
}
#endif
