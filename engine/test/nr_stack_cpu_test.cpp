// No Windows/D3D device, texture readback, model, driver or window is created.
// The math and finalization sequence are the production bodies, not copies.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>
#include "../src/nr_stack_policy.h"
#include "../src/nr_stack_layout.h"
#include "../src/nr_stack_finish.h"
#include "../src/nr_layer_resolution.h"
#include "nr_layer_cpu_cases.h"

namespace stackcpu {
struct float3 {
    float x,y,z;
    float3(float a=0):x(a),y(a),z(a){}
    float3(float a,float b,float c):x(a),y(b),z(c){}
    float operator[](int i)const{return i==0?x:i==1?y:z;}
};
inline float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float3 operator*(float b,float3 a){return a*b;}
inline bool operator==(float3 a,float3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
using std::min;using std::max;using std::abs;using std::clamp;
inline float3 min(float3 a,float3 b){return {min(a.x,b.x),min(a.y,b.y),min(a.z,b.z)};}
inline float3 max(float3 a,float3 b){return {max(a.x,b.x),max(a.y,b.y),max(a.z,b.z)};}
inline bool isfinite(float3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline bool all(bool a){return a;}
inline bool any(bool a){return a;}
inline bool operator<(float3 a,float b){return a.x<b||a.y<b||a.z<b;}
using std::isfinite;
inline float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float saturate(float a){return clamp(a,0.f,1.f);}
inline float lerp(float a,float b,float t){return a+(b-a)*t;}
const float3 kLuma(.2126f,.7152f,.0722f);
#define K033_STACK_MATH(...) __VA_ARGS__
#include "../src/nr_stack_math.inl"
#undef K033_STACK_MATH
inline float smoothstep(float lo,float hi,float value){float t=saturate((value-lo)/(hi-lo));return t*t*(3.f-2.f*t);}
#define K033_EFFECT_MATH(...) __VA_ARGS__
#include "../src/nr_effect_math.inl"
#undef K033_EFFECT_MATH
#define K033_SKIN_LIFT_MATH(...) __VA_ARGS__
#include "../src/nr_skin_lift_math.inl"
#undef K033_SKIN_LIFT_MATH
#define K033_CLARITY_MATH(...) __VA_ARGS__
#include "../src/nr_clarity_math.inl"
#undef K033_CLARITY_MATH
#define K033_NATURAL_MATH(...) __VA_ARGS__
#include "../src/nr_natural_math.inl"
#undef K033_NATURAL_MATH
}
#include "nr_skin_lift_cpu_cases.h"
#include "nr_clarity_cpu_cases.h"
#include "nr_natural_cpu_cases.h"

int main(){
    using namespace stackcpu;
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* why){++checks;if(!good){++failures;std::printf("FAIL: %s\n",why);}};
    layercpu::Run(check);
    skinliftcpu::Run(check);
    claritycpu::Run(check);
    naturalcpu::Run(check);
    auto near=[](float3 a,float3 b){return max(max(abs(a.x-b.x),abs(a.y-b.y)),abs(a.z-b.z))<2e-6f;};
    float3 a[9],b[9];
    auto fill=[&](float3 original,float3 prior){for(int i=0;i<9;++i){a[i]=original;b[i]=prior;}};
    fill(.5f,{.6f,.45f,.45f});
    auto uniform=StackCondition(a,b,0);
    check(near(uniform,b[4]),"uniform colour change must survive, not lose 79 percent of chroma edit");
    float legacyFraction=.025f/(.6f-dot(b[4],kLuma));
    std::printf("COLOUR COUNTEREXAMPLE: legacy chroma fraction %.8f; corrected RGB %.6f %.6f %.6f (target .60 .45 .45)\n",legacyFraction,uniform.x,uniform.y,uniform.z);

    fill(.5f,.5f);for(int i:{1,4,7})b[i]=b[i]+float3(.18f,0,0);
    auto fringe=StackCondition(a,b,0);
    float before=b[4].x-(b[4].y+b[4].z)*.5f,after=fringe.x-(fringe.y+fringe.z)*.5f;
    check(after>0&&after<before*.4f,"one-pixel red fringe reduced with a live colour contribution");
    check(abs(dot(fringe,kLuma)-dot(b[4],kLuma))<2e-6f,"colour cleanup does not darken contour luminance");
    for(int i:{1,4,7})b[i]=.5f;
    for(int i:{0,4,8})b[i]=b[i]+float3(.18f,0,0);
    check(near(fringe,StackCondition(a,b,0)),"diagonal fringe receives same local treatment");
    std::printf("FRINGE: red excess %.6f -> %.6f; luma delta %.8f\n",before,after,dot(fringe,kLuma)-dot(b[4],kLuma));

    fill({.55f,.4f,.3f},{.58f,.43f,.33f});
    check(near(StackCondition(a,b,1),b[4]),"broad facial lighting remains a neural contribution");
    fill({.55f,.4f,.3f},{.55f,.4f,.3f});b[4]=b[4]-.16f;
    auto crease=StackCondition(a,b,1);float dent=dot(a[4]-crease,kLuma);
    check(dent>.005f&&dent<.10f,"skin-like fine crease bounded without replacing the whole face");
    check(near(StackCondition(a,b,0),b[4]),"zero skin cue does not silently apply local skin relief");
    std::printf("CREASE: .160000 -> %.6f; broad facial darkening intentionally uncorrected\n",dent);
    fill({.55f,.4f,.3f},{.275f,.2f,.15f});
    check(near(StackCondition(a,b,1),b[4]),"local bound cannot claim to fix whole-face darkening");
    fill({.8f,.1f,.05f},{.8f,.1f,.05f});
    check(StackCondition(a,b,1)==a[4],"legitimate red material identity is exact");
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        fill(.5f,.5f);b[4].x=invalid;
        check(StackCondition(a,b,1)==a[4],"invalid center answer falls back to the current original");
        fill(.5f,.53f);b[0].x=invalid;
        check(isfinite(StackCondition(a,b,1)),"invalid neighbouring answer cannot poison the result");
    }
    std::mt19937 rng(330537);std::uniform_real_distribution<float> unit(0.f,1.f);
    for(int trial=0;trial<4096;++trial){
        float3 source(unit(rng),unit(rng),unit(rng));
        fill(source,source);check(StackCondition(a,b,unit(rng))==source,"random original identity remains bit-exact");
        float3 answer(unit(rng),unit(rng),unit(rng));
        fill(source,answer);check(near(StackCondition(a,b,unit(rng)),answer),"uniform in-gamut neural recolouring preserved");
        for(int i=0;i<9;++i){a[i]={unit(rng),unit(rng),unit(rng)};b[i]={unit(rng)*2-.5f,unit(rng)*2-.5f,unit(rng)*2-.5f};}
        auto out=StackCondition(a,b,unit(rng));
        check(isfinite(out)&&min(min(out.x,out.y),out.z)>=-1e-6f&&max(max(out.x,out.y),out.z)<=1.000001f,"finite neighbourhood output stays in model gamut");
    }
    // The old absolute-detail bound admitted a .10 light ridge -> -.16
    // dark crease because abs(-.16) < 1.5*.10+.025. The relative bound must
    // protect the original geometry while retaining a bounded neural edit.
    fill({.55f,.4f,.3f},{.55f,.4f,.3f});
    a[4]=a[4]+.10f;b[4]=b[4]-.16f;
    const float oldAllowed=1.5f*.10f+.025f;
    check(.16f<oldAllowed,"counterexample was admitted by the former absolute bound");
    const float ridgeMean=dot(a[0],kLuma);
    const auto ridge=StackCondition(a,b,1.f);
    check(dot(ridge,kLuma)>ridgeMean,"skin protection cannot invert an original light ridge into a crease");
    std::printf("ADDED CREASE: original detail +.100000; model -.160000; corrected %.6f\n",dot(ridge,kLuma)-ridgeMean);
    // No-op, broad-lighting, partial-mask continuity, and no overcorrection.
    for(int k=0;k<=100;++k){
        const float weight=k*.01f;
        const auto partial=StackCondition(a,b,weight);
        const auto disabled=StackCondition(a,b,0.f);
        check(dot(partial,kLuma)>=dot(disabled,kLuma)-2e-6f && dot(partial,kLuma)<=dot(ridge,kLuma)+2e-6f,"partial skin relief stays between the unprotected and protected answer");
    }
    fill({.55f,.4f,.3f},{.55f,.4f,.3f});a[4]=a[4]-.08f;
    for(int i=0;i<9;++i)b[i]=a[i]+.03f;
    check(near(StackCondition(a,b,1.f),b[4]),"original crease plus broad neural lighting remains intact");
    // A smooth colour gradient also survives: its local median is its center.
    fill(.5f,.5f);for(int i=0;i<9;++i)b[i]=float3(.55f+(i%3-1)*.02f,.48f,.47f);
    check(near(StackCondition(a,b,0),b[4]),"smooth broad colour gradient not mistaken for a fringe");

