#pragma once
#include "../src/nr_clarity_policy.h"
#include <string>
namespace claritycpu {
template<class Check>void Run(Check check){
 using namespace stackcpu;
 float y[9];for(float light:{0.f,.001f,.1f,.8f,1.f,4.f,100.f})for(float a:{0.f,.3f,1.f}){
  for(auto& v:y)v=light;
  check(ClarityGain(y,a,0.f)==1.f,"uniform SDR/HDR has exact identity");
 }
 for(auto& v:y)v=.5f;y[4]=.53f;y[1]=.45f;y[8]=.6f;
 const float edge=ClarityGain(y,1.f,0.f);
 check(edge>1.001f,"nonzero sharpening enhances a soft interior edge");
 check(ClarityGain(y,0.f,0.f)==1.f,"zero strength exactly bypasses");
 check(abs(ClarityGain(y,1.f,1.f)-1.f)<abs(edge-1.f)*.31f,"skin cue reduces added fine contrast");
 for(auto& v:y)v=.2f;y[4]=.201f;
 check(ClarityGain(y,1.f,0.f)==1.f,"small flat-region noise is not boosted");
 y[0]=std::numeric_limits<float>::quiet_NaN();
 check(ClarityGain(y,1.f,0.f)==1.f,"invalid neighbor retains center");
 std::mt19937 random(133);std::uniform_real_distribution<float> value(0.f,1.f);
 for(int test=0;test<5000;++test){
  const float scale=test%3==0?32.f:1.f;float lo=1e9f,hi=0.f;
  for(auto& v:y){v=value(random)*scale;lo=min(lo,v);hi=max(hi,v);}
  float amount=value(random),skin=value(random),gain=ClarityGain(y,amount,skin);
  check(std::isfinite(gain)&&gain>=.65f&&gain<=1.35f,"bounded finite relative gain");
  check(y[4]*gain>=lo-1e-5f&&y[4]*gain<=hi+1e-5f,"no new local luminance extrema");
  float rotated[9]={y[6],y[3],y[0],y[7],y[4],y[1],y[8],y[5],y[2]};
  check(abs(ClarityGain(rotated,amount,skin)-gain)<2e-6f,"rotation invariance");
 }
 // Real shared-math counterexamples: ordinary soft edges and an HDR
 // cross maximum that used to make the adaptive weight exactly zero.
 float softBright[9]={.35f,.50f,.60f,.35f,.50f,.60f,.35f,.50f,.60f};
 float softDark[9]={.65f,.50f,.40f,.65f,.50f,.40f,.65f,.50f,.40f};
 float hdrEdge[9]={2.8f,4.f,4.8f,2.8f,4.f,4.8f,2.8f,4.f,4.8f};
 check(ClarityGain(softBright,1.f,0.f)>1.08f,"full clarity bright-side gain meets the requested range");
 check(ClarityGain(softDark,1.f,0.f)<.92f,"full clarity dark-side gain meets the requested range");
 check(ClarityGain(hdrEdge,1.f,0.f)>1.02f,"HDR high-luminance edge no longer has zero CAS weight");
 for(float floor:{.05f,.4f,2.f})for(int phase:{0,1}){
  float checker[9];for(int i=0;i<9;++i)checker[i]=floor+((i+phase)%2)*floor*.3f;
  check(ClarityGain(checker,1.f,0.f)==1.f,"ideal alternating dot grid is not amplified at full clarity");
 }
 struct Resource{int refs=1;};struct Device{} first,other;
 Resource r1,r2;nrclarity::Cache<Resource,Device> cache;
 int creates=0,releases=0;bool fail=false,partial=false;
 auto create=[&](Resource*& r){++creates;if(fail){if(partial)r=&r2;return false;}r=creates==1?&r1:&r2;return true;};
 auto release=[&](Resource* r){--r->refs;++releases;};
 check(!cache.Prepare(&first,8,8,1,false,0,create,release)&&creates==0,"disabled does not allocate");
 check(cache.Prepare(&first,8,8,1,true,0,create,release)==&r1,"allocate enabled output");
 ++r1.refs; // a recorded command list owns an independent reference
 check(cache.Prepare(&first,8,8,1,true,10,create,release)==&r1&&creates==1,"strength changes reuse output");
 fail=true;
 check(!cache.Prepare(&other,8,8,1,true,11,create,release)&&r1.refs==2,"device mismatch allocation failure cannot use stale output");
 check(!cache.Prepare(&other,8,8,1,true,12,create,release)&&creates==2,"allocation failures back off");
 fail=false;
 check(cache.Prepare(&other,8,8,1,true,1011,create,release)==&r2&&r1.refs==1,"replacement preserves recorded frame ownership");
 check(!cache.Prepare(&other,8,8,1,false,1012,create,release)&&r2.refs==0,"disable drops only cache ownership");
 std::string trace;
 auto read=[&](int){trace+='R';};auto dispatch=[&](int a,int b){check(a!=b,"read/write resources never alias");trace+='D';};auto write=[&](int){trace+='W';};
 check(nrclarity::Finish(1,2,read,dispatch,write)==2&&trace=="RDW","output dispatch restores source before copyback");
 trace.clear();check(nrclarity::Finish(1,0,read,dispatch,write)==1&&trace.empty(),"missing scratch preserves regular NR output");
 check(nrclarity::Finish(1,1,read,dispatch,write)==1&&trace.empty(),"alias is rejected without dispatch");
 check(nrstack::ClarityBase>=nrstack::FinalBase+4&&nrstack::ClarityBase+4<=nrstack::DescriptorCount,"clarity bindings cannot overwrite recorded layers");
}
}
