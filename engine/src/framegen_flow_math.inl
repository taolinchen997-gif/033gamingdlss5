// Shared CPU/HLSL arithmetic, not a second reference implementation.
// Preserve a small absolute-lightness term while matching local structure.
float FlowPatchCost(float absoluteDifference, float centeredDifference) {
    return (centeredDifference + 0.15f * absoluteDifference) / 9.0f;
}
float FlowBlockCost(float a[9], float b[9]) {
    float ma=0,mb=0;
    for(int k=0;k<9;++k){ma+=a[k];mb+=b[k];}
    ma/=9;mb/=9;float absolute=0,centered=0;
    for(int k=0;k<9;++k){
        float raw=a[k]-b[k],local=(a[k]-ma)-(b[k]-mb);
        absolute+=raw<0?-raw:raw;centered+=local<0?-local:local;
    }
    return FlowPatchCost(absolute,centered);
}
float FlowConsistency(float residualPixels) {
    return residualPixels >= 2.0f ? 0.0f : 1.0f - residualPixels * 0.5f;
}
