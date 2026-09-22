# Private 033 image frame pacing

Derived from the pinned AMD FidelityFX SDK 3.1.7 swapchain sources listed with
SHA256 in UPSTREAM.json. Original copyright/license notices are retained.
This private fork extends the pacing queue to two intermediate frames and
four double-buffered output resources, with distinct retirement values.
It exports only K033_GetImageSwapchainApi, not the AMD provider ABI. The legacy
SDK structures in this copy are private and are not compatible with SDK callers.
The game/native AMD provider and original SDK sources are unchanged.

Interpolation itself is 033 image-only optical flow, supplied through callback.
This provider supplies presentation, never AMD frame-generation model execution.
CPU/shader/build results do not establish displayed multiplier or visual quality.
