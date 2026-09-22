#pragma once
#include "033_sr.h"
#include "033_ngx_relay.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Ngx11Owner K033_Ngx11Owner;
typedef struct K033_Ngx11OwnerStatus {
    uint32_t size,version,stopping,active_calls;
    int32_t progress_result;uint32_t reserved;
    K033_SourceInfo source;
    K033_Ngx11RelayStatus relay;
} K033_Ngx11OwnerStatus;
typedef struct K033_Ngx11OwnedFrame {
    uint32_t size,version;
    K033_InputFrame2 frame;
    // Borrowed with frame's lease. Queue every consumer on this DIRECT queue
    // before Release. Never retain these pointers beyond the surrendered lease.
    void* device12;void* queue12;
} K033_Ngx11OwnedFrame;
typedef struct K033_Ngx11OwnedFrame3 {
    uint32_t size,version;K033_InputFrame3 frame;K033_NgxShape shape;
    void* device12;void* queue12;
} K033_Ngx11OwnedFrame3;
K033_API int K033_CALL K033_Ngx11OwnerAcquire3(K033_Ngx11Owner*,K033_Ngx11OwnedFrame3*);
typedef struct K033_Ngx11SrStatus {
    // phase: 0 unprepared, 1 pending, 2 admitted, 3 unavailable, 4 failed,
    // 5 closing. Unavailable SDK is NOT evidence of absent native input.
    uint32_t size,version,phase;int32_t result;uint32_t sdk_result;K033_SrStatus sr;
} K033_Ngx11SrStatus;
typedef struct K033_Ngx11SrFrame {
    uint32_t size,version;K033_SrOutput output;void* device12;void* queue12;
} K033_Ngx11SrFrame;
// Internal automatic SR output, not a displayed game frame. Same DIRECT queue
// and lease rules as SrOutput. Borrowed output keeps the source owner alive.
K033_API int K033_CALL K033_Ngx11OwnerAcquireSr(K033_Ngx11Owner*,K033_Ngx11SrFrame*);
K033_API int K033_CALL K033_Ngx11OwnerReleaseSr(K033_Ngx11Owner*,uint64_t);
K033_API int K033_CALL K033_Ngx11OwnerReadSrStatus(K033_Ngx11Owner*,K033_Ngx11SrStatus*);
// INTERNAL mount operation, outside DllMain, on the game's immediate-context
// lane. Creates/owns the same-process Source, same-adapter DX12 device/queue,
// Session and Relay. No HWND, capture picker, helper process or separate UI.
// Mount supplies retained exact originals, trusted color semantics and the
// common settings. Open does not install the mount or call game CreateFeature.
// If Open fails, the mount must still forward the original game call unchanged.
K033_API int K033_CALL K033_Ngx11OwnerOpen(void* context,const K033_Ngx11Originals*,const K033_Ngx11RelayConfig*,const K033_Settings*,K033_Ngx11Owner**);
K033_API uint32_t K033_CALL K033_Ngx11OwnerCreateFeature(K033_Ngx11Owner*,void* context,uint32_t feature,void* parameters,void** out_handle);
K033_API uint32_t K033_CALL K033_Ngx11OwnerEvaluateFeature(K033_Ngx11Owner*,void* context,const void* handle,const void* parameters,void* callback,uint32_t c_callback);
K033_API uint32_t K033_CALL K033_Ngx11OwnerReleaseFeature(K033_Ngx11Owner*,void* handle);
K033_API uint32_t K033_CALL K033_Ngx11OwnerShutdown(K033_Ngx11Owner*,void* device,uint32_t with_device);
// Bounded idle progress. No wait for a GPU fence and no DX11 context commands.
K033_API int K033_CALL K033_Ngx11OwnerPump(K033_Ngx11Owner*);
K033_API int K033_CALL K033_Ngx11OwnerAcquire(K033_Ngx11Owner*,K033_Ngx11OwnedFrame*);
K033_API int K033_CALL K033_Ngx11OwnerRelease(K033_Ngx11Owner*,uint64_t lease);
K033_API int K033_CALL K033_Ngx11OwnerReadStatus(K033_Ngx11Owner*,K033_Ngx11OwnerStatus*);
K033_API int K033_CALL K033_Ngx11OwnerGetSettings(K033_Ngx11Owner*,K033_Settings*);
K033_API int K033_CALL K033_Ngx11OwnerSetSettings(K033_Ngx11Owner*,const K033_Settings*);
K033_API int K033_CALL K033_Ngx11OwnerKey(K033_Ngx11Owner*,uint32_t key,uint32_t down,uint32_t repeat,uint32_t focused);
// Mount must prevent any NEW calls before Close/Retire, including callbacks
// holding the old owner pointer. Existing calls/borrowed frames return BUSY and
// keep *owner valid. A consumer may Release while stopping, then retry teardown.
// Close: caller keeps driving retirement until OK, then *owner=null.
K033_API int K033_CALL K033_Ngx11OwnerClose(K033_Ngx11Owner**);
// Retire: after calls/borrows end, transfer pending GPU ownership into a slot
// reserved BEFORE device creation. OK/null means transfer, not fence completion.
// The in-process service polls even when game callbacks stop; no external host.
K033_API int K033_CALL K033_Ngx11OwnerRetire(K033_Ngx11Owner**);
#ifdef __cplusplus
}
#endif
