#pragma once
#include "033_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
enum K033_NativeFgReason {
    K033_FG_FOLLOW=0, K033_FG_DISABLED=1, K033_FG_GAME_OFF=2,
    K033_FG_OPTIONS_UNKNOWN=3, K033_FG_MODE_UNKNOWN=4, K033_FG_PROVIDER_UNKNOWN=5,
    K033_FG_PACING_UNKNOWN=6, K033_FG_CAP_UNKNOWN=7, K033_FG_OVERRIDE=8,
    K033_FG_UNCHANGED=9, K033_FG_BUSY=10, K033_FG_REENTRANT=11
};
// Separate from grade/NR. expected_revision is an optimistic edit token.
// This first ABI stores process-local intent only; saved_revision remains zero.
typedef struct __declspec(align(8)) K033_NativeFgRequest {
    uint32_t size,version,multiplier,reserved; uint64_t expected_revision;
} K033_NativeFgRequest;
typedef struct __declspec(align(8)) K033_NativeFgView {
    uint64_t binding_epoch,sequence,request_revision,accepted_sequence,state_sequence;
    uint32_t viewport,options_valid,game_mode,game_generated,transmitted_generated,reason;
    uint32_t override_attempted,fallback_attempted,first_result,fallback_result;
    uint32_t accepted_valid,accepted_current,accepted_mode,accepted_generated;
    uint32_t state_seen,state_valid,state_result,state_flags,query_interval_presented;
    uint32_t sdk_cap_known,sdk_max_generated,effective_max_generated,reserved;
} K033_NativeFgView;
typedef struct __declspec(align(8)) K033_NativeFgStatus {
    uint32_t size,version,multiplier,bindings,views,functions,coverage_complete,universal_allowed;
    uint64_t revision,saved_revision,observations_lost;
    K033_NativeFgView view[16];
} K033_NativeFgStatus;
K033_API int K033_CALL K033_ModernSetNativeFgRequest(const K033_NativeFgRequest*);
// Snapshot only. Never queries Streamline or resets its query interval.
K033_API int K033_CALL K033_ModernReadNativeFgStatus(K033_NativeFgStatus*);
#ifdef __cplusplus
}
#endif
