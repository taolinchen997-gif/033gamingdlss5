#include "../src/nr_beta2_input.h"
#include <cstdio>
#include <limits>

// CPU-only contract regression. Does not include D3D, create a window/device,
// load a provider, run a shader, attach hooks or inspect a live game.
int main() {
    unsigned checks=0,failed=0;
    auto check=[&](bool value,const char* label){++checks;if(!value){++failed;std::printf("FAIL %s\n",label);}};
    using nrbeta2::State;
    State reason=State::Waiting;
    check(nrbeta2::NgxInput(true,true,true,true,1.f,-1.f,false,reason)&&reason==State::WaitingModel,"declared native accepts signed scale");
    check(!nrbeta2::NgxInput(false,true,true,true,1.f,1.f,false,reason)&&reason==State::MissingGuides,"missing depth rejected");
    check(!nrbeta2::NgxInput(true,false,true,true,1.f,1.f,false,reason)&&reason==State::MissingGuides,"missing motion rejected");
    check(!nrbeta2::NgxInput(true,true,false,true,1.f,1.f,false,reason)&&reason==State::UnknownFlags,"unknown flags rejected");
    check(!nrbeta2::NgxInput(true,true,true,false,1.f,1.f,false,reason)&&reason==State::UnknownMotionScale,"default scale is not provenance");
    check(!nrbeta2::NgxInput(true,true,true,true,0.f,1.f,false,reason),"zero scale rejected");
    check(!nrbeta2::NgxInput(true,true,true,true,1.f,std::numeric_limits<float>::infinity(),false,reason),"infinite scale rejected");
    check(!nrbeta2::NgxInput(true,true,true,true,std::numeric_limits<float>::quiet_NaN(),1.f,false,reason),"nan scale rejected");
    check(!nrbeta2::NgxInput(true,true,true,true,1.f,1.f,true,reason)&&reason==State::JitteredMotion,"jittered motion awaits normalization");
    nrbeta2::Snapshot snapshot;
    check(!nrbeta2::Read(nullptr),"null status rejected");
    snapshot.version=2;check(!nrbeta2::Read(&snapshot),"foreign status ABI rejected");snapshot.version=1;
    nrbeta2::Recorded(nrbeta2::Source::NgxDeclared,true);
    check(nrbeta2::Read(&snapshot)&&snapshot.records==1&&snapshot.same_evaluate==1&&snapshot.native_guides_recorded==1,"NGX recording is distinct receipt");
    nrbeta2::Recorded(nrbeta2::Source::SceneDeclared,false,27);
    check(nrbeta2::Read(&snapshot)&&snapshot.records==2&&snapshot.same_evaluate==0&&snapshot.source_serial==27,"scene serial does not claim same Evaluate");
    nrbeta2::Note(State::WaitingScene,nrbeta2::Source::SceneDeclared);
    check(nrbeta2::Read(&snapshot)&&snapshot.records==2&&snapshot.native_guides_recorded==0&&snapshot.same_evaluate==0,"waiting clears current admission preserves prior receipts");
    std::printf("beta2 NR CPU checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
