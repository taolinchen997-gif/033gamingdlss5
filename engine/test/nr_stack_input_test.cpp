// Production GPU input shader with controlled colour/skin-like fixtures.
// These establish conditioning invariants, not subjective facial quality.
#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#include "../src/nr_stack_policy.h"
#include <limits>
int main(){try{
 Gpu g;const UINT w=19,h=13;unsigned checks=0,failures=0;
 auto check=[&](bool ok,const char* text){++checks;if(!ok){++failures;printf("FAIL: %s\n",text);}};
 auto run=[&](const std::vector<Pixel>& base,const std::vector<Pixel>& prior){return g.run(base,w,h,0,1,4,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,&prior);};
 std::vector<Pixel> base(w*h,Pixel{.3f,.3f,.3f,.7f});
 for(UINT y=0;y<h;++y)for(UINT x=w/2;x<w;++x)base[y*w+x]={.7f,.7f,.7f,.7f};
 auto identity=run(base,base);bool same=true;for(size_t i=0;i<base.size();++i)same&=std::abs(identity[i].r-base[i].r)<1e-6f&&identity[i].a==base[i].a;
 check(same,"neutral edge identity survives conditioning");
 auto prior=base;for(UINT y=0;y<h;++y)prior[y*w+w/2].r+=.18f;
 auto clean=run(base,prior);auto i=(h/2)*w+w/2;
 float before=prior[i].r-(prior[i].g+prior[i].b)*.5f,after=clean[i].r-(clean[i].g+clean[i].b)*.5f;
 check(after<before*.4f&&after>0,"excess fringe reduced without deleting all model colour");
 auto luma=[](Pixel p){return .2126f*p.r+.7152f*p.g+.0722f*p.b;};
 check(std::abs(luma(clean[i])-luma(prior[i]))<1e-5f,"fringe correction does not darken contour luminance");
 std::fill(base.begin(),base.end(),Pixel{.55f,.4f,.3f,.7f});prior=base;
 for(auto& pixel:prior){pixel.r+=.03f;pixel.g+=.03f;pixel.b+=.03f;}
 auto light=run(base,prior);check(std::abs(light[i].r-prior[i].r)<1e-5f,"new facial lighting reaches next model");
 prior=base;prior[i].r-=.16f;prior[i].g-=.16f;prior[i].b-=.16f;
 auto relief=run(base,prior);float dent=luma(base[i])-luma(relief[i]);
 check(dent>0.005f&&dent<.10f,"synthetic skin crease bounded but face detail is not frozen");
 std::fill(base.begin(),base.end(),Pixel{.8f,.1f,.05f,.7f});prior=base;auto red=run(base,prior);
 check(std::abs(red[i].r-base[i].r)<1e-6f&&std::abs(red[i].g-base[i].g)<1e-6f,"legitimate red material is preserved");
 prior[i].r=std::numeric_limits<float>::quiet_NaN();auto finite=run(base,prior);
 check(std::isfinite(finite[i].r)&&std::abs(finite[i].r-base[i].r)<1e-6f,"invalid preceding answer falls back to current input");
 const auto first=nrstack::ForPass(0,1.3f,1.1f,.9f,1.25f);
 check(first.intensity==1.3f&&first.structure==1.1f&&first.tone==.9f&&first.skin==1.25f,"first model unchanged");
 for(int pass=1;pass<4;++pass){auto tune=nrstack::ForPass(pass,1.3f,1.1f,.9f,1.25f);check(tune.intensity>0&&tune.skin>0&&tune.skin<first.skin&&tune.structure>0&&tune.structure<first.structure&&tune.tone>0&&tune.tone<first.tone,"every subsequent model retains skin and appearance contributions");}
 printf("STACK INPUT: %u checks, %u failures; fringe %.6f -> %.6f, skin crease %.6f -> %.6f; synthetic only\n",checks,failures,before,after,.16f,dent);
 return failures?1:0;
}catch(const std::exception& e){printf("FAIL: %s\n",e.what());return 1;}}
