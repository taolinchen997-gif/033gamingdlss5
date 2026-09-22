#pragma once
#include "033_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_Rect {uint32_t x,y,width,height;} K033_Rect;
// D3D12 native texture, explicit full-resource states and active rectangle.
// No split/enhanced barriers, cross-queue work or unknown/protected resources.
typedef struct K033_Guide {void* texture;K033_Rect rect;uint32_t before_state,after_state;} K033_Guide;
typedef struct K033_InputRequest {
    uint32_t size,version;
    K033_Frame frame; // Original, ungraded source. Do not call Process first.
    K033_Rect color_rect;
    K033_Guide depth,motion; // Both supplied or both completely zero.
    float motion_scale_x,motion_scale_y;
    uint32_t depth_inverted,reset;
} K033_InputRequest;
typedef struct K033_InputFrame {
    uint32_t size,version;
    uint64_t lease,sequence,epoch;
    // Owned immutable D3D12 inputs, valid until ReleaseInput. All three are in
    // NON_PIXEL_SHADER_RESOURCE; consumers must restore that same state.
    void* linear_color;void* depth;void* motion;
    K033_Rect color_rect,depth_rect,motion_rect;
    uint32_t depth_format,motion_format; // DXGI_FORMAT, explicit typed views.
    uint32_t native_guides,reset_required,depth_inverted,source;
    float motion_scale_x,motion_scale_y,diffuse_white;
    // RGBA16F linear BT.709, HDR normalized by diffuse white. Working input,
    // NOT a completed NR/model-encoded/result frame. No inference in this API.
} K033_InputFrame;
// Declarations from the trusted adapter, NOT evidence inferred from pointer
// presence. Failed acquisition is distinct from a game-declared absent field.
enum K033_InputOrigin {K033_ORIGIN_UNKNOWN=0,K033_ORIGIN_GAME=1,K033_ORIGIN_ESTIMATED=2,
    K033_ORIGIN_ABSENT=3,K033_ORIGIN_FAILED=4};
enum K033_MotionJitter {K033_MOTION_JITTER_UNKNOWN=0,K033_MOTION_UNJITTERED=1,K033_MOTION_JITTERED=2};
typedef struct __declspec(align(8)) K033_InputSemantics {
    uint32_t size,version;
    uint64_t epoch,sequence,producer_id; // Exact frame; Session requires source_id.
    uint32_t depth_origin,motion_origin,jitter_origin,pre_exposure_origin,exposure_origin;
    uint32_t motion_jitter; // Whether the supplied motion includes camera jitter.
    float jitter_x,jitter_y; // Current jitter in render pixels, relative to color_rect.
    float pre_exposure,exposure; // Unmodified game scalars; not 033 grade exposure.
    uint64_t absence_evidence; // Adapter capability/descriptor revision, only if ABSENT.
} K033_InputSemantics;
// New wrappers preserve v1 layouts. Resource input stays v1; metadata is copied
// by value under the SAME lease, never paired with a later "latest" metadata read.
typedef struct K033_InputRequest2 {
    uint32_t size,version;K033_InputRequest input;K033_InputSemantics semantics;
} K033_InputRequest2;
typedef struct K033_InputFrame2 {
    uint32_t size,version;K033_InputFrame input;K033_InputSemantics semantics;
} K033_InputFrame2;
// Additive v3: real GPU exposure, never an ExposureScale masquerading as a
// measured exposure. A texture uses GAME origin and semantics.exposure==0.
// No texture: the image is zero, scale=1, format=0; v2 scalar rules still apply.
typedef struct K033_Exposure {
    K033_Guide image;float scale;uint32_t format,reserved;
} K033_Exposure;
typedef struct K033_InputRequest3 {
    uint32_t size,version;K033_InputRequest2 frame;K033_Exposure exposure;
} K033_InputRequest3;
typedef struct K033_InputFrame3 {
    uint32_t size,version;K033_InputFrame2 frame;K033_Exposure exposure;
} K033_InputFrame3;
// Exposure belongs to the SAME color/guide lease and is immutable NPSR on
// Acquire. Older acquisitions reject texture-exposure leases instead of losing it.
K033_API int K033_CALL K033_SubmitInput3(K033_Runtime*,const K033_InputRequest3*,uint64_t*);
K033_API int K033_CALL K033_AcquireInput3(K033_Runtime*,uint64_t,K033_InputFrame3*);
typedef struct K033_InputStatus {
    uint32_t size,version,backend,in_flight,borrowed,faulted;
    uint64_t submitted,acquired,retired,cached_bytes;
    uint32_t neural_evaluations; // S3 always zero; preparation is not inference.
} K033_InputStatus;
// Append preparation to attached DIRECT queue; token != ready textures. Three
// slots maximum. D3D12 only: internal DX11 callers can first use 033_bridge.h.
K033_API int K033_CALL K033_SubmitInput(K033_Runtime*,const K033_InputRequest*,uint64_t* token);
// Poll completed preparation, then borrow immutable textures exactly once.
// Out-of-order acquisition is rejected. reset_required tracks input continuity;
// a future model must also reset after its own skipped/failed evaluations.
K033_API int K033_CALL K033_AcquireInput(K033_Runtime*,uint64_t token,K033_InputFrame*);
// Estimated fields are UNSUPPORTED until an implemented quality-accepted path
// exists. No automatic fallback or game discovery is performed by these APIs.
K033_API int K033_CALL K033_SubmitInput2(K033_Runtime*,const K033_InputRequest2*,uint64_t* token);
// A v2 lease cannot be acquired via the v1 API (UNSUPPORTED, still cancellable).
// AcquireInput2 on v1 work reports UNKNOWN metadata, never fabricated native proof.
K033_API int K033_CALL K033_AcquireInput2(K033_Runtime*,uint64_t token,K033_InputFrame2*);
// After ALL consumers submitted on same attached queue. Cancel before Acquire
// is allowed. Appends retirement fence; no immediate reuse or later consumption.
K033_API int K033_CALL K033_ReleaseInput(K033_Runtime*,uint64_t token);
K033_API int K033_CALL K033_ReadInputStatus(K033_Runtime*,K033_InputStatus*);
#ifdef __cplusplus
}
#endif
