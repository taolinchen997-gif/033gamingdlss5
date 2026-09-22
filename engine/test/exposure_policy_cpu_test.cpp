#include "../src/exposure_policy.h"
#include "../src/nr_controls_abi.h"
#include <array>
#include <cstdio>
#include <limits>
int main(){
 unsigned checks=0,failures=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;printf("FAIL %s\n",why);}};
 using namespace exposurepolicy;
 check(FixedWhite(Legacy,true,19,4)==3.16f,"legacy replica retains fixed 3.16");
 check(FixedWhite(Fixed,true,19,4)==19,"explicit white independent from composition");
 check(FixedWhite(Game,true,2,.5f)==1,"exposure trim reaches white constant");
 check(ExposureGain(Game,3.16f,1)==1&&ExposureGain(Legacy,3.16f,1)==3.16f,"explicit game exposure uses trim, legacy scaling retained");
 check(FixedWhite(Fixed,false,32,1)==32 && White(.01f)==.01f,"full UI range survives normalization");
 check(White(std::numeric_limits<float>::quiet_NaN())==3.16f && Trim(std::numeric_limits<float>::infinity())==1,"nonfinite config is rejected");
 check(CheckedSource(-1)==Legacy&&CheckedSource(3)==Legacy,"invalid source keeps default");
 for(int enc=0;enc<4;++enc)for(int replica=0;replica<2;++replica){
   check(!UsesGame(Fixed,replica!=0,enc),"fixed source never reads game texture");
   check(UsesGame(Game,replica!=0,enc)==(enc==1),"game exposure limited to scene-linear inputs");
   check(UsesGame(Legacy,replica!=0,enc)==(enc==1&&!replica),"legacy routing retained");
 }
 check(CanHold(4,4,100,600,true),"completed same-stream sample held within limit");
 check(!CanHold(4,4,100,601,true),"stale sample expires");
 check(!CanHold(4,5,100,150,true),"exposure never inherited by another stream");
 check(!CanHold(4,4,100,150,false),"uncompleted sample never read");
 check(!CanHold(4,4,200,150,true),"clock reversal rejects age");
 struct Slot {bool used=false;unsigned ticket=0;};std::array<Slot,4> pool{};
 unsigned completed=0;auto done=[&](unsigned ticket){return ticket<=completed;};
 for(int i=0;i<4;++i){const auto slot=Reusable(pool,-1,done);check(slot==i,"each active lease owns immutable bindings");pool[slot]={true,unsigned(i+1)};}
 check(Reusable(pool,-1,done)==-1,"elapsed frames cannot retire GPU work");
 completed=1;check(Reusable(pool,0,done)==-1,"held source excluded even after completion");
 completed=2;check(Reusable(pool,0,done)==1,"only retired non-source slot reusable");
 pool[1].ticket=9;check(Reusable(pool,0,done)==-1,"new slot generation cannot inherit completion");
 using namespace nrcontrolsabi;
 check(Valid(WhiteSource,2)&&!Valid(WhiteSource,1.5f)&&!Valid(WhiteSource,3),"source ABI bounds");
 check(Valid(WhiteTrim,.25f)&&Valid(WhiteTrim,4)&&!Valid(WhiteTrim,0),"trim ABI bounds");
 check(Valid(Id::White,.01f)&&Valid(Id::White,32),"white ABI matches normalization range");
 // V6.1 regression guard: shipped frozen feeders (dlss5-feed.addon64 / dlss5-feed-host64.exe) read a 62-control,
 // 872-byte snapshot. New controls must sit after the frozen range and the frozen layout must stay fillable.
 check(FrozenCount==62&&sizeof(FrozenSnapshot)==872&&SkinLift>=FrozenCount&&Enabled<FrozenCount,"new controls appended after the frozen ABI");
 {Snapshot full;for(uint32_t i=0;i<Count;++i){full.values[i]=float(i)+.5f;full.modelValues[i]=float(i)+.25f;}
  full.pending=3;full.hotkey=0x7A;full.modelW=1234;full.exposureBypass=77;full.layerModelH[2]=999;full.modelWait[5]='x';
  const auto frozen=Narrow<FrozenCount>(full);bool same=true;
  for(uint32_t i=0;i<FrozenCount;++i)same=same&&frozen.values[i]==full.values[i]&&frozen.modelValues[i]==full.modelValues[i];
  check(same&&frozen.size==872&&frozen.count==FrozenCount&&frozen.version==Version&&frozen.pending==3&&frozen.hotkey==0x7A&&
        frozen.modelW==1234&&frozen.exposureBypass==77&&frozen.layerModelH[2]==999&&frozen.modelWait[5]=='x',
        "frozen feeder snapshot keeps every shared control value and tail field");}
 printf("EXPOSURE CPU: %u checks, %u failures; policy and lease selection, no GPU\n",checks,failures);return failures?1:0;
}
