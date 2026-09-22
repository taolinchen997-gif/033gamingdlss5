#pragma once
#include "033_session.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_SourceOwner K033_SourceOwner;
// An internal adapter supplies a known target, never a guessed foreground window.
// External targets require the expected process creation FILETIME. hwnd may be 0.
typedef struct K033_SourceTarget {
    uint32_t size,version,pid,reserved;
    uint64_t creation_time,hwnd,generation;
} K033_SourceTarget;
typedef struct K033_SourceInfo {
    uint32_t size,version,pid,architecture,owner_pid,owner_architecture,window_thread;
    int32_t fault;
    uint64_t creation_time,hwnd;
    K033_Binding binding;
} K033_SourceInfo;
enum K033_GameApi {K033_API_DDRAW=1,K033_API_DX8=8,K033_API_DX9=9,K033_API_DX10=10,
    K033_API_DX11=11,K033_API_DX12=12,K033_API_OPENGL=100};
enum K033_AdapterOutput {K033_OUTPUT_NATIVE=0,K033_OUTPUT_TRANSLATED=1,K033_OUTPUT_CAPTURE=2};
// API/output are declarations of a TRUSTED INTERNAL adapter, not detected from
// process names or DLL presence. TRANSLATED means an adapter already supplies
// modern textures; it does not install/load or attest any compatibility layer.
typedef struct K033_RouteRequest {uint32_t size,version,game_api,output_kind,output_backend;} K033_RouteRequest;
enum K033_RouteMissing {K033_NEED_ADAPTER=1,K033_NEED_IPC=2,K033_NEED_BITNESS_WORKER=4,
    K033_NEED_WINDOW=8,K033_NEED_ARCHITECTURE=16,K033_NEED_FOREIGN_CAPTURE=32};
typedef struct K033_RoutePlan {
    uint32_t size,version,backend,uses_internal_bridge,missing,source_architecture,processing_architecture;
    K033_Binding binding;
} K033_RoutePlan;
K033_API int K033_CALL K033_SourceOpen(const K033_SourceTarget*,K033_SourceOwner**);
// In-process adapter bootstrap: reads own PID/time automatically; no enumeration.
K033_API int K033_CALL K033_SourceOpenCurrent(uint64_t hwnd,uint64_t generation,K033_SourceOwner**);
// Revalidates the retained process object and optional window association. A
// failed check latches invalidation; a later PID/window reuse cannot revive it.
K033_API int K033_CALL K033_SourceInspect(K033_SourceOwner*,K033_SourceInfo*);
// A supported plan is not device/texture validation or game compatibility proof.
K033_API int K033_CALL K033_SourcePlan(K033_SourceOwner*,const K033_RouteRequest*,K033_RoutePlan*);
// Devices/queue remain caller supplied. Creates one S5 session, replacing attach's
// binding internally; attach.binding must be zero. Native factories validate GPUs.
// Source owner is a separate process guard; caller owns the returned session.
K033_API int K033_CALL K033_SourceCreateSession(K033_SourceOwner*,const K033_RouteRequest*,const K033_SessionAttach*,K033_Session**);
// Use these guards for admission/progress. Source loss stops admission and calls
// SessionDrain, which retains borrowed/GPU resources until their normal release.
K033_API int K033_CALL K033_SourceSubmit(K033_SourceOwner*,K033_Session*,const K033_InputRequest*,uint64_t*);
K033_API int K033_CALL K033_SourceSubmit2(K033_SourceOwner*,K033_Session*,const K033_InputRequest2*,uint64_t*);
K033_API int K033_CALL K033_SourceSubmit3(K033_SourceOwner*,K033_Session*,const K033_InputRequest3*,uint64_t*);
K033_API int K033_CALL K033_SourcePump(K033_SourceOwner*,K033_Session*);
// Releases only the retained process handle. Drain/destroy the owned S5 session
// separately, before discarding this guard. Exclude Destroy from concurrent calls.
K033_API int K033_CALL K033_SourceDestroy(K033_SourceOwner**);
#ifdef __cplusplus
}
#endif
