#pragma once
#include "033_sr.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct K033_NrLayer {
    uint32_t style,preset,auto_mask,ui_correction;
    float intensity,local_structure,local_tone,global_tone;
} K033_NrLayer;
// Shared product settings snapshot. Epoch identifies an immutable appearance,
// not proof that a vendor model applied it. At most three independent layers.
typedef struct __declspec(align(8)) K033_NrSettings {
    uint32_t size,version,enabled,layers;uint64_t appearance_epoch;
    K033_NrLayer layer[3];
    // ABI v2. Zero means no saved SR request: retain the legacy consumer value.
    // Layer 1 is relative to the chosen model base; layers 2/3 to layer 1.
    uint32_t sr_work[3];
    // Retired v3 effect slot. Retain layout; no rendering setting occupies it.
    uint32_t reserved;
    // ABI v4 appends persisted model skin controls. Keep the v2/v3 prefix intact.
    // This is not the frozen Feeder controls ABI (872 bytes / 62 controls).
    float skin_structure[3];
    float skin_lift;
    // ABI v5: output-only clarity; the frozen Feeder controls ABI is unchanged.
    float final_clarity;
    // ABI v6 uses former tail padding; old field offsets and total size stay fixed.
    float natural_look;
} K033_NrSettings;
// Requested/persisted state only, never a model-applied appearance receipt.
typedef struct __declspec(align(8)) K033_NrPreferencesStatus {
    uint32_t size,version;uint64_t queued,saved;
    int32_t load_result,save_result;uint32_t active,win32_error,pending,reserved;
} K033_NrPreferencesStatus;
// Optimistic in-memory edit: submit the epoch returned by Get. A changed
// request receives a new epoch. Get reports requested values, not GPU adoption.
K033_API int K033_CALL K033_GetNrSettings(K033_Runtime*,K033_NrSettings*);
K033_API int K033_CALL K033_SetNrSettings(K033_Runtime*,const K033_NrSettings*);
#ifdef __cplusplus
}
#endif
