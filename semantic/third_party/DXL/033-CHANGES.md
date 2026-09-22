# DXL semantic component reuse

Source: https://github.com/LCPD15/DXL/tree/8644a875e61a9ecb8379f78ce9fd3ef3bc4853a4

SemanticMask.h, SemGroups.h and semantic_smoothing.cpp are unmodified copies.
The parent yolo_mask_decode.h extracts the instance-mask decoder from
src/core/SegMaskFilter.cpp. It preserves class selection, NMS, soft prototypes,
pixel-center interpolation and bounding-box feather. Changes are marked in that
header: standalone CPU interface, people-only, bounded/finite inputs and atomic
snapshot publication. The TensorRT driver, game capture/hooks, debug window,
automatic updater and launcher have not been integrated.

License: AGPL-3.0-only; copyright LCPD15 (2026). Preserve this attribution,
LICENSE and corresponding source with any eventual derivative distribution.
This is a private development component; no package has been published.
