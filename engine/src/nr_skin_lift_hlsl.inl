#define K033_SKIN_LIFT_MATH(...) #__VA_ARGS__
#include "nr_skin_lift_math.inl"
#undef K033_SKIN_LIFT_MATH
R"(
// Compute the mask in encoded BT.709 and the gain in linear light for every
// output mode. Subtract the matching conversion to preserve source identity.
float3 SkinLiftOutput(float3 source,uint sourceMode,float diffuseWhite,float amount) {
    if(amount<=0.0f)return source;
    float3 working=EffectTo709(source,sourceMode,diffuseWhite);
    float3 lifted=SkinLift(working,amount);
    if(all(lifted==working))return source;
    return source+EffectFrom709(lifted,sourceMode,diffuseWhite)-EffectFrom709(working,sourceMode,diffuseWhite);
}
)"
