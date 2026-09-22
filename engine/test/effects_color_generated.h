
float3 NrPqToNits(float3 v) {
    const float m1 = 2610.0f / 16384.0f, m2 = 2523.0f / 32.0f;
    const float c1 = 3424.0f / 4096.0f, c2 = 2413.0f / 128.0f, c3 = 2392.0f / 128.0f;
    float3 p = pow(saturate(v), 1.0f / m2);
    return 10000.0f * pow(max(p - c1, 0.0f) / max(c2 - c3 * p, 1e-7f), 1.0f / m1);
}
float3 NrNitsToPq(float3 v) {
    const float m1 = 2610.0f / 16384.0f, m2 = 2523.0f / 32.0f;
    const float c1 = 3424.0f / 4096.0f, c2 = 2413.0f / 128.0f, c3 = 2392.0f / 128.0f;
    float3 p = pow(saturate(v / 10000.0f), m1);
    return pow((c1 + c2 * p) / (1.0f + c3 * p), m2);
}
float3 Nr2020To709(float3 v) {
    return mul(float3x3(1.6604910f,-0.5876411f,-0.0728499f,
                       -0.1245505f,1.1328999f,-0.0083494f,
                       -0.0181508f,-0.1005789f,1.1187297f), v);
}
float3 Nr709To2020(float3 v) {
    return mul(float3x3(0.6274039f,0.3292830f,0.0433131f,
                       0.0690973f,0.9195404f,0.0113623f,
                       0.0163914f,0.0880133f,0.8955953f), v);
}
// Reversible compression towards equal-luminance neutral, only near the gamut
// boundary. Avoid clipping negative BT.709 channels from saturated BT.2020 input.
float3 NrGamut(float3 v, bool inverse) {
    float y = dot(v, float3(0.2126f, 0.7152f, 0.0722f));
    if (y <= 1e-7f) return max(v, 0.0f);
    float radius = max(0.0f, 1.0f - min(v.r, min(v.g, v.b)) / y);
    const float knee = 0.8f;
    if (radius <= knee) return v;
    float mapped = radius;
    if (inverse) {
        float u = min((radius - knee) / (1.0f - knee), 0.99999f);
        mapped = knee + (1.0f - knee) * u / (1.0f - u);
    } else {
        float u = (radius - knee) / (1.0f - knee);
        mapped = knee + (1.0f - knee) * u / (1.0f + u);
    }
    return y + (v - y) * (mapped / radius);
}
float3 NrPqToWorking(float3 v, float diffuseWhite) {
    return NrGamut(Nr2020To709(NrPqToNits(v)), false) / max(diffuseWhite, 1.0f);
}
float3 NrWorkingToPq(float3 v, float diffuseWhite) {
    return NrNitsToPq(Nr709To2020(NrGamut(v * max(diffuseWhite, 1.0f), true)));
}
float3 NrStore(float3 v, uint sourceMode, float diffuseWhite) {
    float3 result = v;
    if (sourceMode == 2) result = NrWorkingToPq(v, diffuseWhite);
    else if (sourceMode == 3) result = v * max(diffuseWhite, 1.0f) / 80.0f;
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
    float peak = NrPeak(max(original, 0.0f));
    float change = NrPeak(abs(proxyEdit));
    float span = NrPeak(abs(delta));
    if (peak <= 0.0f || change <= 0.0f || span <= 0.0f) return float3(0,0,0);
    float g = clamp(limit, 1.0f, 8.0f);
    float budget = peak * g * change / max(NrPeak(abs(proxy)), 1.0f / 512.0f);
    float a = min(1.0f, budget / max(span, 1e-20f));
    // Also keep the edited scene nonnegative (preserve existing negatives) and
    // below the selected source-relative peak. Large legitimate source values
    // remain untouched when the model makes no edit.
     for (int c = 0; c < 3; ++c) {
        if (delta[c] > 0.0f) a = min(a, max(0.0f, peak * g - original[c]) / delta[c]);
        else if (delta[c] < 0.0f) a = min(a, max(0.0f, original[c]) / -delta[c]);
    }
    return delta * saturate(a);
}
