#pragma once
#include "033_input.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Sr K033_Sr;
// INTERNAL provider contract. The manager owns an initialized, caller-serialized
// NGX SDK instance for this exact device, and keeps it alive until SrClose is OK.
// These are the public D3D12 SDK signatures, never private snippet entrypoints.
// Supplying this table is not evidence that initialization actually succeeded.
typedef struct K033_SrSdk {
    uint32_t size,version;void* initialized_device;
    void* allocate_parameters;void* destroy_parameters;
    void* create_feature;void* evaluate_feature;void* release_feature;
} K033_SrSdk;
// Geometry/semantic binding, not a second user settings authority. Quality uses
// the public NGX enum 0..5. Supported flags: IsHDR | optional MVLowRes,
// MVJittered, DepthInverted. No automatic exposure or image-derived guides.
typedef struct K033_SrConfig {
    uint32_t size,version,width,height,output_width,output_height,quality,flags;
} K033_SrConfig;
typedef struct K033_SrInput {
    uint32_t size,version;K033_InputFrame2 frame;
    // Exact producer/consumer DIRECT queue from the borrowed input owner.
    void* queue12;
} K033_SrInput;
typedef struct K033_SrInput3 {uint32_t size,version;K033_InputFrame3 frame;void* queue12;} K033_SrInput3;
// Same SR owner/output lifecycle, with the leased native exposure texture.
K033_API int K033_CALL K033_SrSubmit3(K033_Sr*,const K033_SrInput3*,const K033_Settings*,uint64_t*);
typedef struct K033_SrOutput {
    uint32_t size,version;uint64_t lease,epoch,sequence,producer_id;
    void* texture;uint32_t width,height,reset_applied,reserved;
    // Immutable RGBA16F linear working output, NON_PIXEL_SHADER_RESOURCE.
    // Only RGB is reconstructed; alpha is not guaranteed by this SR feature.
    // Not converted to a display encoding or submitted to a game swapchain.
} K033_SrOutput;
typedef struct K033_SrStatus {
    // model_created: SDK Create reported success with an owned handle.
    // ngx_evaluations: attempted public Evaluate calls, not completed GPU frames.
    uint32_t size,version,stopping,faulted,in_flight,borrowed,model_created,ngx_result;
    uint64_t submitted,completed,acquired,retired,ngx_evaluations;
} K033_SrStatus;
// Manager supplies one initialized provider; this does not load/init NGX, alter
// game features, install hooks, start another process or create a UI/window.
K033_API int K033_CALL K033_SrOpen(const K033_Attach*,const K033_SrSdk*,const K033_SrConfig*,K033_Sr**);
// F11/common enabled applies here; input was already prepared/graded once.
// On OK, model commands were submitted to input's same queue. The source lease
// may then be released normally on that queue. BUSY/failure produces no token;
// source owner still follows its usual cancellation/retirement contract.
K033_API int K033_CALL K033_SrSubmit(K033_Sr*,const K033_SrInput*,const K033_Settings*,uint64_t* token);
K033_API int K033_CALL K033_SrAcquire(K033_Sr*,uint64_t,K033_SrOutput*);
// Submit all output consumers on the attached queue, restoring NPSR, then release.
K033_API int K033_CALL K033_SrRelease(K033_Sr*,uint64_t);
K033_API int K033_CALL K033_SrReadStatus(K033_Sr*,K033_SrStatus*);
// Cancels unborrowed outputs; outstanding borrows/fences keep the entire owner.
// Exclude new API calls before Close; no waits, SDK Shutdown or forced release.
K033_API int K033_CALL K033_SrClose(K033_Sr**);
#ifdef __cplusplus
}
#endif
