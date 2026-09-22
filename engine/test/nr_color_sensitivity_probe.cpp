// Offline diagnostic: controlled model outputs, NOT recorded game/model output.
// Reuses the production shaders and WARP upload/dispatch/readback harness.
#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
static float encodedGrey(float level) {
    const float x=level/3.16f;
    const float y=x/std::sqrt(x*x+1.f);
    return y<=0.0031308f ? 12.92f*y : 1.055f*std::pow(y,1.f/2.4f)-0.055f;
}
int main() { try {
    Gpu gpu;constexpr UINT w=8,h=8;int amplified=0;
    std::puts("Synthetic model perturbations. source=scene-linear curve=Neutwo compose=reference white=3.16 model=FP16");
    std::puts("source,encoded_input,synthetic_srgb_delta,resolved,output_source_ratio");
    for(float level : {1.f,4.f,8.f,16.f,32.f}) {
        const float encoded=encodedGrey(level);
        std::vector<Pixel> input(w*h,Pixel{level,level,level,1.f});
        for(float delta : {-0.001f,0.f,0.001f,0.002f,0.005f,0.01f}) {
            const float v=std::max(0.f,std::min(1.f,encoded+delta));
            std::vector<Pixel> model(w*h,Pixel{v,v,v,1.f});
            auto result=gpu.run(input,w,h,1,1,1,1,0.f,DXGI_FORMAT_R16G16B16A16_FLOAT,false,&model);
            const float ratio=result[0].r/level;
            std::printf("%.3f,%.7f,%+.4f,%.6f,%.6f\n",level,encoded,delta,result[0].r,ratio);
            if(!std::isfinite(ratio)) throw std::runtime_error("Nonfinite resolve result");
            if(delta==0.f && std::abs(ratio-1.f)>0.02f) throw std::runtime_error("Synthetic identity control failed; reject this probe");
            if(ratio>10.f)++amplified;
        }
    }
    std::printf("DIAGNOSTIC: %d controlled cases exceeded 10x source; no inference about actual game model output.\n",amplified);
    return 0;
} catch(const std::exception &e){std::printf("ERROR: %s\n",e.what());return 2;} }
