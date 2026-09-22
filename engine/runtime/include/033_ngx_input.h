#pragma once
#include "033_source.h"
#ifdef __cplusplus
extern "C" {
#endif
// A copied record from a SUCCESSFUL game NGX feature creation callback. The
// managed dispatcher owns feature liveness/generation; this record owns no NGX object.
typedef struct K033_NgxFeature {
    uint32_t size,version,backend,flags;
    const void* handle;
    uint32_t width,height,output_width,output_height;
} K033_NgxFeature;
typedef struct K033_Ngx11Frame {
    uint32_t size,version;
    const void* handle;const void* parameters; // Live NVSDK_NGX_Parameter; read-only.
    void* context11; // Actual NGX immediate context at its serialized call boundary.
    uint64_t sequence,epoch;
    uint32_t color;float diffuse_white; // Trusted renderer color declaration, not guessed.
} K033_Ngx11Frame;
enum K033_NgxStage {K033_NGX_FEATURE=1,K033_NGX_PARAMETERS=2,K033_NGX_EXPOSURE=3,
    K033_NGX_TEXTURES=4,K033_NGX_SOURCE=5,K033_NGX_SUBMIT=6};
enum K033_NgxField {K033_NGX_NONE=0,K033_NGX_FLAGS=1,K033_NGX_DIMENSIONS=2,K033_NGX_COLOR=3,
    K033_NGX_DEPTH=4,K033_NGX_MOTION=5,K033_NGX_REGION=6,K033_NGX_MOTION_SCALE=7,
    K033_NGX_JITTER=8,K033_NGX_RESET=9,K033_NGX_PRE_EXPOSURE=10,K033_NGX_EXPOSURE_INPUT=11};
typedef struct K033_NgxReadReport {
    uint32_t size,version,stage,field,ngx_result,exposure_texture_observed;
    float exposure_scale;uint32_t reserved;
} K033_NgxReadReport;
// game_result and feature_kind are the unmodified original NGX return/id.
// Calls only typed parameter Get; never creates/evaluates NGX features, installs
// hooks, retains parameters or replaces the game's result.
K033_API int K033_CALL K033_NgxDescribeFeature(uint32_t backend,uint32_t feature_kind,uint32_t game_result,
    const void* handle,const void* creation_parameters,K033_NgxFeature*,K033_NgxReadReport*);
// At the live NGX11 evaluation callback BEFORE inputs can be reused. Acquires
// actual typed Color/Depth/Motion resources and submits through SourceSubmit2.
// Owner serializes the NGX context and session. Always continue the original
// game NGX call regardless of this optional intake result. No alternate window.
// This does NOT install the production NGX dispatcher or implement model execution.
K033_API int K033_CALL K033_Ngx11Submit(K033_SourceOwner*,K033_Session*,const K033_NgxFeature*,
    const K033_Ngx11Frame*,uint64_t* ticket,K033_NgxReadReport*);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Same intake and source guard, extended with the native 1x1 GPU exposure and
// its separate multiplier. Null/failed reads do not activate estimated exposure.
K033_API int K033_CALL K033_Ngx11Submit3(K033_SourceOwner*,K033_Session*,const K033_NgxFeature*,const K033_Ngx11Frame*,uint64_t*,K033_NgxReadReport*);
#ifdef __cplusplus
}
#endif
