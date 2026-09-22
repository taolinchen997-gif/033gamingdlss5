#pragma once
#include "033_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Worker K033_Worker;
typedef struct K033_WorkerLaunch {
    uint32_t size,version;
    const wchar_t* executable; // Absolute path in the managed 033 package.
    uint8_t sha256[32]; // Exact packaged x64 Windows-subsystem worker image.
    uint32_t startup_timeout_ms,stop_timeout_ms; // 100..30000; monotonic time.
} K033_WorkerLaunch;
enum K033_WorkerPhase {K033_WORKER_STARTING=1,K033_WORKER_READY=2,K033_WORKER_STOPPING=3,K033_WORKER_STOPPED=4,K033_WORKER_FAILED=5};
typedef struct K033_WorkerStatus {uint32_t size,version,phase,pid;int32_t error;uint32_t pending;uint64_t submitted,completed;uint32_t graceful,reserved;} K033_WorkerStatus;
// OS termination result, not an SDK error. An exit code can be supplied by
// ExitProcess/TerminateProcess or an unhandled exception; it is not a causal
// stack trace. exited requires a signaled process handle, even for code 259.
typedef struct K033_WorkerExit {
    uint32_t size,version,exited,code_known,code,graceful,forced,reserved;
} K033_WorkerExit;
// Starts ONLY the exact checked executable, suspended, restricted inherited
// handles and job ownership established before resume. Never calls a shell.
// Internal transport commands; integers are NT handles in this caller process.
// One outstanding command per owner. Keep shared images/handles alive through
// collection and GPU consumption; before a new frame input_value must cover BOTH
// the new producer copy and all previous reads of the shared output.
enum K033_WorkerOp {K033_WORKER_PROBE=1,K033_WORKER_IMPORT=2,K033_WORKER_FRAME=3,K033_WORKER_RELEASE=4};
// Compiled functionality only; does not attest device/game support or NR/FG.
enum K033_WorkerCapability {K033_WORKER_CAP_SHARED_GRADE=1};
typedef struct __declspec(align(8)) K033_WorkerPacket {
    uint32_t bytes,version,op,flags;
    uint64_t sequence,epoch; // sequence must be 0 on Send; owner assigns ticket.
    uint64_t handles[4]; // input image, output image, producer fence, worker fence
    uint64_t input_value,output_value;
    uint32_t width,height,format,color;
    int32_t adapter_high;uint32_t adapter_low;
    float diffuse_white,exposure,contrast,saturation,warmth,tint,highlights,strength;
    uint32_t enabled,style;uint64_t reserved[6];
} K033_WorkerPacket;
typedef struct __declspec(align(8)) K033_WorkerReply {
    uint32_t bytes,version;int32_t result;uint32_t capabilities;
    uint64_t sequence,epoch,value;uint64_t reserved[3];
} K033_WorkerReply;
K033_API int K033_CALL K033_WorkerSend(K033_Worker*,const K033_WorkerPacket*,uint64_t* ticket);
// OK means a response was collected, not that its result is OK or a frame shown.
K033_API int K033_CALL K033_WorkerCollect(K033_Worker*,uint64_t ticket,K033_WorkerReply*);
K033_API int K033_CALL K033_WorkerStart(const K033_WorkerLaunch*,K033_Worker**);
K033_API int K033_CALL K033_WorkerPoll(K033_Worker*,K033_WorkerStatus*);
// A validated output is filled even when the returned worker result is <0.
K033_API int K033_CALL K033_WorkerReadExit(K033_Worker*,K033_WorkerExit*);
K033_API int K033_CALL K033_WorkerStop(K033_Worker*);
// Nonblocking graceful stop first, bounded owned-child termination on timeout.
// This control-plane object owns no GPU textures; callers retain their leases.
K033_API int K033_CALL K033_WorkerDestroy(K033_Worker**);
#ifdef __cplusplus
}
#endif
