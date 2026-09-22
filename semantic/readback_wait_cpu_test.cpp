#include "readback_wait.h"
#include "../engine/src/command_lifetime.h"
#include <iostream>
#include <stdexcept>
#include <string>
int main()try{
 unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("readback wait assertion "+std::to_string(checks));};
 // A fake native lease permits delivery only AFTER reset and all queues.
 // GPU completion alone, replayable lists, pending submission and poison
 // cannot authorize a read. Simulated clock only; no GPU or real sleep.
 for(unsigned mode=0;mode<8;++mode){
  uint64_t now=0;unsigned reads=0,pauses=0;
  auto ready=[&]{
   const bool reset=mode!=2,recording=mode==7,pinned=mode==4,observed=mode!=5;
   const unsigned pending=mode==3?1u:0u;
   const bool gpuComplete=mode!=6 && (mode==0||now>=34);
   return commandlife::MayRetire(reset,false,recording,pending,pinned,observed,gpuComplete);
  };
  const auto r=yyworker::AwaitReadback(ready,[&]{return now;},[&](unsigned ms){check(ms==2);now+=ms;++pauses;},40);
  if(r==yyworker::ReadbackWait::Ready)++reads;
  check(reads==(mode<=1?1u:0u));check(now==(mode==0?0:mode==1?34:40));
  check(pauses==(mode==0?0:mode==1?17:20));
 }
 uint64_t now=9;unsigned attempts=0;
 check(yyworker::AwaitReadback([]{return false;},[&]{return now;},[&](unsigned){now=8;++attempts;},40)==yyworker::ReadbackWait::TimedOut);
 check(attempts==1);
 // Ready is rechecked at the deadline, but this is NOT a mask age acceptance.
 now=0;check(yyworker::AwaitReadback([&]{return now==40;},[&]{return now;},[&](unsigned ms){now+=ms;},40)==yyworker::ReadbackWait::Ready);
 std::cout<<"PASS readback wait CPU: "<<checks<<" checks; immutable lease gate supplied by caller; no GPU\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
