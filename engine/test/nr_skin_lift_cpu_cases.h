// Properties of the production lift body; no shader/GPU execution.
namespace skinliftcpu {
template<class Check> void Run(Check check) {
    using namespace stackcpu;
    const float3 skin(0.42f,0.33f,0.25f);
    check(SkinWeight(skin)>0.9f,"skin cue accepts the existing midtone example");
    check(SkinLift(skin,0.f)==skin,"zero lift is exact identity");
    check(SkinLift(float3(0.f),1.f)==float3(0.f),"black remains exact black");
    check(SkinLift(float3(0.5f),1.f)==float3(0.5f),"neutral does not become skin");
    check(SkinLift(float3(0.f,1.f,0.f),1.f)==float3(0.f,1.f,0.f),"green is unchanged");
    check(SkinLift(skin*8.f,1.f)==skin*8.f,"HDR highlights are unchanged");
    check(SkinLift(skin,1.f).x>skin.x,"nonzero lift reaches eligible skin");
    for(int l=0;l<=200;++l) {
        float3 input=skin*(l/100.f);
        float previous=input.x;
        for(int a=0;a<=20;++a) {
            float3 output=SkinLift(input,a/20.f);
            check(isfinite(output),"lift remains finite");
            check(output.x>=previous-1e-7f,"amount is monotone");
            check(output.x>=input.x && output.x<=input.x*1.48001f,"bounded positive gain");
            check(abs(output.x*input.y-output.y*input.x)<2e-7f &&
                  abs(output.z*input.y-output.y*input.z)<2e-7f,"single gain preserves chromaticity");
            previous=output.x;
        }
    }
}
}
