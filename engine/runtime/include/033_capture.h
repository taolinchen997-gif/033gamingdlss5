#pragma once
#include "033_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Capture K033_Capture;
typedef struct K033_CaptureFrame {
    uint32_t size,version,width,height;
    uint64_t lease,sequence,epoch;
    void* texture; // Borrowed ID3D11Texture2D, owned until release completes.
} K033_CaptureFrame;
// Windows.Graphics.Capture source, independent of game API and bitness.
// Caller initializes WinRT MTA, supplies its OWN device/immediate context and
// an explicitly selected foreign HWND. SDR capture only in ABI v1.
K033_API int K033_CALL K033_CaptureCreate(void* hwnd,void* device,void* context,K033_Capture** out);
// Returns latest available frame only; no event thread, no unbounded frame queue.
K033_API int K033_CALL K033_CaptureAcquire(K033_Capture*,K033_CaptureFrame*);
// Call after submitting all processing/output commands on the SAME context.
// This enqueues a completion query. It does not immediately free the frame.
K033_API int K033_CALL K033_CaptureRelease(K033_Capture*,uint64_t lease);
// Stop acquiring, release all leases first, retry after ordinary queue progress.
// BUSY preserves the session and all references; no forced flush/CPU wait.
K033_API int K033_CALL K033_CaptureDestroy(K033_Capture**);
#ifdef __cplusplus
}
#endif
