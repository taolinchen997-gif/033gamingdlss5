// Shared by encode and resolve. ST.2084 / BT.2020 matrices are standard colour math.
R"(
float3 NrPqToNits(float3 v) {
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(v), 1.0 / m2);
    return 10000.0 * pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-7), 1.0 / m1);
}
float3 NrNitsToPq(float3 v) {
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(v / 10000.0), m1);
    return pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}
float3 Nr2020To709(float3 v) {
    return mul(float3x3(1.6604910,-0.5876411,-0.0728499,
                       -0.1245505,1.1328999,-0.0083494,
                       -0.0181508,-0.1005789,1.1187297), v);
}
float3 Nr709To2020(float3 v) {
    return mul(float3x3(0.6274039,0.3292830,0.0433131,
                       0.0690973,0.9195404,0.0113623,
                       0.0163914,0.0880133,0.8955953), v);
}
// Reversible compression towards equal-luminance neutral, only near the gamut
// boundary. Avoid clipping negative BT.709 channels from saturated BT.2020 input.
float3 NrGamut(float3 v, bool inverse) {
    float y = dot(v, float3(0.2126, 0.7152, 0.0722));
    if (y <= 1e-7) return max(v, 0.0);
    float radius = max(0.0, 1.0 - min(v.r, min(v.g, v.b)) / y);
    const float knee = 0.8;
    if (radius <= knee) return v;
    float mapped = radius;
    if (inverse) {
        float u = min((radius - knee) / (1.0 - knee), 0.99999);
        mapped = knee + (1.0 - knee) * u / (1.0 - u);
    } else {
        float u = (radius - knee) / (1.0 - knee);
        mapped = knee + (1.0 - knee) * u / (1.0 + u);
    }
    return y + (v - y) * (mapped / radius);
}
float3 NrPqToWorking(float3 v, float diffuseWhite) {
    return NrGamut(Nr2020To709(NrPqToNits(v)), false) / max(diffuseWhite, 1.0);
}
float3 NrWorkingToPq(float3 v, float diffuseWhite) {
    return NrNitsToPq(Nr709To2020(NrGamut(v * max(diffuseWhite, 1.0), true)));
}
float3 NrStore(float3 v, uint sourceMode, float diffuseWhite) {
    float3 result = v;
    if (sourceMode == 2) result = NrWorkingToPq(v, diffuseWhite);
    else if (sourceMode == 3) result = v * max(diffuseWhite, 1.0) / 80.0;
    return result;
}
float NrPeak(float3 v) { return max(v.r, max(v.g, v.b)); }
// Bound the response to an edit, not the absolute HDR scene. The inverse curve
// has unbounded gain near white; a final output ceiling alone still permits
// violent frame-to-frame jumps below that ceiling. This budget also bounds the
// edit's gain relative to the change actually made in the model's linear proxy.
// One scalar limits the residual vector, preserving its direction. No history,
// blur, CPU/GPU wait, or clipping of unchanged scene highlights is introduced.
float3 NrBoundEdit(float3 original, float3 delta, float3 proxyEdit,
                   float3 proxy, float limit) {
    float peak = NrPeak(max(original, 0.0));
    float change = NrPeak(abs(proxyEdit));
    float span = NrPeak(abs(delta));
    if (peak <= 0.0 || change <= 0.0 || span <= 0.0) return float3(0,0,0);
    float g = clamp(limit, 1.0, 8.0);
    float budget = peak * g * change / max(NrPeak(abs(proxy)), 1.0 / 512.0);
    float a = min(1.0, budget / max(span, 1e-20));
    // Also keep the edited scene nonnegative (preserve existing negatives) and
    // below the selected source-relative peak. Large legitimate source values
    // remain untouched when the model makes no edit.
    [unroll] for (int c = 0; c < 3; ++c) {
        if (delta[c] > 0.0) a = min(a, max(0.0, peak * g - original[c]) / delta[c]);
        else if (delta[c] < 0.0) a = min(a, max(0.0, original[c]) / -delta[c]);
    }
    return delta * saturate(a);
}
)"
