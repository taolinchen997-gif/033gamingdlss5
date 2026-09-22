// Original 033 output grade; no NVE shader, LUT or game asset is copied.
// Shared production HLSL / CPU body. No neighbourhood or temporal state.
K033_NATURAL_MATH(
float NaturalLumaGain(float y,float amount) {
    if(amount<=0.0f || y<=0.0f)return 1.0f;
    // User-controlled range v2: clear midtone contrast without new spatial detail.
    float contrast=.60f*(y-.18f)/(y+.18f)*(1.0f-saturate(y));
    float shoulder=.24f*max(y-1.0f,0.0f)/(1.0f+max(y-1.0f,0.0f));
    return 1.0f+saturate(amount)*(contrast-shoulder);
}
float NaturalChromaScale(float y,float skin,float amount) {
    return 1.0f+.24f*saturate(amount)*(1.0f-saturate(skin))*
        smoothstep(.01f,.12f,y)*(1.0f-smoothstep(.65f,1.2f,y));
}
)
