#include "../src/nr_layer_resolution.h"
#include "nr_layer_cpu_cases.h"
#include <cstdio>

namespace {
int checks=0,failures=0,objects[64]{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",name);}}
struct Bank:layercpu::Bank {
    uint32_t sw=5120,sh=2160,ew=5120,eh=2160,tw=5120,th=2160;
    int built_passwork=100,built_passwork3=100;
};
Bank liveBank(int second,int third){
    Bank b;b.feat=&objects[0];b.extra_feat[0]=&objects[1];b.extra_feat[1]=&objects[2];
    b.full=&objects[3];b.model_input=&objects[4];b.out=&objects[5];b.res=&objects[6];
    b.extra_out=&objects[7];b.extra_alt=&objects[8];b.pass_input=&objects[9];b.pass_input3=&objects[10];
    b.refined[0]=&objects[11];b.refined[1]=&objects[12];b.hist[0]=&objects[13];b.hist[1]=&objects[14];
    const auto layout=nrlayersr::Make({b.sw,b.sh},second,third);
    b.ew=layout.layer[1].width;b.eh=layout.layer[1].height;b.tw=layout.layer[2].width;b.th=layout.layer[2].height;
    b.built_passwork=second;b.built_passwork3=third;return b;
}
}
int main(){
    using namespace nrlayersr;
    const auto split=Make({5120,2160},50,100);
    check(Equal(split.layer[0],{5120,2160})&&Equal(split.layer[1],{2560,1080})&&Equal(split.layer[2],{5120,2160}),"third 100% does not inherit second 50%");
    const auto reversed=Make({5120,2160},100,50);
    check(Equal(reversed.layer[1],{5120,2160})&&Equal(reversed.layer[2],{2560,1080}),"second 100% and third 50% are independent");
    check(!AnyScaled(reversed,1)&&!AnyScaled(reversed,2)&&AnyScaled(reversed,3),"only active layers choose full-size refinement scratch");
    const auto separate=Make({5120,2160},75,67);
    check(Equal(separate.layer[1],{3840,1620})&&Equal(separate.layer[2],{3430,1446}),"third percentage uses first-layer base not second output size");
    check(Equal(Scale({1001,501},51),{510,254}),"odd scaled extents round down to even dimensions");
    check(Equal(Scale({31,63},50),{64,64}),"minimum model extent stays 64");
    check(Equal(Scale({5120,2160},49),{2560,1080})&&Equal(Scale({5120,2160},101),{5120,2160}),"scale admission clamps to the existing 50..100 range");
    for(int second:{50,75,100})for(int oldThird:{50,75,100})for(int newThird:{50,75,100}){
        const auto live=liveBank(second,oldThird);auto candidate=live;
        const bool resized=oldThird!=newThird;
        const auto changed=ResizeCandidate(candidate,live,second,newThird);
        check(changed==(resized?4u:0u),"third edit changes only third dimension bit");
        check(candidate.feat==live.feat&&candidate.extra_feat[0]==live.extra_feat[0]&&candidate.pass_input==live.pass_input&&candidate.extra_out==live.extra_out,"third SR preserves first and second feature/resources");
        check(live.extra_feat[1]==&objects[2]&&live.pass_input3==&objects[10]&&live.built_passwork3==oldThird,"planning never mutates the live bank");
        check(resized?(!candidate.extra_feat[1]&&!candidate.extra_alt&&!candidate.pass_input3):(candidate.extra_feat[1]==live.extra_feat[1]&&candidate.pass_input3==live.pass_input3),"resized third model/resources detach only in private candidate");
        check(candidate.built_passes==(resized?2:3),"missing cached third feature cannot masquerade as usable capacity");
        if(resized){candidate.extra_alt=&objects[20];candidate.pass_input3=&objects[21];}
        int creates=0;
        check(nrlayers::ReplaceFeatures(candidate,live,live.model_cfg,[&](int pass,nrlayers::Model,void*& feature){
            ++creates;check(pass==2&&Equal(Of(candidate).layer[pass],Scale({live.sw,live.sh},newThird)),"create uses exact third dimensions only");feature=&objects[22];return true;
        })&&creates==(resized?1:0),"SR-only edit rebuilds exactly the affected feature");
        check(candidate.built_passes==3&&candidate.selected_passes==3,"published capacity restored only after actual feature creation");
        auto old=live;nrlayers::ExcludeBorrowed(old,candidate);
        check(!old.feat&&!old.extra_feat[0]&&!old.pass_input&&!old.extra_out&&!old.refined[0],"successful publication retirement excludes reused first/second/scratch");
        check(resized?(old.extra_feat[1]==live.extra_feat[1]&&old.pass_input3==live.pass_input3&&old.extra_alt==live.extra_alt):(!old.extra_feat[1]&&!old.pass_input3&&!old.extra_alt),"only replaced third allocations remain for lease-gated retirement");
        auto cancelled=candidate;nrlayers::ExcludeBorrowed(cancelled,live);
        check(!cancelled.feat&&!cancelled.extra_feat[0]&&!cancelled.pass_input&&(!resized||cancelled.pass_input3==&objects[21]),"superseded candidate cleanup keeps new third input and excludes borrowed live input");
    }
    const auto live=liveBank(100,100);
    auto failed=live;ResizeCandidate(failed,live,100,50);failed.extra_alt=&objects[20];failed.pass_input3=&objects[21];
    check(!nrlayers::ReplaceFeatures(failed,live,live.model_cfg,[](int,nrlayers::Model,void*& feature){feature=nullptr;return false;}),"failed resized feature rejects partial candidate");
    nrlayers::ExcludeBorrowed(failed,live);
    check(!failed.feat&&!failed.extra_feat[0]&&failed.pass_input3==&objects[21]&&live.extra_feat[1]==&objects[2],"failed allocation cleanup preserves old third model while retaining only candidate ownership");
    auto disabled=live;ResizeCandidate(disabled,live,100,50);auto request=live.model_cfg;request.passes=2;int creates=0;
    check(nrlayers::ReplaceFeatures(disabled,live,request,[&](int,nrlayers::Model,void*&){++creates;return false;})&&creates==0&&disabled.built_passes==2,"inactive third SR saves intent without creating a third feature or retaining false capacity");
    auto enabled=disabled;request.passes=3;
    check(nrlayers::ReplaceFeatures(enabled,disabled,request,[&](int pass,nrlayers::Model,void*& feature){check(pass==2,"reenabling third creates third only");feature=&objects[23];return true;})&&enabled.built_passes==3,"reenabling resized inactive layer requires real creation");
    auto secondChanged=live;check(ResizeCandidate(secondChanged,live,50,100)==2&&secondChanged.extra_feat[1]==live.extra_feat[1]&&secondChanged.pass_input3==live.pass_input3&&!secondChanged.extra_feat[0],"second edit keeps independent third allocations");
    check(!nrlayers::InitializationComplete(8,9)&&nrlayers::InitializationComplete(9,9)&&!nrlayers::BorrowStillValid(true,4,5),"existing fence and borrowed-bank epoch gates still control publication");
    std::printf("NR independent layer SR CPU checks=%d failed=%d; no DLL/GPU execution\n",checks,failures);
    return failures?1:0;
}
