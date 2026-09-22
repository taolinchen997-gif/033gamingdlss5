#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#include <limits>
#include "nr_controls_abi.h"
static bool Equal(const std::vector<Pixel>&a,const std::vector<Pixel>&b){return a.size()==b.size()&&!std::memcmp(a.data(),b.data(),a.size()*sizeof(Pixel));}
int main(){try{
    Gpu g;unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* n){++checks;if(!ok){++failures;std::printf("FAIL %s\n",n);}};
    constexpr UINT w=19,h=13;std::vector<Pixel> input(w*h);
    for(int mode=0;mode<=3;++mode) {
        for(size_t i=0;i<input.size();++i){float v=float(i%19)/3.f;input[i]={v,v*.5f,v*.2f,float(i%11)/10.f};
            if(mode==0){input[i].r/=7;input[i].g/=7;input[i].b/=7;}
            if(mode==2){input[i].r=pq(v*100);input[i].g=pq(v*50);input[i].b=pq(v*20);}}
        auto run=[&](const pregrade::Settings* p,int apply=1,float split=0.f){return g.run(input,w,h,mode,1,1,apply,split,DXGI_FORMAT_R16G16B16A16_FLOAT,false,nullptr,1,2,1,nullptr,p);};
        pregrade::Settings p;p.enabled=1;
        check(Equal(run(&p),input),"enabled neutral is bit exact in every colour space");
        p.exposure=1;
        auto exposed=run(&p),preonly=run(&p,0);
        check(Equal(exposed,preonly),"identity NR preserves pregrade rather than cancelling or doubling it");
        check(!Equal(exposed,input),"pregrade reaches final output");
        bool alpha=true,finite=true;for(size_t i=0;i<input.size();++i){alpha&=exposed[i].a==input[i].a;finite&=std::isfinite(exposed[i].r)&&std::isfinite(exposed[i].g)&&std::isfinite(exposed[i].b);}
        check(alpha && finite,"HDR and alpha remain valid");
        if(mode==1 || mode==3) {
            double error=0;for(size_t i=0;i<input.size();++i)error=std::max(error,double(std::abs(exposed[i].r-2*input[i].r)));
            check(error<0.00001,"one EV produces expected linear exposure independent of encoding");
        }
        p.contrast=1.03f;p.saturation=1.02f;p.warmth=0.03f;p.tint=-0.02f;p.highlights=.04f;
        auto styled=run(&p);bool sane=true;for(const auto& v:styled)sane&=std::isfinite(v.r)&&std::isfinite(v.g)&&std::isfinite(v.b);
        check(sane && !Equal(styled,input),"combined controls yield finite image");
        auto split=run(&p,1,.5f);check(split[0].r==input[0].r && split[w-1].r==styled[w-1].r,"split retains raw reference and graded NR side");
        p.enabled=0;check(Equal(run(&p),input),"off restores original output with non-neutral saved knobs");
        p.enabled=1;p.exposure=std::numeric_limits<float>::quiet_NaN();check(Equal(run(&p),input),"nonfinite settings rejected before GPU dispatch");
    }
    // A display-referred SDR scene never exceeds linear white. Its highlights
    // must still respond, while shadows and model edits remain intact.
    {
        std::vector<Pixel> ramp(w*h);for(size_t i=0;i<ramp.size();++i){float v=float(i%w)/(w-1);ramp[i]={v,v,v,.37f};}
        auto run=[&](pregrade::Settings p,bool encoded=false,int apply=1,const std::vector<Pixel>* model=nullptr){
            return g.run(ramp,w,h,0,1,1,apply,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,model,1,3,1,nullptr,&p,0,encoded);};
        pregrade::Settings neutral;neutral.enabled=1;auto p=neutral;p.highlights=.25f;
        auto inputModel=run(p,true),out=run(p),preonly=run(p,false,0);
        auto identity=run(neutral);
        check(inputModel[w-1].r<.99f,"SDR highlight control changes the actual NR input below or at white");
        check(Equal(out,preonly),"SDR highlight conditioning is applied once across model and resolve");
        bool order=true,shadows=true,alpha=true;for(UINT x=0;x<w;++x){
            if(x)order&=out[x].r>=out[x-1].r;
            if(ramp[x].r<=.5f)shadows&=std::abs(out[x].r-ramp[x].r)<1e-6f;
            alpha&=out[x].a==.37f;
        }
        check(order&&shadows&&alpha,"SDR highlight rolloff preserves ordering shadows and alpha");
        check(Equal(identity,ramp),"zero highlight amount preserves exact SDR identity");
        auto half=p;half.highlights=.125f;auto middle=run(half);
        check(middle[w-1].r>out[w-1].r&&middle[w-1].r<identity[w-1].r,"SDR highlight slider has graded strength");
        auto answer=inputModel;for(auto& pixel:answer)pixel.r=std::max(0.f,pixel.r-.03f);
        check(!Equal(run(p,false,1,&answer),out),"SDR highlight conditioning retains a real model colour edit");
        p.enabled=0;check(Equal(run(p),ramp),"disabled SDR highlight setting restores input exactly");
    }
    pregrade::Settings a,b;b.contrast=1.1f;
    check(pregrade::Signature(a)==pregrade::Signature(b),"disabled knob edits need no NR history reset");
    a.enabled=b.enabled=1;check(pregrade::Signature(a)!=pregrade::Signature(b),"active grade enters temporal contract");
    std::vector<Pixel> skin(w*h,{.45f,.34f,.23f,.37f}),blue(w*h,{.1f,.2f,.6f,.37f});
    auto render=[&](const std::vector<Pixel>& source,const pregrade::Settings& grade,const std::vector<Pixel>* model=nullptr){return g.run(source,w,h,0,1,1,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,model,1,3,1,nullptr,&grade);};
    pregrade::Settings unprotected;unprotected.enabled=1;unprotected.saturation=0;
    auto protectedGrade=unprotected;protectedGrade.skinProtection=1;
    auto grey=render(skin,unprotected),kept=render(skin,protectedGrade);
    check(std::abs(kept[0].r-skin[0].r)<1e-6f && std::abs(grey[0].r-skin[0].r)>.01f,"skin protection limits INPUT grade colour change");
    check(Equal(render(blue,unprotected),render(blue,protectedGrade)),"input skin cue does not alter blue objects");
    pregrade::Settings neutral;neutral.enabled=1;auto neutralSkin=neutral;neutralSkin.skinProtection=1;
    auto neural=skin;for(auto& p:neural){p.r+=.025f;p.g-=.015f;p.b+=.03f;}
    auto baseModel=render(skin,neutral,&neural),protectedModel=render(skin,neutralSkin,&neural);
    check(Equal(baseModel,protectedModel) && !Equal(baseModel,skin),"input protection NEVER erases a neural model colour edit");
    check(pregrade::Signature(neutral)!=pregrade::Signature(neutralSkin),"input protection changes reset input history");
    neutralSkin.skinProtection=NAN;check(!pregrade::Valid(neutralSkin),"invalid input skin protection rejected");

    for(int mode=0;mode<4;++mode) {
        std::vector<Pixel> ramp(w*h);for(size_t i=0;i<ramp.size();++i){float y=.01f+float(i%37)/37.f;ramp[i]={y,y*.61f,y*.28f,.43f};
            if(mode==2)ramp[i]={pq(y*300),pq(y*180),pq(y*84),.43f};else if(mode==3){ramp[i].r*=4;ramp[i].g*=4;ramp[i].b*=4;}}
        auto run=[&](pregrade::Settings grade,bool encoded=false,int apply=1){return g.run(ramp,w,h,mode,1,1,apply,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,nullptr,1,3,1,nullptr,&grade,0,encoded);};
        pregrade::Settings neutral;neutral.enabled=1;auto originalEncoded=run(neutral,true);
        for(unsigned style=1;style<=3;++style){auto p=neutral;p.style=style;
            auto encoded=run(p,true),out=run(p),repeat=run(p),preonly=run(p,false,0);
            check(!Equal(encoded,originalEncoded),"style changes ACTUAL encoded texture passed to model");
            check(!Equal(out,ramp)&&Equal(out,preonly),"style survives identity model exactly once");
            check(Equal(out,repeat),"style has no time/random/history flicker on fixed input");
            bool sane=true;for(auto v:out)sane&=std::isfinite(v.r)&&std::isfinite(v.g)&&std::isfinite(v.b)&&v.a==.43f;
            check(sane,"all input styles preserve finite HDR and alpha");
            p.styleStrength=0;check(Equal(run(p,true),originalEncoded)&&Equal(run(p),ramp),"zero strength is exact neutral input and output");
            p.styleStrength=1;p.enabled=0;check(Equal(run(p,true),originalEncoded)&&Equal(run(p),ramp),"disabled style is exact bypass");
        }
    }
    auto invalid=pregrade::Settings{};invalid.style=4;check(!pregrade::Valid(invalid),"out-of-range pre-style rejected");
    invalid.style=1;invalid.styleStrength=NAN;check(!pregrade::Valid(invalid),"NaN pre-style strength rejected");
    check(nrcontrolsabi::Valid(nrcontrolsabi::PreStyle,3)&&!nrcontrolsabi::Valid(nrcontrolsabi::PreStyle,1.5f),"new controls validate actual enum");
    std::printf("pre-NR grade and matching composition: %u checks, %u failures\n",checks,failures);return failures?1:0;
}catch(const std::exception&e){std::printf("ERROR %s\n",e.what());return 2;}}
