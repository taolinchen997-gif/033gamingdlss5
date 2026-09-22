// CPU-only production math and extracted source bindings. No graphics APIs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
using uint=unsigned;
struct float3 {
 union {struct{float x,y,z;};struct{float r,g,b;};};
 float3(float a=0):x(a),y(a),z(a){} float3(float a,float b,float c):x(a),y(b),z(c){}
 float operator[](int i)const{return i==0?x:i==1?y:z;}
};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float3 operator*(float3 a,float3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
float3 operator/(float3 a,float3 b){return {a.x/b.x,a.y/b.y,a.z/b.z};}
float3& operator*=(float3& a,float3 b){a=a*b;return a;}
bool operator==(float3 a,float3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool operator<(float3 a,float b){return a.x<b||a.y<b||a.z<b;}
float min(float a,float b){return std::min(a,b);} float max(float a,float b){return std::max(a,b);}
float3 min(float3 a,float3 b){return {min(a.x,b.x),min(a.y,b.y),min(a.z,b.z)};}
float3 max(float3 a,float3 b){return {max(a.x,b.x),max(a.y,b.y),max(a.z,b.z)};}
float clamp(float a,float b,float c){return std::clamp(a,b,c);}
float saturate(float v){return clamp(v,0,1);} float3 saturate(float3 v){return {saturate(v.x),saturate(v.y),saturate(v.z)};}
using std::abs;using std::pow;using std::isfinite;
float3 abs(float3 a){return {abs(a.x),abs(a.y),abs(a.z)};}
float3 pow(float3 a,float b){return {pow(a.x,b),pow(a.y,b),pow(a.z,b)};}
bool isfinite(float3 a){return isfinite(a.x)&&isfinite(a.y)&&isfinite(a.z);}
bool all(bool a){return a;}bool any(bool a){return a;}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float smoothstep(float a,float b,float v){float t=saturate((v-a)/(b-a));return t*t*(3-2*t);}
const float3 kLuma(.2126f,.7152f,.0722f);
struct float3x3{float m[9];float3x3(float a,float b,float c,float d,float e,float f,float g,float h,float i):m{a,b,c,d,e,f,g,h,i}{}};
float3 mul(float3x3 m,float3 v){return {m.m[0]*v.x+m.m[1]*v.y+m.m[2]*v.z,m.m[3]*v.x+m.m[4]*v.y+m.m[5]*v.z,m.m[6]*v.x+m.m[7]*v.y+m.m[8]*v.z};}
#include "effects_color_generated.h"
#include "effects_generation.h"
#if EFFECTS_NEW
#define K033_EFFECT_MATH(...) __VA_ARGS__
#include "../src/nr_effect_math.inl"
#undef K033_EFFECT_MATH
#include "effects_bindings_generated.h"
#else
// Reference conversions only. The legacy behavior below calls the actual
// frozen skin and NrBoundEdit bodies; no invented old limiter.
float EffectDecode1(float v){return v<=.04045f?v/12.92f:pow((v+.055f)/1.055f,2.4f);}
float EffectEncode1(float v){return v<=.0031308f?v*12.92f:1.055f*pow(max(v,0.f),1/2.4f)-.055f;}
float3 EffectDecode(float3 v){return {EffectDecode1(v.x),EffectDecode1(v.y),EffectDecode1(v.z)};}
float3 EffectEncode(float3 v){return {EffectEncode1(v.x),EffectEncode1(v.y),EffectEncode1(v.z)};}
float3 EffectTo709(float3 v,uint mode,float white){if(mode==1)return EffectDecode(v);if(mode==2)return Nr2020To709(NrPqToNits(v))/white;if(mode==3)return v*80.f/white;return v;}
float3 EffectFrom709(float3 v,uint mode,float white){if(mode==1)return EffectEncode(v);if(mode==2)return NrNitsToPq(Nr709To2020(v)*white);if(mode==3)return v*white/80.f;return v;}
float3 EffectShadowOutput(float3 source,float3 changed,uint mode,float white,float scene,float guard){
 if(mode==1)return source+NrBoundEdit(source,changed-source,changed-source,source,guard);
 return changed; // curve-3 HDR bridge explicitly skips the old edit bound
}
#endif
#define K033_SKIN_LIFT_MATH(...) __VA_ARGS__
#include "../src/nr_skin_lift_math.inl"
#undef K033_SKIN_LIFT_MATH
#include "effects_skin_bindings_generated.h"
float gap(float3 a,float3 b){auto d=abs(a-b);return max(d.x,max(d.y,d.z));}
int main(){
 unsigned checks=0,failed=0;
 auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;if(failed<=20)printf("FAIL: %s\n",why);}};
 float3 encoded(.42f,.33f,.25f),linear=EffectDecode(encoded);
#if EFFECTS_NEW
 float cue=SkinWeightLinear(linear);
#else
 float cue=SkinWeight(linear); // actual legacy clarity/natural input
#endif
 printf("CUE encoded=%.9f final=%.9f\n",SkinWeight(encoded),cue);
 check(abs(cue-SkinWeight(encoded))<2e-6f,"same physical skin has the same cue at final effects");
 check(EFFECTS_ROUTE_CHECK,"active NR and final effects route through the shared output protection");
 for(float input:{.05f,.1f}){
  auto output=EffectShadowOutput(input,input*3.f,1,203,1,3);
  float source=EffectDecode1(input),ratio=EffectDecode1(output.x)/source;
  float gate=smoothstep(0,.05f,source),budget=1+3*gate/max(1-gate,1e-7f);
  printf("DARK sdr=%.3f proposed=%.3f output=%.9f physical_ratio=%.6f budget=%.6f\n",input,input*3,output.x,ratio,budget);
  check(ratio<=budget+2e-5f,"active SDR dark counterexample respects linear-light budget");
 }
 auto liftedSdr=EffectTo709(SkinLiftOutput(encoded,1,203,.35f),1,203);
 auto liftedLinear=SkinLiftOutput(linear,0,203,.35f);
 printf("LIFT equivalent_sdr_vs_linear_gap=%.9f\n",gap(liftedSdr,liftedLinear));
 check(gap(liftedSdr,liftedLinear)<3e-6f,"skin lift applies the same physical gain in SDR and linear output");
 for(uint mode:{0u,1u,2u,3u}){
  float3 original(.012f,.009f,.006f),changed(.05f,.001f,.09f);
  auto a=EffectFrom709(original,mode,203),b=EffectFrom709(changed,mode,203);
  auto out=EffectTo709(EffectShadowOutput(a,b,mode,203,1,3),mode,203);
  auto reference=EffectShadowOutput(original,changed,0,203,1,3);
  check(gap(out,reference)<3e-6f,"SDR/PQ/scRGB normalize to the same dark edit");
  auto c=EffectFrom709(linear,mode,203);
  check(gap(EffectTo709(SkinLiftOutput(c,mode,203,.35f),mode,203),liftedLinear)<8e-6f,"skin lift output encoding equivalence");
  check(SkinLiftOutput(c,mode,203,0)==c,"zero skin lift preserves encoded bits");
  check(EffectShadowOutput(c,c,mode,203,1,3)==c,"zero model edit preserves encoded bits");
 }
 auto scaled=EffectShadowOutput(float3(.02f)*3.16f,float3(.1f)*3.16f,0,203,3.16f,3)/3.16f;
 check(gap(scaled,EffectShadowOutput(.02f,.1f,0,203,1,3))<1e-7f,"scene-linear white normalization is respected");
#if EFFECTS_NEW
 std::mt19937 random(613);std::uniform_real_distribution<float> unit(0.f,1.f);
 for(int i=0;i<3000;++i){
  float3 original= float3(.25f+unit(random),.25f+unit(random),.25f+unit(random))*(.0001f+.03f*unit(random));
  float3 target=max(original+float3(unit(random)-.5f,unit(random)-.5f,unit(random)-.5f)*.2f,float3(0.f));
  float3 out=EffectShadowLinear(original,target,3.f);float y=dot(original,kLuma),oy=dot(out,kLuma);
  float gate=smoothstep(0,.05f,y),g=1+3*gate/max(1-gate,1e-7f);
  check(isfinite(out)&&!any(out<0.f),"random dark RGB stays finite and nonnegative");
  check(oy<=y*g+1e-7f && oy>=y/g-1e-7f,"random chroma/luma edits honor the luminance envelope");
  auto delta=target-original,kept=out-original;
  check(abs(kept.x*delta.y-kept.y*delta.x)<2e-8f&&abs(kept.z*delta.y-kept.y*delta.z)<2e-8f,"one scalar retains edit direction");
  check(EffectShadowLinear(original,original,3)==original,"arbitrary original detail survives exact identity");
 }
 check(EffectShadowLinear(.02f,.0202f,3)==float3(.0202f),"mild broad dark lighting survives unchanged");
 check(EffectShadowLinear(float3(.3f,.4f,.5f),float3(.6f,.2f,.7f),3)==float3(.6f,.2f,.7f),"bright NR edit is bit-exact unchanged");
 check(EffectShadowLinear(0,.2f,3)==float3(0),"no artificial illumination of true black");
 check(EffectShadowLinear(float3(-.01f,.02f,.03f),float3(.03f,.04f,.05f),3)==float3(.03f,.04f,.05f),"signed HDR is not damaged by shadow policy");
 check(EffectShadowLinear(float3(.01f,.02f,.03f),float3(-.03f,.04f,.05f),3)==float3(-.03f,.04f,.05f),"signed target gamut is preserved");
 for(int i=1;i<=200;++i){
  float x=.05f-1e-5f*i;
  check(gap(EffectShadowLinear(x,x*3,3),EffectShadowLinear(x+1e-5f,(x+1e-5f)*3,3))<.0001f,"dark knee has no discontinuous on/off edge");
  for(float limit:{1.f,3.f,8.f})check(gap(EffectShadowLinear(x,.9f,limit),EffectShadowLinear(x+1e-5f,.9f,limit))<.0001f,"large proposed edits also remain continuous at the release knee");
 }
 auto frameA=EffectShadowLinear(.01f,.07f,3),frameB=EffectShadowLinear(.01f,.005f,3);
 // Require at least 90% suppression of this deliberately excessive edit pair.
 // The continuous release budget gives ~.006065, not the earlier rounded .006.
 float syntheticRatio=abs(frameA.x-frameB.x)/(.07f-.005f);
 printf("SYNTHETIC remaining_variation_ratio=%.9f (CPU constructed edits only)\n",syntheticRatio);
 check(syntheticRatio<.1f,"synthetic dark-frame fluctuations reduced by at least 90 percent without history");
 auto nearBlack=SkinLift(float3(.0002f,.00015f,.0001f),1.f);
 check(nearBlack==float3(.0002f,.00015f,.0001f),"skin lift leaves near-black below linear toe unchanged");
#endif
 printf("EffectsCpu %u checks, %u failures; no GPU/game execution\n",checks,failed);
 return failed?1:0;
}
