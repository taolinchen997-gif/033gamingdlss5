#pragma once
#include "033_ngx_input.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Ngx11Relay K033_Ngx11Relay;
// Trusted interception owner supplies exact, retained original NGX functions.
// Addresses and calls use the unchanged SDK ABI; no handwritten vtable offsets.
typedef struct K033_Ngx11Originals {
    uint32_t size,version;
    void *create_feature,*evaluate_feature,*evaluate_feature_c,*release_feature,*shutdown,*shutdown1;
} K033_Ngx11Originals;
typedef struct K033_Ngx11RelayConfig {uint32_t size,version,color;float diffuse_white;} K033_Ngx11RelayConfig;
typedef struct K033_Ngx11RelayStatus {
    uint32_t size,version,features,pending,borrowed,stopped;
    int32_t input_result;uint32_t reserved;
    uint64_t observed,submitted,epoch,sequence;
    K033_NgxReadReport read;
} K033_Ngx11RelayStatus;
// Exclusive producer/consumer for this source/session until Close. Their
// ownership stays with the internal caller; close relay before destroying them.
// Config is a trusted renderer declaration, not a new user-facing preference.
// This installs no hooks and does not load a module or launch any helper.
K033_API int K033_CALL K033_Ngx11RelayOpen(K033_SourceOwner*,K033_Session*,const K033_Ngx11Originals*,const K033_Ngx11RelayConfig*,K033_Ngx11Relay**);
// These are interception-owner relays, not replacement NGX export signatures.
// Their uint32 return is the original NGX result. Own intake errors are in Status.
// Parameters, output handle pointer and both callback variants pass unchanged.
K033_API uint32_t K033_CALL K033_Ngx11RelayCreateFeature(K033_Ngx11Relay*,void* context,uint32_t feature,void* parameters,void** out_handle);
K033_API uint32_t K033_CALL K033_Ngx11RelayEvaluateFeature(K033_Ngx11Relay*,void* context,const void* handle,const void* parameters,void* callback,uint32_t c_callback);
K033_API uint32_t K033_CALL K033_Ngx11RelayReleaseFeature(K033_Ngx11Relay*,void* handle);
K033_API uint32_t K033_CALL K033_Ngx11RelayShutdown(K033_Ngx11Relay*,void* device,uint32_t with_device);
K033_API int K033_CALL K033_Ngx11RelayAcquire(K033_Ngx11Relay*,K033_InputFrame2*);
// Copied creation geometry, bound to the exact leased frame/feature generation.
// No game handle or mutable parameter pointer escapes through this shape.
typedef struct K033_NgxShape {uint32_t width,height,output_width,output_height,flags;} K033_NgxShape;
typedef struct K033_Ngx11RelayFrame3 {uint32_t size,version;K033_InputFrame3 frame;K033_NgxShape shape;} K033_Ngx11RelayFrame3;
K033_API int K033_CALL K033_Ngx11RelayAcquire3(K033_Ngx11Relay*,K033_Ngx11RelayFrame3*);
K033_API int K033_CALL K033_Ngx11RelayRelease(K033_Ngx11Relay*,uint64_t lease);
K033_API int K033_CALL K033_Ngx11RelayReadStatus(K033_Ngx11Relay*,K033_Ngx11RelayStatus*);
// Stops new intake, revokes features, cancels unborrowed work and pumps session
// retirement. Borrowed work must be explicitly released; Close stays BUSY.
// Exclude Close from all concurrent calls, including original game callbacks.
K033_API int K033_CALL K033_Ngx11RelayClose(K033_Ngx11Relay**);
#ifdef __cplusplus
}
#endif
