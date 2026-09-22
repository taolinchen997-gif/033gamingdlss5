#pragma once
#include "033_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Session K033_Session;
// Logical producer binding supplied by the internal adapter; not an OS/PID
// authentication claim. Generation must increase across every reconfiguration.
typedef struct K033_Binding {uint64_t source_id,generation;} K033_Binding;
typedef struct K033_SessionAttach {
    uint32_t size,version,source_backend;
    K033_Binding binding;
    void* device12;void* queue12;
    void* device11;void* context11; // Both only for DX11; both null for DX12.
} K033_SessionAttach;
enum K033_SessionPhase {K033_SESSION_RUNNING=1,K033_SESSION_PAUSED=2,
    K033_SESSION_RECONFIGURING=3,K033_SESSION_STOPPING=4,K033_SESSION_STOPPED=5,K033_SESSION_FAULTED=6};
typedef struct K033_SessionStatus {
    uint32_t size,version,phase,source_backend,in_flight,borrowed;
    int32_t last_error;
    K033_Binding binding,pending_binding;
    uint64_t submitted,prepared,acquired,discarded;
    uint32_t neural_evaluations; // Input preparation only; always zero in S5.
} K033_SessionStatus;
// Internal same-process owner: creates the existing runtime and optional bridge
// from adapter-supplied devices. No game discovery, UI, workers or GPU execution
// thread is started. All queue/context operations share the caller's serial lane.
K033_API int K033_CALL K033_SessionCreate(const K033_SessionAttach*,K033_Session**);
K033_API int K033_CALL K033_SessionSubmit(K033_Session*,const K033_Binding*,const K033_InputRequest*,uint64_t* ticket);
// Same lifetime, with exact frame-bound metadata. producer_id must equal source_id.
K033_API int K033_CALL K033_SessionSubmit2(K033_Session*,const K033_Binding*,const K033_InputRequest2*,uint64_t* ticket);
K033_API int K033_CALL K033_SessionSubmit3(K033_Session*,const K033_Binding*,const K033_InputRequest3*,uint64_t*);
K033_API int K033_CALL K033_SessionAcquire3(K033_Session*,uint64_t,K033_InputFrame3*);
// Bounded progress; call even when there is no new frame. Never waits for a fence.
K033_API int K033_CALL K033_SessionPump(K033_Session*);
// Input textures only, NOT model/display output. The returned lease is a SESSION
// ticket; return it only through SessionRelease. Discarded tickets return BYPASS
// once, after internal cancellation is queued. Acquire in increasing ticket order.
K033_API int K033_CALL K033_SessionAcquire(K033_Session*,uint64_t ticket,K033_InputFrame*);
K033_API int K033_CALL K033_SessionAcquire2(K033_Session*,uint64_t ticket,K033_InputFrame2*);
// OK accepts the surrender, NOT GPU completion. After this call no more use of
// the frame is allowed. Pump retries BUSY retirement internally; do not re-release.
// ALL consumers must already be queued on the attached DX12 DIRECT queue.
K033_API int K033_CALL K033_SessionRelease(K033_Session*,uint64_t ticket);
// Pause admission only; already admitted frames remain acquirable/cancellable.
K033_API int K033_CALL K033_SessionSetPaused(K033_Session*,uint32_t paused);
// OK stages a validated replacement. Pump drains the old owner before activating
// it; ReadStatus reports the active/pending bindings. No new frame is admitted
// during transition. Failed staging leaves the old owner running. Borrowed old
// frames MUST be surrendered. Old tickets never alias tickets of the replacement.
K033_API int K033_CALL K033_SessionReconfigure(K033_Session*,const K033_SessionAttach*);
K033_API int K033_CALL K033_SessionReadStatus(K033_Session*,K033_SessionStatus*);
// One persistent settings/key/config authority, including between bindings.
// Explicit common disable cancels unborrowed inputs. F11 changes only NR intent,
// preserving common settings/native intake. Owners stop the NR-specific SR/NR
// chain on progress; pre-adjustment and game SR/FG settings remain independent.
// accepted keys do not prove a model/output has adopted that intent.
// Borrowed work cannot be recalled. No per-game defaults/config are created.
K033_API int K033_CALL K033_SessionGetSettings(K033_Session*,K033_Settings*);
K033_API int K033_CALL K033_SessionSetSettings(K033_Session*,const K033_Settings*);
K033_API int K033_CALL K033_SessionKey(K033_Session*,uint32_t key,uint32_t down,uint32_t repeat,uint32_t focused);
K033_API int K033_CALL K033_SessionLoadConfigW(K033_Session*,const wchar_t*);
K033_API int K033_CALL K033_SessionSaveConfigW(K033_Session*,const wchar_t*);
// Drain cancels replacement/unborrowed work; BUSY keeps the whole owner alive.
// Surrender borrowed frames even on device loss. A stopped session can Reconfigure.
K033_API int K033_CALL K033_SessionDrain(K033_Session*);
K033_API int K033_CALL K033_SessionDestroy(K033_Session**);
#ifdef __cplusplus
}
#endif
