// S29 route policy: pure CPU checks with the timings seen in the S27b/S28 sessions.
#include "recognition_route.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>
static unsigned checks=0;
static void Check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
using namespace yyroute;
// Runs `n` recognitions 40 ms apart (25 per second); gpu(i) and cpu(i) give each pass's time.
template<class G,class C> static Route Play(unsigned n,G gpu,C cpu,bool cpuAvailable=true,std::vector<Where>* trace=nullptr){
 Route r;uint64_t now=1000;
 for(unsigned i=0;i<n;++i,now+=40){const Where w=r.Next(now,cpuAvailable);if(trace)trace->push_back(w);
  r.Done(w,w==Where::Gpu?gpu(i):cpu(i),now,cpuAvailable);}
 return r;
}
int main()try{
 Check(Provider(Where::Gpu)==2&&Provider(Where::Cpu)==1,"provider codes: GPU keeps 2, CPU is 1");
 Check(Bucket(24.9)==0&&Bucket(25)==1&&Bucket(50)==2&&Bucket(100)==3,"CPU histogram buckets");
 {// 1) S27b first 3.5 minutes: every GPU pass 10-25 ms. Never leaves the GPU.
  auto r=Play(6000,[](unsigned i){return 10.0+(i%16);},[](unsigned){return 50.0;});
  Check(r.mode==Where::Gpu&&r.toCpu==0&&r.cpuRuns==0&&r.gpuRuns==6000,"a quick GPU keeps every recognition on the GPU");}
 {// 2) The GPU pass is always 150 ms: the CPU takes over after exactly MinGpuRuns passes.
  std::vector<Where> trace;auto r=Play(4,[](unsigned){return 150.0;},[](unsigned){return 50.0;},true,&trace);
  Check(r.mode==Where::Cpu&&r.toCpu==1&&r.gpuRuns==4,"four slow GPU passes move recognition to the CPU");
  auto three=Play(3,[](unsigned){return 150.0;},[](unsigned){return 50.0;});
  Check(three.mode==Where::Gpu,"no move before MinGpuRuns passes (one bad moment is not enough)");}
 {// 3) Stuck-slow GPU for a minute: stays on the CPU; probes every ProbeEveryMs, never more often.
  std::vector<Where> trace;auto r=Play(1500,[](unsigned){return 150.0;},[](unsigned){return 50.0;},true,&trace);
  Check(r.mode==Where::Cpu&&r.toGpu==0,"slow probes keep recognition on the CPU");
  const double seconds=1500*0.04;Check(r.probes>=unsigned(seconds/2.0)-2&&r.probes<=unsigned(seconds/2.0)+1,"one probe every two seconds");
  Check(r.cpuRuns+r.gpuRuns==1500&&r.cpuRuns>=1400,"almost every recognition runs on the CPU");
  Check(r.cpuEma>49&&r.cpuEma<51,"the CPU average follows the CPU passes");}
 {// 4) S28 slow phase: two quick passes, then one of 150 ms. Three slow passes in the
  //    window (mean ~66 ms against ~50 ms on the CPU): most recognitions end up on the CPU.
  auto r=Play(1500,[](unsigned i){return i%3==2?150.0:15.0;},[](unsigned){return 50.0;});
  Check(r.toCpu>=1&&r.cpuRuns>r.gpuRuns,"a GPU that is slow every third pass hands most recognitions to the CPU");}
 {// 5) Recovery: slow for 10 s, quick afterwards. Two quick probes in a row bring it back.
  auto r=Play(1500,[](unsigned i){return i<250?150.0:12.0;},[](unsigned){return 50.0;});
  Check(r.mode==Where::Gpu&&r.toCpu==1&&r.toGpu==1,"back to the GPU once it is quick again");
  Check(r.probes>=2,"the return needs probes");}
 {// 6) One quick probe between slow ones is not enough.
  Route r;uint64_t now=0;for(int i=0;i<4;++i){now+=40;r.Done(r.Next(now,true),150,now,true);}
  Check(r.mode==Where::Cpu,"on the CPU");
  now+=ProbeEveryMs;Check(r.Next(now,true)==Where::Gpu,"a probe is due after ProbeEveryMs");
  Check(r.Next(now-1,true)==Where::Cpu,"no probe one millisecond early");
  r.Done(Where::Gpu,10,now,true);Check(r.mode==Where::Cpu&&r.quickProbes==1,"one quick probe: still on the CPU");
  now+=40;Check(r.Next(now,true)==Where::Cpu,"no second probe right away");
  now+=ProbeEveryMs;r.Done(Where::Gpu,150,now,true);Check(r.mode==Where::Cpu&&r.quickProbes==0,"a slow probe resets the count");
  now+=ProbeEveryMs;r.Done(Where::Gpu,10,now,true);now+=ProbeEveryMs;r.Done(Where::Gpu,10,now,true);
  Check(r.mode==Where::Gpu&&r.toGpu==1,"two quick probes in a row: back to the GPU");}
 {// 7) No CPU session: never leaves the GPU, whatever the GPU does.
  auto r=Play(1000,[](unsigned){return 250.0;},[](unsigned){return 50.0;},false);
  Check(r.mode==Where::Gpu&&r.toCpu==0&&r.cpuRuns==0,"without a CPU session the GPU is the only route");}
 {// 8) The CPU session is lost while on the CPU: straight back to the GPU.
  Route r;for(int i=0;i<4;++i)r.Done(Where::Gpu,150,40*i,true);Check(r.mode==Where::Cpu,"on the CPU");
  r.CpuLost();Check(r.mode==Where::Gpu&&r.toGpu==1&&r.Next(10000,false)==Where::Gpu,"CPU lost: GPU only");}
 {// 9) A CPU that is itself slow (busy machine) does not take over from a GPU that is only
  //    a little slow: the GPU must be MarginMs worse than the CPU.
  Route r;for(int i=0;i<20;++i)r.Done(Where::Cpu,90,40*i,true);// measured CPU ~90 ms (hypothetical)
  for(int i=0;i<40;++i)r.Done(Where::Gpu,80,1000+40*i,true);
  Check(r.mode==Where::Gpu,"a GPU at 80 ms stays when the CPU takes 90 ms");}
 {// 10) Garbage times count as very slow, never as quick.
  Route r;for(int i=0;i<4;++i)r.Done(Where::Gpu,std::numeric_limits<double>::quiet_NaN(),40*i,true);
  Check(r.mode==Where::Cpu,"NaN pass times count as slow");
  r.Done(Where::Gpu,-5,5000,true);Check(r.quickProbes==0,"a negative probe time is not quick");}
 {// 11) The S28 session's worst stretch: GPU 150-250 ms for 40 s. With the CPU route the
  //     recognitions per second roughly triple (sum of pass times over the stretch).
  double gpuOnly=0,withRoute=0;Route r;uint64_t now=0;
  for(unsigned i=0;i<1000;++i){const double g=150+(i%5)*25.0;gpuOnly+=g;const Where w=r.Next(now,true);const double t=w==Where::Gpu?g:50;withRoute+=t;r.Done(w,t,now,true);now+=uint64_t(t);}
  Check(withRoute*2.5<gpuOnly,"CPU route at least 2.5x more recognitions in the slow stretch");}
 printf("S29 ROUTE CPU: %u checks, 0 failures; route policy only, no model, GPU or game\n",checks);return 0;
}catch(const std::exception& e){printf("S29 ROUTE CPU FAILED: %s\n",e.what());return 1;}
