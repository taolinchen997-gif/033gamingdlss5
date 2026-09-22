// Actual shader execution: identity, bounded edits, controls, and highlight
// continuity. Uses synthetic model outputs; not a game or NR quality claim.
#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
static float encode(float level,int curve){
    float x=level/3.16f;
    float y=curve==1?x/std::sqrt(x*x+1.f):x<=.75f?x:.75f+.25f*((x-.75f)/.25f)/std::sqrt(1.f+std::pow((x-.75f)/.25f,2.f));
    return y<=.0031308f?12.92f*y:1.055f*std::pow(y,1.f/2.4f)-.055f;
}
static double nits(float v){
    const double p=std::pow(std::clamp(double(v),0.0,1.0),32.0/2523.0);
    return 10000.0*std::pow(std::max(p-3424.0/4096.0,0.0)/std::max(2413.0/128.0-2392.0/128.0*p,1e-7),16384.0/2610.0);
}
int main(){try{
    Gpu gpu;int checks=0,failures=0;constexpr UINT w=8,h=8;
    auto check=[&](bool ok,const char *name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}};
    double largestSmallEdit=0;
    for(int curve:{1,2})for(auto fmt:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT})
    for(float level:{.001f,.1f,1.f,4.f,16.f,32.f,128.f,1024.f}){
        std::vector<Pixel> original(w*h,Pixel{level,level,level,.37f});
        const float e=encode(level,curve);
        float previous=-1.f;
        for(float delta:{-.01f,-.001f,0.f,.001f,.005f,.01f}){
            float value=std::clamp(e+delta,0.f,1.f);
            std::vector<Pixel> model(w*h,Pixel{value,value,value,1.f});
            auto out=gpu.run(original,w,h,1,curve,1,1,0.f,fmt,false,&model,1.f,3.f);
            float got=out[0].r,ratio=got/level;
            check(std::isfinite(got)&&got>=0.f&&got<=level*3.001f,"finite source-relative HDR bound");
            check(out[0].a==original[0].a,"original alpha");
            check(got+level*.003f>=previous,"monotonic highlight response");previous=got;
            if(level>=4.f&&delta!=0.f){
                check(std::abs(ratio-1.f)<.15f,"small highlight edit cannot become flash");
                largestSmallEdit=std::max(largestSmallEdit,double(std::abs(ratio-1.f)));
            }
            if(delta==0.f)check(std::abs(ratio-1.f)<.015f,"synthetic identity control");
        }
    }
    std::vector<Pixel> original(w*h,Pixel{.2f,.3f,.4f,.7f});
    std::vector<Pixel> changed(w*h,Pixel{.8f,.7f,.9f,1.f});
    auto off=gpu.run(original,w,h,1,1,1,1,0,DXGI_FORMAT_R16G16B16A16_FLOAT,false,&changed,0.f);
    auto half=gpu.run(original,w,h,1,1,1,1,0,DXGI_FORMAT_R16G16B16A16_FLOAT,false,&changed,.5f);
    auto on=gpu.run(original,w,h,1,1,1,1,0,DXGI_FORMAT_R16G16B16A16_FLOAT,false,&changed,1.f);
    check(off[0].r==original[0].r&&off[0].g==original[0].g&&off[0].b==original[0].b,"mix zero is exact original");
    check(std::abs(on[0].r-original[0].r)>.01f,"model detail is not disabled");
    check(half[0].r>off[0].r&&half[0].r<on[0].r,"mix strength responds");
    auto neutral=gpu.run(original,w,h,1,1,1,1,0,DXGI_FORMAT_R16G16B16A16_FLOAT,false,&changed,1.f,3.f,0.f);
    check(std::abs(neutral[0].r/neutral[0].g-2.f/3.f)<.001f&&std::abs(neutral[0].b/neutral[0].g-4.f/3.f)<.001f,"colour zero preserves source chromaticity");
    std::vector<Pixel> invalid(w*h,Pixel{NAN,INFINITY,-INFINITY,0.f});
    auto bad=gpu.run(original,w,h,1,1,1,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,&invalid);
    check(std::isfinite(bad[0].r)&&std::isfinite(bad[0].g)&&std::isfinite(bad[0].b),"invalid model cannot emit nonfinite RGB");
    for(int mode:{0,1,2,3})for(int curve:{1,2})for(Pixel scene:{Pixel{16,1,0,.4f},Pixel{0,4,16,.4f},Pixel{4,16,2,.4f}}){
        if(mode==0){scene.r/=16;scene.g/=16;scene.b/=16;}
        if(mode==2){scene.r=pq(scene.r*62.5);scene.g=pq(scene.g*62.5);scene.b=pq(scene.b*62.5);}
        std::vector<Pixel> source(w*h,scene);
        for(Pixel answer:{Pixel{.99f,.1f,.99f,1},Pixel{.1f,.99f,.2f,1},Pixel{.999f,.999f,.999f,1}}){
            std::vector<Pixel> model(w*h,answer);
            auto result=gpu.run(source,w,h,mode,curve,1,1,0,DXGI_FORMAT_R16G16B16A16_FLOAT,false,&model,1,3);
            double peak=0,originalPeak=0;
            for(int c=0;c<3;++c){
                const float v=(&result[0].r)[c],o=(&scene.r)[c];
                peak=std::max(peak,mode==2?nits(v):double(v));
                originalPeak=std::max(originalPeak,mode==2?nits(o):double(o));
                check(std::isfinite(v)&&v>=-.00001f,"coloured output finite/nonnegative");
            }
            bool bounded=peak<=originalPeak*3.01+.001;
            if(!bounded)std::printf("HDR bound detail: mode=%d curve=%d source=%.4f output=%.4f\n",mode,curve,originalPeak,peak);
            check(bounded,"destination colour space peak remains bounded");
            check(result[0].a==scene.a,"coloured output preserves alpha");
        }
    }
    std::printf("bounded production resolve: %d checks, %d failures; largest small highlight edit %.4f%%\n",checks,failures,largestSmallEdit*100);
    return failures?1:0;
}catch(const std::exception&e){std::printf("ERROR %s\n",e.what());return 2;}}
