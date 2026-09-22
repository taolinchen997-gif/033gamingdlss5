// Same gate is compiled by the production HLSL and offline CPU regression.
bool FlowFitSubpixel(float zeroMotionCost, float confidence) {
    // Preserve the estimator's existing stationary decision. An asymmetric
    // neighbourhood is not evidence of movement between identical frames.
    return zeroMotionCost >= 0.00001f && confidence > 0.05f;
}
