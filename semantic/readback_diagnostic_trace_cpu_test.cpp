#include "readback_diagnostic_trace.h"
#include <cstdio>
#include <limits>
using namespace yyworker::diagnostic;
static unsigned checks=0,failed=0;
static void Check(bool value,const char* label){++checks;if(!value){++failed;std::printf("FAIL %s\n",label);}}
static bool Near(double a,double b){return std::fabs(a-b)<1e-8;}
int main(){
 Cycle cycle;
 Check(cycle.Add(Phase::QueueWait,250),"idle queue wait recorded");
 Check(cycle.Add(Phase::Prepare,70),"prepare includes canceled old active and promotions");
 Check(cycle.AddPreparePart(PreparePart::Probe,20),"nested probe");
 Check(cycle.AddPreparePart(PreparePart::Sleep,48),"nested sleep");
 Check(cycle.AddPreparePart(PreparePart::Promote,2),"nested promotion (may include probes)");
 Check(cycle.Add(Phase::Init,1),"init");Check(cycle.Add(Phase::Convert,2),"convert");
 Check(cycle.Add(Phase::IPC,22),"IPC");Check(cycle.Add(Phase::Publish,1),"publish");
 Check(cycle.Add(Phase::Release,3),"post-publish explicit release");
 Check(cycle.Finish(100),"non-overlap conservation excludes queue idle and subphases");
 Check(Near(cycle.phase[unsigned(Phase::Other)],1),"other residual");
 Check(!cycle.Add(Phase::IPC,1),"finished cycle immutable");
 Check(!cycle.Finish(100),"cannot finish twice");
 Cycle expired;expired.Add(Phase::Prepare,101);expired.Add(Phase::Release,2);
 Check(expired.Finish(104),"expired-before-map cycle has own accounting");
 Check(expired.phase[unsigned(Phase::IPC)]==0,"expired does no inference accounting");
 Check(Near(expired.phase[unsigned(Phase::Other)],1),"expired residual");
 Cycle overlap;overlap.Add(Phase::Prepare,60);overlap.Add(Phase::IPC,50);
 Check(!overlap.Finish(100)&&overlap.invalid==1,"overlapping primary timings reported invalid");
 Check(overlap.phase[unsigned(Phase::Other)]==0,"invalid overlap never invents negative other");
 Cycle invalid;
 Check(!invalid.Add(Phase::Prepare,-1),"negative interval rejected");
 Check(!invalid.Add(Phase::IPC,(std::numeric_limits<double>::infinity)()),"infinite interval rejected");
 Check(!invalid.AddPreparePart(PreparePart::Probe,(std::numeric_limits<double>::quiet_NaN)()),"NaN rejected");
 Check(!invalid.Add(Phase::Other,5),"caller cannot overwrite residual");
 Check(!invalid.Add(Phase(300),5),"phase bounds");
 Check(!invalid.AddPreparePart(PreparePart(300),5),"nested phase bounds");
 Check(!invalid.Finish(-3),"reverse service clock rejected");
 double huge=(std::numeric_limits<double>::max)();
 Check(!Accumulate(huge,huge)&&std::isfinite(huge),"finite total overflow rejected");
 uint64_t saturated=UINT64_MAX;Increment(saturated);Check(saturated==UINT64_MAX,"counter saturates");
 Totals total;total.Observe(expired);Check(total.cycles==1&&total.invalid==0,"totals complete cycle");
 total.Observe(overlap);Check(total.cycles==2&&total.invalid==1,"invalid cycle visible in totals");
 LogBudget budget;unsigned sampled=0;
 for(uint64_t i=0;i<=16384;++i){
  const bool expected=i>=1&&i<=8192&&(i<=16||i==32||i==64||i==128||i==256||i==512||i==1024||i==2048||i==4096||i==8192);
  Check(Sampled(i)==expected,"finite first16 and powers to8192");if(budget.Take(i))++sampled;
 }
 Check(sampled==25&&budget.emitted==25,"25 eligible ordinal records per stream");
 for(unsigned i=0;i<500;++i)budget.Take(1);
 Check(budget.emitted==128&&!budget.Take(8192),"global S16 budget still finite for shared streams");
 // CPU counterexamples model required ordering; they are NOT GPU execution tests.
 const unsigned ownCopyDone=5,listReset=101,modelCost=22;
 Check(ownCopyDone<100&&listReset>100,"full retirement can reject despite fast own copy");
 const unsigned pendingCaptured[2]={28,56},pendingReset[2]={129,157};
 bool latestReady=false;for(unsigned i=0;i<2;++i)latestReady|=80>=pendingReset[i]&&80-pendingCaptured[i]<=100;
 Check(!latestReady,"latest-ready cannot help when all fresh pending remain replayable");
 Check(listReset+modelCost>100,"timing cannot be fixed by a ready result that is stale after inference");
 int borrowedReduced=1;const int sourceStamp=1;
 borrowedReduced=2; // Legal later replay after original list completion, before an injected side copy reads.
 const int privateReadback=borrowedReduced;
 Check(privateReadback!=sourceStamp,"private destination cannot establish source stability");
 const int trulyOwnedSnapshot=1;borrowedReduced=3;
 Check(trulyOwnedSnapshot==sourceStamp,"true immutable source stays independent of borrowed replay");
 std::printf("S16 CPU diagnostics: %u checks, %u failures; no GPU/game; modeled lifecycle counterexamples only\n",checks,failed);
 return failed?1:0;
}