    auto first=nrstack::ForPass(0,2.f,1.8f,1.99f,1.4f,1.7f,2,3);
    check(first.intensity==2&&first.structure==1.8f&&first.tone==1.99f&&first.skin==1.4f&&first.globalTone==1.7f&&first.style==2&&first.preset==3,"first-layer request unchanged");
    for(int pass=1;pass<nrfeatures::MaxPasses;++pass){
        auto tune=nrstack::ForPass(pass,2.f,1.8f,1.99f,1.4f,1.7f);
        check(tune.intensity>0&&tune.intensity<first.intensity&&tune.structure>0&&tune.structure<first.structure&&tune.tone==0&&tune.skin>0&&tune.skin<first.skin&&tune.globalTone==0&&tune.style==0&&tune.preset==0,"later layers use independent standard detail requests without repeated tone or style");
        auto zero=nrstack::ForPass(pass,0,0,0,0,0);
        check(zero.intensity==tune.intensity&&zero.structure==tune.structure&&zero.tone==0&&zero.skin==tune.skin&&zero.globalTone==0,"later layers do not inherit disabled first-layer controls");
        check(nrstack::ForPass(pass,2,2,2,-1,1,2,3).skin==tune.skin&&nrstack::ForPass(pass,2,2,2,-1,1,2,3).style==0,"first-layer skin sentinel/cinema/preset do not affect later layers");
        auto again=nrstack::ForPass(pass,2.f,1.8f,1.99f,1.4f,1.7f);
        check(tune.skin==again.skin&&tune.structure==again.structure&&tune.globalTone==again.globalTone,"cached layer request independent of total selected layer count");
        std::printf("LAYER %d: intensity=%.6f structure=%.6f localTone=%.6f skin=%.6f globalTone=%.6f\n",pass+1,tune.intensity,tune.structure,tune.tone,tune.skin,tune.globalTone);
    }
    // Banks allocate refined[0] for one layer or scaled extra layers, and
    // pass_input for any extra layer. Exercise every cached selected prefix.
    // The selector is shared production code; red baseline came from old Stage.
    for(int built=1;built<=3;++built)for(int work2:{50,73,100})for(int work3:{50,73,100}){
        const auto layout=nrlayersr::Make({5120,2160},work2,work3);
        struct Scratch {nrlayersr::Extent extent;};
        Scratch refined{layout.layer[0]},passInput{layout.layer[1]};
        auto* haveRefined=(built==1||nrlayersr::AnyScaled(layout,built))?&refined:nullptr;
        auto* havePassInput=built>1?&passInput:nullptr;
        for(int selected=1;selected<=built;++selected){
            auto* chosen=nrstack::FinalScratch(selected,nrlayersr::AnyScaled(layout,selected),haveRefined,havePassInput);
            check(chosen&&nrlayersr::Equal(chosen->extent,layout.layer[0]),"every cached layer prefix has an allocated full-size final scratch");
        }
    }
    // Descriptor writes must all remain unique on one recorded command list.
    for(int layers=1;layers<=nrfeatures::MaxPasses;++layers)for(bool reduced:{false,true}){
        std::vector<bool> used(nrstack::DescriptorCount,false);
        auto reserve=[&](unsigned base,unsigned count){for(unsigned i=base;i<base+count;++i){check(i<used.size(),"descriptor in immutable frame heap");if(i<used.size()){check(!used[i],"no descriptor overwrite across recorded dispatches");used[i]=true;}}};
        reserve(0,14);reserve(48,6);
        for(int pass=1;pass<layers;++pass){unsigned base=nrstack::PassBase(pass);reserve(base,4);if(reduced){reserve(base+4,2);reserve(base+6,4);}}
        reserve(nrstack::FinalBase,4);
        struct Resource {bool readable=false;};
        Resource result,scratch;unsigned reads=0,dispatches=0,writes=0;
        auto* out=nrstack::Finish(layers,&result,&scratch,
            [&](Resource* src){check(src==&result&&!src->readable,"final source starts in UAV state");src->readable=true;++reads;},
            [&](Resource* src,Resource* dst){check(src!=dst&&src->readable&&dst==&scratch&&!dst->readable,"final filter reads result into distinct writable retained scratch");++dispatches;},
            [&](Resource* src){check(src==&result&&src->readable,"restore the source after recording the final filter");src->readable=false;++writes;});
        check(out==&scratch&&reads==1&&dispatches==1&&writes==1,"all layer counts receive one final condition, never another model evaluation");
        check(!result.readable&&!scratch.readable,"finish returns resident source and scratch in UAV state");
    }
    std::printf("STACK CPU: %u checks, %u failures; shared production math and mocked recording only; no GPU/model/game validation\n",checks,failures);
    return failures?1:0;
}
