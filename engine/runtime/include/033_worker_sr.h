#pragma once
#include "033_worker.h"
#include "033_sr.h"
#include "033_nr.h"
#ifdef __cplusplus
extern "C" {
#endif
// Additive native-only SR command. Fixed width, no pointers across processes.
// Separate API preserves the original 192-byte color-only command layout.
typedef struct __declspec(align(8)) K033_WorkerSrPlane {
    uint32_t width,height,format,reserved;uint64_t handle;
} K033_WorkerSrPlane;
typedef struct __declspec(align(8)) K033_WorkerSrPacket {
    uint32_t bytes,version,op,flags;uint64_t sequence,epoch;
    uint64_t producer_fence,output_fence,input_value,output_value;
    int32_t adapter_high;uint32_t adapter_low;
    // Color RGBA16F, depth R32F, motion RG16F/RG32F, optional exposure R32F,
    // output RGBA16F. Import includes caller-owned NT handles; Frame has zero
    // handles and the EXACT imported shapes/configuration.
    K033_WorkerSrPlane planes[5];K033_SrConfig config;
    K033_InputSemantics semantics; // original game sequence, distinct from RPC ticket
    K033_Rect color_rect,depth_rect,motion_rect;
    float motion_scale_x,motion_scale_y,diffuse_white,exposure_scale;
    uint32_t source,native_guides,reset_required,depth_inverted;
    K033_Settings settings;uint32_t reserved0;uint64_t reserved[6];
} K033_WorkerSrPacket;
enum K033_WorkerSrCapability {K033_WORKER_CAP_NATIVE_SR=2};
// Operations use PROBE/IMPORT/FRAME/RELEASE. One outstanding command shared
// with WorkerSend. Import/read/copy/model/copy-out retire before reply.
// FRAME input_value covers all producer writes and previous consumer reads;
// producer must keep handles/resources alive until worker release/exit and
// local GPU retirement. Reply OK is not proof of displayed pixels or quality.
// Only a successful native FRAME with nonzero output uses Reply.reserved[0]:
// bit 0 is the SDK controller's actual reset_applied flag. Other bits/words zero.
// Legacy replies retain all-zero reserved words; matched package is required.
K033_API int K033_CALL K033_WorkerSendSr(K033_Worker*,const K033_WorkerSrPacket*,uint64_t*);
// Additive SR -> NR command. The old packet and its semantics remain intact.
// NR settings are immutable for an imported binding and its appearance epoch.
typedef struct K033_WorkerSrPacket2 {K033_WorkerSrPacket frame;K033_NrSettings nr;} K033_WorkerSrPacket2;
K033_API int K033_CALL K033_WorkerSendSr2(K033_Worker*,const K033_WorkerSrPacket2*,uint64_t*);
#ifdef __cplusplus
}
#endif
