#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#include "../src/nr_controls_abi.h"
#include <limits>
int main(){try{
 Gpu g;unsigned checks=0,failures=0;
 auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}};
 constexpr UINT w=19,h=13;
 auto run=[&](const std::vector<Pixel>& input,const std::vector<Pixel>* model,float protect,int mode=0,int apply=1){
  return g.run(input,w,h,mode,1,1,apply,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,model,1,3,1,nullptr,nullptr,protect);};
 for(float level:{.15f,.5f,1.f}) {
  std::vector<Pixel> original(w*h,{.45f*level,.34f*level,.23f*level,.37f});auto model=original;
  for(auto& p:model){p.r+=.03f*level;p.g-=.01f*level;p.b+=.01f*level;}
  auto base=run(original,&model,0),guarded=run(original,&model,1),half=run(original,&model,.5f);
  const auto difference=[&](const Pixel& p){const auto& q=original[0];return std::abs(p.r-q.r)+std::abs(p.g-q.g)+std::abs(p.b-q.b);};
  check(difference(guarded[0])<difference(half[0]) && difference(half[0])<difference(base[0]),"skin colour edits reduced continuously across luminance levels");
  check(guarded[0].a==original[0].a && std::isfinite(guarded[0].r),"alpha unchanged and finite");
 }
 for(Pixel colour:{Pixel{.4f,.4f,.4f,.5f},Pixel{.1f,.2f,.6f,.6f},Pixel{.1f,.5f,.1f,.7f}}){
  std::vector<Pixel> original(w*h,colour),model=original;for(auto& p:model)p.r+=.02f;
  auto a=run(original,&model,0),b=run(original,&model,1);
  check(!memcmp(a.data(),b.data(),a.size()*sizeof(Pixel)),"neutral, blue and green edits not suppressed by skin colour cue");
 }
 for(int mode=0;mode<4;++mode){
  std::vector<Pixel> original(w*h,{.45f,.34f,.23f,.3f});
  auto same=run(original,nullptr,1,mode),disabled=run(original,nullptr,1,mode,0);
  check(!memcmp(same.data(),original.data(),same.size()*sizeof(Pixel)) && !memcmp(disabled.data(),original.data(),same.size()*sizeof(Pixel)),"skin protection preserves model identity and apply-off in every colour space");
 }
 std::vector<Pixel> original(w*h,{.45f,.34f,.23f,.3f}),model=original;for(auto& p:model)p.r+=.02f;
 auto a=run(original,&model,0),b=run(original,&model,std::numeric_limits<float>::quiet_NaN());
 check(!memcmp(a.data(),b.data(),a.size()*sizeof(Pixel)),"invalid protection value falls back to off");
 check(nrcontrolsabi::Valid(nrcontrolsabi::Skin,1.25f) && !nrcontrolsabi::Valid(nrcontrolsabi::Preset,.5f) && !nrcontrolsabi::Valid(nrcontrolsabi::Skin,3.f),"control schema rejects malformed model settings");
 printf("portrait composition: %u checks, %u failures\n",checks,failures);return failures?1:0;
}catch(const std::exception& e){printf("ERROR %s\n",e.what());return 2;}}
